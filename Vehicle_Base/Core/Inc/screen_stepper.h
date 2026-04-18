/*
 * screen_stepper.h  —  Stepper motor status screen
 * Board : STM32 Nucleo-F767ZI  (Vehicle Base — merged firmware)
 *
 * Live 128×64 OLED panel showing Motor #1 (NEMA 17, TIM3 CH1 PWM) state:
 *   · Run/stop badge        (inverted when PWM is pulsing)
 *   · Step frequency in Hz  (signed; negative = CCW)
 *   · Direction label       (CW / CCW / STP)
 *   · Pot1 raw + percent    (the input driving Motor #1)
 *   · Pot2 raw              (reserved — Motor #2, not yet implemented)
 *   · Bar graph             (|current_freq| scaled to STEPPER_FREQ_MAX)
 *
 * The module is deliberately passive: it reads STEPPER_GetStatus() and the
 * adc_dma_buf[] global from main.c.  It does NOT drive the stepper — that
 * stays the job of STEPPER_Update() called in the main loop.
 *
 * Keys handled on this screen:
 *   '*'  →  return to main menu
 *   '0'  →  emergency stop (calls STEPPER_Stop)
 *   any  →  ignored
 */

#ifndef SCREEN_STEPPER_H
#define SCREEN_STEPPER_H

#include "stm32f7xx_hal.h"

/* Action returned by key handler so ui.c can change screens without
 * screen_stepper needing to know about the UI_Screen_t enum. */
typedef enum {
    SCREEN_STEPPER_ACTION_NONE = 0,
    SCREEN_STEPPER_ACTION_BACK       /* User pressed '*' */
} ScreenStepper_Action_t;

/** Render the stepper status panel.  Call from UI_Update's render switch. */
void ScreenStepper_Draw(void);

/** Handle a keypad event.  Returns an action for the UI layer to apply. */
ScreenStepper_Action_t ScreenStepper_HandleKey(uint8_t key);

#endif /* SCREEN_STEPPER_H */
