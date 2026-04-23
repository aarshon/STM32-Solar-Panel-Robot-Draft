/*
 * estop.h  —  Emergency-stop arbitrator for Vehicle_Base (master)
 *
 *  Hardware: momentary/latching mushroom switch wired PF15 ⟶ GND, internal
 *            pull-up. Active-low (fail-safe: a broken wire reads "pressed").
 *
 *  Contract
 *  ────────
 *    - ESTOP_FromISR() is the single ISR entry. It MUST:
 *        1. set the latch atomically (sig_atomic_t)
 *        2. command zero duty to both VESCs via VESC_Stop()
 *        3. request that ESTOP_Service() broadcast ESTOP_ASSERT on the next tick
 *      Everything else (UI, frame emission) happens later in main-loop context.
 *
 *    - While the latch is asserted the main loop MUST:
 *        - skip all motor command blocks
 *        - refuse to forward 0x20/0x21/0x30 frames to the slave
 *        - hard-jump UI to SCREEN_ESTOP regardless of menu state
 *
 *    - ESTOP_Clear() is the keypad-driven release (sequence: *, #). It drops
 *      the latch and broadcasts ESTOP_CLEAR so slaves resume normal operation.
 */

#ifndef ESTOP_H
#define ESTOP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configure PF15 EXTI + clear any residual latch. Call once from main(). */
void ESTOP_Init(void);

/* Call from EXTI15_10 → HAL_GPIO_EXTI_Callback(GPIO_PIN_15). */
void ESTOP_FromISR(void);

/* Main-loop service. Emits ESTOP_ASSERT when the latch was just raised. */
void ESTOP_Service(void);

/* 1 while latched, 0 otherwise. Safe to call from ISR or main loop. */
uint8_t ESTOP_IsActive(void);

/* Reason byte reported with ESTOP_ASSERT (FAULT_ESTOP_* from comm_protocol.h). */
uint8_t ESTOP_Reason(void);

/* Release the latch from main-loop context (e.g. keypad "*" then "#").
 * Broadcasts ESTOP_CLEAR. No-op if already cleared. */
void ESTOP_Clear(void);

/* Software-triggered e-stop (battery critical, slave fault escalation, …).
 * Safe from main loop only. reason is a FAULT_ESTOP_* code. */
void ESTOP_AssertSoftware(uint8_t reason);

#ifdef __cplusplus
}
#endif

#endif /* ESTOP_H */
