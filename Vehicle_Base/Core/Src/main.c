/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    main.c
 * @brief   Solar Panel Robot — Merged STM32 Firmware
 *          Board: STM32 Nucleo-F767ZI
 *
 * SYSTEM OVERVIEW
 * ───────────────
 *
 *  Raspberry Pi  ──WiFi──►  ESP8266 (click shield)  ──UART4──►  STM32
 *                             (MQTT bridge)                    (this file)
 *
 *  1. Raspberry Pi reads MCP3008 joystick ADC and publishes to MQTT broker
 *     (localhost:1883, topic "vehicle/base") as {"x": float, "y": float}
 *     where both values are normalised to [0.0, 1.0], centre = 0.5.
 *
 *  2. ESP8266 click shield (flashed with esp8266_mqtt.ino):
 *     - Creates WiFi AP "ESP32_AP"
 *     - Subscribes to Pi's MQTT broker at 192.168.4.2:1883
 *     - Converts JSON to 4-byte binary frame and sends over UART to STM32:
 *         [0xAA] [x_byte] [y_byte] [0x55]
 *         where x_byte/y_byte are uint8 (0-255, centre = 128)
 *
 *  3. STM32 (this firmware):
 *     - Parses ESP8266 frames on UART4
 *     - Applies Ian's tank-drive mixing math
 *     - Commands both VESC ESCs (FESC 6.7) via bldc_interface library
 *     - Displays live telemetry + Pi connection status on SSD1306 OLED
 *     - Provides local keypad fallback control
 *
 * UART ASSIGNMENTS
 * ────────────────
 *   UART4  (PA1  RX) ← ESP8266 joystick frames  [0xAA][x][y][0x55]
 *   UART5  (PC12 TX, PC11 RX) ↔ Left  VESC ESC (bldc_interface + telemetry)
 *   UART7  (PE8  TX) → Right VESC ESC (bldc_interface, TX only)
 *   USART3 (PD8  TX) → ST-Link virtual COM (debug output)
 *   I2C1   (PB8 SCL, PB9 SDA) ↔ SSD1306 OLED 128×64
 *
 * MOTOR CONTROL PROTOCOL
 * ──────────────────────
 *   The bldc_interface library (Benjamin Vedder, GPLv3) serialises commands
 *   as VESC binary frames: [0x02][len][payload][CRC16_hi][CRC16_lo][0x03]
 *   Commands used: COMM_SET_DUTY (0x05), COMM_GET_VALUES (0x04)
 *
 * SAFETY WATCHDOG
 * ───────────────
 *   If no ESP8266 frame arrives within CMD_TIMEOUT_MS (500 ms), both motors
 *   are stopped and the OLED shows "Pi: TIMEOUT".  The VESC ESC has its own
 *   independent 1-second timeout that also cuts motor power if it sees no
 *   commands — this is a second layer of protection.
 ******************************************************************************
 * @attention
 * Copyright (c) 2026 STMicroelectronics / Solar Panel Robot Team.
 * All rights reserved.
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "string.h"

/* USER CODE BEGIN Includes */
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "vesc.h"
#include "keypad.h"
#include "ui.h"
#include "bldc_interface_uart.h"   /* bldc_interface_uart_run_timer() for packet timeout */
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private defines -----------------------------------------------------------*/
/* USER CODE BEGIN PD */

/** CMD_TIMEOUT_MS — stop motors if no ESP8266 frame arrives within this window.
 *  500 ms matches the VESC firmware's own keep-alive timeout.
 *  Ian noted the VESC will also self-stop after 1 s — this fires first. */
#define CMD_TIMEOUT_MS   500u

/* ESP8266 frame framing bytes (must match esp8266_mqtt.ino) */
#define ESP_START_BYTE   0xAAu
#define ESP_END_BYTE     0x55u
#define ESP_FRAME_LEN    4u      /* [START][x_byte][y_byte][END] */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
#if defined ( __ICCARM__ ) /*!< IAR Compiler */
  #pragma location=0x2007c000
  ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT];
  #pragma location=0x2007c0a0
  ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT];
#elif defined ( __CC_ARM )
  __attribute__((at(0x2007c000))) ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT];
  __attribute__((at(0x2007c0a0))) ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT];
#elif defined ( __GNUC__ )
  ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT] __attribute__((section(".RxDecripSection")));
  ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT] __attribute__((section(".TxDecripSection")));
#endif

ETH_TxPacketConfig TxConfig;
ETH_HandleTypeDef  heth;
I2C_HandleTypeDef  hi2c1;

/* UART handles — names match the peripheral numbers */
UART_HandleTypeDef huart3;   /* USART3  — ST-Link debug TX           */
UART_HandleTypeDef huart4;   /* UART4   — ESP8266 RX (joystick data) */
UART_HandleTypeDef huart5;   /* UART5   — Left  VESC TX+RX           */
UART_HandleTypeDef huart7;   /* UART7   — Right VESC TX              */

/* USER CODE BEGIN PV */

/* ── ESP8266 packet parser state ──────────────────────────────────────────
 * The ESP8266 sends 4-byte frames: [0xAA][x_byte][y_byte][0x55]
 * We receive one byte at a time via interrupt (huart4).
 * esp_rx_byte  : single-byte DMA target for UART4 ISR
 * esp_frame[]  : accumulates the current frame
 * esp_frame_idx: write index (0-3); resets on start byte or bad end byte
 * esp_cmd_ready: set to 1 by ISR when a valid frame has been latched
 * esp_cmd_x/y  : latched x,y bytes from the last valid frame (default = 128 = centre)
 */
static volatile uint8_t esp_rx_byte   = 0;
static volatile uint8_t esp_frame[4]  = {0};
static volatile uint8_t esp_frame_idx = 0;
static volatile uint8_t esp_cmd_ready = 0;
static volatile uint8_t esp_cmd_x     = 128u;  /* centre = no movement */
static volatile uint8_t esp_cmd_y     = 128u;

/* ── Left VESC telemetry RX ───────────────────────────────────────────────
 * One byte at a time from UART5; fed to bldc_interface_uart_process_byte(). */
static volatile uint8_t vesc_rx_byte = 0;

/* ── Motor watchdog state ─────────────────────────────────────────────────
 * These are also read by ui.c to show connection status on the OLED. */
uint32_t lastCmdTime  = 0;   /* HAL_GetTick() of last valid ESP8266 frame */
uint8_t  motorRunning = 0;   /* 1 = motors have been commanded to move     */

/* ── Keypad ───────────────────────────────────────────────────────────────*/
static uint8_t pendingKey = KEY_NONE;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_ETH_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_UART4_Init(void);
static void MX_UART5_Init(void);
static void MX_UART7_Init(void);
static void MX_I2C1_Init(void);

/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */

/* ──────────────────────────────────────────────────────────────────────────
 * VESC telemetry callback
 * Called from VESC_ProcessRxByte → bldc_interface internals each time a full
 * COMM_GET_VALUES response is decoded and CRC-verified.
 * UI_TelemetryUpdate is safe to call here: it only writes to a struct, no I2C.
 * ────────────────────────────────────────────────────────────────────────── */
static void on_vesc_values(mc_values *val)
{
    UI_TelemetryUpdate(val);
}

/* ──────────────────────────────────────────────────────────────────────────
 * HAL_UART_RxCpltCallback
 * Invoked automatically by HAL when the 1-byte interrupt transfer completes.
 *
 * UART4 handler — ESP8266 joystick frame parser
 *   Frame format: [0xAA][x_byte][y_byte][0x55]
 *   We synchronise on the start byte (0xAA) and validate the end byte (0x55).
 *   If the frame is corrupt (bad end byte) the parser resets and re-syncs.
 *
 * UART5 handler — Left VESC telemetry byte feed
 *   Each byte is forwarded to bldc_interface_uart_process_byte() which
 *   assembles the VESC response frame, validates CRC, then fires on_vesc_values().
 * ────────────────────────────────────────────────────────────────────────── */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    /* ── ESP8266 joystick frame parser (UART4) ── */
    if (huart->Instance == UART4)
    {
        if (esp_frame_idx == 0)
        {
            /* Waiting for start byte — discard anything that isn't 0xAA */
            if (esp_rx_byte == ESP_START_BYTE)
            {
                esp_frame[esp_frame_idx++] = esp_rx_byte;
            }
        }
        else
        {
            /* Collecting bytes 1–3 of the frame */
            esp_frame[esp_frame_idx++] = esp_rx_byte;

            if (esp_frame_idx == ESP_FRAME_LEN)
            {
                /* Full frame received — validate end byte */
                if (esp_frame[3] == ESP_END_BYTE)
                {
                    /* Valid frame: latch x and y and signal the main loop */
                    esp_cmd_x     = esp_frame[1];
                    esp_cmd_y     = esp_frame[2];
                    esp_cmd_ready = 1;
                }
                /* Reset parser for next frame regardless of validity */
                esp_frame_idx = 0;
            }
        }

        /* Re-arm UART4 interrupt for the next byte */
        HAL_UART_Receive_IT(&huart4, (uint8_t *)&esp_rx_byte, 1);
    }

    /* ── Left VESC telemetry (UART5 RX) ── */
    if (huart->Instance == UART5)
    {
        /* Feed byte to bldc_interface packet decoder.
         * When a complete COMM_GET_VALUES response is assembled, on_vesc_values()
         * will be called automatically inside bldc_interface_uart_process_byte(). */
        VESC_ProcessRxByte((uint8_t)vesc_rx_byte);

        /* Re-arm UART5 RX interrupt for the next byte */
        HAL_UART_Receive_IT(&huart5, (uint8_t *)&vesc_rx_byte, 1);
    }
}

/* USER CODE END 0 */

/**
 * @brief  Application entry point.
 */
int main(void)
{
    /* USER CODE BEGIN 1 */
    /* USER CODE END 1 */

    /* MPU Configuration */
    MPU_Config();

    /* HAL Init — resets all peripherals, initialises Flash and SysTick */
    HAL_Init();

    /* USER CODE BEGIN Init */
    /* USER CODE END Init */

    /* System Clock Configuration — 96 MHz from HSE PLL */
    SystemClock_Config();

    /* USER CODE BEGIN SysInit */
    /* USER CODE END SysInit */

    /* Peripheral initialisation ─────────────────────────────────────────── */
    MX_GPIO_Init();
    MX_ETH_Init();
    MX_USART3_UART_Init();   /* Debug serial (ST-Link)     */
    MX_UART4_Init();         /* ESP8266 RX (joystick data) */
    MX_UART5_Init();         /* Left  VESC TX+RX           */
    MX_UART7_Init();         /* Right VESC TX              */
    MX_I2C1_Init();          /* SSD1306 OLED display       */

    /* USER CODE BEGIN 2 ────────────────────────────────────────────────── */

    /* Initialise display, keypad, and UI state machine */
    ssd1306_Init();
    KEYPAD_Init();
    UI_Init();               /* Shows splash screen immediately */

    /* Initialise VESC driver (sets up bldc_interface with left-motor callback) */
    VESC_Init();
    VESC_SetValuesCallback(on_vesc_values);

    /* Start UART interrupt receivers */
    HAL_UART_Receive_IT(&huart4, (uint8_t *)&esp_rx_byte,  1);  /* ESP8266  */
    HAL_UART_Receive_IT(&huart5, (uint8_t *)&vesc_rx_byte, 1);  /* Left VESC */

    /* Debug: announce firmware start over ST-Link */
    HAL_UART_Transmit(&huart3,
        (uint8_t *)"[BASE] Solar Robot firmware started\r\n", 38, 50);

    /* USER CODE END 2 */

    /* ── Main Loop ────────────────────────────────────────────────────────
     *
     * Each iteration:
     *  1. Scan keypad for local input
     *  2. Update UI (handles input, redraws OLED if dirty)
     *  3. Process ESP8266 joystick frame (if a new one arrived via ISR)
     *  4. Check motor watchdog (stop if no command in CMD_TIMEOUT_MS)
     *  5. Poll VESC telemetry every 200 ms
     * ────────────────────────────────────────────────────────────────────── */
    /* USER CODE BEGIN WHILE */
    while (1)
    {
        /* USER CODE END WHILE */
        /* USER CODE BEGIN 3 */

        /* ── 1. Keypad scan ── */
        pendingKey = KEYPAD_Scan();

        /* ── 2. OLED UI update ── */
        UI_Update(pendingKey);

        /* ── 3. Process joystick command from ESP8266 ──
         *
         * esp_cmd_ready is set to 1 by HAL_UART_RxCpltCallback (UART4 ISR).
         * We clear it here and apply Ian's tank-drive mixing math:
         *
         *   x = steering  (negative = left,  positive = right)
         *   y = throttle  (negative = back,  positive = forward)
         *
         *   left_duty  = y + x     (left motor)
         *   right_duty = y - x     (right motor)
         *
         * Both values are in [-1, +1] and are passed to VESC_JoystickDrive()
         * which clamps and scales them by VESC_MAX_DUTY_FLOAT (0.50) before
         * sending to the VESC ESCs.
         *
         * The uint8 [0, 255] bytes from ESP8266 are converted to float [-1, +1]:
         *   float = (byte - 127.5) / 127.5
         *   centre byte (128) → 0.008 ≈ 0  (dead-band handled by ESP8266 ADC)
         */
        if (esp_cmd_ready)
        {
            esp_cmd_ready = 0;         /* clear flag before reading values */
            lastCmdTime   = HAL_GetTick();
            motorRunning  = 1;

            /* Convert [0, 255] → [-1.0, +1.0]; 127.5 is the exact midpoint */
            float x = ((float)esp_cmd_x - 127.5f) / 127.5f;  /* steering  */
            float y = ((float)esp_cmd_y - 127.5f) / 127.5f;  /* throttle  */

            /* Apply Ian's mixing and send to both VESCs */
            VESC_JoystickDrive(x, y);

            /* Sync UI: mark motor as running so status screen shows REMOTE */
            UI_ForceRedraw();
        }

        /* ── 4. Motor watchdog ──
         *
         * If the Raspberry Pi stops sending commands (e.g. disconnected from
         * WiFi, crash, or script stopped), we must stop the motors automatically.
         * CMD_TIMEOUT_MS = 500 ms; VESC firmware has a secondary 1 s timeout.
         */
        if (motorRunning && (HAL_GetTick() - lastCmdTime > CMD_TIMEOUT_MS))
        {
            VESC_Stop();
            motorRunning = 0;
            UI_ForceRedraw();   /* triggers OLED to show "Pi: TIMEOUT" */

            HAL_UART_Transmit(&huart3,
                (uint8_t *)"[BASE] Watchdog: motor stop\r\n", 29, 20);
        }

        /* ── 5. VESC telemetry poll (every 200 ms) ──
         *
         * VESC_RequestValues() sends a COMM_GET_VALUES request to the left VESC.
         * The response arrives byte-by-byte on UART5 and is decoded in the ISR
         * via VESC_ProcessRxByte().  Once a full frame is decoded, on_vesc_values()
         * calls UI_TelemetryUpdate() to refresh the OLED status screen.
         */
        static uint32_t lastPoll = 0;
        if (HAL_GetTick() - lastPoll >= 200u)
        {
            lastPoll = HAL_GetTick();
            VESC_RequestValues();

            /* Keep bldc_interface packet timeout counter alive (~1 kHz ideal,
             * but called here at 5 Hz is sufficient for a 2-count timeout) */
            bldc_interface_uart_run_timer();
        }

    }   /* end while(1) */
    /* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration — 96 MHz from HSE (8 MHz) via PLL
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Configure backup access (required before LSE config) */
    HAL_PWR_EnableBkUpAccess();

    /* Set voltage regulator scale for 96 MHz operation */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

    /* HSE bypass: Nucleo board uses the ST-Link MCO as the HSE source */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_BYPASS;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 4;
    RCC_OscInitStruct.PLL.PLLN       = 96;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;   /* SYSCLK = 96 MHz */
    RCC_OscInitStruct.PLL.PLLQ       = 4;
    RCC_OscInitStruct.PLL.PLLR       = 2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_PWREx_EnableOverDrive() != HAL_OK)
    {
        Error_Handler();
    }

    /* Bus clocks: AHB=96, APB1=48, APB2=96 */
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
    {
        Error_Handler();
    }
}

/* ── Peripheral Initialisation Functions ─────────────────────────────────── */

/**
 * @brief ETH — Ethernet MAC (required by HAL even if unused)
 */
static void MX_ETH_Init(void)
{
    static uint8_t MACAddr[6];

    heth.Instance        = ETH;
    MACAddr[0] = 0x00; MACAddr[1] = 0x80; MACAddr[2] = 0xE1;
    MACAddr[3] = 0x00; MACAddr[4] = 0x00; MACAddr[5] = 0x00;
    heth.Init.MACAddr       = &MACAddr[0];
    heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
    heth.Init.TxDesc         = DMATxDscrTab;
    heth.Init.RxDesc         = DMARxDscrTab;
    heth.Init.RxBuffLen      = 1524;

    if (HAL_ETH_Init(&heth) != HAL_OK)
    {
        Error_Handler();
    }

    memset(&TxConfig, 0, sizeof(ETH_TxPacketConfig));
    TxConfig.Attributes      = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
    TxConfig.ChecksumCtrl    = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
    TxConfig.CRCPadCtrl      = ETH_CRC_PAD_INSERT;
}

/**
 * @brief I2C1 — SSD1306 OLED display (PB8 SCL, PB9 SDA, 400 kHz)
 */
static void MX_I2C1_Init(void)
{
    hi2c1.Instance              = I2C1;
    hi2c1.Init.Timing           = 0x2010091A;   /* 400 kHz fast-mode */
    hi2c1.Init.OwnAddress1      = 0;
    hi2c1.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2      = 0;
    hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c1.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK)         Error_Handler();
    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK) Error_Handler();
    if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK) Error_Handler();
}

/**
 * @brief USART3 — ST-Link debug output (PD8 TX, PD9 RX, 115200 baud)
 */
static void MX_USART3_UART_Init(void)
{
    huart3.Instance            = USART3;
    huart3.Init.BaudRate       = 115200;
    huart3.Init.WordLength     = UART_WORDLENGTH_8B;
    huart3.Init.StopBits       = UART_STOPBITS_1;
    huart3.Init.Parity         = UART_PARITY_NONE;
    huart3.Init.Mode           = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl      = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling   = UART_OVERSAMPLING_16;
    huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart3) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief UART4 — ESP8266 click shield RX (PA1 RX, 115200 baud)
 *        Receives 4-byte joystick frames: [0xAA][x][y][0x55]
 *        Only RX is used; TX is not connected.
 */
static void MX_UART4_Init(void)
{
    huart4.Instance            = UART4;
    huart4.Init.BaudRate       = 115200;
    huart4.Init.WordLength     = UART_WORDLENGTH_8B;
    huart4.Init.StopBits       = UART_STOPBITS_1;
    huart4.Init.Parity         = UART_PARITY_NONE;
    huart4.Init.Mode           = UART_MODE_TX_RX;
    huart4.Init.HwFlowCtl      = UART_HWCONTROL_NONE;
    huart4.Init.OverSampling   = UART_OVERSAMPLING_16;
    huart4.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart4) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief UART5 — Left VESC ESC (PC12 TX, PC11 RX, 115200 baud)
 *        TX sends bldc_interface motor commands.
 *        RX receives COMM_GET_VALUES telemetry responses.
 */
static void MX_UART5_Init(void)
{
    huart5.Instance            = UART5;
    huart5.Init.BaudRate       = 115200;
    huart5.Init.WordLength     = UART_WORDLENGTH_8B;
    huart5.Init.StopBits       = UART_STOPBITS_1;
    huart5.Init.Parity         = UART_PARITY_NONE;
    huart5.Init.Mode           = UART_MODE_TX_RX;
    huart5.Init.HwFlowCtl      = UART_HWCONTROL_NONE;
    huart5.Init.OverSampling   = UART_OVERSAMPLING_16;
    huart5.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart5.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart5) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief UART7 — Right VESC ESC (PE8 TX, 115200 baud)
 *        TX only; right-motor telemetry is not collected in this firmware.
 */
static void MX_UART7_Init(void)
{
    huart7.Instance            = UART7;
    huart7.Init.BaudRate       = 115200;
    huart7.Init.WordLength     = UART_WORDLENGTH_8B;
    huart7.Init.StopBits       = UART_STOPBITS_1;
    huart7.Init.Parity         = UART_PARITY_NONE;
    huart7.Init.Mode           = UART_MODE_TX_RX;
    huart7.Init.HwFlowCtl      = UART_HWCONTROL_NONE;
    huart7.Init.OverSampling   = UART_OVERSAMPLING_16;
    huart7.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart7.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart7) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief GPIO — Nucleo board pins (LEDs, user button, USB, keypad)
 */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable all GPIO port clocks used by this board */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* Output default levels */
    HAL_GPIO_WritePin(GPIOB, LD1_Pin|GPIO_PIN_10|GPIO_PIN_11|LD3_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_13|GPIO_PIN_14, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9|GPIO_PIN_11, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(USB_PowerSwitchOn_GPIO_Port, USB_PowerSwitchOn_Pin, GPIO_PIN_RESET);

    /* User button — rising edge interrupt */
    GPIO_InitStruct.Pin  = USER_Btn_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(USER_Btn_GPIO_Port, &GPIO_InitStruct);

    /* Board LEDs */
    GPIO_InitStruct.Pin   = LD1_Pin|GPIO_PIN_10|GPIO_PIN_11|LD3_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin   = GPIO_PIN_13|GPIO_PIN_14;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    GPIO_InitStruct.Pin   = GPIO_PIN_9|GPIO_PIN_11;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    /* USB power switch */
    GPIO_InitStruct.Pin   = USB_PowerSwitchOn_Pin;
    HAL_GPIO_Init(USB_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

    /* USB over-current detect */
    GPIO_InitStruct.Pin  = USB_OverCurrent_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(USB_OverCurrent_GPIO_Port, &GPIO_InitStruct);

    /* USB FS pins — alternate function */
    GPIO_InitStruct.Pin       = USB_SOF_Pin|USB_DM_Pin|USB_DP_Pin;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USB VBUS sense */
    GPIO_InitStruct.Pin  = USB_VBUS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(USB_VBUS_GPIO_Port, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/* MPU Configuration --------------------------------------------------------*/

void MPU_Config(void)
{
    MPU_Region_InitTypeDef MPU_InitStruct = {0};

    HAL_MPU_Disable();

    /* Configure region 0: all 4 GB — no access by default (catch stray accesses) */
    MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
    MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
    MPU_InitStruct.BaseAddress      = 0x0;
    MPU_InitStruct.Size             = MPU_REGION_SIZE_4GB;
    MPU_InitStruct.SubRegionDisable = 0x87;
    MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
    MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
    MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
    MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
    MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&MPU_InitStruct);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
 * @brief Error Handler — disables interrupts and halts (for debugging via debugger)
 */
void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    __disable_irq();
    while (1)
    {
    }
    /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
