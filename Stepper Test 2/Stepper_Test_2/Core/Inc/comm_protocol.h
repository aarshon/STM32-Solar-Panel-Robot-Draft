/*
 * comm_protocol.h  —  Solar Robot fixed-frame protocol (v2)
 *
 * Wire format (12 bytes, fixed length):
 *
 *   byte  0 : 0x41   (start marker 'A')
 *   byte  1 : 0x5A   (start marker 'Z')
 *   byte  2 : sysID  (routing — see SYS_* below)
 *   byte  3 : mode   (only meaningful when sysID targets the Arm)
 *   byte  4 : xH     ┐ joystick X, big-endian uint16, centre = 32768
 *   byte  5 : xL     ┘
 *   byte  6 : yH     ┐ joystick Y, big-endian uint16, centre = 32768
 *   byte  7 : yL     ┘
 *   byte  8 : zDir   (end-effector raise/lower button — see BTN_* below)
 *   byte  9 : yaw    (end-effector yaw button)
 *   byte 10 : 0x59   (end marker 'Y')
 *   byte 11 : 0x42   (end marker 'B')
 *
 * Routing on Base (decided by sysID alone):
 *   0x01  CTRL → BASE   : dispatched to the local handler (drives motors).
 *   0x02  CTRL → ARM    : forwarded verbatim out the Arm UART.
 *   0x03  ARM  → CTRL   : forwarded verbatim out the bridge UART.
 *
 * No CRC. The 4 marker bytes (0x41/0x5A start, 0x59/0x42 end) are the only
 * integrity check; a bit-flip in the middle of a frame can slip through.
 * Acceptable for v1 because every command is re-sent at >=10 Hz.
 */

#ifndef COMM_PROTOCOL_H
#define COMM_PROTOCOL_H

#include "stm32f7xx_hal.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Frame constants ---------------------------------------------------- */
#define COMM_FRAME_LEN      12u
#define COMM_PAYLOAD_LEN    8u           /* sysID..yaw */
#define COMM_START_1        0x41u        /* 'A' */
#define COMM_START_2        0x5Au        /* 'Z' */
#define COMM_END_1          0x59u        /* 'Y' */
#define COMM_END_2          0x42u        /* 'B' */

/* ---- sysID values (routing) -------------------------------------------- */
#define SYS_CTRL_TO_BASE    0x01u
#define SYS_CTRL_TO_ARM     0x02u
#define SYS_ARM_TO_CTRL     0x03u

/* ---- mode values (Arm only) -------------------------------------------- */
#define MODE_CONTROLLER     0x01u        /* live joystick / button input    */
#define MODE_ZEROG          0x02u        /* zero-gravity / freedrive        */
#define MODE_HOMING         0x03u        /* run homing routine              */

/* ---- Joystick encoding ------------------------------------------------- */
#define JOY_CENTRE          32768u       /* unsigned uint16, idle position  */
#define JOY_MIN             0u
#define JOY_MAX             65535u

/* ---- Button encoding (one byte, three states) -------------------------- */
#define BTN_IDLE            0x00u
#define BTN_POSITIVE        0x01u        /* raise / +yaw                    */
#define BTN_NEGATIVE        0x02u        /* lower / -yaw                    */

/* ---- Decoded frame ----------------------------------------------------- */
typedef struct {
    uint8_t  sysID;
    uint8_t  mode;
    uint16_t x;        /* unsigned, centre = JOY_CENTRE */
    uint16_t y;        /* unsigned, centre = JOY_CENTRE */
    uint8_t  zDir;
    uint8_t  yaw;
} comm_frame_t;

/* ---- Stream identifiers ------------------------------------------------ */
typedef enum {
    COMM_STREAM_BRIDGE = 0,   /* UART4, to/from ESP32-S3 (CTRL link)        */
    COMM_STREAM_ARM    = 1,   /* UART1, to/from Arm STM32                    */
    COMM_STREAM_COUNT
} comm_stream_t;

/* Handler invoked when a frame routes locally to Base (sysID = 0x01). */
typedef void (*comm_frame_handler_t)(const comm_frame_t *frame);

/* ---- Public API -------------------------------------------------------- */

/* Bind both parser instances to their UARTs. Either may be NULL to disable
 * that direction (useful for host tests). */
void COMM_Init(UART_HandleTypeDef *bridge_uart,
               UART_HandleTypeDef *arm_uart);

/* Register the local handler for sysID = SYS_CTRL_TO_BASE frames.
 * Frames with other sysIDs are forwarded by the parser without invoking it. */
void COMM_SetLocalHandler(comm_frame_handler_t fn);

/* Feed one RX byte into the parser FSM for the given stream.
 * Safe to call from a UART RX ISR. */
void COMM_FeedByte(comm_stream_t s, uint8_t byte);

/* Build and emit a frame on the UART implied by sysID:
 *   SYS_CTRL_TO_BASE  -> dispatch locally (no UART emission)
 *   SYS_CTRL_TO_ARM   -> Arm UART
 *   SYS_ARM_TO_CTRL   -> bridge UART
 * Returns 1 on success, 0 if the target UART is NULL. */
uint8_t COMM_Send(uint8_t sysID, uint8_t mode,
                  uint16_t x, uint16_t y,
                  uint8_t zDir, uint8_t yaw);

/* HAL_GetTick() value of the last fully-validated frame on each stream. */
uint32_t COMM_LastRxTick(comm_stream_t s);

/* Count of frames rejected by the parser (bad markers). For OLED INFO. */
uint16_t COMM_FrameErrorCount(void);

#ifdef __cplusplus
}
#endif

#endif /* COMM_PROTOCOL_H */
