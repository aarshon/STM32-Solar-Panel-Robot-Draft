#include "motor_config.h"

StepperMotor motor1;
StepperMotor motor2;
StepperMotor motor3;

void Motors_Init(void)
{
    motor1 = (StepperMotor){
        .stepPort = GPIOA, .stepPin = GPIO_PIN_3,
        .dirPort  = GPIOC, .dirPin  = GPIO_PIN_0,
        .enPort   = GPIOF, .enPin   = GPIO_PIN_6,
        .currentSteps = 0,
        .targetSteps = 0,
        .stepsPerRev = 51200.0f * 6.0f
    };

    motor2 = (StepperMotor){
        .stepPort = GPIOC, .stepPin = GPIO_PIN_3,
        .dirPort  = GPIOF, .dirPin  = GPIO_PIN_3,
        .enPort   = GPIOF, .enPin   = GPIO_PIN_7,
        .currentSteps = 0,
        .targetSteps = 0,
        .stepsPerRev = 51200.0f * 20.0f
    };

    motor3 = (StepperMotor){
        .stepPort = GPIOF, .stepPin = GPIO_PIN_5,
        .dirPort  = GPIOF, .dirPin  = GPIO_PIN_10,
        .enPort   = GPIOF, .enPin   = GPIO_PIN_8,
        .currentSteps = 0,
        .targetSteps = 0,
        .stepsPerRev = 51200.0f * 10.0f
    };

    Stepper_Enable(&motor1, 1);
    Stepper_Enable(&motor2, 1);
    Stepper_Enable(&motor3, 1);
}
