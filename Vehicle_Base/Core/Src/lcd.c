/*
 * lcd.c  —  16x2 HD44780 LCD via I2C / PCF8574 backpack
 * Board : STM32 Nucleo-F767ZI  (Vehicle Base)
 *
 * PCF8574 bit layout (standard module wiring):
 *
 *   Bit:  7    6    5    4    3    2    1    0
 *   Pin: D7   D6   D5   D4   BL   EN   RW   RS
 *
 *  RS  — Register Select  (0 = command, 1 = data)
 *  RW  — Read/Write       (always 0 = write in this driver)
 *  EN  — Enable latch     (pulse high → low to write)
 *  BL  — Backlight        (keep high to keep backlight on)
 *  D4-D7 — 4-bit data bus (HD44780 operated in 4-bit mode)
 */

#include "lcd.h"
#include <stdio.h>
#include <string.h>

/* ---- PCF8574 control-bit masks ----------------------------------------- */
#define PCF_RS  0x01    /* Register select                                   */
#define PCF_RW  0x02    /* Read/write (always 0 here)                        */
#define PCF_EN  0x04    /* Enable latch                                      */
#define PCF_BL  0x08    /* Backlight — keep this always set                  */

/* ---- Low-level I2C write ----------------------------------------------- */
/* Sends one byte to the PCF8574 expander.                                   */
static void pcf_write(uint8_t data)
{
    /* HAL I2C API expects the 7-bit address shifted left by 1              */
    data |= PCF_BL;     /* Backlight always on                               */
    HAL_I2C_Master_Transmit(&LCD_I2C, LCD_I2C_ADDR << 1, &data, 1, 5);
}

/* ---- Pulse the EN line to latch 4 bits of data ------------------------- */
static void lcd_pulse_en(uint8_t data)
{
    pcf_write(data | PCF_EN);   /* EN high — data is latched on falling edge */
    HAL_Delay(1);
    pcf_write(data & ~PCF_EN);  /* EN low  — latch happens here              */
    HAL_Delay(1);
}

/* ---- Send one nibble (4 bits) to the LCD ------------------------------- */
/* rs = 0 for commands, PCF_RS for data characters                          */
static void lcd_send_nibble(uint8_t nibble, uint8_t rs)
{
    /* Upper nibble sits in bits [7:4] of the PCF byte (D7-D4)             */
    uint8_t data = (nibble & 0xF0) | rs;
    lcd_pulse_en(data);
}

/* ---- Send a full byte as two nibbles (MSB first) ----------------------- */
static void lcd_send_byte(uint8_t byte, uint8_t rs)
{
    lcd_send_nibble( byte & 0xF0,       rs);    /* High nibble first        */
    lcd_send_nibble((byte << 4) & 0xF0, rs);    /* Low  nibble second       */
}

/* ---- Send an LCD command (RS = 0) -------------------------------------- */
static void lcd_command(uint8_t cmd)
{
    lcd_send_byte(cmd, 0x00);
    HAL_Delay(2);   /* Most commands need < 1.5 ms; 2 ms is safe            */
}

/* ---- Send an LCD data character (RS = 1) ------------------------------- */
static void lcd_char(uint8_t ch)
{
    lcd_send_byte(ch, PCF_RS);
}

/* ========================================================================
 * Public API
 * ====================================================================== */

/* ---- LCD_Init : power-on initialisation sequence ----------------------- */
/* Must be called once in main() after HAL_Init() and I2C init.             */
void LCD_Init(void)
{
    HAL_Delay(50);      /* Wait for LCD power-up (datasheet: ≥ 40 ms)       */

    /* HD44780 4-bit initialisation sequence (3× function-set then switch) */
    lcd_send_nibble(0x30, 0); HAL_Delay(5);
    lcd_send_nibble(0x30, 0); HAL_Delay(1);
    lcd_send_nibble(0x30, 0); HAL_Delay(1);
    lcd_send_nibble(0x20, 0);               /* Switch to 4-bit mode         */

    lcd_command(0x28);  /* Function set: 4-bit, 2 lines, 5×8 font           */
    lcd_command(0x0C);  /* Display on, cursor off, blink off                 */
    lcd_command(0x06);  /* Entry mode: increment cursor, no display shift    */
    lcd_command(0x01);  /* Clear display                                     */
    HAL_Delay(2);       /* Clear needs extra time                            */
}

/* ---- LCD_Clear : clear all characters and return cursor to home --------- */
void LCD_Clear(void)
{
    lcd_command(0x01);
    HAL_Delay(2);
}

/* ---- LCD_SetCursor : move cursor to (row, col) ------------------------- */
/* row: 0 = top line, 1 = bottom line   col: 0-15                           */
void LCD_SetCursor(uint8_t row, uint8_t col)
{
    /* HD44780 DDRAM addresses: row 0 starts at 0x00, row 1 at 0x40        */
    uint8_t addr = (row == 0) ? 0x00 : 0x40;
    lcd_command(0x80 | (addr + col));
}

/* ---- LCD_Print : write a null-terminated string at current cursor ------ */
void LCD_Print(const char *str)
{
    while (*str) {
        lcd_char((uint8_t)*str++);
    }
}

/* ---- LCD_WriteStatus : update both display lines at once --------------- */
/*
 * Writes line1 on row 0 and line2 on row 1.
 * Each line is padded / truncated to exactly 16 characters so
 * old characters from longer previous strings are overwritten.
 *
 * Example:
 *   LCD_WriteStatus("FORWARD", "Speed: 200");
 *
 *   Row 0: "FORWARD         "
 *   Row 1: "Speed: 200      "
 */
void LCD_WriteStatus(const char *line1, const char *line2)
{
    char buf[17];   /* 16 visible chars + null terminator                   */

    LCD_SetCursor(0, 0);
    snprintf(buf, sizeof(buf), "%-16s", line1); /* Left-align, pad to 16   */
    LCD_Print(buf);

    LCD_SetCursor(1, 0);
    snprintf(buf, sizeof(buf), "%-16s", line2);
    LCD_Print(buf);
}
