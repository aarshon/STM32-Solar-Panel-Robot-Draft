/*
 * oled.h  —  SSD1306 128x64 OLED, I2C
 * Board : STM32 Nucleo-F767ZI  (Vehicle Base)
 *
 * Wiring:
 *   VCC  →  5V
 *   GND  →  GND
 *   SDA  →  PB9  (I2C1 SDA)
 *   SCL  →  PB8  (I2C1 SCL)
 */

#ifndef OLED_H
#define OLED_H

#include "stm32f7xx_hal.h"
#include <stdint.h>

/* I2C handle — I2C1 configured by CubeMX */
extern I2C_HandleTypeDef hi2c1;
#define OLED_I2C       hi2c1

/* 7-bit I2C address — 0x3C is standard, use 0x3D if nothing shows */
#define OLED_I2C_ADDR  0x3C

/* Display dimensions */
#define OLED_WIDTH   128
#define OLED_HEIGHT   64
#define OLED_PAGES     8    /* 64px / 8px per page */

/* ---- Public API -------------------------------------------------------- */
void OLED_Init       (void);
void OLED_Clear      (void);
void OLED_SetCursor  (uint8_t page, uint8_t col);  /* page 0-7, col 0-127  */
void OLED_Print      (const char *str);
void OLED_WriteStatus(const char *line1, const char *line2);

#endif /* OLED_H */
