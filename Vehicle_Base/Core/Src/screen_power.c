/*
 * screen_power.c  —  Battery / power status screen
 *
 *  Layout (128 × 64, Font_7x10 unless noted):
 *    y= 0  "POWER"                          +   "[OK]" / "[LOW]" / "[CRIT]" badge
 *    y=11  divider
 *    y=13  "Pack: 12.4V"                    (IIR-filtered pack voltage)
 *    y=25  "SoC : 78%"                      (linear SoC in [0,100])
 *    y=37  "VESC: 12.2V"                    (VESC v_in — only if valid)
 *    y=48  divider
 *    y=50  SoC bar graph, 126 px wide, filled to percent
 *    y=58  Font_6x8  "[*]Back"
 */

#include "screen_power.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "battery.h"
#include "ui.h"
#include <stdio.h>

extern UI_State_t *UI_StatePtr(void);  /* exposed by ui.c for screens         */

static void draw_badge(uint8_t crit, uint8_t low)
{
    /* Right-aligned state badge. CRIT takes priority over LOW. */
    if (crit) {
        ssd1306_FillRectangle(80, 0, 127, 10, White);
        ssd1306_SetCursor(82, 0);
        ssd1306_WriteString("[CRIT]", Font_7x10, Black);
    } else if (low) {
        ssd1306_FillRectangle(88, 0, 127, 10, White);
        ssd1306_SetCursor(90, 0);
        ssd1306_WriteString("[LOW]", Font_7x10, Black);
    } else {
        ssd1306_SetCursor(91, 0);
        ssd1306_WriteString("[OK]", Font_7x10, White);
    }
}

void ScreenPower_Draw(void)
{
    char buf[22];
    float   volts = BATTERY_GetVoltage();
    uint8_t pct   = BATTERY_GetPercent();
    uint8_t low   = BATTERY_IsLow();
    uint8_t crit  = BATTERY_IsCritical();

    ssd1306_Fill(Black);

    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString("POWER", Font_7x10, White);
    draw_badge(crit, low);
    ssd1306_Line(0, 11, 127, 11, White);

    snprintf(buf, sizeof(buf), "Pack: %.2fV", volts);
    ssd1306_SetCursor(0, 13);
    ssd1306_WriteString(buf, Font_7x10, White);

    snprintf(buf, sizeof(buf), "SoC : %3u%%", (unsigned)pct);
    ssd1306_SetCursor(0, 25);
    ssd1306_WriteString(buf, Font_7x10, White);

    /* VESC cross-check: compare against the filtered pack voltage so a wiring
     * fault on the divider is visible as a big delta between the two rows. */
    UI_State_t *ui = UI_StatePtr();
    if (ui && ui->tele.valid) {
        snprintf(buf, sizeof(buf), "VESC: %.2fV", ui->tele.v_in);
    } else {
        snprintf(buf, sizeof(buf), "VESC: --.--V");
    }
    ssd1306_SetCursor(0, 37);
    ssd1306_WriteString(buf, Font_7x10, White);

    ssd1306_Line(0, 48, 127, 48, White);

    /* SoC bar */
    ssd1306_DrawRectangle(0, 50, 127, 56, White);
    uint32_t fill = ((uint32_t)pct * 125u) / 100u;
    if (fill > 0) {
        ssd1306_FillRectangle(1, 51, (uint8_t)(1u + fill), 55, White);
    }

    ssd1306_SetCursor(0, 58);
    ssd1306_WriteString("[*]Back", Font_6x8, White);

    ssd1306_UpdateScreen();
}

ScreenPower_Action_t ScreenPower_HandleKey(uint8_t key)
{
    uint8_t k = key & 0x7Fu;
    if (k == '*') return SCREEN_POWER_ACTION_BACK;
    return SCREEN_POWER_ACTION_NONE;
}
