/*
 * vesc.c  —  VESC Motor Driver Implementation
 * Board : STM32 Nucleo-F767ZI  (Vehicle Base)
 *
 * VESC binary protocol summary:
 *   Frame = [0x02][len][payload...][CRC16_hi][CRC16_lo][0x03]
 *   CRC   = CRC16/XMODEM over payload bytes only (poly 0x1021, init 0x0000)
 *
 * COMM_SET_DUTY (0x05):
 *   payload = [0x05][duty_b3][duty_b2][duty_b1][duty_b0]   (big-endian int32)
 *   duty range : -100 000 … +100 000  (= -100 % … +100 %)
 *
 * COMM_FORWARD_CAN (0x21):
 *   payload = [0x21][CAN_ID][inner_CMD][duty_b3..b0]
 *
 * COMM_GET_VALUES (0x04):
 *   Request  payload = [0x04]
 *   Response payload = [0x04][fields...] — see decode_get_values() for layout
 */

#include "vesc.h"

/* =========================================================================
 * CRC16/XMODEM  (poly 0x1021, init 0x0000)
 * Run over payload bytes only (not the framing bytes).
 * ========================================================================= */
static uint16_t crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

/* =========================================================================
 * Frame builder / sender
 * ========================================================================= */
static void sendFrame(const uint8_t *payload, uint8_t payloadLen)
{
    uint8_t frame[16];
    uint8_t idx = 0;
    uint16_t crc = crc16(payload, payloadLen);

    frame[idx++] = 0x02;
    frame[idx++] = payloadLen;
    for (uint8_t i = 0; i < payloadLen; i++) {
        frame[idx++] = payload[i];
    }
    frame[idx++] = (uint8_t)(crc >> 8);
    frame[idx++] = (uint8_t)(crc & 0xFF);
    frame[idx++] = 0x03;

    HAL_UART_Transmit(&VESC_UART, frame, idx, 10);
}

/* =========================================================================
 * Motor control — TX only
 * ========================================================================= */

void VESC_SetDuty(int32_t duty)
{
    if (duty >  VESC_MAX_DUTY) duty =  VESC_MAX_DUTY;
    if (duty <  VESC_MIN_DUTY) duty =  VESC_MIN_DUTY;

    uint8_t payload[5];
    payload[0] = VESC_CMD_SET_DUTY;
    payload[1] = (uint8_t)((duty >> 24) & 0xFF);
    payload[2] = (uint8_t)((duty >> 16) & 0xFF);
    payload[3] = (uint8_t)((duty >>  8) & 0xFF);
    payload[4] = (uint8_t)( duty        & 0xFF);

    sendFrame(payload, 5);
}

void VESC_SetDutyRight(int32_t duty)
{
    if (duty >  VESC_MAX_DUTY) duty =  VESC_MAX_DUTY;
    if (duty <  VESC_MIN_DUTY) duty =  VESC_MIN_DUTY;

    uint8_t payload[7];
    payload[0] = VESC_CMD_FWD_CAN;
    payload[1] = VESC_CAN_ID_RIGHT;
    payload[2] = VESC_CMD_SET_DUTY;
    payload[3] = (uint8_t)((duty >> 24) & 0xFF);
    payload[4] = (uint8_t)((duty >> 16) & 0xFF);
    payload[5] = (uint8_t)((duty >>  8) & 0xFF);
    payload[6] = (uint8_t)( duty        & 0xFF);

    sendFrame(payload, 7);
}

void VESC_TankDrive(uint8_t direction, uint8_t speed)
{
    int32_t duty = ((int32_t)speed * VESC_MAX_DUTY) / 255;

    int32_t left  = 0;
    int32_t right = 0;

    switch (direction) {
        case DIR_FORWARD:   left =  duty; right =  duty; break;
        case DIR_BACKWARD:  left = -duty; right = -duty; break;
        case DIR_LEFT:      left = -duty; right =  duty; break;
        case DIR_RIGHT:     left =  duty; right = -duty; break;
        default:            left =  0;    right =  0;    break;
    }

    VESC_SetDuty(left);
    VESC_SetDutyRight(right);
}

void VESC_Stop(void)
{
    VESC_SetDuty(0);
    VESC_SetDutyRight(0);
}

/* =========================================================================
 * Telemetry — RX parser
 *
 * COMM_GET_VALUES response payload byte layout (offset from payload[0]):
 *
 *   [0]      command byte = 0x04
 *   [1..2]   temp_mos         float16  scale 1e1   → divide by 10.0
 *   [3..4]   temp_motor       float16  scale 1e1
 *   [5..8]   current_motor    float32  scale 1e2   → divide by 100.0
 *   [9..12]  current_in       float32  scale 1e2
 *   [13..16] id               float32  scale 1e2
 *   [17..20] iq               float32  scale 1e2
 *   [21..22] duty_now         float16  scale 1e3   → divide by 1000.0
 *   [23..26] rpm              float32  scale 1e0   → cast directly
 *   [27..28] v_in             float16  scale 1e1
 *   [29..32] amp_hours        float32  scale 1e4   → divide by 10000.0
 *   [33..36] amp_hours_chgd   float32  scale 1e4
 *   [37..40] watt_hours       float32  scale 1e4
 *   [41..44] watt_hours_chgd  float32  scale 1e4
 *   [45..48] tachometer       int32    big-endian
 *   [49..52] tachometer_abs   int32    big-endian
 *   [53]     fault_code       uint8
 *   [54..57] position         float32  scale 1e6
 *   [58]     vesc_id          uint8
 *   [59..60] temp_mos_1       float16  scale 1e1   (not stored — padding)
 *   [61..62] temp_mos_2       float16  scale 1e1   (not stored — padding)
 *   [63..64] temp_mos_3       float16  scale 1e1   (not stored — padding)
 *   [65..68] vd               float32  scale 1e3
 *   [69..72] vq               float32  scale 1e3
 *   [73]     status byte      (timeout / killswitch flags)
 *
 * Total payload length = 74 bytes (including leading 0x04 command byte).
 * ========================================================================= */

#define VESC_RX_BUF_SIZE   96      /* Generous margin over max frame size    */
#define GET_VALUES_PL_LEN  74      /* Expected payload length for 0x04 reply */

static uint8_t          rxBuf[VESC_RX_BUF_SIZE];
static uint16_t         rxIdx    = 0;
static uint8_t          rxActive = 0;   /* 1 = inside a frame               */
static uint16_t         rxExpLen = 0;   /* Expected total frame byte count   */

static vesc_values_cb_t valuesCallback = NULL;

/* ---- Register your callback -------------------------------------------- */
void VESC_SetValuesCallback(vesc_values_cb_t cb)
{
    valuesCallback = cb;
}

/* ---- Send a COMM_GET_VALUES request ------------------------------------ */
void VESC_RequestValues(void)
{
    uint8_t payload[1] = { VESC_CMD_GET_VALUES };
    sendFrame(payload, 1);
}

/* ---- Big-endian read helpers ------------------------------------------ */
static inline int16_t read_i16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline int32_t read_i32(const uint8_t *p)
{
    return (int32_t)(((uint32_t)p[0] << 24) |
                     ((uint32_t)p[1] << 16) |
                     ((uint32_t)p[2] <<  8) |
                      (uint32_t)p[3]);
}

/* ---- Decode a verified payload into mc_values and fire callback -------- */
static void decode_get_values(const uint8_t *pl, uint16_t len)
{
    /* Minimum length check: must have at least up to tachometer_abs        */
    if (len < 53) return;

    mc_values v;

    v.temp_mos          = (float)read_i16(pl + 1)  / 10.0f;
    v.temp_motor        = (float)read_i16(pl + 3)  / 10.0f;
    v.current_motor     = (float)read_i32(pl + 5)  / 100.0f;
    v.current_in        = (float)read_i32(pl + 9)  / 100.0f;
    v.id                = (float)read_i32(pl + 13) / 100.0f;
    v.iq                = (float)read_i32(pl + 17) / 100.0f;
    v.duty_now          = (float)read_i16(pl + 21) / 1000.0f;
    v.rpm               = (float)read_i32(pl + 23);
    v.v_in              = (float)read_i16(pl + 27) / 10.0f;
    v.amp_hours         = (float)read_i32(pl + 29) / 10000.0f;
    v.amp_hours_charged = (float)read_i32(pl + 33) / 10000.0f;
    v.watt_hours        = (float)read_i32(pl + 37) / 10000.0f;
    v.watt_hours_charged= (float)read_i32(pl + 41) / 10000.0f;
    v.tachometer        =        read_i32(pl + 45);
    v.tachometer_abs    =        read_i32(pl + 49);
    v.fault_code        = (mc_fault_code)pl[53];

    /* Optional extended fields — only read if payload is long enough       */
    v.position  = (len >= 58) ? (float)read_i32(pl + 54) / 1000000.0f : 0.0f;
    v.vesc_id   = (len >= 59) ? pl[58] : 0;
    v.vd        = (len >= 69) ? (float)read_i32(pl + 65) / 1000.0f : 0.0f;
    v.vq        = (len >= 73) ? (float)read_i32(pl + 69) / 1000.0f : 0.0f;

    if (valuesCallback != NULL) {
        valuesCallback(&v);
    }
}

/* =========================================================================
 * VESC_ProcessRxByte
 *
 * Call this from your UART RX interrupt for every byte received on VESC_UART.
 * It accumulates bytes, verifies the frame, and fires the callback.
 *
 * Usage in HAL_UART_RxCpltCallback:
 *
 *   if (huart->Instance == VESC_UART.Instance) {
 *       VESC_ProcessRxByte(vescRxByte);
 *       HAL_UART_Receive_IT(&huart2, &vescRxByte, 1);
 *   }
 * ========================================================================= */
void VESC_ProcessRxByte(uint8_t byte)
{
    if (!rxActive) {
        /* Wait for start byte */
        if (byte == 0x02) {
            rxBuf[0] = byte;
            rxIdx    = 1;
            rxActive = 1;
            rxExpLen = 0;
        }
        return;
    }

    if (rxIdx >= VESC_RX_BUF_SIZE) {
        /* Buffer overrun — reset and wait for next frame */
        rxActive = 0;
        rxIdx    = 0;
        return;
    }

    rxBuf[rxIdx++] = byte;

    /* Second byte is the payload length */
    if (rxIdx == 2) {
        /* Total frame size = 1 (start) + 1 (len) + payloadLen + 2 (CRC) + 1 (end) */
        rxExpLen = 1u + 1u + (uint16_t)byte + 2u + 1u;

        if (rxExpLen > VESC_RX_BUF_SIZE) {
            /* Frame too large for our buffer — skip it */
            rxActive = 0;
            rxIdx    = 0;
        }
        return;
    }

    /* Not enough bytes yet */
    if (rxIdx < rxExpLen) return;

    /* ---- Full frame received — validate -------------------------------- */
    uint8_t  payloadLen = rxBuf[1];
    uint8_t *payload    = &rxBuf[2];
    uint8_t  endByte    = rxBuf[rxIdx - 1];
    uint16_t rxCrc      = ((uint16_t)rxBuf[rxIdx - 3] << 8) | rxBuf[rxIdx - 2];
    uint16_t calcCrc    = crc16(payload, payloadLen);

    rxActive = 0;
    rxIdx    = 0;

    if (endByte != 0x03)      return;   /* Bad end byte  */
    if (rxCrc   != calcCrc)   return;   /* CRC mismatch  */

    /* Only handle GET_VALUES responses */
    if (payload[0] == VESC_CMD_GET_VALUES) {
        decode_get_values(payload, payloadLen);
    }
}
