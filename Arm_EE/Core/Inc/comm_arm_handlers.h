/*
 * comm_arm_handlers.h  —  SRR-CP command handlers for ADDR_ARM_EE
 *
 * Each handler matches the comm_handler_t signature in comm_protocol.h.
 * Wire them up at boot with COMM_RegisterHandler(CMD_*, handle_*).
 *
 * Payload formats (subject to change — keep Base + Pi side in sync):
 *   CMD_ARM_TARGET   : int16 x_mm, int16 y_mm, int16 z_mm  (big-endian, 6 B)
 *   CMD_ARM_HOME     : uint8 joint_mask                    (1 B; 0x07 = all)
 *   CMD_ARM_JOG      : uint8 joint, int8 dir, uint8 speed  (3 B)
 *   CMD_ARM_ZERO     : uint8 joint_mask                    (1 B)
 *   CMD_EE_TORQUE    : int8  torque                        (1 B)
 *   CMD_EE_PULSE     : uint16 duration_ms                  (2 B, big-endian)
 *   CMD_ESTOP_ASSERT : uint8 reason_code                   (1 B)
 *   CMD_ESTOP_CLEAR  : (none)
 *   CMD_STATUS_REQ   : (none) — replies with STATUS_REPLY
 *   CMD_HEARTBEAT    : uint16 uptime_seconds               (2 B, big-endian)
 *   CMD_PING         : (none) — replies with CMD_PONG echoing payload
 */

#ifndef COMM_ARM_HANDLERS_H
#define COMM_ARM_HANDLERS_H

#include "comm_protocol.h"

void Arm_RegisterAllHandlers(void);

/* Periodic helpers driven from main loop */
void Arm_SendHeartbeat(uint32_t uptime_ms);
void Arm_CheckBaseLiveness(uint32_t now_ms);  /* trips e-stop if no HB */

/* Last fault we emitted; latest STATUS reply pulls from this. */
uint8_t Arm_LastFault(void);

#endif /* COMM_ARM_HANDLERS_H */
