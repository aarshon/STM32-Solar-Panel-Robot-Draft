/*
 * battery.c  —  Pack-voltage sense + IIR-filtered SoC
 *
 *  The ADC1 peripheral converts PC3 continuously into a single-word circular
 *  DMA buffer managed by main.c. We just read that slot at 1 Hz, convert it
 *  to volts through the divider, run it through a 1st-order IIR so the OLED
 *  display doesn't jitter, then translate to percent.
 *
 *  Why 1 Hz? The pack voltage moves much slower than the ADC can sample.
 *  A faster update rate buys us noise, not signal, and burns I²C bandwidth
 *  on redundant OLED refreshes.
 */

#include "battery.h"
#include "stm32f7xx_hal.h"

/* ---- Tuning --------------------------------------------------------------- */
#define ADC_VREF_VOLTS       3.3f         /* STM32F767 ADC reference    */
#define ADC_MAX_COUNTS       4095.0f      /* 12-bit resolution          */
#define UPDATE_INTERVAL_MS   1000u        /* 1 Hz sample cadence        */
#define IIR_ALPHA            0.25f        /* new sample weight          */
#define LOW_THRESHOLD_PCT    15u
#define CRIT_THRESHOLD_PCT   5u

/* ---- Module state --------------------------------------------------------- */
static volatile uint16_t *s_dma_slot   = 0;
static float              s_divider    = 0.1754f;
static float              s_v_full     = 12.6f;
static float              s_v_empty    = 9.0f;

static float              s_voltage_v  = 0.0f;  /* IIR-filtered pack volts  */
static uint32_t           s_last_ms    = 0;
static uint8_t            s_seeded     = 0;

/* ---- Helpers -------------------------------------------------------------- */
static float raw_to_pack_volts(uint16_t raw)
{
    float v_adc = ((float)raw / ADC_MAX_COUNTS) * ADC_VREF_VOLTS;
    /* V_pack = V_adc / divider_ratio */
    return v_adc / s_divider;
}

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

void BATTERY_Init(volatile uint16_t *dma_slot,
                  float divider,
                  float v_full,
                  float v_empty)
{
    s_dma_slot  = dma_slot;
    s_divider   = divider;
    s_v_full    = v_full;
    s_v_empty   = v_empty;
    s_voltage_v = 0.0f;
    s_seeded    = 0;
    s_last_ms   = 0;
}

void BATTERY_Update(void)
{
    if (s_dma_slot == 0) return;

    uint32_t now = HAL_GetTick();
    if (s_seeded && (now - s_last_ms) < UPDATE_INTERVAL_MS) return;
    s_last_ms = now;

    float sample = raw_to_pack_volts(*s_dma_slot);

    if (!s_seeded) {
        /* Prime the filter with the first sample so the OLED doesn't
         * ramp from 0 V on boot. */
        s_voltage_v = sample;
        s_seeded    = 1;
    } else {
        s_voltage_v = (IIR_ALPHA * sample) + ((1.0f - IIR_ALPHA) * s_voltage_v);
    }
}

float BATTERY_GetVoltage(void)
{
    return s_voltage_v;
}

uint8_t BATTERY_GetPercent(void)
{
    if (s_v_full <= s_v_empty) return 0;

    float pct = (s_voltage_v - s_v_empty) * 100.0f / (s_v_full - s_v_empty);
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return (uint8_t)(pct + 0.5f);
}

uint8_t BATTERY_IsLow(void)
{
    return (BATTERY_GetPercent() <= LOW_THRESHOLD_PCT) ? 1u : 0u;
}

uint8_t BATTERY_IsCritical(void)
{
    return (BATTERY_GetPercent() <= CRIT_THRESHOLD_PCT) ? 1u : 0u;
}
