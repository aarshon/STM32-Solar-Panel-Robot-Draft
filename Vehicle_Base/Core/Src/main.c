/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    main.c
 * @brief   Solar Panel Robot — Vehicle Base Firmware (master)
 *          Board: STM32 Nucleo-F767ZI
 *
 * SYSTEM OVERVIEW
 * ───────────────
 *
 *   CTRL (Pi/operator) ──WiFi/BLE──► ESP32-S3 ──UART4──► Base STM32 ──UART1──► Arm STM32
 *
 *  All serial hops carry the same fixed 12-byte frame defined in
 *  comm_protocol.h:
 *
 *      0x41 0x5A | sysID mode xH xL yH yL zDir yaw | 0x59 0x42
 *
 *  Routing on Base is by sysID alone:
 *      0x01  CTRL→BASE : decode joystick → tank-drive both VESCs
 *      0x02  CTRL→ARM  : forward verbatim out the Arm UART
 *      0x03  ARM→CTRL  : forward verbatim out the bridge UART
 *
 * UART ASSIGNMENTS
 * ────────────────
 *   UART4  (PC10 TX / PC11 RX, 460800) ↔ ESP32-S3 bridge   [comm_protocol]
 *   UART1  (PB6  TX / PB7  RX, 115200) ↔ Arm STM32         [comm_protocol]
 *   UART5  (PC12 TX / PD2  RX, 115200) ↔ Left VESC ESC     [bldc_interface]
 *   UART7  (PE8  TX,           115200) → Right VESC ESC    [bldc_interface]
 *   USART3 (PD8  TX, 115200)           → ST-Link VCP debug
 *   I2C1   (PB8 SCL / PB9 SDA)         ↔ SSD1306 OLED 128×64
 *
 * SAFETY WATCHDOG
 * ───────────────
 *   If no SYS_CTRL_TO_BASE frame arrives within CMD_TIMEOUT_MS (500 ms),
 *   both motors are stopped. The VESC ESC has its own independent 1 s
 *   keep-alive timeout as a second layer.
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
#include "comm_protocol.h"
#include "battery.h"
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/** CMD_TIMEOUT_MS — stop motors if no SYS_CTRL_TO_BASE frame arrives within
 *  this window. 500 ms matches the VESC firmware's own keep-alive timeout;
 *  the VESC will self-stop after 1 s as a second layer. */
#define CMD_TIMEOUT_MS      500u

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
UART_HandleTypeDef huart4;   /* UART4  (PC11 RX / PC10 TX) — ESP32-S3 bridge */
UART_HandleTypeDef huart7;   /* UART7  (PE8 TX)           — Right VESC ESC   */

/* ── Single-byte RX staging for each UART stream ─────────────────────────
 * Each byte fires HAL_UART_RxCpltCallback, which feeds the parser FSM in
 * comm_protocol.c and re-arms the interrupt. */
static volatile uint8_t bridge_rx_byte = 0;  /* UART4 ← ESP32-S3              */
static volatile uint8_t arm_rx_byte    = 0;  /* UART1 ← Arm STM32             */

/* ── Left VESC telemetry RX (UART5) ───────────────────────────────────── */
static volatile uint8_t vesc_rx_byte  = 0;

/* ── ADC1 circular-DMA destination for battery sense (PC3, IN13) ──────── */
static volatile uint16_t adc_battery_raw = 0;

/* ── Motor watchdog state (still consumed by ui.c for REMOTE badge) ───── */
uint32_t lastCmdTime  = 0;   /* HAL_GetTick() of last valid drive frame     */
uint8_t  motorRunning = 0;   /* 1 = motors currently commanded              */

/* ── Keypad ───────────────────────────────────────────────────────────────*/
static uint8_t pendingKey = KEY_NONE;

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
/* Forward declarations for manually-added peripheral init (not in .ioc) */
static void MX_UART4_Init(void);
static void MX_UART7_Init(void);

/* Local handler for SYS_CTRL_TO_BASE frames (registered after COMM_Init) */
static void handle_base_frame(const comm_frame_t *frame);
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
 * HAL_UART_RxCpltCallback — 1-byte interrupt completion from any UART.
 *
 *   UART4 (bridge stream) → COMM_FeedByte(COMM_STREAM_BRIDGE, byte)
 *   UART1 (arm stream)    → COMM_FeedByte(COMM_STREAM_ARM, byte)
 *   UART5 (Left VESC)     → bldc_interface packet decoder
 *
 * SYS_CTRL_TO_ARM and SYS_ARM_TO_CTRL frames are forwarded directly inside
 * the parser; only SYS_CTRL_TO_BASE frames invoke the local handler below.
 * ────────────────────────────────────────────────────────────────────────── */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == UART4)
    {
        COMM_FeedByte(COMM_STREAM_BRIDGE, bridge_rx_byte);
        HAL_UART_Receive_IT(&huart4, (uint8_t *)&bridge_rx_byte, 1);
    }
    else if (huart->Instance == USART1)
    {
        COMM_FeedByte(COMM_STREAM_ARM, arm_rx_byte);
        HAL_UART_Receive_IT(&huart1, (uint8_t *)&arm_rx_byte, 1);
    }
    else if (huart->Instance == UART5)
    {
        VESC_ProcessRxByte((uint8_t)vesc_rx_byte);
        HAL_UART_Receive_IT(&huart5, (uint8_t *)&vesc_rx_byte, 1);
    }
}

/* ──────────────────────────────────────────────────────────────────────────
 * Local handler for SYS_CTRL_TO_BASE frames.
 * Decodes the unsigned uint16 joystick (centre = 32768) into a normalised
 * [-1.0, +1.0] float pair and feeds VESC_JoystickDrive's tank-drive mixer.
 * mode/zDir/yaw are ignored on Base.
 * ────────────────────────────────────────────────────────────────────────── */
static void handle_base_frame(const comm_frame_t *frame)
{
    /* Map [0..65535] with centre 32768 → [-1.0, +1.0]. Divisor is the
     * larger half-range (32767) so full-positive maps cleanly to +1. */
    float x = ((float)frame->x - (float)JOY_CENTRE) / 32767.0f;
    float y = ((float)frame->y - (float)JOY_CENTRE) / 32767.0f;
    if (x < -1.0f) x = -1.0f; else if (x > 1.0f) x = 1.0f;
    if (y < -1.0f) y = -1.0f; else if (y > 1.0f) y = 1.0f;

    VESC_JoystickDrive(x, y);

    lastCmdTime  = HAL_GetTick();
    motorRunning = 1;
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

  /* ── Initialise peripherals not in the .ioc ── */
  MX_UART4_Init();       /* PI bridge link — UART4 (PC11 RX, PC10 TX) 460800 */
  MX_UART7_Init();       /* Right VESC TX                                    */
  /* ADC1 is initialised by the auto-generated MX_ADC1_Init() called from
   * main() above; the battery-sense override lives in its USER CODE 2 block. */

  /* ── Initialise display, keypad, and OLED UI state machine ── */
  ssd1306_Init();
  KEYPAD_Init();
  UI_Init();          /* shows splash screen immediately */

  /* ── Initialise VESC motor driver (bldc_interface, left-motor callback) ── */
  VESC_Init();
  VESC_SetValuesCallback(on_vesc_values);

  /* ── Battery monitor ── */
  BATTERY_Init(&adc_battery_raw, 0.1754f, 12.6f, 9.0f);

  /* ── Communication protocol: bind streams + register local handler ── */
  COMM_Init(&huart4, &huart1);
  COMM_SetLocalHandler(handle_base_frame);

  /* ── Start ADC1 circular DMA into the single-word battery slot ── */
  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&adc_battery_raw, 1);

  /* ── Arm UART interrupt receivers (byte-at-a-time) ── */
  HAL_UART_Receive_IT(&huart4, (uint8_t *)&bridge_rx_byte, 1);  /* ESP32-S3 */
  HAL_UART_Receive_IT(&huart1, (uint8_t *)&arm_rx_byte,    1);  /* Arm STM32 */
  HAL_UART_Receive_IT(&huart5, (uint8_t *)&vesc_rx_byte,   1);  /* Left VESC */

  /* ── Debug banner over ST-Link serial ── */
  HAL_UART_Transmit(&huart3,
      (uint8_t *)"[BASE] comm v2 online\r\n", 23, 50);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

        uint32_t now = HAL_GetTick();

        /* ── 1. Keypad scan ── */
        pendingKey = KEYPAD_Scan();

        /* ── 2. OLED UI update ── */
        UI_Update(pendingKey);

        /* ── 3. Battery sense IIR (rate-limited to 1 Hz internally) ── */
        BATTERY_Update();

        /* Auto-stop drive on battery critical (no latch — reasserts each loop
         * while critical, joystick takes over again as soon as voltage recovers). */
        if (BATTERY_IsCritical() && motorRunning) {
            VESC_Stop();
            motorRunning = 0;
        }

        /* ── 4. Motor watchdog — no DRIVE frame for CMD_TIMEOUT_MS → stop. */
        if (motorRunning && (now - lastCmdTime > CMD_TIMEOUT_MS))
        {
            VESC_Stop();
            motorRunning = 0;
            UI_ForceRedraw();
            HAL_UART_Transmit(&huart3,
                (uint8_t *)"[BASE] Watchdog: motor stop\r\n", 29, 20);
        }

        /* ── 5. VESC telemetry poll (5 Hz) ── */
        static uint32_t lastPoll = 0;
        if (now - lastPoll >= 200u)
        {
            lastPoll = now;
            VESC_RequestValues();
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
  /* Override Cube's dual-rank (stepper-era) config — only rank 1 = IN13 (PC3,
   * battery divider) is used, with circular DMA into adc_battery_raw.
   * HAL_ADC_Init on a non-RESET state re-applies the register config without
   * re-running MspInit, so the DMA linkage set up above is preserved. */
  hadc1.Init.ScanConvMode          = DISABLE;
  hadc1.Init.NbrOfConversion       = 1;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

  sConfig.Channel      = ADC_CHANNEL_13;
  sConfig.Rank         = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_56CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();
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
 * MX_UART4_Init — ESP32-S3 bridge link (PC11 RX, PC10 TX, 460800 baud 8N1).
 *
 * Not in the .ioc so the GPIO alternate-function setup happens here.
 *
 * Why PC11 instead of PA1: on the Nucleo-F767ZI the on-board LAN8742A PHY
 * drives 50 MHz REFCLK onto PA1 by hardware, regardless of whether the
 * STM32 ETH peripheral is configured. Using PA1 as UART RX caused
 * continuous spurious RXNE → ISR storm → main loop never ran.
 * UART4_RX is also routable to PC11 (AF8); same alternate function.
 */
static void MX_UART4_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PC11 — UART4_RX */
    gpio.Pin       = GPIO_PIN_11;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;                 /* idle-high for bare 3V3 link */
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* PC10 — UART4_TX */
    gpio.Pin       = GPIO_PIN_10;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOC, &gpio);

    huart4.Instance            = UART4;
    huart4.Init.BaudRate       = 460800;
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

    /* NVIC — priority 5 matches UART5/UART7. */
    HAL_NVIC_SetPriority(UART4_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(UART4_IRQn);
}

/**
 * MX_UART7_Init — Right VESC ESC TX (PE8, 115200 baud).
 * TX only; right-motor telemetry is not collected.
 */
static void MX_UART7_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_UART7_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* PE8 — UART7_TX */
    gpio.Pin       = GPIO_PIN_8;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF8_UART7;
    HAL_GPIO_Init(GPIOE, &gpio);

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

    HAL_NVIC_SetPriority(UART7_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(UART7_IRQn);
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
