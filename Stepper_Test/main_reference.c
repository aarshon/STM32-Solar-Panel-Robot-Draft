/*
 * main_reference.c — reference snippets for Stepper_Test on Nucleo-F767ZI.
 *
 * This is NOT a standalone file. After CubeMX generates your project,
 * paste each section below into the corresponding USER CODE region of the
 * generated Core/Src/main.c. The CubeMX-generated HAL init / clock config
 * is left untouched.
 *
 * CubeMX config expected (see README notes in chat):
 *   - TIM1 Channel 1: PWM Generation CH1 on PE9  (STEP pulse)
 *   - GPIO Output: PE10 (DIR)
 *   - TIM1 global interrupt ENABLED in NVIC settings
 *   - Clock: default HSE bypass @ 216 MHz from Nucleo ST-LINK
 */


/* ==========================================================================
 * /* USER CODE BEGIN Includes */
 * ========================================================================== */
#include "StepMotor_lib.h"
/* USER CODE END Includes */


/* ==========================================================================
 * /* USER CODE BEGIN PV */
 * ========================================================================== */
m_cnfg_s stepperMotor;
/* USER CODE END PV */


/* ==========================================================================
 * /* USER CODE BEGIN 0 */
 *   (PWM pulse-finished ISR — increments step counter, stops when target hit)
 * ========================================================================== */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1) {
        if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {
            stepperMotor.m_set.counter++;
        }
        stopMotor(&stepperMotor);
    }
}
/* USER CODE END 0 */


/* ==========================================================================
 * /* USER CODE BEGIN 2 */
 *   (runs once after all peripheral init, before the main while(1))
 *
 *   Set TIM1 Pulse (CCR1) to ~50% duty. The library starts PWM via
 *   HAL_TIM_PWM_Start_IT, but the CCR value must be > 0 or the pulse
 *   never goes high. Easiest: set Pulse = Period/2 in CubeMX. If you
 *   left it at 0, force it here:
 * ========================================================================== */
__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (__HAL_TIM_GET_AUTORELOAD(&htim1) + 1) / 2);

stepper_motor_init(&stepperMotor, &htim1, TIM_CHANNEL_1, GPIOE, GPIO_PIN_10);

/* Kick off the first move: 2000 steps CCW. Watch the motor turn. */
startMotor(&stepperMotor, CCW, 2000);
/* USER CODE END 2 */


/* ==========================================================================
 * /* USER CODE BEGIN WHILE */
 *   (optional: ping-pong direction every ~2 seconds once previous move done)
 * ========================================================================== */
while (1)
{
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
    if (stepperMotor.m_set.state == MOTOR_OFF) {
        HAL_Delay(500);
        static direction_e dir = CW;
        startMotor(&stepperMotor, dir, 2000);
        dir = (dir == CW) ? CCW : CW;
    }
}
/* USER CODE END 3 */
