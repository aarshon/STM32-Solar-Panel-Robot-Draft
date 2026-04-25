#ifndef MOTOR_CONFIG_H
#define MOTOR_CONFIG_H

#include "stm32f7xx_hal.h"
#include "stepper.h"

/* motors */
extern StepperMotor motor1;
extern StepperMotor motor2;
extern StepperMotor motor3;

void Motors_Init(void);

#endif
