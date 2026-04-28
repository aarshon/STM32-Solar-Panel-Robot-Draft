/*
 * oled.h  —  SSD1306 128x64 OLED, I2C, blocking driver
 * Board : STM32 Nucleo-F767ZI
 *
 * Wiring (matches CubeMX I2C1 default):
 *   VCC  ->  3V3 or 5V
 *   GND  ->  GND
 *   SDA  ->  PB9  (I2C1 SDA)
 *   SCL  ->  PB8  (I2C1 SCL)
 *
 * Page-addressing mode. 5x8 ASCII font + 1px gap = 6px/char, so a row is
 * 21 characters wide and the panel is 8 rows tall.
 */

#ifndef OLED_H
#define OLED_H

#include "stm32f7xx_hal.h"
#include <stdint.h>

extern I2C_HandleTypeDef hi2c1;
#define OLED_I2C       hi2c1
#define OLED_I2C_ADDR  0x3C   /* 7-bit; try 0x3D if the panel stays blank */

#define OLED_WIDTH     128
#define OLED_HEIGHT     64
#define OLED_PAGES       8

void OLED_Init       (void);
void OLED_Clear      (void);
void OLED_SetCursor  (uint8_t page, uint8_t col);   /* page 0-7, col 0-127 */
void OLED_Print      (const char *str);
void OLED_WriteStatus(const char *line1, const char *line2);

#endif /* OLED_H */
