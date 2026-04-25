/*
 * main.c  —  Arm/EE STM32 (ADDR_ARM_EE)
 * Board : STM32 Nucleo-F767ZI
 *
 * Talks to Vehicle_Base over USART6 (PG14 TX / PG9 RX, 115200 8N1) using
 * the SRR-CP protocol. Hosts 3 stepper joints driven by TIM1 + Tyler's
 * stepper.c / kinematics.c / motor_config.c (kept untouched).
 *
 * Wiring to Base (3 jumpers):
 *   Arm PG14 (USART6_TX)  ──────  Base PB7 (USART1_RX)
 *   Arm PG9  (USART6_RX)  ──────  Base PB6 (USART1_TX)
 *   Arm GND               ──────  Base GND
 *
 * USART6 leaves PD8/PD9 (ST-Link VCP) free, so you can still bring up a
 * separate UART_HandleTypeDef on USART3 for printf-over-USB if you want
 * arm-side debug output. Not initialised here.
 */

#include "main.h"
#include "motor_config.h"
#include "kinematics.h"
#include "stepper.h"

#include "comm_protocol.h"
#include "comm_arm_handlers.h"
#include "arm_motion.h"

/* --- Peripheral handles ----------------------------------------------- */
TIM_HandleTypeDef  htim1;
UART_HandleTypeDef huart6;

/* --- Forward declarations --------------------------------------------- */
void        SystemClock_Config(void);
void        MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART6_UART_Init(void);

/* --- SRR-CP RX byte staging (single-byte IT receive) ----------------- */
static volatile uint8_t s_base_rx_byte = 0;

/* =========================================================================
 * UART RX byte completion — feeds the SRR-CP parser FSM
 * ========================================================================= */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART6)
    {
        COMM_FeedByte(COMM_STREAM_SLAVE, s_base_rx_byte);
        HAL_UART_Receive_IT(&huart6, (uint8_t *)&s_base_rx_byte, 1);
    }
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    MPU_Config();
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_TIM1_Init();
    MX_USART6_UART_Init();

    HAL_TIM_Base_Start_IT(&htim1);

    /* Tyler's hardware init — assigns motor structs and enables drivers. */
    Motors_Init();

    /* SRR-CP: bind the parser to USART6 (slave-stream-only on this node)
     * and register the arm-side command handlers. */
    COMM_Init(NULL, &huart6);
    Arm_RegisterAllHandlers();

    /* Arm the byte-at-a-time RX interrupt — handler re-arms after each. */
    HAL_UART_Receive_IT(&huart6, (uint8_t *)&s_base_rx_byte, 1);

    uint32_t last_hb_tx = 0;

    while (1)
    {
        uint32_t now = HAL_GetTick();

        /* 1. Periodic heartbeat to Base (1 Hz) so Base's slave-link UI
         *    indicator stays alive and we get the same in return. */
        if ((now - last_hb_tx) >= 1000u)
        {
            last_hb_tx = now;
            Arm_SendHeartbeat(now);
        }

        /* 2. Watchdog — if Base goes silent for >BASE_TIMEOUT_MS, halt
         *    motors locally. Latch persists until ESTOP_CLEAR arrives. */
        Arm_CheckBaseLiveness(now);

        /* 3. (TYLER) any non-ISR motion bookkeeping goes here. The step
         *    pulse generation runs in HAL_TIM_PeriodElapsedCallback. */
    }
}

/* =========================================================================
 * Clocks — HSI 16 MHz, no PLL. (TYLER: bring up to PLL when ramping is in
 * place; current step rate is ~3.8 kHz which caps joint speed at ~4.5°/s.)
 * ========================================================================= */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

    osc.OscillatorType        = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState              = RCC_HSI_ON;
    osc.HSICalibrationValue   = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState          = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    clk.ClockType             = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource          = RCC_SYSCLKSOURCE_HSI;
    clk.AHBCLKDivider         = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider        = RCC_HCLK_DIV1;
    clk.APB2CLKDivider        = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0) != HAL_OK) Error_Handler();
}

/* =========================================================================
 * TIM1 — step-pulse ISR clock (Tyler tunes prescaler/period for step rate)
 * ========================================================================= */
static void MX_TIM1_Init(void)
{
    TIM_ClockConfigTypeDef clkcfg  = {0};
    TIM_MasterConfigTypeDef mscfg  = {0};

    htim1.Instance               = TIM1;
    htim1.Init.Prescaler         = 84 - 1;          /* TYLER: tune for clock */
    htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim1.Init.Period            = 50 - 1;          /* TYLER: tune for ISR rate */
    htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim1);

    clkcfg.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    HAL_TIM_ConfigClockSource(&htim1, &clkcfg);

    mscfg.MasterOutputTrigger = TIM_TRGO_RESET;
    mscfg.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim1, &mscfg);

    HAL_NVIC_SetPriority(TIM1_UP_TIM10_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM1_UP_TIM10_IRQn);
}

/* =========================================================================
 * USART6 — SRR-CP slave link to Vehicle_Base (PG14 TX / PG9 RX)
 *
 * Hand-rolled because the .ioc was generated minimal (no UART block).
 * Tyler can later add USART6 to the .ioc; Cube's auto-generated init will
 * land in main() above this and supersede this function.
 *
 * Pin alternates: USART6_TX is also available on PC6, USART6_RX on PC7.
 * If your wiring uses those instead, swap the AF mapping + GPIO clock here.
 * ========================================================================= */
static void MX_USART6_UART_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_USART6_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* PG14 = USART6_TX, PG9 = USART6_RX (both AF8) */
    gpio.Pin       = GPIO_PIN_14 | GPIO_PIN_9;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;                   /* idle-high for bare 3V3 */
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF8_USART6;
    HAL_GPIO_Init(GPIOG, &gpio);

    huart6.Instance            = USART6;
    huart6.Init.BaudRate       = 115200;
    huart6.Init.WordLength     = UART_WORDLENGTH_8B;
    huart6.Init.StopBits       = UART_STOPBITS_1;
    huart6.Init.Parity         = UART_PARITY_NONE;
    huart6.Init.Mode           = UART_MODE_TX_RX;
    huart6.Init.HwFlowCtl      = UART_HWCONTROL_NONE;
    huart6.Init.OverSampling   = UART_OVERSAMPLING_16;
    huart6.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart6.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart6) != HAL_OK) Error_Handler();

    HAL_NVIC_SetPriority(USART6_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART6_IRQn);
}

/* =========================================================================
 * GPIO — stepper STEP/DIR/ENA pins (Tyler's pin map)
 *
 * Motor 1: STEP=PA3, DIR=PC0, ENA=PF6
 * Motor 2: STEP=PC3, DIR=PF3, ENA=PF7
 * Motor 3: STEP=PF5, DIR=PF10, ENA=PF8
 * ========================================================================= */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    /* Default states — STEP/DIR low, ENA high (drivers are typically
     * active-low; Stepper_Enable(true) drives ENA low). */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_3, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0 | GPIO_PIN_3, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOF,
        GPIO_PIN_3 | GPIO_PIN_5 | GPIO_PIN_6 |
        GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_10,
        GPIO_PIN_SET);

    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    /* Motor 1 */
    gpio.Pin = GPIO_PIN_3;                  HAL_GPIO_Init(GPIOA, &gpio);  /* STEP */
    gpio.Pin = GPIO_PIN_0;                  HAL_GPIO_Init(GPIOC, &gpio);  /* DIR  */
    gpio.Pin = GPIO_PIN_6;                  HAL_GPIO_Init(GPIOF, &gpio);  /* ENA  */

    /* Motor 2 */
    gpio.Pin = GPIO_PIN_3;                  HAL_GPIO_Init(GPIOC, &gpio);  /* STEP */
    gpio.Pin = GPIO_PIN_3 | GPIO_PIN_7;     HAL_GPIO_Init(GPIOF, &gpio);  /* DIR + ENA */

    /* Motor 3 */
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_10 | GPIO_PIN_8;
    HAL_GPIO_Init(GPIOF, &gpio);
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/* =========================================================================
 * MPU — keep Cube's default null-pointer trap region.
 * ========================================================================= */
void MPU_Config(void)
{
    MPU_Region_InitTypeDef r = {0};

    HAL_MPU_Disable();

    r.Enable             = MPU_REGION_ENABLE;
    r.Number             = MPU_REGION_NUMBER0;
    r.BaseAddress        = 0x0;
    r.Size               = MPU_REGION_SIZE_4GB;
    r.SubRegionDisable   = 0x87;
    r.TypeExtField       = MPU_TEX_LEVEL0;
    r.AccessPermission   = MPU_REGION_NO_ACCESS;
    r.DisableExec        = MPU_INSTRUCTION_ACCESS_DISABLE;
    r.IsShareable        = MPU_ACCESS_SHAREABLE;
    r.IsCacheable        = MPU_ACCESS_NOT_CACHEABLE;
    r.IsBufferable       = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&r);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) { }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file; (void)line;
}
#endif
