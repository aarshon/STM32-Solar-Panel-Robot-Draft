/*
 * oled_status.h  —  Receive-only Solar Bot status display, in-process variant.
 *
 * Architecture:
 *   This module is meant to be merged into the Base STM32 firmware
 *   (integration-ian/Base-Ctrl-integration). The OLED hangs off I2C1 on the
 *   same Nucleo that already owns the bridge UART, the arm UART, and the
 *   motor link. There is no UART tap and no parser — Ian's code already
 *   validates 12-byte frames on receive, so this module just exposes a few
 *   "snapshot setter" entry points for him to call after his own marker
 *   check, plus a periodic refresh that paints the screen.
 *
 * Frame layout (matches Vehicle_Base/Core/Inc/comm_protocol.h):
 *
 *   byte  0 : 0x41  'A'   start1
 *   byte  1 : 0x5A  'Z'   start2
 *   byte  2 : sysID       0x01=DRIVE (CTRL->BASE)
 *                         0x02=ARM   (CTRL->ARM, forwarded out by Base)
 *                         0x03=STATE (ARM->CTRL)
 *   byte  3 : mode        arm-only
 *   byte  4 : xH  ┐ joystick X, big-endian uint16, centre 32768
 *   byte  5 : xL  ┘
 *   byte  6 : yH  ┐ joystick Y, big-endian uint16, centre 32768
 *   byte  7 : yL  ┘
 *   byte  8 : zDir
 *   byte  9 : yaw
 *   byte 10 : 0x59  'Y'   end1
 *   byte 11 : 0x42  'B'   end2
 *
 * Wiring (configured in CubeMX on the Base board):
 *   - I2C1 : PB8 SCL / PB9 SDA, fast mode preferred, OLED at 0x3C.
 *   - That is the only peripheral this module uses. No UART, no GPIO, no DMA.
 */

#ifndef OLED_STATUS_H
#define OLED_STATUS_H

#include <stdint.h>

/* Call once after OLED_Init(). Clears state and paints the splash. */
void OLED_Status_Init(void);

/* Call from the bridge-UART receive path AFTER Ian's marker check passes.
 * The 12 bytes are interpreted as a CTRL-side frame and routed by sysID:
 *   0x01 -> DRIVE snapshot
 *   0x02 -> ARM-cmd snapshot
 * Any other sysID is ignored silently. */
void OLED_Status_NoteCtrlFrame(const uint8_t buf12[12]);

/* Call from the arm-UART receive path. The 12 bytes are stored as the
 * ARM-state snapshot regardless of sysID byte (Ian's arm path doesn't
 * validate the inner header). */
void OLED_Status_NoteArmFrame(const uint8_t buf12[12]);

/* Call from the marker-fail / brake branch. Just bumps an error counter
 * displayed at the bottom of the screen — useful for catching baud or
 * wiring issues at a glance. */
void OLED_Status_NoteCommError(void);

/* Call from the motor-poll tick (any rate is fine — only the latest values
 * survive to the next refresh). Values in the [-1.0, +1.0] duty range as
 * Ian already uses; the display formats them as signed hundredths. */
void OLED_Status_NoteThrottle(float left, float right);

/* Render the current snapshot to the OLED. Cheap to call hot in while(1) —
 * self-gates to ~2 Hz internally. */
void OLED_Status_Refresh(void);

#endif /* OLED_STATUS_H */
