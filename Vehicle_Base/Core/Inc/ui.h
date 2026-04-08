/*
 * ui.h  —  Multi-screen OLED UI
 * Board : STM32 Nucleo-F767ZI  (Vehicle Base — merged firmware)
 *
 * Screens:
 *   SPLASH         → animated power-on screen (1.5 s auto-advance)
 *   MAIN_MENU      → 4-item menu with cursor
 *   STATUS_MONITOR → live VESC telemetry (RPM, V, A, duty, temp, fault)
 *                    + Pi MQTT connection status (LIVE / TIMEOUT)
 *   MOTOR_CONTROL  → drive robot directly from keypad (local fallback)
 *   ROBOT_ARM      → placeholder (arm team will implement)
 *   INFO           → uptime, firmware label, fault log
 *
 * Note: mc_values and mc_fault_code come from datatypes.h (bldc_interface
 * library) via vesc.h.  Do NOT include datatypes.h directly here.
 */

#ifndef UI_H
#define UI_H

#include "stm32f7xx_hal.h"
#include "vesc.h"
#include "keypad.h"
#include <stdint.h>

/* ---- Screen identifiers ------------------------------------------------ */
typedef enum {
    SCREEN_SPLASH = 0,
    SCREEN_MAIN_MENU,
    SCREEN_STATUS_MONITOR,
    SCREEN_MOTOR_CONTROL,
    SCREEN_ROBOT_ARM,
    SCREEN_INFO,
    SCREEN_COUNT
} UI_Screen_t;

/* ---- Motor direction sub-state ----------------------------------------- */
typedef enum {
    MCTRL_IDLE = 0,
    MCTRL_FORWARD,
    MCTRL_BACKWARD,
    MCTRL_LEFT,
    MCTRL_RIGHT
} UI_MotorDir_t;

/* ---- Telemetry snapshot (written from VESC callback, read in main loop) - */
typedef struct {
    float         rpm;
    float         v_in;
    float         current_motor;
    float         duty_pct;        /* duty_now * 100.0f                       */
    float         temp_mos;
    mc_fault_code fault_code;
    uint8_t       valid;           /* 1 once first packet arrives             */
} UI_TelemetrySnapshot_t;

/* ---- Master UI state --------------------------------------------------- */
typedef struct {
    UI_Screen_t   currentScreen;

    /* Main menu */
    uint8_t       menuCursor;      /* 0–3                                     */

    /* Motor control */
    uint8_t       motorSpeed;      /* 0–255, default 150                      */
    UI_MotorDir_t motorDir;

    /* Telemetry — volatile because written in ISR context */
    volatile UI_TelemetrySnapshot_t tele;

    /* Splash timing */
    uint32_t      splashStartMs;

    /* Redraw flag — set by key handlers, cleared after render */
    uint8_t       needsRedraw;

    /* Uptime reference tick */
    uint32_t      bootTick;

    /* Fault history — circular buffer of last 4 faults */
    mc_fault_code faultLog[4];
    uint8_t       faultLogIdx;
    uint8_t       faultLogCount;

    /* Fault flash — counts down; each 200 ms tick inverts screen once */
    uint8_t       faultFlashCount;
    uint32_t      lastFlashMs;

    /* Status screen refresh gating */
    uint32_t      lastStatusRedrawMs;

} UI_State_t;

/* ---- Public API -------------------------------------------------------- */
void    UI_Init             (void);
void    UI_Update           (uint8_t key);          /* Call every main loop  */
void    UI_TelemetryUpdate  (mc_values *val);        /* Call from VESC callback */
void    UI_ForceRedraw      (void);

#endif /* UI_H */
