/*
 * comm_protocol.h  —  SRR-CP v1.0 (Solar Robot Command Protocol)
 *
 *  Uniform packet format used on every serial hop in the robot:
 *      Pi  <-MQTT/BLE->  ESP32-S3  <-UART4->  Base STM32  <-UART1->  Arm/EE STM32
 *
 *  Frame layout (8..24 bytes):
 *      [STX=0xAA] [DST] [SRC] [CMD] [LEN<=16] [P0..P(LEN-1)] [CRC8] [ETX=0x55]
 *  CRC8 poly 0x31 (Dallas/Maxim), init 0x00, covers DST..last_payload.
 *  All multi-byte payload values are big-endian.
 */

#ifndef COMM_PROTOCOL_H
#define COMM_PROTOCOL_H

#include "stm32f7xx_hal.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Framing constants ---------------------------------------------------- */
#define COMM_STX                0xAAu
#define COMM_ETX                0x55u
#define COMM_PAYLOAD_MAX        16u
#define COMM_FRAME_MAX          (6u + COMM_PAYLOAD_MAX + 2u) /* 24 bytes */

/* ---- Addresses ------------------------------------------------------------ */
#define ADDR_PI                 0x00u
#define ADDR_BASE               0x01u
#define ADDR_ARM_EE             0x02u
#define ADDR_ESP32              0xFEu
#define ADDR_BROADCAST          0xFFu

/* ---- Command IDs (grouped by high nibble) --------------------------------- */
#define CMD_DRIVE               0x10u
#define CMD_DRIVE_STOP          0x11u
#define CMD_DRIVE_TRIM          0x12u

#define CMD_ARM_TARGET          0x20u
#define CMD_ARM_HOME            0x21u
#define CMD_ARM_JOG             0x22u
#define CMD_ARM_ZERO            0x23u

#define CMD_EE_TORQUE           0x30u
#define CMD_EE_PULSE            0x31u

/* 0x40..0x42 reserved (formerly CMD_ESTOP_*; e-stop subsystem removed) */

#define CMD_STATUS_REQ          0x50u
#define CMD_STATUS_REPLY        0x51u
#define CMD_STATUS_STREAM       0x52u

#define CMD_FAULT_REPORT        0x60u
#define CMD_FAULT_CLEAR         0x61u

#define CMD_HEARTBEAT           0x70u
#define CMD_PING                0x71u
#define CMD_PONG                0x72u

/* ---- Status field IDs (used with STATUS_REQ/REPLY/STREAM) ----------------- */
#define FIELD_BATT_MV           0x01u
#define FIELD_BATT_PCT          0x02u
#define FIELD_VESC_L_RPM        0x10u
#define FIELD_VESC_L_CURRENT    0x11u
#define FIELD_VESC_L_DUTY       0x12u
#define FIELD_VESC_L_TEMP       0x13u
#define FIELD_VESC_L_FAULT      0x14u
#define FIELD_SLAVE_HB_AGE      0x30u
/* 0x50 reserved (formerly FIELD_ESTOP_STATE) */
#define FIELD_FW_VERSION        0xFFu

/* ---- Seed fault codes (full catalogue deferred) --------------------------- */
#define FAULT_NONE              0x00u
#define FAULT_BATT_LOW          0x40u
#define FAULT_BATT_CRITICAL     0x41u
#define FAULT_WATCHDOG          0x52u   /* slave/base comms loss → motor halt */
/* 0x50, 0x51, 0x53, 0x54 reserved (formerly FAULT_ESTOP_*) */

/* ---- Stream identifiers --------------------------------------------------- */
typedef enum {
    COMM_STREAM_PI    = 0,   /* UART4, upstream to ESP32-S3 / Pi         */
    COMM_STREAM_SLAVE = 1,   /* UART1, downstream to Arm+EE STM32        */
    COMM_STREAM_COUNT
} comm_stream_t;

/* ---- Handler callback signature ------------------------------------------- */
typedef void (*comm_handler_t)(uint8_t src,
                               const uint8_t *payload,
                               uint8_t len);

/* ---- Public API ----------------------------------------------------------- */

/* Initialise both parser instances and bind them to the two UART handles.
 * Either pointer may be NULL to disable that direction (useful for host tests). */
void COMM_Init(UART_HandleTypeDef *pi_uart,
               UART_HandleTypeDef *slave_uart);

/* Register a local handler for CMD. Overwrites any previous registration. */
void COMM_RegisterHandler(uint8_t cmd, comm_handler_t fn);

/* Feed one RX byte into the parser FSM for the given stream. Call from ISR. */
void COMM_FeedByte(comm_stream_t s, uint8_t byte);

/* Build and emit a frame. SRC is fixed to ADDR_BASE. Routing:
 *   dst==ADDR_BASE          -> dispatch locally (no UART emission)
 *   dst==ADDR_ARM_EE        -> UART1
 *   dst==ADDR_PI/ADDR_ESP32 -> UART4
 *   dst==ADDR_BROADCAST     -> both UARTs, local dispatch also runs
 * Returns 1 on success, 0 if LEN>16 or UART unavailable. */
uint8_t COMM_Send(uint8_t dst, uint8_t cmd,
                  const uint8_t *payload, uint8_t len);

/* Raw emitter — caller chooses stream and SRC. Used for frame forwarding. */
uint8_t COMM_SendOnStream(comm_stream_t s,
                          uint8_t dst, uint8_t src,
                          uint8_t cmd,
                          const uint8_t *payload, uint8_t len);

/* Millisecond timestamp of the last fully-validated frame on each stream.
 * Used by main loop for slave-timeout / Pi-timeout UI. */
uint32_t COMM_LastRxTick(comm_stream_t s);

/* Counters for the INFO screen (protocol error stats). */
uint16_t COMM_ProtoErrorCount(void);

/* 256-byte Dallas/Maxim CRC8, poly 0x31, init 0x00. */
uint8_t crc8(const uint8_t *buf, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* COMM_PROTOCOL_H */
