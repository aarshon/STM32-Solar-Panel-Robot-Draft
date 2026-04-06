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
 *   Routes the duty command to the slave VESC (right motor) over CAN.
 */

#include "vesc.h"

/* ---- CRC16/XMODEM --------------------------------------------------------
 * Poly 0x1021, init value 0x0000.
 * Run over the payload bytes only (not the framing bytes).
 */
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

/* ---- Build a VESC frame and send it over UART -------------------------- */
static void sendFrame(const uint8_t *payload, uint8_t payloadLen)
{
    uint8_t frame[16];
    uint8_t idx = 0;
    uint16_t crc = crc16(payload, payloadLen);

    frame[idx++] = 0x02;                    /* Start byte               */
    frame[idx++] = payloadLen;              /* Payload length           */

    for (uint8_t i = 0; i < payloadLen; i++) {
        frame[idx++] = payload[i];
    }

    frame[idx++] = (uint8_t)(crc >> 8);    /* CRC high byte            */
    frame[idx++] = (uint8_t)(crc & 0xFF);  /* CRC low byte             */
    frame[idx++] = 0x03;                    /* End byte                 */

    /* Transmit — 10 ms timeout is plenty for a 9-byte frame at 115200  */
    HAL_UART_Transmit(&VESC_UART, frame, idx, 10);
}

/* ---- VESC_SetDuty : send duty to the master VESC (left motor) ---------- */
void VESC_SetDuty(int32_t duty)
{
    /* Clamp to safe range */
    if (duty >  VESC_MAX_DUTY) duty =  VESC_MAX_DUTY;
    if (duty <  VESC_MIN_DUTY) duty =  VESC_MIN_DUTY;

    uint8_t payload[5];
    payload[0] = VESC_CMD_SET_DUTY;
    payload[1] = (uint8_t)((duty >> 24) & 0xFF);   /* MSB first */
    payload[2] = (uint8_t)((duty >> 16) & 0xFF);
    payload[3] = (uint8_t)((duty >>  8) & 0xFF);
    payload[4] = (uint8_t)( duty        & 0xFF);   /* LSB last  */

    sendFrame(payload, 5);
}

/* ---- VESC_SetDutyRight : CAN-forward duty to slave VESC (right motor) -- */
void VESC_SetDutyRight(int32_t duty)
{
    if (duty >  VESC_MAX_DUTY) duty =  VESC_MAX_DUTY;
    if (duty <  VESC_MIN_DUTY) duty =  VESC_MIN_DUTY;

    /* COMM_FORWARD_CAN wraps a SET_DUTY payload with a CAN device ID     */
    uint8_t payload[7];
    payload[0] = VESC_CMD_FWD_CAN;
    payload[1] = VESC_CAN_ID_RIGHT;             /* Slave VESC CAN ID     */
    payload[2] = VESC_CMD_SET_DUTY;
    payload[3] = (uint8_t)((duty >> 24) & 0xFF);
    payload[4] = (uint8_t)((duty >> 16) & 0xFF);
    payload[5] = (uint8_t)((duty >>  8) & 0xFF);
    payload[6] = (uint8_t)( duty        & 0xFF);

    sendFrame(payload, 7);
}

/* ---- VESC_TankDrive : map a direction + speed to both motor duties ----- */
void VESC_TankDrive(uint8_t direction, uint8_t speed)
{
    /* Scale speed (0-255) up to duty range (0 to VESC_MAX_DUTY)          */
    int32_t duty = ((int32_t)speed * VESC_MAX_DUTY) / 255;

    int32_t left  = 0;
    int32_t right = 0;

    switch (direction) {
        case DIR_FORWARD:
            left  =  duty;
            right =  duty;
            break;

        case DIR_BACKWARD:
            left  = -duty;
            right = -duty;
            break;

        case DIR_LEFT:
            /* Pivot left: left motor reverses, right motor drives forward */
            left  = -duty;
            right =  duty;
            break;

        case DIR_RIGHT:
            /* Pivot right: left motor forward, right motor reverses       */
            left  =  duty;
            right = -duty;
            break;

        default:        /* DIR_STOP or unknown */
            left  = 0;
            right = 0;
            break;
    }

    VESC_SetDuty(left);         /* Master VESC  — left  motor (direct UART) */
    VESC_SetDutyRight(right);   /* Slave  VESC  — right motor (via CAN)     */
}

/* ---- VESC_Stop : zero both motors immediately -------------------------- */
void VESC_Stop(void)
{
    VESC_SetDuty(0);
    VESC_SetDutyRight(0);
}
