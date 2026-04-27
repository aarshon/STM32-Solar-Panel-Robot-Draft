/*
 * comm_protocol.c  —  Solar Robot fixed-frame protocol (v2)
 *
 * See comm_protocol.h for the wire format. Each UART stream owns one
 * 5-state parser. On a fully-validated frame the parser routes by sysID.
 *
 * Role flag — defined per project so the same source compiles on Base and
 * on Arm without behavioural divergence anywhere except the local-dispatch
 * sysID:
 *   - Base build  (default, flag absent) : sysID 0x01 dispatches locally;
 *                                          0x02 forwarded to Arm UART;
 *                                          0x03 forwarded to bridge UART.
 *   - Arm build   (COMM_ROLE_ARM defined): sysID 0x02 dispatches locally;
 *                                          0x01 / 0x03 paths inert (Arm has
 *                                          only one UART).
 */

#define COMM_ROLE_ARM   /* Stepper_Test_2 (Arm) build flag */

#include "comm_protocol.h"
#include <string.h>

/* =========================================================================
 * Per-stream parser state
 * ========================================================================= */
typedef enum {
    PS_WAIT_START_1 = 0,   /* expecting 0x41                       */
    PS_WAIT_START_2,       /* got 0x41, expecting 0x5A             */
    PS_COLLECT,            /* collecting 8 payload bytes           */
    PS_WAIT_END_1,         /* payload done, expecting 0x59         */
    PS_WAIT_END_2          /* got 0x59, expecting 0x42             */
} parser_state_t;

typedef struct {
    parser_state_t state;
    uint8_t  idx;                       /* index into payload during COLLECT */
    uint8_t  raw[COMM_FRAME_LEN];       /* full frame bytes for forwarding   */
    uint32_t last_rx_tick;
} parser_t;

/* =========================================================================
 * Module state
 * ========================================================================= */
static UART_HandleTypeDef *s_bridge_uart = NULL;
static UART_HandleTypeDef *s_arm_uart    = NULL;

static parser_t              s_parser[COMM_STREAM_COUNT];
static comm_frame_handler_t  s_local_handler = NULL;
static uint16_t              s_frame_errors  = 0;

/* =========================================================================
 * Helpers
 * ========================================================================= */
static void parser_reset(parser_t *p)
{
    p->state = PS_WAIT_START_1;
    p->idx   = 0;
}

static UART_HandleTypeDef *uart_for(comm_stream_t s)
{
    return (s == COMM_STREAM_BRIDGE) ? s_bridge_uart : s_arm_uart;
}

static void emit_raw(comm_stream_t s, const uint8_t *frame12)
{
    UART_HandleTypeDef *u = uart_for(s);
    if (u == NULL) return;
    /* 12 B @ 460800 ≈ 0.26 ms; @ 115200 ≈ 1.04 ms — both safe to block. */
    HAL_UART_Transmit(u, (uint8_t *)frame12, COMM_FRAME_LEN, 10);
}

static void build_frame(uint8_t out[COMM_FRAME_LEN],
                        uint8_t sysID, uint8_t mode,
                        uint16_t x, uint16_t y,
                        uint8_t zDir, uint8_t yaw)
{
    out[0]  = COMM_START_1;
    out[1]  = COMM_START_2;
    out[2]  = sysID;
    out[3]  = mode;
    out[4]  = (uint8_t)(x >> 8);
    out[5]  = (uint8_t)(x & 0xFFu);
    out[6]  = (uint8_t)(y >> 8);
    out[7]  = (uint8_t)(y & 0xFFu);
    out[8]  = zDir;
    out[9]  = yaw;
    out[10] = COMM_END_1;
    out[11] = COMM_END_2;
}

static void decode_frame(const uint8_t raw[COMM_FRAME_LEN], comm_frame_t *f)
{
    f->sysID = raw[2];
    f->mode  = raw[3];
    f->x     = ((uint16_t)raw[4] << 8) | raw[5];
    f->y     = ((uint16_t)raw[6] << 8) | raw[7];
    f->zDir  = raw[8];
    f->yaw   = raw[9];
}

/* Route a fully-validated frame based on sysID alone. The source stream
 * is informational only; routing decisions don't depend on it. */
static void route_frame(comm_stream_t src_stream, const parser_t *p)
{
    (void)src_stream;
    const uint8_t sysID = p->raw[2];

    switch (sysID)
    {
#ifdef COMM_ROLE_ARM
        case SYS_CTRL_TO_ARM:
            /* Arm build — local dispatch instead of forwarding. */
            if (s_local_handler) {
                comm_frame_t f;
                decode_frame(p->raw, &f);
                s_local_handler(&f);
            }
            break;

        case SYS_CTRL_TO_BASE:
            /* Not addressed to us — drop silently. */
            break;

        case SYS_ARM_TO_CTRL:
            /* Forward upstream out the bridge UART (only one UART on Arm —
             * if bridge_uart is NULL the emit_raw is a no-op). */
            emit_raw(COMM_STREAM_BRIDGE, p->raw);
            break;
#else
        case SYS_CTRL_TO_BASE:
            if (s_local_handler) {
                comm_frame_t f;
                decode_frame(p->raw, &f);
                s_local_handler(&f);
            }
            break;

        case SYS_CTRL_TO_ARM:
            /* Forward CTRL→ARM verbatim out the Arm UART. */
            emit_raw(COMM_STREAM_ARM, p->raw);
            break;

        case SYS_ARM_TO_CTRL:
            /* Forward ARM→CTRL verbatim out the bridge UART. */
            emit_raw(COMM_STREAM_BRIDGE, p->raw);
            break;
#endif

        default:
            /* Unknown sysID — count and drop. */
            s_frame_errors++;
            break;
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */
void COMM_Init(UART_HandleTypeDef *bridge_uart,
               UART_HandleTypeDef *arm_uart)
{
    s_bridge_uart = bridge_uart;
    s_arm_uart    = arm_uart;
    s_local_handler = NULL;
    s_frame_errors  = 0;
    for (int i = 0; i < COMM_STREAM_COUNT; i++) {
        parser_reset(&s_parser[i]);
        s_parser[i].last_rx_tick = 0;
    }
}

void COMM_SetLocalHandler(comm_frame_handler_t fn)
{
    s_local_handler = fn;
}

void COMM_FeedByte(comm_stream_t s, uint8_t byte)
{
    if (s >= COMM_STREAM_COUNT) return;
    parser_t *p = &s_parser[s];

    switch (p->state)
    {
        case PS_WAIT_START_1:
            if (byte == COMM_START_1) {
                p->raw[0] = byte;
                p->state  = PS_WAIT_START_2;
            }
            /* Any other byte: silent drop — bus noise / mid-frame resync. */
            break;

        case PS_WAIT_START_2:
            if (byte == COMM_START_2) {
                p->raw[1] = byte;
                p->idx    = 0;
                p->state  = PS_COLLECT;
            } else {
                /* If we got 0x41 again, stay armed for the second marker. */
                s_frame_errors++;
                if (byte == COMM_START_1) {
                    p->raw[0] = byte;
                } else {
                    parser_reset(p);
                }
            }
            break;

        case PS_COLLECT:
            p->raw[2 + p->idx] = byte;
            p->idx++;
            if (p->idx >= COMM_PAYLOAD_LEN) {
                p->state = PS_WAIT_END_1;
            }
            break;

        case PS_WAIT_END_1:
            if (byte == COMM_END_1) {
                p->raw[10] = byte;
                p->state   = PS_WAIT_END_2;
            } else {
                s_frame_errors++;
                parser_reset(p);
            }
            break;

        case PS_WAIT_END_2:
            if (byte == COMM_END_2) {
                p->raw[11] = byte;
                p->last_rx_tick = HAL_GetTick();
                route_frame(s, p);
            } else {
                s_frame_errors++;
            }
            parser_reset(p);
            break;

        default:
            parser_reset(p);
            break;
    }
}

uint8_t COMM_Send(uint8_t sysID, uint8_t mode,
                  uint16_t x, uint16_t y,
                  uint8_t zDir, uint8_t yaw)
{
    uint8_t frame[COMM_FRAME_LEN];
    build_frame(frame, sysID, mode, x, y, zDir, yaw);

    switch (sysID)
    {
        case SYS_CTRL_TO_BASE:
            if (s_local_handler) {
                comm_frame_t f;
                decode_frame(frame, &f);
                s_local_handler(&f);
            }
            return 1;

        case SYS_CTRL_TO_ARM:
            if (!s_arm_uart) return 0;
            emit_raw(COMM_STREAM_ARM, frame);
            return 1;

        case SYS_ARM_TO_CTRL:
            if (!s_bridge_uart) return 0;
            emit_raw(COMM_STREAM_BRIDGE, frame);
            return 1;

        default:
            return 0;
    }
}

uint32_t COMM_LastRxTick(comm_stream_t s)
{
    if (s >= COMM_STREAM_COUNT) return 0;
    return s_parser[s].last_rx_tick;
}

uint16_t COMM_FrameErrorCount(void)
{
    return s_frame_errors;
}
