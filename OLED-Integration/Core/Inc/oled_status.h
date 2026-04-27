/*
 * oled_status.h  —  Receive-only Solar Bot status display.
 *
 * Architecture:
 *   This board is a passive listener. It taps onto the bridge UART (or any
 *   line carrying the 12-byte Solar Bot frame format) and renders the latest
 *   state of each known stream onto an SSD1306 OLED via I2C1. It never
 *   transmits on the UART side and never reads back from the OLED.
 *
 * Frame format (mirror of Vehicle_Base/Core/Inc/comm_protocol.h):
 *
 *   byte  0 : 0x41  'A'   start1
 *   byte  1 : 0x5A  'Z'   start2
 *   byte  2 : sysID       0x01=DRIVE (CTRL->BASE)
 *                          0x02=ARM-CMD (CTRL->ARM)
 *                          0x03=ARM-STATE (ARM->CTRL)
 *   byte  3 : mode        arm-only (CONTROLLER/ZEROG/HOMING)
 *   byte  4 : xH  ┐ joystick X, big-endian uint16, centre 32768
 *   byte  5 : xL  ┘
 *   byte  6 : yH  ┐ joystick Y, big-endian uint16, centre 32768
 *   byte  7 : yL  ┘
 *   byte  8 : zDir        end-effector raise/lower button
 *   byte  9 : yaw         end-effector yaw button
 *   byte 10 : 0x59  'Y'   end1
 *   byte 11 : 0x42  'B'   end2
 *
 * Wiring expectations (configured in CubeMX):
 *   - I2C1 : PB8 SCL / PB9 SDA, fast mode, OLED at 0x3C
 *   - One UART (any) : RX pin tapped onto the bridge data line, common GND.
 *     Match the bridge baud rate (the ESP32 uses 460800; check the source).
 */

#ifndef OLED_STATUS_H
#define OLED_STATUS_H

#include <stdint.h>

/* Call once after OLED_Init(). Clears state and paints the splash. */
void OLED_Status_Init(void);

/* Feed every received UART byte. Safe to call from a HAL UART RX callback or
 * straight from a polling loop draining the DMA buffer. */
void OLED_Status_FeedByte(uint8_t b);

/* Render the current snapshot to the OLED. Cheap to call too often; it gates
 * itself internally to ~2 Hz so the I2C bus does not starve other work. */
void OLED_Status_Refresh(void);

#endif /* OLED_STATUS_H */
