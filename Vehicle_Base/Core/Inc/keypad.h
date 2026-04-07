/*
 * keypad.h  —  3x4 Membrane Keypad Driver
 * Board : STM32 Nucleo-F767ZI  (Vehicle Base)
 *
 * Wiring:
 *   ROW 1 → PE7    ROW 2 → PE8    ROW 3 → PE10   ROW 4 → PE12
 *   COL 1 → PE14   COL 2 → PG9    COL 3 → PG14
 *
 * Physical layout:
 *        COL1(PE14)  COL2(PG9)  COL3(PG14)
 *  ROW1(PE7)  [1]       [2]       [3]
 *  ROW2(PE8)  [4]       [5]       [6]
 *  ROW3(PE10) [7]       [8]       [9]
 *  ROW4(PE12) [*]       [0]       [#]
 *
 * Scan method: drive each row LOW one at a time, read columns (input+pullup).
 * A pressed key pulls its column LOW.
 *
 * Return values:
 *   KEYPAD_Scan() returns the ASCII character of the pressed key,
 *   or KEY_NONE (0xFF) if no key is pressed.
 *   A hold event returns  (char | 0x80)  — high bit set.
 *   Menu screens should mask with 0x7F and ignore hold events.
 */

#ifndef KEYPAD_H
#define KEYPAD_H

#include "stm32f7xx_hal.h"
#include <stdint.h>

/* ---- Row GPIO (all on GPIOE) ------------------------------------------- */
#define KP_ROW1_PORT  GPIOE
#define KP_ROW1_PIN   GPIO_PIN_7
#define KP_ROW2_PORT  GPIOE
#define KP_ROW2_PIN   GPIO_PIN_8
#define KP_ROW3_PORT  GPIOE
#define KP_ROW3_PIN   GPIO_PIN_10
#define KP_ROW4_PORT  GPIOE
#define KP_ROW4_PIN   GPIO_PIN_12

/* ---- Column GPIO ------------------------------------------------------- */
#define KP_COL1_PORT  GPIOE
#define KP_COL1_PIN   GPIO_PIN_14
#define KP_COL2_PORT  GPIOG
#define KP_COL2_PIN   GPIO_PIN_9
#define KP_COL3_PORT  GPIOG
#define KP_COL3_PIN   GPIO_PIN_14

/* ---- Timing ------------------------------------------------------------ */
#define KEYPAD_DEBOUNCE_MS   30    /* Key must be stable this long to register */
#define KEYPAD_HOLD_MS      600    /* Hold time before repeat events start     */
#define KEYPAD_REPEAT_MS    150    /* Auto-repeat interval after hold          */

/* ---- Special return value ---------------------------------------------- */
#define KEY_NONE  0xFF             /* No key pressed / no event               */

/* ---- Public API -------------------------------------------------------- */
void    KEYPAD_Init (void);
uint8_t KEYPAD_Scan (void);   /* Returns key char, KEY_NONE, or char|0x80 for hold */

#endif /* KEYPAD_H */
