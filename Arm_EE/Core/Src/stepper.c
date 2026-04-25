#include "stepper.h"
#include <math.h>

static inline void stepPulse(GPIO_TypeDef* port, uint16_t pin)
{
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
    __NOP(); __NOP(); __NOP();
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
}

void Stepper_Enable(StepperMotor* m, int enable)
{
    HAL_GPIO_WritePin(m->enPort, m->enPin,
        enable ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void Stepper_StepISR(StepperMotor* m)
{
    if (m->currentSteps == m->targetSteps)
        return;

    int delta = m->targetSteps - m->currentSteps;

    HAL_GPIO_WritePin(m->dirPort, m->dirPin,
        (delta > 0) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    if (delta > 0) m->currentSteps++;
    else m->currentSteps--;

    stepPulse(m->stepPort, m->stepPin);
}

/* gear-aware conversion */
int angleToStepsMotor(StepperMotor* m, float rad)
{
    return (int)(rad * (m->stepsPerRev / (2.0f * M_PI)));
}

float stepsToAngleMotor(StepperMotor* m)
{
    return (m->currentSteps * (2.0f * M_PI)) / m->stepsPerRev;
}

/* degrees helpers */
float degToRad(float deg)
{
    return deg * (M_PI / 180.0f);
}

float radToDeg(float rad)
{
    return rad * (180.0f / M_PI);
}

void Stepper_GoToAngleDeg(StepperMotor* m, float deg)
{
    float rad = degToRad(deg);
    m->targetSteps = angleToStepsMotor(m, rad);
}
