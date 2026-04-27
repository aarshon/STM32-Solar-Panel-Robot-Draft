/*
 * screen_power.h  —  Battery / power status screen
 *
 *  Shows live pack voltage (from battery.c IIR filter), state-of-charge
 *  percent, a bar graph, and — when telemetry is available — the VESC
 *  v_in reading as a cross-check. Flags low-battery / critical-battery
 *  states prominently.
 *
 *  Keys:
 *    '*'  → return to main menu
 */

#ifndef SCREEN_POWER_H
#define SCREEN_POWER_H

#include <stdint.h>

typedef enum {
    SCREEN_POWER_ACTION_NONE = 0,
    SCREEN_POWER_ACTION_BACK
} ScreenPower_Action_t;

void                  ScreenPower_Draw(void);
ScreenPower_Action_t  ScreenPower_HandleKey(uint8_t key);

#endif /* SCREEN_POWER_H */
