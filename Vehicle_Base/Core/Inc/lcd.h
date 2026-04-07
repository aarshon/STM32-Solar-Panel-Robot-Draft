/*
 * lcd.h  —  16x2 HD44780 LCD, direct GPIO, 4-bit mode
 * Board : STM32 Nucleo-F767ZI  (Vehicle Base)
 *
 * Wiring (all on Port E — CN10 connector):
 *
 *   LCD Pin  Signal  STM32 Pin  CN10 Pin
 *   ───────  ──────  ─────────  ────────
 *      1     VSS     GND        (any GND)
 *      2     VDD     5V         Pin 18
 *      3     VO      10K pot middle leg (outer legs → 5V and GND)
 *      4     RS      PE0        Pin 34
 *      5     RW      GND        (tie to ground — always write)
 *      6     E       PE1        Pin 36
 *      7-10  D0-D3   NOT CONNECTED (4-bit mode)
 *      11    D4      PE2        Pin 38
 *      12    D5      PE3        Pin 40
 *      13    D6      PE4        Pin 42
 *      14    D7      PE5        Pin 44
 *      15    A       220Ω → 5V  (backlight anode)
 *      16    K       GND        (backlight cathode)
 */

#ifndef LCD_H
#define LCD_H

#include "stm32f7xx_hal.h"
#include <stdint.h>

/* ---- GPIO pin assignments ---------------------------------------------- */
#define LCD_RS_PORT   GPIOB
#define LCD_RS_PIN    GPIO_PIN_10

#define LCD_E_PORT    GPIOB
#define LCD_E_PIN     GPIO_PIN_11

#define LCD_D4_PORT   GPIOF
#define LCD_D4_PIN    GPIO_PIN_13

#define LCD_D5_PORT   GPIOE
#define LCD_D5_PIN    GPIO_PIN_9

#define LCD_D6_PORT   GPIOE
#define LCD_D6_PIN    GPIO_PIN_11

#define LCD_D7_PORT   GPIOF
#define LCD_D7_PIN    GPIO_PIN_14

/* ---- Public API -------------------------------------------------------- */
void LCD_Init       (void);
void LCD_Clear      (void);
void LCD_SetCursor  (uint8_t row, uint8_t col);   /* row 0/1, col 0–15    */
void LCD_Print      (const char *str);
void LCD_WriteStatus(const char *line1, const char *line2);

#endif /* LCD_H */
