/*
 * screen_stepper.c  —  Stepper motor status screen
 * Board : STM32 Nucleo-F767ZI  (Vehicle Base)
 *
 * See screen_stepper.h for behaviour and key bindings.
 *
 * Layout (128×64, drawn by ssd1306 driver):
 *
 *   y= 0  Font_7x10   "STEPPER"  +  inverted "[RUN]" / plain "[STP]" badge
 *   y=11  divider line
 *   y=13  Font_7x10   "Freq:+1234Hz"          (signed current frequency)
 *   y=25  Font_7x10   "Dir: CW "              (CW / CCW / STP)
 *   y=37  Font_7x10   "CW :1234 ( 30%)"       (CW  throttle pot raw + %)
 *   y=48  divider line
 *   y=50  Font_6x8    "CCW:1234  [*]Back"     (CCW throttle pot raw + hint)
 *   y=58  ─ bar graph ─ width scaled to |current_freq| / STEPPER_FREQ_MAX
 */

#include "screen_stepper.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "stepper.h"
#include <stdio.h>

/* ── Helpers ──────────────────────────────────────────────────────────── */

static int32_t abs32_local(int32_t v) { return v < 0 ? -v : v; }

/** Map a 12-bit throttle-pot sample (0..4095, rest at 0) to percent [0,100]. */
static uint8_t pot_pct(uint16_t adc_raw)
{
    uint32_t pct = ((uint32_t)adc_raw * 100u) / 4095u;
    if (pct > 100u) pct = 100u;
    return (uint8_t)pct;
}

/* ── Renderer ─────────────────────────────────────────────────────────── */

void ScreenStepper_Draw(void)
{
    STEPPER_Status_t st;
    STEPPER_GetStatus(&st);

    char buf[24];
    ssd1306_Fill(Black);

    /* ── Title + RUN/STP badge ─────────────────────────────────────────── */
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString("STEPPER", Font_7x10, White);

    if (st.running)
    {
        /* Inverted [RUN] badge — attention-grabbing when pulsing */
        ssd1306_FillRectangle(88, 0, 127, 10, White);
        ssd1306_SetCursor(90, 0);
        ssd1306_WriteString("[RUN]", Font_7x10, Black);
    }
    else
    {
        ssd1306_SetCursor(90, 0);
        ssd1306_WriteString("[STP]", Font_7x10, White);
    }
    ssd1306_Line(0, 11, 127, 11, White);

    /* ── Row 1: signed step frequency in Hz ────────────────────────────── */
    snprintf(buf, sizeof(buf), "Freq:%+5ldHz", (long)st.current_freq);
    ssd1306_SetCursor(0, 13);
    ssd1306_WriteString(buf, Font_7x10, White);

    /* ── Row 2: direction label ────────────────────────────────────────── */
    const char *dir =
        (st.current_freq > 0) ? "CW " :
        (st.current_freq < 0) ? "CCW" : "STP";
    snprintf(buf, sizeof(buf), "Dir: %s", dir);
    ssd1306_SetCursor(0, 25);
    ssd1306_WriteString(buf, Font_7x10, White);

    /* ── Row 3: CW-throttle pot raw + deflection % ─────────────────────── */
    snprintf(buf, sizeof(buf), "CW :%4u (%3u%%)",
             (unsigned)st.cw_adc, (unsigned)pot_pct(st.cw_adc));
    ssd1306_SetCursor(0, 37);
    ssd1306_WriteString(buf, Font_7x10, White);

    /* ── Divider ────────────────────────────────────────────────────────── */
    ssd1306_Line(0, 48, 127, 48, White);

    /* ── Row 4: CCW-throttle pot raw + back hint ───────────────────────── */
    snprintf(buf, sizeof(buf), "CCW:%4u  [*]Back",
             (unsigned)st.ccw_adc);
    ssd1306_SetCursor(0, 50);
    ssd1306_WriteString(buf, Font_6x8, White);

    /* ── Bar graph: |current_freq| scaled to STEPPER_FREQ_MAX ──────────── */
    uint32_t mag  = (uint32_t)abs32_local(st.current_freq);
    if (mag > STEPPER_FREQ_MAX) mag = STEPPER_FREQ_MAX;
    uint32_t barW = (mag * 126u) / STEPPER_FREQ_MAX;
    ssd1306_DrawRectangle(0, 59, 127, 63, White);
    if (barW > 0)
    {
        ssd1306_FillRectangle(1, 60, (uint8_t)(1u + barW), 62, White);
    }

    ssd1306_UpdateScreen();
}

/* ── Key handler ──────────────────────────────────────────────────────── */

ScreenStepper_Action_t ScreenStepper_HandleKey(uint8_t key)
{
    uint8_t k = key & 0x7Fu;   /* Strip hold bit */

    if (k == '*')
    {
        return SCREEN_STEPPER_ACTION_BACK;
    }
    if (k == '0')
    {
        /* Hard stop — useful if the pot is stuck or wiring is suspect */
        STEPPER_Stop();
    }
    return SCREEN_STEPPER_ACTION_NONE;
}
