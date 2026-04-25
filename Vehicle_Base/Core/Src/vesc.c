/*
 * vesc.c  —  VESC Motor Driver (bldc_interface wrapper)
 * Board   : STM32 Nucleo-F767ZI  (Vehicle Base — merged firmware)
 *
 * This file wraps the bldc_interface / bldc_interface_uart library (Benjamin
 * Vedder, GPLv3) to provide the VESC_* API declared in vesc.h.
 *
 * Architecture
 * ────────────
 *   bldc_interface is a single-instance library: it holds one internal send
 *   function pointer.  To control two motors on different UARTs we swap that
 *   pointer before each command (left → UART5, right → UART7) using
 *   bldc_interface_uart_init().  Each call is very fast (just stores a pointer)
 *   so there is no meaningful overhead.
 *
 * Telemetry
 * ─────────
 *   Only the LEFT motor (UART5) returns telemetry to the STM32.  Bytes arrive
 *   byte-by-byte via the UART5 ISR; main.c calls VESC_ProcessRxByte() for each
 *   one.  bldc_interface_uart_process_byte() accumulates them, validates CRC,
 *   then fires the rx_value callback registered by VESC_SetValuesCallback().
 *
 * Thread safety
 * ─────────────
 *   VESC_ProcessRxByte() is called from ISR context.
 *   All other functions are called from the main loop — do not call motor
 *   control functions from an ISR.
 */

#include "vesc.h"
#include "bldc_interface.h"
#include "bldc_interface_uart.h"

/* =========================================================================
 * Private: UART send callbacks — one per motor
 * ========================================================================= */

/**
 * send_packet_left — transmit a bldc_interface packet to the LEFT VESC.
 * Connected to UART5 (PC12 TX).  Called internally by bldc_interface.
 *
 * Blocking TX on purpose: bldc_interface / packet.c share a single tx_buffer
 * in handler_states[0].  Non-blocking (IT/DMA) TX would let the next
 * set_duty_* call overwrite that buffer mid-transmission — and would also
 * return HAL_BUSY if the previous IT transfer is still draining, silently
 * dropping packets.  At 115200 baud an 11-byte SET_DUTY frame clocks out in
 * ~1 ms, which is fine for the ~1 kHz main loop.
 */
static void send_packet_left(unsigned char *data, unsigned int len)
{
    HAL_UART_Transmit(&huart5, data, len, 10 /* ms */);
}

/**
 * send_packet_right — transmit a bldc_interface packet to the RIGHT VESC.
 * Connected to UART7 (PE8 TX).  Called internally by bldc_interface.
 * See send_packet_left above for why this is blocking.
 */
static void send_packet_right(unsigned char *data, unsigned int len)
{
    HAL_UART_Transmit(&huart7, data, len, 10 /* ms */);
}

/* =========================================================================
 * Private: duty clamp helper
 * ========================================================================= */

/** clamp_duty — clamp a float to [-1.0, +1.0] before passing to bldc_interface */
static float clamp_duty(float v)
{
    if (v >  1.0f) return  1.0f;
    if (v < -1.0f) return -1.0f;
    return v;
}

/* =========================================================================
 * Private: set duty on a single motor
 *
 * We temporarily redirect bldc_interface's send function to the desired UART,
 * issue the command, then leave the pointer on the left VESC so that telemetry
 * requests (VESC_RequestValues) always go to the correct motor by default.
 * ========================================================================= */

/** set_duty_left — route command to left VESC via UART5 */
static void set_duty_left(float duty)
{
    bldc_interface_uart_init(send_packet_left);   /* point library at left VESC  */
    bldc_interface_set_duty_cycle(duty);          /* library serialises & sends  */
}

/** set_duty_right — route command to right VESC via UART7 */
static void set_duty_right(float duty)
{
    bldc_interface_uart_init(send_packet_right);  /* point library at right VESC */
    bldc_interface_set_duty_cycle(duty);
    bldc_interface_uart_init(send_packet_left);   /* restore default to left VESC */
}

/* =========================================================================
 * Public API implementation
 * ========================================================================= */

void VESC_Init(void)
{
    /* Initialise bldc_interface with the left-motor send callback.
     * This also sets up the internal packet handler (packet.c, handler 0). */
    bldc_interface_uart_init(send_packet_left);
}

void VESC_TankDrive(uint8_t direction, uint8_t speed)
{
    /* Map 0–255 → 0.0 – VESC_MAX_DUTY_FLOAT (e.g. 0 – 0.50) */
    float s = ((float)speed / 255.0f) * VESC_MAX_DUTY_FLOAT;

    float left  = 0.0f;
    float right = 0.0f;

    switch (direction)
    {
        case DIR_FORWARD:   left =  s;  right =  s;  break;
        case DIR_BACKWARD:  left = -s;  right = -s;  break;
        case DIR_LEFT:      left = -s;  right =  s;  break;  /* spin left  */
        case DIR_RIGHT:     left =  s;  right = -s;  break;  /* spin right */
        default:            left =  0;  right =  0;  break;  /* stop       */
    }

    set_duty_left(left);
    set_duty_right(right);
}

void VESC_JoystickDrive(float x, float y)
{
    /* Ian's tank-drive mixing:
     *   left  motor duty = throttle + steering
     *   right motor duty = throttle - steering
     *
     * Scale by VESC_MAX_DUTY_FLOAT so we never exceed the demo safety limit.
     * Clamp the result to [-1.0, +1.0] in case x+y or x-y goes out of range. */
    float left_duty  = clamp_duty(y + x) * VESC_MAX_DUTY_FLOAT;
    float right_duty = clamp_duty(y - x) * VESC_MAX_DUTY_FLOAT;

    set_duty_left(left_duty);
    set_duty_right(right_duty);
}

void VESC_Stop(void)
{
    /* Send zero duty to both motors to ensure a clean stop.
     * This is also called by the 500 ms watchdog in main.c. */
    set_duty_left(0.0f);
    set_duty_right(0.0f);
}

void VESC_SetValuesCallback(vesc_values_cb_t cb)
{
    /* Register caller's function as the bldc_interface telemetry handler.
     * It will be invoked from VESC_ProcessRxByte → bldc_interface_uart_process_byte
     * → bldc_interface_process_packet → rx_value_func each time a complete
     * COMM_GET_VALUES response is decoded. */
    bldc_interface_set_rx_value_func(cb);
}

void VESC_RequestValues(void)
{
    /* Send COMM_GET_VALUES request to the left VESC.
     * The response will be decoded in VESC_ProcessRxByte() as UART5 bytes arrive. */
    bldc_interface_uart_init(send_packet_left);  /* ensure we target left VESC */
    bldc_interface_get_values();
}

void VESC_ProcessRxByte(uint8_t byte)
{
    /* Called from HAL_UART_RxCpltCallback (UART5 RX interrupt).
     * Feeds one byte into the bldc_interface packet state machine.
     * When a complete, CRC-verified packet is assembled, the telemetry
     * callback registered via VESC_SetValuesCallback() is invoked. */
    bldc_interface_uart_process_byte(byte);
}
