/*
 * vesc.h  —  VESC Motor Driver Abstraction Layer
 * Board   : STM32 Nucleo-F767ZI  (Vehicle Base — merged firmware)
 *
 * This header defines the motor control API used throughout the project.
 * Internally, the implementation (vesc.c) uses the VESC bldc_interface library
 * (Benjamin Vedder, GPLv3) rather than the hand-rolled binary protocol used in
 * the original Vehicle_Base firmware.
 *
 * Physical wiring:
 *   Left  VESC  ← UART5  (PC12 TX)  direct connection
 *   Right VESC  ← UART7  (PE8  TX)  direct connection
 *
 * Telemetry is requested from the LEFT VESC only (UART5 RX) and is surfaced
 * to callers via a callback registered with VESC_SetValuesCallback().
 *
 * Motor data types (mc_values, mc_fault_code) come from datatypes.h which is
 * part of the bldc_interface library.  Do NOT re-define them here.
 */

#ifndef VESC_H
#define VESC_H

#include "stm32f7xx_hal.h"
#include "datatypes.h"   /* mc_values, mc_fault_code — from bldc_interface lib */
#include <stdint.h>

/* =========================================================================
 * UART handles used by this driver (defined in main.c)
 * ========================================================================= */
extern UART_HandleTypeDef huart5;   /* Left  VESC — TX commands + RX telemetry */
extern UART_HandleTypeDef huart7;   /* Right VESC — TX commands only           */

/* =========================================================================
 * Demo-safety duty cycle limit
 *
 * bldc_interface_set_duty_cycle() expects a value in [-1.0, +1.0].
 * We cap at ±0.50 (50 % throttle) during the demo to prevent runaway.
 * Ian's mixing math outputs values in [-1,+1] and they are multiplied by
 * this constant before being sent to the VESCs.
 * ========================================================================= */
#define VESC_MAX_DUTY_FLOAT   0.50f

/* =========================================================================
 * Tank-drive direction constants (used by keypad UI + VESC_TankDrive)
 * ========================================================================= */
#define DIR_STOP     0
#define DIR_FORWARD  1
#define DIR_BACKWARD 2
#define DIR_LEFT     3
#define DIR_RIGHT    4

/* =========================================================================
 * Callback type for received VESC telemetry
 *
 * Register a function of this type with VESC_SetValuesCallback().
 * It will be called from inside the UART5 RX interrupt each time a complete,
 * CRC-verified COMM_GET_VALUES response is decoded.
 *
 * Example:
 *   void on_vesc_values(mc_values *v) {
 *       float rpm = v->rpm;
 *   }
 *   VESC_SetValuesCallback(on_vesc_values);
 * ========================================================================= */
typedef void (*vesc_values_cb_t)(mc_values *val);

/* =========================================================================
 * Public API
 * ========================================================================= */

/**
 * VESC_Init — must be called once after all UARTs are initialised.
 * Sets up the bldc_interface library with the left-VESC send callback.
 */
void VESC_Init(void);

/**
 * VESC_TankDrive — drive both motors from a direction + speed byte.
 *
 * direction : DIR_FORWARD / DIR_BACKWARD / DIR_LEFT / DIR_RIGHT / DIR_STOP
 * speed     : 0-255 (linearly mapped to 0 – VESC_MAX_DUTY_FLOAT)
 *
 * Left/right differential steering:
 *   FWD  → left=+s, right=+s
 *   BCK  → left=-s, right=-s
 *   LFT  → left=-s, right=+s   (spin left in place)
 *   RGT  → left=+s, right=-s   (spin right in place)
 */
void VESC_TankDrive(uint8_t direction, uint8_t speed);

/**
 * VESC_JoystickDrive — drive from pre-normalised joystick floats (Ian's math).
 *
 * x : steering  ∈ [-1.0, +1.0]  (negative = left,  positive = right)
 * y : throttle  ∈ [-1.0, +1.0]  (negative = back,  positive = forward)
 *
 * Mixing:  left_duty  = clamp(y + x) * VESC_MAX_DUTY_FLOAT
 *          right_duty = clamp(y - x) * VESC_MAX_DUTY_FLOAT
 */
void VESC_JoystickDrive(float x, float y);

/**
 * VESC_Stop — send zero duty to both motors immediately.
 */
void VESC_Stop(void);

/**
 * VESC_SetValuesCallback — register telemetry callback.
 * The callback is invoked from UART5 RX ISR context when a
 * COMM_GET_VALUES response has been fully decoded and CRC-verified.
 */
void VESC_SetValuesCallback(vesc_values_cb_t cb);

/**
 * VESC_RequestValues — request telemetry from the left VESC.
 * Call at ~5 Hz (every 200 ms) in the main loop.
 */
void VESC_RequestValues(void);

/**
 * VESC_ProcessRxByte — feed one byte received on UART5 (left VESC) to the
 * packet decoder.  Call this from inside HAL_UART_RxCpltCallback when
 * huart->Instance == UART5.
 */
void VESC_ProcessRxByte(uint8_t byte);

#endif /* VESC_H */
