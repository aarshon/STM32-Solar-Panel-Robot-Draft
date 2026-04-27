/*
 * battery.h  —  Pack-voltage sense + state-of-charge for Vehicle_Base
 *
 *  Hardware
 *  ────────
 *    V_pack ─[R1]─┬─[R2]─ GND          R1 = 47 kΩ (top)
 *                 │                     R2 = 10 kΩ (bottom)
 *                 ├── PC3 (ADC1_IN13)   ratio = 10/(47+10) = 0.1754
 *                 │                     12.6 V pack → 2.21 V at ADC
 *               100 nF
 *                 │
 *                GND
 *
 *  Sampling
 *  ────────
 *    ADC1 runs continuous conversion at 12-bit resolution with a circular
 *    DMA into a single uint16_t slot (adc_battery_raw) owned by main.c.
 *    BATTERY_Init() receives a pointer to that slot; BATTERY_Update() reads
 *    it at 1 Hz, IIR-filters, and refreshes voltage / percent / low flags.
 */

#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bind the DMA result slot and pack parameters. Call once after MX_ADC1_Init.
 *   dma_slot  — pointer to the single-word circular DMA buffer updated by ADC1.
 *   divider   — V_adc / V_pack (e.g. 0.1754 for 47k/10k).
 *   v_full    — pack voltage at 100% SoC (e.g. 12.6 V).
 *   v_empty   — pack voltage at 0% SoC   (e.g.  9.0 V). */
void BATTERY_Init(volatile uint16_t *dma_slot,
                  float divider,
                  float v_full,
                  float v_empty);

/* Sample + IIR-filter. Cheap; call every main-loop iteration. Internally
 * rate-limited to 1 Hz, so frequent calls cost just a timestamp compare. */
void BATTERY_Update(void);

/* Latest filtered pack voltage in volts. */
float BATTERY_GetVoltage(void);

/* State-of-charge, 0..100, linear between v_empty and v_full. */
uint8_t BATTERY_GetPercent(void);

/* 1 when the pack is under 15% SoC, 0 otherwise. Sticky until recharge. */
uint8_t BATTERY_IsLow(void);

/* 1 when under 5% SoC — main loop calls VESC_Stop() in this state. */
uint8_t BATTERY_IsCritical(void);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_H */
