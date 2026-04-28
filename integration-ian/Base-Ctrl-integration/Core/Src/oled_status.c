/*
 * oled_status.c  —  Receive-only Solar Bot status display, in-process variant.
 *
 * Lives on the same MCU as the Base controller. Owns three things:
 *   1. Per-stream snapshots (DRIVE / ARM-cmd / ARM-state) updated whenever
 *      the Base main loop hands us a validated 12-byte frame.
 *   2. A live throttle pair updated on every motor poll.
 *   3. A 2 Hz OLED refresh that paints those snapshots.
 *
 * No UART here. No parser. No marker check. The caller (Base's existing
 * receive code) has already validated the frame — we just record what it
 * carried.
 */

#include "oled_status.h"
#include "oled.h"
#include "stm32f7xx_hal.h"
#include <stdio.h>
#include <string.h>

/* ---- sysID values (mirror of comm_protocol.h) -------------------------- */
#define SYS_DRIVE       0x01u
#define SYS_ARM_CMD     0x02u

/* ---- Refresh cadence --------------------------------------------------- */
#define REFRESH_PERIOD_MS   500u
#define LINK_FRESH_MS       500u

/* ---- Per-stream snapshot ----------------------------------------------- */
typedef struct {
    uint32_t last_tick;   /* HAL_GetTick() of last frame; 0 = never */
    uint16_t count;       /* frames recorded so far                 */
    uint8_t  sys;
    uint8_t  mode;
    uint16_t x;
    uint16_t y;
    uint8_t  z;
    uint8_t  yaw;
} stream_snap_t;

static stream_snap_t s_drive;
static stream_snap_t s_armcmd;
static stream_snap_t s_armstate;

static uint16_t s_err_count    = 0;
static float    s_throttle_l   = 0.0f;
static float    s_throttle_r   = 0.0f;
static uint32_t s_last_refresh = 0;

/* ---- Helpers ----------------------------------------------------------- */
static uint32_t mark_tick(void)
{
    /* Reserve 0 to mean "never seen" — coerce real zero ticks to 1. */
    uint32_t t = HAL_GetTick();
    return (t == 0u) ? 1u : t;
}

static void fill_snap(stream_snap_t *s, const uint8_t b[12])
{
    s->last_tick = mark_tick();
    s->count++;
    s->sys  = b[2];
    s->mode = b[3];
    s->x    = ((uint16_t)b[4] << 8) | b[5];
    s->y    = ((uint16_t)b[6] << 8) | b[7];
    s->z    = b[8];
    s->yaw  = b[9];
}

/* ---- Public: lifecycle + setters --------------------------------------- */
void OLED_Status_Init(void)
{
    memset(&s_drive,    0, sizeof(s_drive));
    memset(&s_armcmd,   0, sizeof(s_armcmd));
    memset(&s_armstate, 0, sizeof(s_armstate));
    s_err_count    = 0;
    s_throttle_l   = 0.0f;
    s_throttle_r   = 0.0f;
    s_last_refresh = 0;

    OLED_Clear();
    OLED_SetCursor(2, 0);  OLED_Print("     SOLAR BOT       ");
    OLED_SetCursor(4, 0);  OLED_Print("    starting up...   ");
}

void OLED_Status_NoteCtrlFrame(const uint8_t buf12[12])
{
    if (buf12 == NULL) return;
    switch (buf12[2]) {
        case SYS_DRIVE:    fill_snap(&s_drive,  buf12); break;
        case SYS_ARM_CMD:  fill_snap(&s_armcmd, buf12); break;
        default:           /* unknown sysID — ignore */                 break;
    }
}

void OLED_Status_NoteArmFrame(const uint8_t buf12[12])
{
    if (buf12 == NULL) return;
    fill_snap(&s_armstate, buf12);
}

void OLED_Status_NoteCommError(void)
{
    s_err_count++;
}

void OLED_Status_NoteThrottle(float left, float right)
{
    s_throttle_l = left;
    s_throttle_r = right;
}

/* ---- Refresh ----------------------------------------------------------- */

/* Resolve "OK  " / "----" + age (ms, capped at 99999). */
static uint32_t age_ms(uint32_t last_tick, uint32_t now, const char **state_out)
{
    if (last_tick == 0u) {
        *state_out = "----";
        return 99999u;
    }
    uint32_t age = now - last_tick;
    if (age > 99999u) age = 99999u;
    *state_out = (age < LINK_FRESH_MS) ? "OK  " : "----";
    return age;
}

void OLED_Status_Refresh(void)
{
    uint32_t now = HAL_GetTick();
    if ((now - s_last_refresh) < REFRESH_PERIOD_MS) return;
    s_last_refresh = now;

    char buf[22];   /* 21 chars + null */
    const char *st;
    uint32_t age;

    /* Row 0 — title */
    OLED_SetCursor(0, 0);
    OLED_Print("     SOLAR BOT       ");

    /* Row 1 — separator */
    OLED_SetCursor(1, 0);
    OLED_Print("---------------------");

    /* Row 2 — DRIVE link */
    age = age_ms(s_drive.last_tick, now, &st);
    OLED_SetCursor(2, 0);
    snprintf(buf, sizeof(buf), "DRIVE %s %5lums  ", st, (unsigned long)age);
    OLED_Print(buf);

    /* Row 3 — ARM-cmd link */
    age = age_ms(s_armcmd.last_tick, now, &st);
    OLED_SetCursor(3, 0);
    snprintf(buf, sizeof(buf), "ARMcm %s %5lums  ", st, (unsigned long)age);
    OLED_Print(buf);

    /* Row 4 — ARM-state link */
    age = age_ms(s_armstate.last_tick, now, &st);
    OLED_SetCursor(4, 0);
    snprintf(buf, sizeof(buf), "ARMst %s %5lums  ", st, (unsigned long)age);
    OLED_Print(buf);

    /* Row 5 — last drive joystick */
    OLED_SetCursor(5, 0);
    snprintf(buf, sizeof(buf), "x:%5u y:%5u    ", s_drive.x, s_drive.y);
    OLED_Print(buf);

    /* Row 6 — live throttle (signed hundredths, e.g. +050 = +0.50 duty) */
    int t1 = (int)(s_throttle_l * 100.0f);
    int t2 = (int)(s_throttle_r * 100.0f);
    if (t1 >  100) t1 =  100;
    if (t1 < -100) t1 = -100;
    if (t2 >  100) t2 =  100;
    if (t2 < -100) t2 = -100;
    OLED_SetCursor(6, 0);
    snprintf(buf, sizeof(buf), "Drv L:%+4d R:%+4d  ", t1, t2);
    OLED_Print(buf);

    /* Row 7 — frame totals + comm error count */
    uint32_t total = (uint32_t)s_drive.count + s_armcmd.count + s_armstate.count;
    if (total > 99999u) total = 99999u;
    OLED_SetCursor(7, 0);
    snprintf(buf, sizeof(buf), "rx:%5lu err:%5u ",
             (unsigned long)total, s_err_count);
    OLED_Print(buf);
}
