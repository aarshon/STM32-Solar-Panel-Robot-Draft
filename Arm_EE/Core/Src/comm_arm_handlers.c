/*
 * comm_arm_handlers.c  —  SRR-CP command handlers for ADDR_ARM_EE
 *
 * All handlers are invoked from comm_protocol.c after a frame has passed
 * STX, length, and CRC validation.  They run in main-loop context (called
 * from Arm_Tick() in main.c, NOT from the UART RX ISR).
 *
 * Liveness rule: if no CMD_HEARTBEAT (or any frame) has arrived from
 * ADDR_BASE for BASE_TIMEOUT_MS, we assert a software e-stop locally and
 * report it back upstream once Base reappears.
 */

#include "comm_arm_handlers.h"
#include "arm_motion.h"

#include <string.h>

/* --- Tunables ---------------------------------------------------------- */
#define BASE_TIMEOUT_MS     1500u     /* if no Base traffic this long, halt */

/* --- Module state ------------------------------------------------------ */
static uint8_t  s_last_fault     = FAULT_NONE;
static uint8_t  s_estop_latched  = 0;
static uint8_t  s_estop_reason   = 0;
static uint32_t s_last_base_rx   = 0;

uint8_t Arm_LastFault(void) { return s_last_fault; }

/* --- Outbound helper -------------------------------------------------- */
/* COMM_Send in comm_protocol.c hardcodes the source as ADDR_BASE and
 * treats ADDR_BASE as "self" (dispatches locally). That's correct on the
 * Base node but wrong here. We always emit on the slave stream with
 * src=ADDR_ARM_EE, dst=whatever the caller asked for. */
static inline uint8_t arm_tx(uint8_t dst, uint8_t cmd,
                             const uint8_t *p, uint8_t len)
{
    return COMM_SendOnStream(COMM_STREAM_SLAVE, dst, ADDR_ARM_EE,
                             cmd, p, len);
}

/* =========================================================================
 * Per-command handlers
 * ========================================================================= */

static void handle_arm_target(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src;
    if (len < 6) { s_last_fault = FAULT_NONE; return; }

    int16_t x = (int16_t)((p[0] << 8) | p[1]);
    int16_t y = (int16_t)((p[2] << 8) | p[3]);
    int16_t z = (int16_t)((p[4] << 8) | p[5]);

    if (!ArmMotion_SetTargetXYZ_mm(x, y, z)) {
        /* unreachable — bubble up as a fault */
        s_last_fault = FAULT_NONE;   /* TODO: define FAULT_ARM_UNREACH */
        uint8_t reason = 0xA0;       /* placeholder reason byte */
        arm_tx(ADDR_BASE, CMD_FAULT_REPORT, &reason, 1);
    }
}

static void handle_arm_home(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src;
    uint8_t mask = (len >= 1) ? p[0] : 0x07;   /* default = all joints */
    ArmMotion_StartHoming(mask);
}

static void handle_arm_jog(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src;
    if (len < 3) return;
    uint8_t joint = p[0];
    int8_t  dir   = (int8_t)p[1];
    uint8_t speed = p[2];
    ArmMotion_Jog(joint, dir, speed);
}

static void handle_arm_zero(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src;
    uint8_t mask = (len >= 1) ? p[0] : 0x07;
    ArmMotion_ZeroSteps(mask);
}

static void handle_ee_torque(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src;
    if (len < 1) return;
    ArmMotion_EE_SetTorque((int8_t)p[0]);
}

static void handle_ee_pulse(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src;
    if (len < 2) return;
    uint16_t ms = ((uint16_t)p[0] << 8) | p[1];
    ArmMotion_EE_Pulse(ms);
}

static void handle_estop_assert(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src;
    s_estop_latched = 1;
    s_estop_reason  = (len >= 1) ? p[0] : FAULT_ESTOP_SOFTWARE;
    s_last_fault    = s_estop_reason;
    ArmMotion_HaltAll();
}

static void handle_estop_clear(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src; (void)p; (void)len;
    s_estop_latched = 0;
    s_estop_reason  = 0;
    s_last_fault    = FAULT_NONE;
    ArmMotion_ResumeAll();
}

static void handle_estop_query(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src; (void)p; (void)len;
    uint8_t pl[2] = {
        FIELD_ESTOP_STATE,
        s_estop_latched ? s_estop_reason : 0u
    };
    arm_tx(ADDR_BASE, CMD_STATUS_REPLY, pl, 2);
}

static void handle_status_req(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src; (void)p; (void)len;

    /* Pack a compact STATUS_REPLY: estop state + last fault.
     * Joint angles streamed separately via STATUS_STREAM if needed. */
    uint8_t pl[6];
    pl[0] = FIELD_ESTOP_STATE;
    pl[1] = s_estop_latched ? s_estop_reason : 0u;
    pl[2] = FIELD_FW_VERSION;
    pl[3] = 0x01;                /* major */
    pl[4] = 0x00;                /* minor */
    pl[5] = s_last_fault;
    arm_tx(ADDR_BASE, CMD_STATUS_REPLY, pl, sizeof(pl));
}

static void handle_heartbeat(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src; (void)p; (void)len;
    s_last_base_rx = HAL_GetTick();
}

static void handle_ping(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src;
    /* Echo payload back as PONG, capped at 16 B */
    uint8_t n = (len > COMM_PAYLOAD_MAX) ? COMM_PAYLOAD_MAX : len;
    arm_tx(ADDR_BASE, CMD_PONG, p, n);
}

static void handle_fault_clear(uint8_t src, const uint8_t *p, uint8_t len)
{
    (void)src; (void)p; (void)len;
    s_last_fault = FAULT_NONE;
}

/* =========================================================================
 * Registration
 * ========================================================================= */

void Arm_RegisterAllHandlers(void)
{
    COMM_RegisterHandler(CMD_ARM_TARGET,    handle_arm_target);
    COMM_RegisterHandler(CMD_ARM_HOME,      handle_arm_home);
    COMM_RegisterHandler(CMD_ARM_JOG,       handle_arm_jog);
    COMM_RegisterHandler(CMD_ARM_ZERO,      handle_arm_zero);

    COMM_RegisterHandler(CMD_EE_TORQUE,     handle_ee_torque);
    COMM_RegisterHandler(CMD_EE_PULSE,      handle_ee_pulse);

    COMM_RegisterHandler(CMD_ESTOP_ASSERT,  handle_estop_assert);
    COMM_RegisterHandler(CMD_ESTOP_CLEAR,   handle_estop_clear);
    COMM_RegisterHandler(CMD_ESTOP_QUERY,   handle_estop_query);

    COMM_RegisterHandler(CMD_STATUS_REQ,    handle_status_req);
    COMM_RegisterHandler(CMD_HEARTBEAT,     handle_heartbeat);
    COMM_RegisterHandler(CMD_PING,          handle_ping);
    COMM_RegisterHandler(CMD_FAULT_CLEAR,   handle_fault_clear);

    s_last_base_rx = HAL_GetTick();   /* avoid an immediate liveness trip */
}

/* =========================================================================
 * Periodic helpers
 * ========================================================================= */

void Arm_SendHeartbeat(uint32_t uptime_ms)
{
    uint32_t s = uptime_ms / 1000u;
    uint8_t pl[2] = { (uint8_t)((s >> 8) & 0xFFu),
                      (uint8_t)(s & 0xFFu) };
    arm_tx(ADDR_BASE, CMD_HEARTBEAT, pl, 2);
}

void Arm_CheckBaseLiveness(uint32_t now_ms)
{
    if (s_estop_latched) return;   /* already halted */

    if ((now_ms - s_last_base_rx) > BASE_TIMEOUT_MS)
    {
        s_estop_latched = 1;
        s_estop_reason  = FAULT_ESTOP_WATCHDOG;
        s_last_fault    = FAULT_ESTOP_WATCHDOG;
        ArmMotion_HaltAll();
        /* Don't spam fault reports — Base will see us silent and infer */
    }
}
