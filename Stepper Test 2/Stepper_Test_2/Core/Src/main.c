/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    main.c
 * @brief   Stepper Test 2 — Arm STM32 single-joint bring-up firmware
 *          Board: STM32 Nucleo-F767ZI
 *
 * SYSTEM OVERVIEW
 * ───────────────
 *
 *   CTRL (Pi) ─MQTT─► ESP32-S3 ─UART4─► Vehicle_Base ─USART1─► [THIS BOARD]
 *
 *  This firmware is the simplest possible Arm-side test: receive 12-byte
 *  SYS_CTRL_TO_ARM frames from Vehicle_Base on USART1, decode the joystick
 *  Y axis, drive one stepper through TIM3 CH1 PWM + a DIR GPIO. Diagnostic
 *  state shown on an SSD1306 OLED.
 *
 *  Frame format (mirror of comm_protocol.h):
 *      0x41 0x5A | sysID mode xH xL yH yL zDir yaw | 0x59 0x42
 *  We only act on sysID = SYS_CTRL_TO_ARM (0x02), mode = MODE_CONTROLLER (0x01).
 *
 * UART ASSIGNMENTS
 * ────────────────
 *   USART1 (PB6 TX / PB7 RX, 115200) ↔ Vehicle_Base USART1 link
 *   USART3 (PD8 TX,           115200) → ST-Link VCP debug
 *   I2C1   (PB8 SCL / PB9 SDA)        ↔ SSD1306 OLED 128×64
 *   TIM3   CH1 (PB4, AF2, PWM 50%)    → TB6600 PUL+
 *   PC8    (GPIO output)              → TB6600 DIR+
 *
 * SAFETY WATCHDOG
 * ───────────────
 *   If no SYS_CTRL_TO_ARM frame arrives within ARM_RX_TIMEOUT_MS (500 ms),
 *   the target frequency is forced to 0 and the stepper ramps to a stop.
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "string.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "comm_protocol.h"
#include "stepper.h"
#include "screen_stepper.h"
#include "ssd1306.h"
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/** ARM_RX_TIMEOUT_MS — stop motor if no SYS_CTRL_TO_ARM frame in this window. */
#define ARM_RX_TIMEOUT_MS    500u

/** JOY_DEADBAND — counts around the 32768 centre treated as "no motion". */
#define JOY_DEADBAND         3000u

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */

/* ── Single-byte RX staging for USART1 ───────────────────────────────────
 * Each byte fires HAL_UART_RxCpltCallback, which feeds the parser FSM in
 * comm_protocol.c and re-arms the interrupt. */
static volatile uint8_t base_rx_byte = 0;

/* ── Frame handler state ──────────────────────────────────────────────────*/
static uint32_t last_rx_ms = 0;
static int32_t  target_hz  = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM3_Init(void);
/* USER CODE BEGIN PFP */
static void handle_arm_frame(const comm_frame_t *frame);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ──────────────────────────────────────────────────────────────────────────
 * SYS_CTRL_TO_ARM frame handler
 * Decodes the joystick X axis (uint16, centre 32768, big-endian on the wire,
 * already de-byte-swapped by the parser into frame->x) and turns it into a
 * signed step frequency target — left/right on the stick → CCW/CW rotation.
 * ────────────────────────────────────────────────────────────────────────── */
static void handle_arm_frame(const comm_frame_t *frame)
{
    /* v1: only act on MODE_CONTROLLER. ZEROG/HOMING are no-ops here. */
    if (frame->mode != MODE_CONTROLLER) return;

    /* Convert unsigned uint16 (centre 32768) to signed offset. */
    int32_t offset = (int32_t)frame->x - (int32_t)JOY_CENTRE;

    /* Apply a deadband around centre so a slightly off-centre stick doesn't
     * keep the motor crawling. */
    if (offset >  (int32_t)JOY_DEADBAND)      offset -= (int32_t)JOY_DEADBAND;
    else if (offset < -(int32_t)JOY_DEADBAND) offset += (int32_t)JOY_DEADBAND;
    else                                       offset  = 0;

    /* Linear map to [-STEPPER_FREQ_MAX, +STEPPER_FREQ_MAX]. The maximum
     * post-deadband magnitude is (32768 - JOY_DEADBAND); divide by it so
     * full deflection saturates exactly at STEPPER_FREQ_MAX. */
    const int32_t max_off = 32768 - (int32_t)JOY_DEADBAND;
    int32_t hz = (offset * (int32_t)STEPPER_FREQ_MAX) / max_off;

    target_hz  = hz;
    last_rx_ms = HAL_GetTick();
}

/* ──────────────────────────────────────────────────────────────────────────
 * HAL_UART_RxCpltCallback — 1-byte interrupt completion from USART1.
 * Forwards the byte to comm_protocol's parser FSM and re-arms.
 * ────────────────────────────────────────────────────────────────────────── */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        COMM_FeedByte(COMM_STREAM_BRIDGE, base_rx_byte);
        HAL_UART_Receive_IT(&huart1, (uint8_t *)&base_rx_byte, 1);
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
  MX_USART3_UART_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_TIM3_Init();
  /* USER CODE BEGIN 2 */

  /* ── Display + stepper modules ── */
  ssd1306_Init();
  STEPPER_Init();

  /* ── Comm protocol: USART1 is our only link.
   * COMM_STREAM_BRIDGE is bound to it so 0x02 frames dispatch locally
   * (compiled in via COMM_ROLE_ARM in comm_protocol.c). */
  COMM_Init(&huart1, NULL);
  COMM_SetLocalHandler(handle_arm_frame);

  /* Arm the byte-at-a-time RX interrupt. */
  HAL_UART_Receive_IT(&huart1, (uint8_t *)&base_rx_byte, 1);

  /* Debug banner over ST-Link VCP. */
  HAL_UART_Transmit(&huart3,
      (uint8_t *)"[ARM] Stepper Test 2 online\r\n", 29, 50);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    uint32_t now = HAL_GetTick();

    /* ── 1. RX watchdog — no frame for ARM_RX_TIMEOUT_MS → halt ── */
    if (now - last_rx_ms > ARM_RX_TIMEOUT_MS) {
        target_hz = 0;
    }

    /* ── 2. Push latest target to the stepper module + run one tick ── */
    STEPPER_SetTargetFreq(target_hz);
    STEPPER_Tick();

    /* ── 3. OLED redraw at ~10 Hz ── */
    static uint32_t last_draw = 0;
    if (now - last_draw >= 100u) {
        last_draw = now;
        ScreenStepper_Draw();
    }

    /* 1 ms pacing to match the stepper ramp tuning (STEPPER_RAMP_STEP per
     * call ≈ 100 Hz/ms). */
    HAL_Delay(1);

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

  /** Configure LSE Drive Capability */
  HAL_PWR_EnableBkUpAccess();

  /** Configure the main internal regulator output voltage */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators (HSE 8 MHz from ST-Link MCO → PLL → 192 MHz).
   *  Matches Vehicle_Base for parity. */
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

  /** Activate the Over-Drive mode */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks */
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
  * @brief I2C1 Initialization Function
  */
static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x2010091A;          /* 100 kHz @ 96 MHz APB1 */
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)               { Error_Handler(); }
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1,
        I2C_ANALOGFILTER_ENABLE) != HAL_OK)         { Error_Handler(); }
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0)
        != HAL_OK)                                  { Error_Handler(); }
}

/**
  * @brief USART1 Initialization Function — 115200 8N1, PB6 TX / PB7 RX
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate     = 115200;
  huart1.Init.WordLength   = UART_WORDLENGTH_8B;
  huart1.Init.StopBits     = UART_STOPBITS_1;
  huart1.Init.Parity       = UART_PARITY_NONE;
  huart1.Init.Mode         = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling     = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK) { Error_Handler(); }
}

/**
  * @brief USART3 Initialization Function — 115200 8N1, ST-Link VCP
  */
static void MX_USART3_UART_Init(void)
{
  huart3.Instance = USART3;
  huart3.Init.BaudRate     = 115200;
  huart3.Init.WordLength   = UART_WORDLENGTH_8B;
  huart3.Init.StopBits     = UART_STOPBITS_1;
  huart3.Init.Parity       = UART_PARITY_NONE;
  huart3.Init.Mode         = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling     = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK) { Error_Handler(); }
}

/**
  * @brief TIM3 Initialization Function — CH1 PWM on PB4 (AF2)
  *        Prescaler 95 → 1 µs tick. ARR/CCR1 are runtime-overwritten by
  *        stepper.c to set the step frequency; values here are placeholders
  *        until the first STEPPER_SetTargetFreq() call.
  */
static void MX_TIM3_Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  htim3.Instance = TIM3;
  htim3.Init.Prescaler         = 95;
  htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim3.Init.Period            = 4999;
  htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK) { Error_Handler(); }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
                                          { Error_Handler(); }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) { Error_Handler(); }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
                                          { Error_Handler(); }
  sConfigOC.OCMode     = TIM_OCMODE_PWM1;
  sConfigOC.Pulse      = 2499;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1)
        != HAL_OK)                        { Error_Handler(); }
  HAL_TIM_MspPostInit(&htim3);
}

/**
  * @brief GPIO Initialization Function
  *        Only the pins this project owns: PC8 (STEP_DIR output) and the
  *        on-board LEDs / user button. CubeMX will add the AF setup for
  *        TIM3_CH1 (PB4) and I2C1 / USART1 / USART3 pins via their MSPs.
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* PC8 — DIR pin to TB6600 driver */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_8, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin   = GPIO_PIN_8;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

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
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

/**
  * @brief  This function is executed in case of error occurrence.
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1) { }
}

#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* User can add own implementation. */
}
#endif /* USE_FULL_ASSERT */
