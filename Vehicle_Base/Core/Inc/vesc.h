/*
 * vesc.h  —  VESC Motor Driver (FlipSky FESC 6.7)
 * Board : STM32 Nucleo-F767ZI  (Vehicle Base)
 *
 * Sends duty-cycle commands over UART using the VESC binary protocol.
 * Left motor  → direct UART frame (COMM_SET_DUTY)
 * Right motor → CAN-forwarded frame (COMM_FORWARD_CAN) sent on same UART
 *
 * Frame layout:
 *   [0x02] [payload_len] [payload...] [CRC16_hi] [CRC16_lo] [0x03]
 */

#ifndef VESC_H
#define VESC_H

#include "stm32f7xx_hal.h"
#include <stdint.h>

/* ---- UART handle wired to VESC ----------------------------------------- */
/* This extern references the handle CubeMX generates in main.c             */
extern UART_HandleTypeDef huart2;           /* PD5(TX) / PD6(RX)           */
#define VESC_UART   huart2

/* ---- VESC protocol command codes --------------------------------------- */
#define VESC_CMD_SET_DUTY    0x05           /* Set duty cycle directly      */
#define VESC_CMD_FWD_CAN     0x21           /* Forward command via CAN bus  */

/* ---- Duty cycle limits (unit = 1/100 000 of full scale) ---------------- */
/* 50 000 = 50 % — safe upper limit while testing on a bench               */
#define VESC_MAX_DUTY        50000
#define VESC_MIN_DUTY       -50000

/* ---- CAN device ID of the right-side VESC ----------------------------- */
#define VESC_CAN_ID_RIGHT    1

/* ---- Direction constants (match tokens parsed from ESP8266 commands) --- */
#define DIR_STOP     0
#define DIR_FORWARD  1
#define DIR_BACKWARD 2
#define DIR_LEFT     3                      /* Pivot left  (spin in place)  */
#define DIR_RIGHT    4                      /* Pivot right (spin in place)  */

/* ---- Public API -------------------------------------------------------- */
void VESC_SetDuty      (int32_t duty);             /* Left  motor, direct  */
void VESC_SetDutyRight (int32_t duty);             /* Right motor, via CAN */
void VESC_TankDrive    (uint8_t direction, uint8_t speed); /* Both motors  */
void VESC_Stop         (void);                     /* Zero both motors     */

#endif /* VESC_H */
