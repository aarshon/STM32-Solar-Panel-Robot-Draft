/*
 * screen_estop.c  —  Emergency-stop full-screen banner
 *
 *  Drawn while ESTOP_IsActive(). The caller (ui.c) flips `UI_StatePtr()->estop_*`
 *  each time the latch or reason byte changes so this module can re-render
 *  without needing direct access to estop.c internals.
 *
 *  Clear sequence: '*' then '#'. We track the '*' press in a static flag so
 *  a stray '#' on its own does nothing.
 */

#include "screen_estop.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "comm_protocol.h"
#include "estop.h"
#include "stm32f7xx_hal.h"
#include <stdio.h>

static uint8_t  s_star_seen     = 0;
static uint32_t s_last_flash_ms = 0;
static uint8_t  s_flash_state   = 0;

static const char *reason_text(uint8_t code)
{
    switch (code) {
        case FAULT_ESTOP_BUTTON:    return "BUTTON";
        case FAULT_ESTOP_SOFTWARE:  return "SOFTWARE";
        case FAULT_ESTOP_WATCHDOG:  return "WATCHDOG";
        case FAULT_ESTOP_BATT_CRIT: return "BATT CRIT";
        case FAULT_ESTOP_SLAVE:     return "SLAVE FLT";
        default:                    return "UNKNOWN";
    }
}

void ScreenEstop_Reset(void)
{
    s_star_seen     = 0;
    s_last_flash_ms = HAL_GetTick();
    s_flash_state   = 0;
}

void ScreenEstop_Draw(void)
{
    char buf[22];
    uint32_t now = HAL_GetTick();
    if ((now - s_last_flash_ms) >= 400u) {
        s_last_flash_ms = now;
        s_flash_state  ^= 1u;
    }

    ssd1306_Fill(s_flash_state ? White : Black);
    SSD1306_COLOR fg = s_flash_state ? Black : White;

    /* Big banner */
    ssd1306_SetCursor(6, 6);
    ssd1306_WriteString("EMERGENCY", Font_11x18, fg);
    ssd1306_SetCursor(30, 28);
    ssd1306_WriteString("STOP", Font_11x18, fg);

    /* Reason */
    snprintf(buf, sizeof(buf), "Reason: %s", reason_text(ESTOP_Reason()));
    ssd1306_SetCursor(0, 50);
    ssd1306_WriteString(buf, Font_6x8, fg);

    /* Clear hint — changes when '*' has been registered */
    ssd1306_SetCursor(0, 58);
    if (s_star_seen) {
        ssd1306_WriteString("Press [#] to clear", Font_6x8, fg);
    } else {
        ssd1306_WriteString("Clear: [*] then [#]", Font_6x8, fg);
    }

    ssd1306_UpdateScreen();
}

ScreenEstop_Action_t ScreenEstop_HandleKey(uint8_t key)
{
    uint8_t k = key & 0x7Fu;

    if (k == '*') {
        s_star_seen = 1;
        return SCREEN_ESTOP_ACTION_NONE;
    }
    if (k == '#' && s_star_seen) {
        s_star_seen = 0;
        return SCREEN_ESTOP_ACTION_CLEAR;
    }
    /* Any other key resets the sequence so a mis-press doesn't pre-arm. */
    if (k != 0) s_star_seen = 0;
    return SCREEN_ESTOP_ACTION_NONE;
}
