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
#include "comm_protocol.h"
#include "estop.h"
#include "battery.h"
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/** CMD_TIMEOUT_MS — stop motors if no DRIVE frame arrives within this window.
 *  500 ms matches the VESC firmware's own keep-alive timeout. The VESC will
 *  also self-stop after 1 s — this fires first. */
#define CMD_TIMEOUT_MS      500u

/** Slave liveness: expect a HEARTBEAT frame at least every 1 s on UART1. */
#define SLAVE_TIMEOUT_MS    1000u

/** Base emits its own HEARTBEAT to the slave at 2 Hz. */
#define HEARTBEAT_PERIOD_MS 500u

/** Telemetry STATUS_STREAM push to Pi at 4 Hz. */
#define TELEMETRY_PERIOD_MS 250u

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

ETH_HandleTypeDef heth;

I2C_HandleTypeDef hi2c1;

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

UART_HandleTypeDef huart5;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */

/* ── UART handles for peripherals NOT in the .ioc file ───────────────────
 * huart4 and huart7 were added manually (the .ioc only has UART5/USART3).
 * Placed here inside USER CODE so they survive CubeIDE code regeneration.
 * Their MSP (GPIO + clock init) is in MX_UART4_Init / MX_UART7_Init below. */
UART_HandleTypeDef huart4;   /* UART4  (PA1 RX / PC10 TX) — ESP32-S3 bridge  */
UART_HandleTypeDef huart7;   /* UART7  (PE8 TX)           — Right VESC ESC   */

/* ── SRR-CP single-byte RX staging for each UART stream ──────────────────
 * Each byte fires HAL_UART_RxCpltCallback, which feeds the parser FSM in
 * comm_protocol.c and re-arms the interrupt. */
static volatile uint8_t pi_rx_byte    = 0;  /* UART4 ← ESP32-S3               */
static volatile uint8_t slave_rx_byte = 0;  /* UART1 ← Arm/EE STM32           */

/* ── Left VESC telemetry RX (UART5) ───────────────────────────────────── */
static volatile uint8_t vesc_rx_byte  = 0;

/* ── ADC1 circular-DMA destination for battery sense (PC3, IN13) ──────── */
static volatile uint16_t adc_battery_raw = 0;

/* ── Motor watchdog state (still consumed by ui.c for REMOTE badge) ───── */
uint32_t lastCmdTime  = 0;   /* HAL_GetTick() of last valid DRIVE frame     */
uint8_t  motorRunning = 0;   /* 1 = motors currently commanded              */

/* ── Slave-link liveness (last HEARTBEAT arrival on UART1) ────────────── */
static uint32_t slave_last_seen_ms = 0;

/* ── Keypad ───────────────────────────────────────────────────────────────*/
static uint8_t pendingKey = KEY_NONE;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_ETH_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_UART5_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
/* Forward declarations for manually-added peripheral init (not in .ioc) */
static void MX_UART4_Init(void);
static void MX_UART7_Init(void);
static void MX_ADC1_Init(void);
static void MX_ESTOP_GPIO_Init(void);

/* SRR-CP local handlers (registered with COMM_RegisterHandler after init) */
static void handle_drive       (uint8_t src, const uint8_t *p, uint8_t len);
static void handle_drive_stop  (uint8_t src, const uint8_t *p, uint8_t len);
static void handle_estop_assert(uint8_t src, const uint8_t *p, uint8_t len);
static void handle_estop_clear (uint8_t src, const uint8_t *p, uint8_t len);
static void handle_estop_query (uint8_t src, const uint8_t *p, uint8_t len);
static void handle_status_req  (uint8_t src, const uint8_t *p, uint8_t len);
static void handle_heartbeat   (uint8_t src, const uint8_t *p, uint8_t len);
static void handle_ping        (uint8_t src, const uint8_t *p, uint8_t len);
static void handle_fault_report(uint8_t src, const uint8_t *p, uint8_t len);
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
 *   UART4 (PI stream)    → COMM_FeedByte(COMM_STREAM_PI, byte)
 *   UART1 (SLAVE stream) → COMM_FeedByte(COMM_STREAM_SLAVE, byte)
 *   UART5 (Left VESC)    → bldc_interface packet decoder
 *
 * Everything else (handler dispatch, frame emission) happens in main loop.
 * ────────────────────────────────────────────────────────────────────────── */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == UART4)
    {
        COMM_FeedByte(COMM_STREAM_PI, pi_rx_byte);
        HAL_UART_Receive_IT(&huart4, (uint8_t *)&pi_rx_byte, 1);
    }
    else if (huart->Instance == USART1)
    {
        COMM_FeedByte(COMM_STREAM_SLAVE, slave_rx_byte);
        HAL_UART_Receive_IT(&huart1, (uint8_t *)&slave_rx_byte, 1);
    }
    else if (huart->Instance == UART5)
    {
        VESC_ProcessRxByte((uint8_t)vesc_rx_byte);
        HAL_UART_Receive_IT(&huart5, (uint8_t *)&vesc_rx_byte, 1);
    }
}

/* ──────────────────────────────────────────────────────────────────────────
 * SRR-CP handler bodies (registered in main() after COMM_Init)
 * ────────────────────────────────────────────────────────────────────────── */

/* CMD_DRIVE — payload [x_byte, y_byte]. Both uint8, 128 = centre. */
static void handle_drive(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src;
    if (len < 2) return;
    if (ESTOP_IsActive()) return;  /* latch suppresses motor commands */

    uint8_t x_byte = p[0];
    uint8_t y_byte = p[1];

    float x = ((float)x_byte - 127.5f) / 127.5f;
    float y = ((float)y_byte - 127.5f) / 127.5f;

    VESC_JoystickDrive(x, y);

    lastCmdTime  = HAL_GetTick();
    motorRunning = 1;
}

static void handle_drive_stop(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src; (void)p; (void)len;
    VESC_Stop();
    motorRunning = 0;
    lastCmdTime  = HAL_GetTick();
}

static void handle_estop_assert(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src;
    uint8_t reason = (len >= 1) ? p[0] : FAULT_ESTOP_SOFTWARE;
    /* Someone else on the bus raised e-stop — latch locally too. */
    ESTOP_AssertSoftware(reason);
}

static void handle_estop_clear(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src; (void)p; (void)len;
    /* Only honour remote clears when we're already latched; the local ESTOP
     * button still overrides via ESTOP_IsActive() until the operator runs the
     * keypad sequence. Remote CLEAR is informational only here. */
}

static void handle_estop_query(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)p; (void)len;
    uint8_t reply[3];
    reply[0] = FIELD_ESTOP_STATE;
    reply[1] = ESTOP_IsActive() ? 1u : 0u;
    reply[2] = ESTOP_Reason();
    COMM_Send(src, CMD_STATUS_REPLY, reply, 3);
}

static void handle_status_req(uint8_t src, const uint8_t *p, uint8_t len)
{
    if (len < 1) return;
    uint8_t field = p[0];
    uint8_t reply[3] = { field, 0, 0 };

    switch (field) {
        case FIELD_BATT_MV: {
            uint16_t mv = (uint16_t)(BATTERY_GetVoltage() * 1000.0f);
            reply[1] = (uint8_t)(mv >> 8);
            reply[2] = (uint8_t)(mv & 0xFFu);
            break;
        }
        case FIELD_BATT_PCT:
            reply[1] = 0;
            reply[2] = BATTERY_GetPercent();
            break;
        case FIELD_ESTOP_STATE:
            reply[1] = ESTOP_IsActive() ? 1u : 0u;
            reply[2] = ESTOP_Reason();
            break;
        case FIELD_SLAVE_HB_AGE: {
            uint32_t age = HAL_GetTick() - slave_last_seen_ms;
            if (age > 65535u) age = 65535u;
            reply[1] = (uint8_t)(age >> 8);
            reply[2] = (uint8_t)(age & 0xFFu);
            break;
        }
        case FIELD_FW_VERSION:
            reply[1] = 1;   /* major */
            reply[2] = 0;   /* minor */
            break;
        default:
            /* Unknown field — return zeros so the requester sees "field=0". */
            break;
    }
    COMM_Send(src, CMD_STATUS_REPLY, reply, 3);
}

static void handle_heartbeat(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)p; (void)len;
    if (src == ADDR_ARM_EE) {
        slave_last_seen_ms = HAL_GetTick();
    }
}

static void handle_ping(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)p; (void)len;
    COMM_Send(src, CMD_PONG, NULL, 0);
}

static void handle_fault_report(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src;
    if (len < 2) return;
    /* Log the last slave fault; ui.c mirrors it. CRIT escalation is
     * deferred until the fault severity table is populated. */
    UI_State_t *ui = UI_StatePtr();
    if (ui) ui->slave_fault_code = p[1];
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
  MX_ETH_Init();
  MX_USART3_UART_Init();
  MX_I2C1_Init();
  MX_UART5_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  /* ── Initialise peripherals not in the .ioc ── */
  MX_UART4_Init();       /* PI bridge link — UART4 (PA1 RX, PC10 TX) 460800  */
  MX_UART7_Init();       /* Right VESC TX                                    */
  MX_ADC1_Init();        /* Battery sense on PC3 (circular DMA, single word) */
  MX_ESTOP_GPIO_Init();  /* PF15 input + EXTI                                */

  /* ── Initialise display, keypad, and OLED UI state machine ── */
  ssd1306_Init();
  KEYPAD_Init();
  UI_Init();          /* shows splash screen immediately */

  /* ── Initialise VESC motor driver (bldc_interface, left-motor callback) ── */
  VESC_Init();
  VESC_SetValuesCallback(on_vesc_values);

  /* ── Battery & e-stop modules ── */
  BATTERY_Init(&adc_battery_raw, 0.1754f, 12.6f, 9.0f);
  ESTOP_Init();

  /* ── SRR-CP protocol: bind streams + register local handlers ── */
  COMM_Init(&huart4, &huart1);
  COMM_RegisterHandler(CMD_DRIVE,        handle_drive);
  COMM_RegisterHandler(CMD_DRIVE_STOP,   handle_drive_stop);
  COMM_RegisterHandler(CMD_ESTOP_ASSERT, handle_estop_assert);
  COMM_RegisterHandler(CMD_ESTOP_CLEAR,  handle_estop_clear);
  COMM_RegisterHandler(CMD_ESTOP_QUERY,  handle_estop_query);
  COMM_RegisterHandler(CMD_STATUS_REQ,   handle_status_req);
  COMM_RegisterHandler(CMD_HEARTBEAT,    handle_heartbeat);
  COMM_RegisterHandler(CMD_PING,         handle_ping);
  COMM_RegisterHandler(CMD_FAULT_REPORT, handle_fault_report);

  /* ── Start ADC1 circular DMA into the single-word battery slot ── */
  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&adc_battery_raw, 1);

  /* ── Arm UART interrupt receivers (byte-at-a-time) ── */
  HAL_UART_Receive_IT(&huart4, (uint8_t *)&pi_rx_byte,    1);  /* ESP32-S3 bridge */
  HAL_UART_Receive_IT(&huart1, (uint8_t *)&slave_rx_byte, 1);  /* Arm/EE slave    */
  HAL_UART_Receive_IT(&huart5, (uint8_t *)&vesc_rx_byte,  1);  /* Left VESC       */

  /* ── Debug banner over ST-Link serial ── */
  HAL_UART_Transmit(&huart3,
      (uint8_t *)"[BASE] SRR-CP v1.0 online\r\n", 27, 50);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

        uint32_t now = HAL_GetTick();

        /* ── 0. E-stop service (always first). Emits the ESTOP_ASSERT broadcast
         *      on first latch and re-commands zero duty while latched. */
        ESTOP_Service();

        /* ── 1. Keypad scan ── */
        pendingKey = KEYPAD_Scan();

        /* ── 2. OLED UI update ── */
        UI_Update(pendingKey);

        /* ── 3. Battery sense IIR (rate-limited to 1 Hz internally) ── */
        BATTERY_Update();

        /* Auto-escalate: critical battery → software e-stop (once only). */
        if (BATTERY_IsCritical() && !ESTOP_IsActive()) {
            ESTOP_AssertSoftware(FAULT_ESTOP_BATT_CRIT);
        }

        /* While e-stop is active, skip motor watchdog + telemetry streaming.
         * Drive frames are already rejected in handle_drive(). */
        if (ESTOP_IsActive()) {
            goto loop_tail;
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

        /* ── 6. Slave heartbeat TX + liveness check ── */
        static uint32_t last_hb_tx = 0;
        if (now - last_hb_tx >= HEARTBEAT_PERIOD_MS) {
            last_hb_tx = now;
            uint32_t uptime_s = (now - UI_StatePtr()->bootTick) / 1000u;
            uint8_t hb[2] = {
                (uint8_t)((uptime_s >> 8) & 0xFFu),
                (uint8_t)(uptime_s & 0xFFu)
            };
            COMM_Send(ADDR_ARM_EE, CMD_HEARTBEAT, hb, 2);
        }

        /* Mirror slave link state to UI. */
        {
            UI_State_t *ui = UI_StatePtr();
            if (ui) ui->slave_last_seen_ms = slave_last_seen_ms;
        }

        /* ── 7. Telemetry push to Pi (STATUS_STREAM, 4 Hz) ── */
        static uint32_t last_tele_tx = 0;
        if (now - last_tele_tx >= TELEMETRY_PERIOD_MS) {
            last_tele_tx = now;

            uint16_t mv = (uint16_t)(BATTERY_GetVoltage() * 1000.0f);
            uint8_t f[3] = { FIELD_BATT_MV,
                             (uint8_t)(mv >> 8),
                             (uint8_t)(mv & 0xFFu) };
            COMM_Send(ADDR_PI, CMD_STATUS_STREAM, f, 3);

            f[0] = FIELD_BATT_PCT;
            f[1] = 0;
            f[2] = BATTERY_GetPercent();
            COMM_Send(ADDR_PI, CMD_STATUS_STREAM, f, 3);

            uint32_t hb_age = now - slave_last_seen_ms;
            if (hb_age > 65535u) hb_age = 65535u;
            f[0] = FIELD_SLAVE_HB_AGE;
            f[1] = (uint8_t)(hb_age >> 8);
            f[2] = (uint8_t)(hb_age & 0xFFu);
            COMM_Send(ADDR_PI, CMD_STATUS_STREAM, f, 3);
        }

    loop_tail: ;

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
 * MX_UART4_Init — ESP32-S3 bridge link (PA1 RX, PC10 TX, 460800 baud 8N1).
 *
 * Not in the .ioc so the GPIO alternate-function setup happens here. We
 * override PA1's previous ETH_REF_CLK alternate (AF11) with UART4_RX (AF8)
 * — the Ethernet peripheral isn't used on this build.
 */
static void MX_UART4_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* PA1 — UART4_RX */
    gpio.Pin       = GPIO_PIN_1;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;                 /* idle-high for bare 3V3 link */
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PC10 — UART4_TX  (new with the S3; ESP8266 build had RX-only) */
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

    /* NVIC — priority 5 matches UART5/UART7. ESTOP (PF15) sits at 2 so it
     * preempts any UART RX in progress. */
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

/**
 * MX_ADC1_Init — single-rank ADC1 IN13 (PC3, battery divider) on circular DMA
 *                into adc_battery_raw. DMA2_Stream0 is configured by the HAL
 *                MSP (stm32f7xx_hal_msp.c).
 */
static void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    /* DMA2 controller clock (MSP configures the stream but not the clock). */
    __HAL_RCC_DMA2_CLK_ENABLE();

    hadc1.Instance                   = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = DISABLE;
    hadc1.Init.ContinuousConvMode    = ENABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion       = 1;
    hadc1.Init.DMAContinuousRequests = ENABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

    sConfig.Channel      = ADC_CHANNEL_13;      /* PC3 — battery divider tap */
    sConfig.Rank         = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_56CYCLES;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();
}

/**
 * MX_ESTOP_GPIO_Init — PF15 input with pull-up, EXTI rising+falling trigger.
 *                      Active-low button shorts PF15 to GND. EXTI sits at
 *                      priority 2 — preempts UART RX (5) but yields to
 *                      SysTick/ETH.
 */
static void MX_ESTOP_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOF_CLK_ENABLE();

    gpio.Pin  = ESTOP_Pin;
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(ESTOP_GPIO_Port, &gpio);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
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
