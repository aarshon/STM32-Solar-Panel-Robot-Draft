/*
 * vesc.h  —  VESC Motor Driver (FlipSky FESC 6.7)
 * Board : STM32 Nucleo-F767ZI  (Vehicle Base)
 *
 * Sends duty-cycle commands over UART using the VESC binary protocol.
 * Left motor  → direct UART frame (COMM_SET_DUTY)
 * Right motor → CAN-forwarded frame (COMM_FORWARD_CAN) sent on same UART
 *
 * Also supports requesting and receiving real-time telemetry via
 * COMM_GET_VALUES (0x04). Call VESC_RequestValues() to trigger a fetch;
 * the parsed result is delivered to the callback set by
 * VESC_SetValuesCallback().
 *
 * Frame layout:
 *   [0x02] [payload_len] [payload...] [CRC16_hi] [CRC16_lo] [0x03]
 */

#ifndef VESC_H
#define VESC_H

#include "stm32f7xx_hal.h"
#include <stdint.h>

/* ---- UART handle wired to VESC ----------------------------------------- */
extern UART_HandleTypeDef huart2;           /* PD5(TX) / PD6(RX)           */
#define VESC_UART   huart2

/* ---- VESC protocol command codes --------------------------------------- */
#define VESC_CMD_GET_VALUES  0x04           /* Request real-time telemetry  */
#define VESC_CMD_SET_DUTY    0x05           /* Set duty cycle directly      */
#define VESC_CMD_FWD_CAN     0x21           /* Forward command via CAN bus  */

/* ---- Duty cycle limits (unit = 1/100 000 of full scale) ---------------- */
#define VESC_MAX_DUTY        50000
#define VESC_MIN_DUTY       -50000

/* ---- CAN device ID of the right-side VESC ----------------------------- */
#define VESC_CAN_ID_RIGHT    1

/* ---- Direction constants ----------------------------------------------- */
#define DIR_STOP     0
#define DIR_FORWARD  1
#define DIR_BACKWARD 2
#define DIR_LEFT     3
#define DIR_RIGHT    4

/* ---- mc_fault_code enum (matches VESC firmware) ------------------------ */
typedef enum {
    FAULT_CODE_NONE = 0,
    FAULT_CODE_OVER_VOLTAGE,
    FAULT_CODE_UNDER_VOLTAGE,
    FAULT_CODE_DRV,
    FAULT_CODE_ABS_OVER_CURRENT,
    FAULT_CODE_OVER_TEMP_FET,
    FAULT_CODE_OVER_TEMP_MOTOR,
    FAULT_CODE_GATE_DRIVER_OVER_VOLTAGE,
    FAULT_CODE_GATE_DRIVER_UNDER_VOLTAGE,
    FAULT_CODE_MCU_UNDER_VOLTAGE,
    FAULT_CODE_BOOTING_FROM_WATCHDOG_RESET,
    FAULT_CODE_ENCODER_SPI_FAULT,
    FAULT_CODE_ENCODER_SINCOS_BELOW_MIN_AMPLITUDE,
    FAULT_CODE_ENCODER_SINCOS_ABOVE_MAX_AMPLITUDE,
    FAULT_CODE_FLASH_CORRUPTION,
    FAULT_CODE_HIGH_OFFSET_CURRENT_SENSOR_1,
    FAULT_CODE_HIGH_OFFSET_CURRENT_SENSOR_2,
    FAULT_CODE_HIGH_OFFSET_CURRENT_SENSOR_3,
    FAULT_CODE_UNBALANCED_CURRENTS,
    FAULT_CODE_BRK,
    FAULT_CODE_RESOLVER_LOT,
    FAULT_CODE_RESOLVER_DOS,
    FAULT_CODE_RESOLVER_LOS,
    FAULT_CODE_FLASH_CORRUPTION_APP_CFG,
    FAULT_CODE_FLASH_CORRUPTION_MC_CFG,
    FAULT_CODE_ENCODER_NO_MAGNET,
    FAULT_CODE_ENCODER_MAGNET_TOO_STRONG
} mc_fault_code;

/* ---- Real-time telemetry struct (mirrors mc_values in VESC firmware) --- */
typedef struct {
    float           temp_mos;           /* FET temperature        (°C)       */
    float           temp_motor;         /* Motor temperature      (°C)       */
    float           current_motor;      /* Motor phase current    (A)        */
    float           current_in;         /* Input/battery current  (A)        */
    float           id;                 /* D-axis current         (A)        */
    float           iq;                 /* Q-axis current         (A)        */
    float           duty_now;           /* Duty cycle             (-1…+1)    */
    float           rpm;                /* Electrical RPM                    */
    float           v_in;              /* Input voltage          (V)        */
    float           amp_hours;          /* Consumed charge        (Ah)       */
    float           amp_hours_charged;  /* Regenerated charge     (Ah)       */
    float           watt_hours;         /* Consumed energy        (Wh)       */
    float           watt_hours_charged; /* Regenerated energy     (Wh)       */
    int32_t         tachometer;         /* Tachometer value       (counts)   */
    int32_t         tachometer_abs;     /* Absolute tachometer    (counts)   */
    mc_fault_code   fault_code;         /* Active fault code                 */
    float           position;           /* PID position           (deg)      */
    uint8_t         vesc_id;            /* Responding VESC CAN ID            */
    float           vd;                 /* D-axis voltage         (V)        */
    float           vq;                 /* Q-axis voltage         (V)        */
} mc_values;

/* ---- Callback type for received telemetry ------------------------------ */
/* Define your own function matching this signature and pass it to
 * VESC_SetValuesCallback(). It will be called from VESC_ProcessRxByte()
 * each time a complete, CRC-verified GET_VALUES response is decoded.
 *
 * Example:
 *   void my_callback(mc_values *val) {
 *       float duty_pct = val->duty_now * 100.0f;
 *   }
 */
typedef void (*vesc_values_cb_t)(mc_values *val);

/* ---- Public API -------------------------------------------------------- */

/* Motor control */
void VESC_SetDuty      (int32_t duty);
void VESC_SetDutyRight (int32_t duty);
void VESC_TankDrive    (uint8_t direction, uint8_t speed);
void VESC_Stop         (void);

/* Telemetry */
void VESC_SetValuesCallback (vesc_values_cb_t cb);   /* Register callback   */
void VESC_RequestValues     (void);                  /* Send 0x04 request   */
void VESC_ProcessRxByte     (uint8_t byte);          /* Feed one RX byte    */

#endif /* VESC_H */
