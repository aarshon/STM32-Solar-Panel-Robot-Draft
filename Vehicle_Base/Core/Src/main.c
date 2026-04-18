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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "vesc.h"
#include "keypad.h"
#include "ui.h"
#include "bldc_interface_uart.h"   /* bldc_interface_uart_run_timer() for packet timeout */
#include "stepper.h"               /* NEMA 17 arm motor (TIM3 PWM + ADC pot) */
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
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
ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
#pragma location=0x2007c0a0
ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

#elif defined ( __CC_ARM )  /* MDK ARM Compiler */

__attribute__((at(0x2007c000))) ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
__attribute__((at(0x2007c0a0))) ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

#elif defined ( __GNUC__ ) /* GNU Compiler */

ETH_DMADescTypeDef DMARxDscrTab[ETH_RX_DESC_CNT] __attribute__((section(".RxDecripSection"))); /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef DMATxDscrTab[ETH_TX_DESC_CNT] __attribute__((section(".TxDecripSection")));   /* Ethernet Tx DMA Descriptors */
#endif

ETH_TxPacketConfig TxConfig;

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

ETH_HandleTypeDef heth;

I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart5;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */

/* ── UART handles for peripherals NOT in the .ioc file ───────────────────
 * huart4 and huart7 were added manually (the .ioc only has UART5/USART3).
 * Placed here inside USER CODE so they survive CubeIDE code regeneration.
 * Their MSP (GPIO + clock init) is in MX_UART4_Init / MX_UART7_Init below. */
UART_HandleTypeDef huart4;   /* UART4  (PA1  RX) — ESP8266 click shield       */
UART_HandleTypeDef huart7;   /* UART7  (PE8  TX) — Right VESC ESC             */

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

/* ── Stepper / ADC DMA ────────────────────────────────────────────────────
 * CubeIDE generates MX_ADC1_Init() and MX_DMA_Init() from the .ioc.
 * This buffer is the destination for the DMA circular transfer:
 *   adc_dma_buf[0] = PC0 (ADC1_IN10) = CW  throttle pot  → Motor 1 clockwise
 *   adc_dma_buf[1] = PA3 (ADC1_IN3)  = CCW throttle pot  → Motor 1 counter-CW
 * Values are 12-bit [0, 4095]; rest (≤STEPPER_DEADBAND) = stopped.
 * The DMA keeps this buffer live without any CPU involvement. */
uint16_t adc_dma_buf[2] = {0u, 0u};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_ETH_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_UART5_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */
/* Forward declarations for manually-added UART init functions (not in .ioc) */
static void MX_UART4_Init(void);
static void MX_UART7_Init(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
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
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_ETH_Init();
  MX_USART3_UART_Init();
  MX_I2C1_Init();
  MX_UART5_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_ADC1_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */

  /* ── Initialise peripherals not in the .ioc (UART4 = ESP8266, UART7 = right VESC) */
  MX_UART4_Init();
  MX_UART7_Init();

  /* ── Initialise display, keypad, and OLED UI state machine ── */
  ssd1306_Init();
  KEYPAD_Init();
  UI_Init();          /* shows splash screen immediately */

  /* ── Initialise VESC motor driver (bldc_interface, left-motor callback) ── */
  VESC_Init();
  VESC_SetValuesCallback(on_vesc_values);

  /* ── Initialise stepper arm motor and start ADC DMA ── */
  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma_buf, 2);
  STEPPER_Init();

  /* ── Arm UART interrupt receivers ──
   * Each HAL_UART_Receive_IT call enables the RX interrupt for 1 byte.
   * After each byte arrives, HAL_UART_RxCpltCallback re-arms the interrupt. */
  HAL_UART_Receive_IT(&huart4, (uint8_t *)&esp_rx_byte,  1);  /* ESP8266  */
  HAL_UART_Receive_IT(&huart5, (uint8_t *)&vesc_rx_byte, 1);  /* left VESC telemetry */

  /* ── Debug banner over ST-Link serial ── */
  HAL_UART_Transmit(&huart3,
      (uint8_t *)"[BASE] Solar Robot firmware started\r\n", 38, 50);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

        /* ── 0. Stepper arm update ──
         *
         * STEPPER_Update() reads the latest DMA-filled ADC values and:
         *   • Picks target freq from whichever throttle pot is past deadband
         *       adc_dma_buf[0] → CW  pot  (PC0 / ADC1_IN10)
         *       adc_dma_buf[1] → CCW pot  (PA3 / ADC1_IN3)
         *   • Ramps current frequency toward target (avoids missed steps)
         *   • Updates TIM3 ARR/CCR1 for the new frequency in hardware
         *   • Toggles DIR only when motor is at rest (safe reversal) */
        STEPPER_Update(adc_dma_buf[0], adc_dma_buf[1]);

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
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 96;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 2;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_10;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* Fix 1 — rank 2 should sample PA3 (ADC1_IN3) for the CCW throttle pot.
   * CubeIDE leaves rank 2 = ADC_CHANNEL_10 (same as rank 1) by default,
   * and the .ioc still has PC3/IN13 wired — override to ADC_CHANNEL_3. */
  {
      ADC_ChannelConfTypeDef sfix = {0};
      sfix.Channel      = ADC_CHANNEL_3;
      sfix.Rank         = ADC_REGULAR_RANK_2;
      sfix.SamplingTime = ADC_SAMPLETIME_480CYCLES;
      if (HAL_ADC_ConfigChannel(&hadc1, &sfix) != HAL_OK)
      {
          Error_Handler();
      }
  }

  /* Fix 2 — MX_USART2_UART_Init() (called earlier) puts PA3 into AF7
   * (USART2_RX).  USART2 is unused in this firmware, so steal PA3 back
   * and re-configure it as an analog input for ADC1_IN3. */
  {
      __HAL_RCC_GPIOA_CLK_ENABLE();
      GPIO_InitTypeDef pa3 = {0};
      pa3.Pin  = GPIO_PIN_3;
      pa3.Mode = GPIO_MODE_ANALOG;
      pa3.Pull = GPIO_NOPULL;
      HAL_GPIO_Init(GPIOA, &pa3);
  }

  /* Fix 3 — CubeIDE set DMAContinuousRequests = DISABLE, which stops the
   * DMA after the first 2 conversions.  Enable it so circular DMA keeps
   * the adc_dma_buf[] refreshed indefinitely. */
  hadc1.Init.DMAContinuousRequests = ENABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
      Error_Handler();
  }

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief ETH Initialization Function
  * @param None
  * @retval None
  */
static void MX_ETH_Init(void)
{

  /* USER CODE BEGIN ETH_Init 0 */

  /* USER CODE END ETH_Init 0 */

   static uint8_t MACAddr[6];

  /* USER CODE BEGIN ETH_Init 1 */

  /* USER CODE END ETH_Init 1 */
  heth.Instance = ETH;
  MACAddr[0] = 0x00;
  MACAddr[1] = 0x80;
  MACAddr[2] = 0xE1;
  MACAddr[3] = 0x00;
  MACAddr[4] = 0x00;
  MACAddr[5] = 0x00;
  heth.Init.MACAddr = &MACAddr[0];
  heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
  heth.Init.TxDesc = DMATxDscrTab;
  heth.Init.RxDesc = DMARxDscrTab;
  heth.Init.RxBuffLen = 1524;

  /* USER CODE BEGIN MACADDRESS */

  /* USER CODE END MACADDRESS */

  if (HAL_ETH_Init(&heth) != HAL_OK)
  {
    Error_Handler();
  }

  memset(&TxConfig, 0 , sizeof(ETH_TxPacketConfig));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;
  /* USER CODE BEGIN ETH_Init 2 */

  /* USER CODE END ETH_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x2010091A;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 95;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 4999;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 2499;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief UART5 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART5_Init(void)
{

  /* USER CODE BEGIN UART5_Init 0 */

  /* USER CODE END UART5_Init 0 */

  /* USER CODE BEGIN UART5_Init 1 */

  /* USER CODE END UART5_Init 1 */
  huart5.Instance = UART5;
  huart5.Init.BaudRate = 115200;
  huart5.Init.WordLength = UART_WORDLENGTH_8B;
  huart5.Init.StopBits = UART_STOPBITS_1;
  huart5.Init.Parity = UART_PARITY_NONE;
  huart5.Init.Mode = UART_MODE_TX_RX;
  huart5.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart5.Init.OverSampling = UART_OVERSAMPLING_16;
  huart5.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart5.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart5) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN UART5_Init 2 */

  /* USER CODE END UART5_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, LD1_Pin|GPIO_PIN_10|GPIO_PIN_11|LD3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_13|GPIO_PIN_14, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9|GPIO_PIN_11, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(USB_PowerSwitchOn_GPIO_Port, USB_PowerSwitchOn_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(STEP_DIR_GPIO_Port, STEP_DIR_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : USER_Btn_Pin */
  GPIO_InitStruct.Pin = USER_Btn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USER_Btn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LD1_Pin PB10 PB11 LD3_Pin */
  GPIO_InitStruct.Pin = LD1_Pin|GPIO_PIN_10|GPIO_PIN_11|LD3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PF13 PF14 */
  GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_14;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /*Configure GPIO pins : PE9 PE11 */
  GPIO_InitStruct.Pin = GPIO_PIN_9|GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_PowerSwitchOn_Pin */
  GPIO_InitStruct.Pin = USB_PowerSwitchOn_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(USB_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_OverCurrent_Pin */
  GPIO_InitStruct.Pin = USB_OverCurrent_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USB_OverCurrent_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : STEP_DIR_Pin */
  GPIO_InitStruct.Pin = STEP_DIR_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(STEP_DIR_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : USB_SOF_Pin USB_DM_Pin USB_DP_Pin */
  GPIO_InitStruct.Pin = USB_SOF_Pin|USB_DM_Pin|USB_DP_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_VBUS_Pin */
  GPIO_InitStruct.Pin = USB_VBUS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USB_VBUS_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/**
 * MX_UART4_Init — ESP8266 click shield RX (PA1, 115200 baud).
 * Not generated by CubeIDE because UART4 is not in the .ioc.
 * Receives 4-byte joystick frames: [0xAA][x][y][0x55]
 */
static void MX_UART4_Init(void)
{
    /* HAL_UART_MspInit() has no UART4 case (not in .ioc), so configure
     * clock, GPIO, and NVIC manually here before calling HAL_UART_Init(). */
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA1 → UART4_RX (AF8).
     * NOTE: PA1 is also ETH_REF_CLK on the Nucleo.  If Ethernet is disabled
     * in the .ioc this is safe.  If Ethernet is needed, move UART4_RX to
     * PC11 (also AF8) and update the ESP8266 wiring accordingly. */
    GPIO_InitStruct.Pin       = GPIO_PIN_1;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(UART4_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(UART4_IRQn);

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
 * MX_UART7_Init — Right VESC ESC TX (PE8, 115200 baud).
 * Not generated by CubeIDE because UART7 is not in the .ioc.
 * TX only; right-motor telemetry is not collected.
 */
static void MX_UART7_Init(void)
{
    /* HAL_UART_MspInit() has no UART7 case (not in .ioc), so configure
     * clock and GPIO manually here before calling HAL_UART_Init(). */
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_UART7_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* PE8 → UART7_TX (AF8).  TX only — right VESC receives no telemetry. */
    GPIO_InitStruct.Pin       = GPIO_PIN_8;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_UART7;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
    /* No NVIC needed for TX-only — HAL_UART_Transmit_IT() enables TX interrupt
     * internally when called. */

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

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
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
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
