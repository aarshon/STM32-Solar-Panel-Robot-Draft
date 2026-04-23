/*
 * ui.h  —  Multi-screen OLED UI
 * Board : STM32 Nucleo-F767ZI  (Vehicle Base — merged firmware)
 *
 * Screens:
 *   SPLASH         → animated power-on screen (1.5 s auto-advance)
 *   MAIN_MENU      → 5-item menu with cursor
 *   STATUS_MONITOR → live VESC telemetry + Pi/slave link status + battery row
 *   MOTOR_CONTROL  → drive robot directly from keypad (local fallback)
 *   ROBOT_ARM      → placeholder (arm team will implement)
 *   POWER          → battery voltage, SoC %, low/crit warning, VESC V cross-check
 *   INFO           → uptime, firmware label, fault log
 *   ESTOP          → full-screen flashing banner while ESTOP_IsActive()
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
    SCREEN_STATUS_MONITOR,   /* menu cursor 0 */
    SCREEN_MOTOR_CONTROL,    /* menu cursor 1 */
    SCREEN_ROBOT_ARM,        /* menu cursor 2 */
    SCREEN_POWER,            /* menu cursor 3 */
    SCREEN_INFO,             /* menu cursor 4 */
    SCREEN_ESTOP,            /* not menu-reachable; hard-jump only */
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
    uint8_t       menuCursor;      /* 0–4                                     */

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

    /* Battery snapshot — refreshed from battery.c in the main loop */
    float         battery_voltage;
    uint8_t       battery_pct;

    /* Slave-link state (ms since last heartbeat, last reported fault) */
    uint32_t      slave_last_seen_ms;
    uint8_t       slave_fault_code;

    /* E-stop mirror — updated by main loop from estop.c */
    uint8_t       estop_active;
    uint8_t       estop_reason;

} UI_State_t;

/* Accessor for screen_*.c modules that need a peek at live state
 * (telemetry snapshot, battery, etc.) without pulling in ui.c globals. */
UI_State_t *UI_StatePtr(void);

/* ---- Public API -------------------------------------------------------- */
void    UI_Init             (void);
void    UI_Update           (uint8_t key);          /* Call every main loop  */
void    UI_TelemetryUpdate  (mc_values *val);        /* Call from VESC callback */
void    UI_ForceRedraw      (void);

#endif /* UI_H */
