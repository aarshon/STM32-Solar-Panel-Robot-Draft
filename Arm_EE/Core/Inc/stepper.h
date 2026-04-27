#ifndef INC_STEPPER_H_
#define INC_STEPPER_H_

#include "stm32f7xx_hal.h"

typedef struct
{
    GPIO_TypeDef* stepPort;
    uint16_t stepPin;

    GPIO_TypeDef* dirPort;
    uint16_t dirPin;

    GPIO_TypeDef* enPort;
    uint16_t enPin;

    int currentSteps;
    int targetSteps;

    float stepsPerRev;
} StepperMotor;

/* API */
void Stepper_Enable(StepperMotor* m, int enable);
void Stepper_StepISR(StepperMotor* m);
void Stepper_SetTarget(StepperMotor* m, int target);

void Stepper_GoToAngleDeg(StepperMotor* m, float deg);

/* conversion */
int angleToStepsMotor(StepperMotor* m, float rad);
float stepsToAngleMotor(StepperMotor* m);

float degToRad(float deg);
float radToDeg(float rad);

#endif
