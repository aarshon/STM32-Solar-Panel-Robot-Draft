/*
 * stepper.c  —  NEMA 17 Stepper Motor Control (potentiometer input)
 * Board  : STM32 Nucleo-F767ZI  (Vehicle Base — merged firmware)
 *
 * See stepper.h for full wiring and configuration details.
 *
 * Design notes
 * ────────────
 * Step pulse generation
 *   TIM3 CH1 runs in PWM mode with 50 % duty cycle.  The timer generates
 *   step edges in hardware — zero CPU overhead once ARR is set.
 *   Changing ARR at runtime (with ARPE preload enabled) is glitch-free:
 *   the new period only takes effect after the current period completes,
 *   so no partial pulse is ever emitted.
 *
 * Variable-frequency control
 *   Timer tick = 1 µs  (prescaler 95, 96 MHz APB1×2 clock).
 *   Step period in µs = 1 000 000 / freq_Hz.
 *   ARR  = period_us − 1
 *   CCR1 = period_us / 2   (50 % duty → clean rising edge for driver)
 *
 * Soft ramp
 *   s_current_freq is a signed integer (positive = CW, negative = CCW).
 *   Each call to STEPPER_Update() moves s_current_freq toward s_target_freq
 *   by at most STEPPER_RAMP_STEP Hz.  This limits acceleration to
 *   STEPPER_RAMP_STEP × call_rate  (= 100 Hz/ms × 1 kHz ≈ 100 000 Hz/s).
 *
 * Direction reversal safety
 *   If s_target_freq would cross zero (i.e. reversal), s_target_freq is
 *   clamped to 0 for that call.  The ramp will decelerate the motor to a
 *   stop, then on the next call where the new direction is set, it ramps
 *   up in the opposite direction.  This guarantees the DIR pin is only
 *   toggled when the motor is at rest — preventing missed steps.
 *
 * PWM start/stop
 *   HAL_TIM_PWM_Stop / HAL_TIM_PWM_Start only when state changes.
 *   The running state is detected by checking TIM3 CCER CC1E bit directly
 *   rather than keeping a separate flag, so there is no stale-flag risk.
 */

#include "stepper.h"
#include "main.h"    /* for htim3 handle declared by CubeIDE */

/* ── Extern handle (declared in main.c by CubeIDE code generator) ─────── */
extern TIM_HandleTypeDef htim3;

/* ── Private state ─────────────────────────────────────────────────────── */

/* Signed step frequency in Hz.
 *   > 0  →  CW  (DIR pin HIGH)
 *   < 0  →  CCW (DIR pin LOW)
 *   = 0  →  stopped (PWM output disabled) */
static int32_t  s_current_freq = 0;
static int32_t  s_target_freq  = 0;

/* Latest ADC values handed to STEPPER_Update — exposed via STEPPER_GetStatus
 * so the UI can show pot positions without touching adc_dma_buf directly.
 * Rest state for both pots is ~0 counts (wiper at GND end). */
static uint16_t s_last_cw_adc  = 0u;
static uint16_t s_last_ccw_adc = 0u;

/* ── Private helpers ───────────────────────────────────────────────────── */

/** abs32 — integer absolute value (avoids pulling in <stdlib.h> abs which
 *  may operate on int, not int32_t on all toolchains). */
static inline int32_t abs32(int32_t v) { return v < 0 ? -v : v; }

/** clamp32 — clamp v to [lo, hi]. */
static inline int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/**
 * apply_to_hardware — push the current s_current_freq value to TIM3 and
 * the DIR GPIO.  Called only from STEPPER_Update after ramping.
 */
static void apply_to_hardware(void)
{
    if (s_current_freq == 0)
    {
        /* Stop: disable PWM output.  The pin returns to its idle level
         * (low), which the driver treats as no-step — safe. */
        HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
        return;
    }

    /* ── Direction ───────────────────────────────────────────────────── */
    HAL_GPIO_WritePin(STEP_DIR_PORT, STEP_DIR_PIN,
                      s_current_freq > 0 ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* ── Step frequency (ARR + CCR1) ─────────────────────────────────── */
    uint32_t freq     = (uint32_t)abs32(s_current_freq);
    uint32_t period   = 1000000UL / freq;   /* µs per step (timer tick = 1µs) */

    /* Sanity clamp — prevent ARR=0 which would cause continuous pulses.
     * Should never trigger given STEPPER_FREQ_MAX = 5000 Hz (period ≥ 200). */
    if (period < 2u) period = 2u;

    /* Write new period and compare value.
     * ARPE is enabled → new ARR takes effect at next update event,
     * so no glitch on the current pulse in progress. */
    __HAL_TIM_SET_AUTORELOAD(&htim3, period - 1u);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, period / 2u);

    /* ── Start PWM only if not already running ───────────────────────── */
    if (!(TIM3->CCER & TIM_CCER_CC1E))
    {
        HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    }
}

/* ── Public API ────────────────────────────────────────────────────────── */

void STEPPER_Init(void)
{
    /* Direction pin defaults to CW (high).
     * TIM3 and ADC1 are already initialised by MX_TIM3_Init() and
     * MX_ADC1_Init() which CubeIDE generates in main.c — nothing to do here
     * except make sure PWM output is disabled until the first command. */
    HAL_GPIO_WritePin(STEP_DIR_PORT, STEP_DIR_PIN, GPIO_PIN_SET);
    HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);

    s_current_freq = 0;
    s_target_freq  = 0;
}

/**
 * map_pot_to_freq — linear map from ADC deflection to throttle frequency.
 * Caller must only invoke this when adc > STEPPER_DEADBAND.
 */
static int32_t map_pot_to_freq(uint16_t adc)
{
    int32_t range = (int32_t)adc - (int32_t)STEPPER_DEADBAND;
    int32_t span  = 4095 - (int32_t)STEPPER_DEADBAND;   /* ≈ 3945 */

    int32_t freq = (int32_t)STEPPER_FREQ_MIN
                 + range * ((int32_t)STEPPER_FREQ_MAX
                            - (int32_t)STEPPER_FREQ_MIN) / span;

    return clamp32(freq, (int32_t)STEPPER_FREQ_MIN,
                          (int32_t)STEPPER_FREQ_MAX);
}

void STEPPER_Update(uint16_t cw_adc, uint16_t ccw_adc)
{
    /* Record latest samples for status queries (UI diagnostic panel) */
    s_last_cw_adc  = cw_adc;
    s_last_ccw_adc = ccw_adc;

    /* ── 1. Select target frequency from the two throttles ──────────────
     *   Both at rest       → stop
     *   Only CW  active    → positive freq (CW  rotation)
     *   Only CCW active    → negative freq (CCW rotation)
     *   Both active        → stop (conflict — safety first; user must
     *                             release one pot before motion resumes) */
    uint8_t cw_active  = (cw_adc  > (uint16_t)STEPPER_DEADBAND);
    uint8_t ccw_active = (ccw_adc > (uint16_t)STEPPER_DEADBAND);

    if (cw_active && !ccw_active)
    {
        s_target_freq =  map_pot_to_freq(cw_adc);
    }
    else if (ccw_active && !cw_active)
    {
        s_target_freq = -map_pot_to_freq(ccw_adc);
    }
    else
    {
        /* Neither active, or both active (conflict) → stop */
        s_target_freq = 0;
    }

    STEPPER_Tick();
}

void STEPPER_SetTargetFreq(int32_t hz)
{
    if (hz >  (int32_t)STEPPER_FREQ_MAX) hz =  (int32_t)STEPPER_FREQ_MAX;
    if (hz < -(int32_t)STEPPER_FREQ_MAX) hz = -(int32_t)STEPPER_FREQ_MAX;

    /* Keep the status snapshot honest so screen_stepper shows the right
     * pseudo-pot deflection — the OLED diagnostic was built around the
     * dual-pot input, but for the joystick path we map the active half
     * to "full deflection" so the bar graph still reads correctly. */
    s_last_cw_adc  = (hz > 0) ? 4095u : 0u;
    s_last_ccw_adc = (hz < 0) ? 4095u : 0u;
    s_target_freq  = hz;
}

void STEPPER_Tick(void)
{
    /* ── Direction reversal: must pass through zero first ───────────────
     * If the requested direction is opposite to current motion, clamp
     * target to 0 this cycle so the ramp decelerates to a stop. On the
     * next call the target will correctly be in the new direction. */
    if ((s_target_freq > 0 && s_current_freq < 0) ||
        (s_target_freq < 0 && s_current_freq > 0))
    {
        s_target_freq = 0;
    }

    /* ── Ramp s_current_freq toward s_target_freq ──────────────────────── */
    int32_t delta = s_target_freq - s_current_freq;

    if (delta > 0)
    {
        s_current_freq += (delta < (int32_t)STEPPER_RAMP_STEP)
                          ? delta : (int32_t)STEPPER_RAMP_STEP;
    }
    else if (delta < 0)
    {
        s_current_freq += (delta > -(int32_t)STEPPER_RAMP_STEP)
                          ? delta : -(int32_t)STEPPER_RAMP_STEP;
    }

    /* ── Push to hardware ──────────────────────────────────────────────── */
    apply_to_hardware();
}

void STEPPER_Stop(void)
{
    s_target_freq  = 0;
    s_current_freq = 0;
    HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
}

void STEPPER_GetStatus(STEPPER_Status_t *out)
{
    if (out == NULL) return;
    out->current_freq = s_current_freq;
    out->target_freq  = s_target_freq;
    out->cw_adc       = s_last_cw_adc;
    out->ccw_adc      = s_last_ccw_adc;
    out->running      = (TIM3->CCER & TIM_CCER_CC1E) ? 1u : 0u;
}
