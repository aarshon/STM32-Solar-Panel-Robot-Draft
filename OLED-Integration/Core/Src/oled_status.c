/*
 * oled_status.c  —  Receive-only Solar Bot status display.
 *
 * Owns three things:
 *   1. A byte-at-a-time FSM that re-frames the 12-byte Solar Bot packets out
 *      of the raw UART stream.
 *   2. Per-stream snapshots (DRIVE / ARM-CMD / ARM-STATE) refreshed on every
 *      validated frame.
 *   3. A 2 Hz OLED refresh that paints those snapshots in a fixed layout.
 *
 * The display is the only output; nothing on this board ever writes to UART
 * or reads back from the OLED.
 */

#include "oled_status.h"
#include "oled.h"
#include "stm32f7xx_hal.h"
#include <stdio.h>
#include <string.h>

/* ---- Frame constants (mirror of comm_protocol.h) ----------------------- */
#define FRAME_LEN       12u
#define PAYLOAD_LEN      8u   /* sysID..yaw */
#define START_1       0x41u   /* 'A' */
#define START_2       0x5Au   /* 'Z' */
#define END_1         0x59u   /* 'Y' */
#define END_2         0x42u   /* 'B' */

#define SYS_DRIVE     0x01u   /* CTRL -> BASE  */
#define SYS_ARM_CMD   0x02u   /* CTRL -> ARM   */
#define SYS_ARM_STATE 0x03u   /* ARM  -> CTRL  */

/* ---- Refresh cadence --------------------------------------------------- */
#define REFRESH_PERIOD_MS   500u   /* 2 Hz */
#define LINK_FRESH_MS       500u   /* "OK" if a frame arrived within this window */

/* ---- Per-stream snapshot ----------------------------------------------- */
typedef struct {
    uint32_t last_tick;   /* HAL_GetTick() of last validated frame; 0 = none */
    uint16_t count;       /* validated frames received so far                 */
    uint8_t  mode;
    uint16_t x;
    uint16_t y;
    uint8_t  z;
    uint8_t  yaw;
} stream_snap_t;

static stream_snap_t s_drive;
static stream_snap_t s_armcmd;
static stream_snap_t s_armstate;

static uint16_t s_err_count = 0;     /* frames rejected by the FSM */
static uint32_t s_last_refresh = 0;

/* ---- Re-framer FSM ----------------------------------------------------- */
typedef enum {
    PS_W1 = 0,
    PS_W2,
    PS_COLLECT,
    PS_E1,
    PS_E2
} parser_state_t;

static parser_state_t s_state = PS_W1;
static uint8_t        s_idx = 0;
static uint8_t        s_buf[FRAME_LEN];

static void on_frame(const uint8_t *raw)
{
    uint8_t  sysID = raw[2];
    uint8_t  mode  = raw[3];
    uint16_t x     = ((uint16_t)raw[4] << 8) | raw[5];
    uint16_t y     = ((uint16_t)raw[6] << 8) | raw[7];
    uint8_t  z     = raw[8];
    uint8_t  yaw   = raw[9];
    uint32_t now   = HAL_GetTick();

    stream_snap_t *snap = NULL;
    switch (sysID) {
        case SYS_DRIVE:     snap = &s_drive;    break;
        case SYS_ARM_CMD:   snap = &s_armcmd;   break;
        case SYS_ARM_STATE: snap = &s_armstate; break;
        default: return;   /* unknown sysID — leave it for debugging via err counter */
    }

    snap->last_tick = (now == 0) ? 1 : now;   /* never store 0 — we use it as "never" */
    snap->mode      = mode;
    snap->x         = x;
    snap->y         = y;
    snap->z         = z;
    snap->yaw       = yaw;
    snap->count++;
}

void OLED_Status_FeedByte(uint8_t b)
{
    switch (s_state) {
        case PS_W1:
            if (b == START_1) { s_buf[0] = b; s_state = PS_W2; }
            break;

        case PS_W2:
            if (b == START_2) {
                s_buf[1] = b;
                s_idx = 0;
                s_state = PS_COLLECT;
            } else if (b == START_1) {
                /* keep waiting for START_2 — handles a stray 0x41 then real frame */
                s_buf[0] = b;
            } else {
                s_err_count++;
                s_state = PS_W1;
            }
            break;

        case PS_COLLECT:
            s_buf[2 + s_idx++] = b;
            if (s_idx >= PAYLOAD_LEN) s_state = PS_E1;
            break;

        case PS_E1:
            if (b == END_1) {
                s_buf[10] = b;
                s_state = PS_E2;
            } else {
                s_err_count++;
                s_state = PS_W1;
            }
            break;

        case PS_E2:
            if (b == END_2) {
                s_buf[11] = b;
                on_frame(s_buf);
            } else {
                s_err_count++;
            }
            s_state = PS_W1;
            break;

        default:
            s_state = PS_W1;
            break;
    }
}

/* ---- Init -------------------------------------------------------------- */
void OLED_Status_Init(void)
{
    memset(&s_drive,    0, sizeof(s_drive));
    memset(&s_armcmd,   0, sizeof(s_armcmd));
    memset(&s_armstate, 0, sizeof(s_armstate));
    s_err_count   = 0;
    s_last_refresh = 0;
    s_state = PS_W1;
    s_idx   = 0;

    OLED_Clear();
    OLED_SetCursor(2, 0);  OLED_Print("     SOLAR BOT       ");
    OLED_SetCursor(4, 0);  OLED_Print("    listening...     ");
}

/* ---- Refresh ----------------------------------------------------------- */

/* Format "OK   " / "----" plus an age in ms (capped at 99999). */
static void fmt_link(char *out, uint8_t out_len,
                     const char *label, uint32_t last_tick, uint32_t now)
{
    if (last_tick == 0) {
        snprintf(out, out_len, "%-5s ----  ----ms", label);
        return;
    }
    uint32_t age = now - last_tick;
    if (age > 99999u) age = 99999u;

    const char *state = (age < LINK_FRESH_MS) ? "OK  " : "----";
    snprintf(out, out_len, "%-5s %s %5lums", label, state, (unsigned long)age);
}

void OLED_Status_Refresh(void)
{
    uint32_t now = HAL_GetTick();
    if ((now - s_last_refresh) < REFRESH_PERIOD_MS) return;
    s_last_refresh = now;

    char buf[22];   /* 21 chars + null */

    /* Title */
    OLED_SetCursor(0, 0);
    OLED_Print("     SOLAR BOT       ");

    /* Separator */
    OLED_SetCursor(1, 0);
    OLED_Print("---------------------");

    /* DRIVE row + its joystick on the next row */
    OLED_SetCursor(2, 0);
    fmt_link(buf, sizeof(buf), "DRIVE", s_drive.last_tick, now);
    /* fmt_link writes up to 19 chars; pad to 21 */
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "  ");
    OLED_Print(buf);

    OLED_SetCursor(3, 0);
    snprintf(buf, sizeof(buf), " x:%5u y:%5u   ", s_drive.x, s_drive.y);
    OLED_Print(buf);

    /* ARM-CMD row + the latest mode/buttons it carried */
    OLED_SetCursor(4, 0);
    fmt_link(buf, sizeof(buf), "ARM",   s_armcmd.last_tick, now);
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "  ");
    OLED_Print(buf);

    OLED_SetCursor(5, 0);
    snprintf(buf, sizeof(buf), " md:%02X z:%u yaw:%u    ",
             s_armcmd.mode, s_armcmd.z, s_armcmd.yaw);
    OLED_Print(buf);

    /* ARM-STATE row + frame error count */
    OLED_SetCursor(6, 0);
    fmt_link(buf, sizeof(buf), "STATE", s_armstate.last_tick, now);
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "  ");
    OLED_Print(buf);

    OLED_SetCursor(7, 0);
    uint16_t total = (uint16_t)(s_drive.count + s_armcmd.count + s_armstate.count);
    snprintf(buf, sizeof(buf), "rx:%5u err:%5u ", total, s_err_count);
    OLED_Print(buf);
}
