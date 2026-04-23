/*
 * estop.c  —  Emergency-stop arbitrator
 *
 *  See estop.h for the contract. Implementation notes:
 *
 *   - s_latched / s_broadcast_pending are sig_atomic_t so the ISR can set
 *     them without a lock. Clearing happens only in main-loop context.
 *   - ESTOP_FromISR stops the motors directly because VESC_Stop() boils
 *     down to HAL_UART_Transmit_IT, which is safe at any priority below
 *     the UART5/7 IRQs (those are the only things it races with, and they
 *     only feed TXE/TC bits — no shared buffers).
 *   - We do NOT emit the SRR-CP broadcast frame from the ISR. Framing and
 *     routing call into COMM_Send, which touches static parser state and
 *     blocks on HAL_UART_Transmit. Defer that to ESTOP_Service().
 */

#include "estop.h"
#include "comm_protocol.h"
#include "vesc.h"
#include "main.h"

#include <signal.h>

/* ---- Module state --------------------------------------------------------- */
static volatile sig_atomic_t s_latched            = 0;
static volatile sig_atomic_t s_broadcast_pending  = 0;
static volatile uint8_t      s_reason             = 0;

/* ========================================================================= */
/* Public API                                                                */
/* ========================================================================= */

void ESTOP_Init(void)
{
    /* PF15 GPIO/EXTI configuration is done in MX_GPIO_Init (main.c) so the
     * generated HAL layout is kept in one place. Here we only reset the
     * software latch in case we're recovering from a warm boot that kept
     * SRAM contents. */
    s_latched           = 0;
    s_broadcast_pending = 0;
    s_reason            = 0;
}

void ESTOP_FromISR(void)
{
    /* Step 1 — latch. Must precede the VESC_Stop() call so any main-loop
     *          preemption sees the latch and skips motor commands. */
    s_latched = 1;
    s_reason  = FAULT_ESTOP_BUTTON;

    /* Step 2 — halt actuators. bldc_interface queues via HAL_UART_Transmit_IT
     *          so this returns quickly even under ISR context. */
    VESC_Stop();

    /* Step 3 — defer the SRR-CP broadcast to main loop. */
    s_broadcast_pending = 1;
}

void ESTOP_AssertSoftware(uint8_t reason)
{
    /* Same semantics as the ISR path but used for policy-triggered stops
     * (battery critical, slave CRIT fault, software watchdog). */
    if (s_latched) return;  /* already asserted — don't stomp the reason */

    s_latched           = 1;
    s_reason            = reason;
    VESC_Stop();
    s_broadcast_pending = 1;
}

void ESTOP_Service(void)
{
    if (s_broadcast_pending) {
        /* Clear first: if COMM_Send itself caused another assert (it won't,
         * but be defensive) the next pass will catch it. */
        s_broadcast_pending = 0;

        uint8_t reason = s_reason;
        COMM_Send(ADDR_BROADCAST, CMD_ESTOP_ASSERT, &reason, 1);
    }

    /* While latched we keep re-commanding zero duty periodically. The main
     * loop calls us every iteration, which is plenty fast. This defends
     * against a stray re-enable from any pending command path. */
    if (s_latched) {
        VESC_Stop();
    }
}

uint8_t ESTOP_IsActive(void)
{
    return (uint8_t)s_latched;
}

uint8_t ESTOP_Reason(void)
{
    return s_reason;
}

void ESTOP_Clear(void)
{
    if (!s_latched) return;

    s_latched           = 0;
    s_reason            = 0;
    s_broadcast_pending = 0;

    /* Tell everyone the latch is released. Slaves resume accepting commands
     * on the next frame. */
    COMM_Send(ADDR_BROADCAST, CMD_ESTOP_CLEAR, NULL, 0);
}
