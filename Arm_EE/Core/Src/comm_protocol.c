/*
 * comm_protocol.c  —  SRR-CP v1.0 implementation
 * See comm_protocol.h for the wire format.
 */

#include "comm_protocol.h"
#include <string.h>

/* =========================================================================
 * CRC8 — Dallas/Maxim 1-Wire, poly 0x31, init 0x00, no reflection, no XOR-out
 * ========================================================================= */
static const uint8_t crc8_table[256] = {
    0x00,0x07,0x0E,0x09,0x1C,0x1B,0x12,0x15,0x38,0x3F,0x36,0x31,0x24,0x23,0x2A,0x2D,
    0x70,0x77,0x7E,0x79,0x6C,0x6B,0x62,0x65,0x48,0x4F,0x46,0x41,0x54,0x53,0x5A,0x5D,
    0xE0,0xE7,0xEE,0xE9,0xFC,0xFB,0xF2,0xF5,0xD8,0xDF,0xD6,0xD1,0xC4,0xC3,0xCA,0xCD,
    0x90,0x97,0x9E,0x99,0x8C,0x8B,0x82,0x85,0xA8,0xAF,0xA6,0xA1,0xB4,0xB3,0xBA,0xBD,
    0xC7,0xC0,0xC9,0xCE,0xDB,0xDC,0xD5,0xD2,0xFF,0xF8,0xF1,0xF6,0xE3,0xE4,0xED,0xEA,
    0xB7,0xB0,0xB9,0xBE,0xAB,0xAC,0xA5,0xA2,0x8F,0x88,0x81,0x86,0x93,0x94,0x9D,0x9A,
    0x27,0x20,0x29,0x2E,0x3B,0x3C,0x35,0x32,0x1F,0x18,0x11,0x16,0x03,0x04,0x0D,0x0A,
    0x57,0x50,0x59,0x5E,0x4B,0x4C,0x45,0x42,0x6F,0x68,0x61,0x66,0x73,0x74,0x7D,0x7A,
    0x89,0x8E,0x87,0x80,0x95,0x92,0x9B,0x9C,0xB1,0xB6,0xBF,0xB8,0xAD,0xAA,0xA3,0xA4,
    0xF9,0xFE,0xF7,0xF0,0xE5,0xE2,0xEB,0xEC,0xC1,0xC6,0xCF,0xC8,0xDD,0xDA,0xD3,0xD4,
    0x69,0x6E,0x67,0x60,0x75,0x72,0x7B,0x7C,0x51,0x56,0x5F,0x58,0x4D,0x4A,0x43,0x44,
    0x19,0x1E,0x17,0x10,0x05,0x02,0x0B,0x0C,0x21,0x26,0x2F,0x28,0x3D,0x3A,0x33,0x34,
    0x4E,0x49,0x40,0x47,0x52,0x55,0x5C,0x5B,0x76,0x71,0x78,0x7F,0x6A,0x6D,0x64,0x63,
    0x3E,0x39,0x30,0x37,0x22,0x25,0x2C,0x2B,0x06,0x01,0x08,0x0F,0x1A,0x1D,0x14,0x13,
    0xAE,0xA9,0xA0,0xA7,0xB2,0xB5,0xBC,0xBB,0x96,0x91,0x98,0x9F,0x8A,0x8D,0x84,0x83,
    0xDE,0xD9,0xD0,0xD7,0xC2,0xC5,0xCC,0xCB,0xE6,0xE1,0xE8,0xEF,0xFA,0xFD,0xF4,0xF3
};

uint8_t crc8(const uint8_t *buf, size_t n)
{
    uint8_t c = 0x00;
    for (size_t i = 0; i < n; i++) {
        c = crc8_table[c ^ buf[i]];
    }
    return c;
}

/* =========================================================================
 * Parser state machine — one instance per stream
 * ========================================================================= */
typedef enum {
    PS_IDLE = 0,
    PS_DST,
    PS_SRC,
    PS_CMD,
    PS_LEN,
    PS_PAYLOAD,
    PS_CRC,
    PS_ETX
} parser_state_t;

typedef struct {
    parser_state_t state;
    uint8_t  dst;
    uint8_t  src;
    uint8_t  cmd;
    uint8_t  len;
    uint8_t  idx;
    uint8_t  payload[COMM_PAYLOAD_MAX];
    uint32_t last_rx_tick;
} parser_t;

/* =========================================================================
 * Module state
 * ========================================================================= */
#define HANDLER_SLOTS   16u   /* one slot per high-nibble category */

static UART_HandleTypeDef *s_pi_uart    = NULL;
static UART_HandleTypeDef *s_slave_uart = NULL;

static parser_t        s_parser[COMM_STREAM_COUNT];
static comm_handler_t  s_handlers[256] = {0};
static uint16_t        s_proto_errors  = 0;

/* =========================================================================
 * Helpers
 * ========================================================================= */
static void parser_reset(parser_t *p)
{
    p->state = PS_IDLE;
    p->idx   = 0;
}

static UART_HandleTypeDef *uart_for(comm_stream_t s)
{
    return (s == COMM_STREAM_PI) ? s_pi_uart : s_slave_uart;
}

static void emit_frame(comm_stream_t s,
                       uint8_t dst, uint8_t src, uint8_t cmd,
                       const uint8_t *payload, uint8_t len)
{
    UART_HandleTypeDef *u = uart_for(s);
    if (u == NULL || len > COMM_PAYLOAD_MAX) return;

    uint8_t frame[COMM_FRAME_MAX];
    uint8_t n = 0;

    frame[n++] = COMM_STX;
    frame[n++] = dst;
    frame[n++] = src;
    frame[n++] = cmd;
    frame[n++] = len;
    for (uint8_t i = 0; i < len; i++) frame[n++] = payload[i];
    frame[n++] = crc8(&frame[1], (size_t)(4u + len));
    frame[n++] = COMM_ETX;

    /* Blocking transmit with a short timeout — worst-case 24 B @ 115200 ≈ 2.1 ms.
     * Called from the main loop (not ISR), so blocking is acceptable. */
    HAL_UART_Transmit(u, frame, n, 10);
}

/* =========================================================================
 * Dispatch + routing
 * ========================================================================= */
static void dispatch_local(uint8_t src, uint8_t cmd,
                           const uint8_t *payload, uint8_t len)
{
    comm_handler_t h = s_handlers[cmd];
    if (h) {
        h(src, payload, len);
    } else {
        s_proto_errors++;   /* PROTO_UNKNOWN_CMD */
    }
}

static void route_frame(comm_stream_t src_stream, const parser_t *p)
{
    /* Frame has been fully validated (CRC + ETX + LEN). */
    const uint8_t dst = p->dst;

    if (src_stream == COMM_STREAM_PI)
    {
        if (dst == ADDR_BASE || dst == ADDR_BROADCAST) {
            dispatch_local(p->src, p->cmd, p->payload, p->len);
        }
        if (dst == ADDR_ARM_EE || dst == ADDR_BROADCAST) {
            /* Forward to slave; rewrite SRC to Base so slave knows who asked. */
            emit_frame(COMM_STREAM_SLAVE, dst, ADDR_BASE,
                       p->cmd, p->payload, p->len);
        }
        if (dst != ADDR_BASE && dst != ADDR_ARM_EE &&
            dst != ADDR_BROADCAST) {
            s_proto_errors++;   /* PROTO_UNKNOWN_DST */
        }
    }
    else /* COMM_STREAM_SLAVE */
    {
        if (dst == ADDR_BASE || dst == ADDR_BROADCAST) {
            dispatch_local(p->src, p->cmd, p->payload, p->len);
        }
        if (dst == ADDR_PI || dst == ADDR_BROADCAST) {
            /* Upstream — keep SRC intact so Pi can see which board spoke. */
            emit_frame(COMM_STREAM_PI, dst, p->src,
                       p->cmd, p->payload, p->len);
        }
        if (dst != ADDR_BASE && dst != ADDR_PI &&
            dst != ADDR_BROADCAST) {
            s_proto_errors++;
        }
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */
void COMM_Init(UART_HandleTypeDef *pi_uart,
               UART_HandleTypeDef *slave_uart)
{
    s_pi_uart    = pi_uart;
    s_slave_uart = slave_uart;
    memset(s_handlers, 0, sizeof(s_handlers));
    for (int i = 0; i < COMM_STREAM_COUNT; i++) {
        parser_reset(&s_parser[i]);
        s_parser[i].last_rx_tick = 0;
    }
    s_proto_errors = 0;
}

void COMM_RegisterHandler(uint8_t cmd, comm_handler_t fn)
{
    s_handlers[cmd] = fn;
}

void COMM_FeedByte(comm_stream_t s, uint8_t byte)
{
    if (s >= COMM_STREAM_COUNT) return;
    parser_t *p = &s_parser[s];

    switch (p->state)
    {
        case PS_IDLE:
            if (byte == COMM_STX) {
                p->state = PS_DST;
            } else {
                /* byte outside a frame — count once and wait for resync */
                s_proto_errors++;   /* PROTO_BAD_STX, rate-limit on display */
            }
            break;

        case PS_DST:
            p->dst = byte; p->state = PS_SRC; break;

        case PS_SRC:
            p->src = byte; p->state = PS_CMD; break;

        case PS_CMD:
            p->cmd = byte; p->state = PS_LEN; break;

        case PS_LEN:
            if (byte > COMM_PAYLOAD_MAX) {
                s_proto_errors++;   /* PROTO_LEN_OVERFLOW */
                parser_reset(p);
            } else {
                p->len = byte;
                p->idx = 0;
                p->state = (byte == 0) ? PS_CRC : PS_PAYLOAD;
            }
            break;

        case PS_PAYLOAD:
            p->payload[p->idx++] = byte;
            if (p->idx >= p->len) p->state = PS_CRC;
            break;

        case PS_CRC: {
            /* Recompute CRC over header + payload and compare. */
            uint8_t buf[4 + COMM_PAYLOAD_MAX];
            buf[0] = p->dst; buf[1] = p->src;
            buf[2] = p->cmd; buf[3] = p->len;
            for (uint8_t i = 0; i < p->len; i++) buf[4 + i] = p->payload[i];
            uint8_t expected = crc8(buf, (size_t)(4u + p->len));
            if (byte == expected) {
                p->state = PS_ETX;
            } else {
                s_proto_errors++;   /* PROTO_BAD_CRC */
                parser_reset(p);
            }
            break;
        }

        case PS_ETX:
            if (byte == COMM_ETX) {
                p->last_rx_tick = HAL_GetTick();
                route_frame(s, p);
            } else {
                s_proto_errors++;   /* PROTO_BAD_ETX */
            }
            parser_reset(p);
            break;

        default:
            parser_reset(p);
            break;
    }
}

uint8_t COMM_SendOnStream(comm_stream_t s,
                          uint8_t dst, uint8_t src,
                          uint8_t cmd,
                          const uint8_t *payload, uint8_t len)
{
    if (len > COMM_PAYLOAD_MAX) return 0;
    UART_HandleTypeDef *u = uart_for(s);
    if (u == NULL) return 0;
    emit_frame(s, dst, src, cmd, payload, len);
    return 1;
}

uint8_t COMM_Send(uint8_t dst, uint8_t cmd,
                  const uint8_t *payload, uint8_t len)
{
    if (len > COMM_PAYLOAD_MAX) return 0;

    if (dst == ADDR_BASE) {
        dispatch_local(ADDR_BASE, cmd, payload, len);
        return 1;
    }
    if (dst == ADDR_ARM_EE) {
        return COMM_SendOnStream(COMM_STREAM_SLAVE, dst, ADDR_BASE,
                                 cmd, payload, len);
    }
    if (dst == ADDR_PI || dst == ADDR_ESP32) {
        return COMM_SendOnStream(COMM_STREAM_PI, dst, ADDR_BASE,
                                 cmd, payload, len);
    }
    if (dst == ADDR_BROADCAST) {
        uint8_t ok = 1;
        ok &= COMM_SendOnStream(COMM_STREAM_SLAVE, dst, ADDR_BASE,
                                cmd, payload, len);
        ok &= COMM_SendOnStream(COMM_STREAM_PI, dst, ADDR_BASE,
                                cmd, payload, len);
        dispatch_local(ADDR_BASE, cmd, payload, len);
        return ok;
    }
    return 0;
}

uint32_t COMM_LastRxTick(comm_stream_t s)
{
    if (s >= COMM_STREAM_COUNT) return 0;
    return s_parser[s].last_rx_tick;
}

uint16_t COMM_ProtoErrorCount(void)
{
    return s_proto_errors;
}
