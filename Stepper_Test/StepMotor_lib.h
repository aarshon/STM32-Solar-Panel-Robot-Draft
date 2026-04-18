/*
 * StepMotor_lib.h
 *
 * Source: https://github.com/ilbeylia/StepperMotorLib
 * Patched for STM32F7 (F767ZI) — original targeted F4.
 */

#ifndef INC_STEPMOTOR_LIB_H_
#define INC_STEPMOTOR_LIB_H_

#include "stm32f7xx.h"

typedef enum {
    CCW,
    CW
} direction_e;

typedef enum {
    MOTOR_ON,
    MOTOR_OFF
} state_e;

typedef struct {
    TIM_HandleTypeDef *tim_handle;
    uint32_t tim_channel;

    GPIO_TypeDef *dir_portx;
    uint32_t dir_pin;

    struct {
        int counter;
        int state;
        int step;
    } m_set;
} m_cnfg_s;

void stepper_motor_init(m_cnfg_s *motor_conf, TIM_HandleTypeDef *tim_handle,
                        uint32_t tim_channel, GPIO_TypeDef *dir_portx, uint32_t dir_pin);

void startMotor(m_cnfg_s *motor_conf, direction_e direction, int step);

void stopMotor(m_cnfg_s *motor_conf);

#endif /* INC_STEPMOTOR_LIB_H_ */
