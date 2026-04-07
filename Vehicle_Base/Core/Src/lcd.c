/*
 * lcd.c  —  16x2 HD44780 LCD, direct GPIO, 4-bit mode
 * Board : STM32 Nucleo-F767ZI  (Vehicle Base)
 *
 * Communication summary:
 *   The HD44780 is driven in 4-bit mode using PE0–PE5.
 *   Each byte is sent as two nibbles (high then low), with an E pulse
 *   after each nibble to latch the data into the LCD controller.
 *   RW is permanently tied to GND (write-only).
 */

#include "lcd.h"
#include <stdio.h>
#include <string.h>

/* ---- Write one nibble (4 bits) to D4–D7 and pulse E ------------------- */
/* rs: 0 = command register, 1 = data register                              */
static void lcd_write_nibble(uint8_t nibble, uint8_t rs)
{
    /* RS line */
    HAL_GPIO_WritePin(LCD_RS_PORT, LCD_RS_PIN,
                      rs ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* Data lines D4–D7 — nibble bits 0–3 map to D4–D7 respectively */
    HAL_GPIO_WritePin(LCD_D4_PORT, LCD_D4_PIN,
                      (nibble & 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_D5_PORT, LCD_D5_PIN,
                      (nibble & 0x02) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_D6_PORT, LCD_D6_PIN,
                      (nibble & 0x04) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_D7_PORT, LCD_D7_PIN,
                      (nibble & 0x08) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* Pulse E high → low to latch the nibble */
    HAL_GPIO_WritePin(LCD_E_PORT, LCD_E_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(LCD_E_PORT, LCD_E_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
}

/* ---- Send a full byte as two nibbles (high nibble first) --------------- */
static void lcd_send_byte(uint8_t byte, uint8_t rs)
{
    lcd_write_nibble(byte >> 4,   rs);   /* High nibble */
    lcd_write_nibble(byte & 0x0F, rs);   /* Low  nibble */
}

/* ---- Send a command byte (RS = 0) -------------------------------------- */
static void lcd_command(uint8_t cmd)
{
    lcd_send_byte(cmd, 0);
    HAL_Delay(2);   /* Most commands need < 1.5 ms; 2 ms is safe            */
}

/* ---- Send a data character (RS = 1) ------------------------------------ */
static void lcd_char(uint8_t ch)
{
    lcd_send_byte(ch, 1);
}

/* ========================================================================
 * Public API
 * ====================================================================== */

/* ---- LCD_Init : power-on initialisation sequence ----------------------- */
void LCD_Init(void)
{
    HAL_Delay(50);   /* Wait for LCD power-up (datasheet: ≥ 40 ms)          */

    /* HD44780 4-bit initialisation sequence (send 0x3 three times, then switch) */
    lcd_write_nibble(0x03, 0); HAL_Delay(5);
    lcd_write_nibble(0x03, 0); HAL_Delay(1);
    lcd_write_nibble(0x03, 0); HAL_Delay(1);
    lcd_write_nibble(0x02, 0);              /* Switch to 4-bit mode          */

    lcd_command(0x28);  /* Function set: 4-bit, 2 lines, 5×8 font           */
    lcd_command(0x0C);  /* Display on, cursor off, blink off                 */
    lcd_command(0x06);  /* Entry mode: cursor increments, no display shift   */
    lcd_command(0x01);  /* Clear display                                     */
    HAL_Delay(2);
}

/* ---- LCD_Clear --------------------------------------------------------- */
void LCD_Clear(void)
{
    lcd_command(0x01);
    HAL_Delay(2);
}

/* ---- LCD_SetCursor : row 0/1, col 0–15 --------------------------------- */
void LCD_SetCursor(uint8_t row, uint8_t col)
{
    uint8_t addr = (row == 0) ? 0x00 : 0x40;
    lcd_command(0x80 | (addr + col));
}

/* ---- LCD_Print : write null-terminated string at current cursor -------- */
void LCD_Print(const char *str)
{
    while (*str) {
        lcd_char((uint8_t)*str++);
    }
}

/* ---- LCD_WriteStatus : update both lines at once ----------------------- */
void LCD_WriteStatus(const char *line1, const char *line2)
{
    char buf[17];   /* 16 visible chars + null terminator                   */

    LCD_SetCursor(0, 0);
    snprintf(buf, sizeof(buf), "%-16s", line1);
    LCD_Print(buf);

    LCD_SetCursor(1, 0);
    snprintf(buf, sizeof(buf), "%-16s", line2);
    LCD_Print(buf);
}
