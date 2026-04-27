/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f7xx_it.c
  * @brief   Interrupt Service Routines — Stepper Test 2 (Arm STM32)
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f7xx_it.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */
/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern UART_HandleTypeDef huart1;
/* USER CODE BEGIN EV */
/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M7 Processor Interruption and Exception Handlers          */
/******************************************************************************/

void NMI_Handler(void)
{
  while (1) { }
}

void HardFault_Handler(void)
{
  while (1) { }
}

void MemManage_Handler(void)
{
  while (1) { }
}

void BusFault_Handler(void)
{
  while (1) { }
}

void UsageFault_Handler(void)
{
  while (1) { }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}

/******************************************************************************/
/* STM32F7xx Peripheral Interrupt Handlers                                    */
/******************************************************************************/

/**
  * @brief USART1 global interrupt — base-link RX byte.
  *        HAL dispatches the byte to HAL_UART_RxCpltCallback() in main.c.
  */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

/* USER CODE BEGIN 1 */
/* USER CODE END 1 */
