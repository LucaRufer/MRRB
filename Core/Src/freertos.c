/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "sys.h"
#include "tim.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#ifdef __GNUC__
#define USED __attribute__((used))
#else
#define USED
#endif
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
const unsigned char *const header =
"\n\n\n"
"                    =============================\n"
"                    |                           |\n"
"                    |  MRRB Retarget Example    |\n"
"                    |  -----------------------  |\n"
"                    |  Platform: NUCLEO-H723ZG  |\n"
"                    |  Author:   Luca Rufer     |\n"
"                    |                           |\n"
"                    =============================\n"
"\n\n\n";

// RunTimeCounter counting variable
volatile uint32_t _runTimeCounter_OverflowCount = 0;

// Workaround for OpenOCD to detect FreeRTOS versions since 7.5.3
#if (tskKERNEL_VERSION_MAJOR) > 7 || \
    ((tskKERNEL_VERSION_MAJOR) == 7 && (tskKERNEL_VERSION_MINOR > 5)) || \
    ((tskKERNEL_VERSION_MAJOR) == 7 && (tskKERNEL_VERSION_MINOR == 5) && tskKERNEL_VERSION_BUILD >= 3)
const int USED uxTopUsedPriority = configMAX_PRIORITIES - 1;
#endif

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void TIM23_PeriodElapsedCallback(TIM_HandleTypeDef *htim);
void _print_threads_status(void);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

extern void MX_LWIP_Init(void);
void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void configureTimerForRunTimeStats(void);
unsigned long getRunTimeCounterValue(void);
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);

/* USER CODE BEGIN 1 */
/* Functions needed when configGENERATE_RUN_TIME_STATS is on */
void configureTimerForRunTimeStats(void)
{
  // If callback registration is enabled, register the period elapsed callback
#if USE_HAL_TIM_REGISTER_CALLBACKS
  HAL_TIM_RegisterCallback(&htim23, HAL_TIM_PERIOD_ELAPSED_CB_ID, TIM23_PeriodElapsedCallback);
#endif /* USE_HAL_TIM_REGISTER_CALLBACKS */
  // If Debug is enabled, freeze the timer when the debugger halts the core
#ifdef DEBUG
  __HAL_DBGMCU_FREEZE_TIM23();
#endif /* DEBUG */
  // Set the overflow to 0
  _runTimeCounter_OverflowCount = 0;
  // Start the timer
  HAL_TIM_Base_Start_IT(&htim23);
}

unsigned long getRunTimeCounterValue(void)
{
  uint32_t upper, lower;
  uint64_t runtime_us;
  // Acquire upper and lower 32-bit counter values
  upper = _runTimeCounter_OverflowCount;
  lower = __HAL_TIM_GET_COUNTER(&htim23);
  __DSB();
  // Check that no overflow occurrend while reading the values
  // Only single re-try, as this happens once per 1.2 hours.
  if (upper != _runTimeCounter_OverflowCount) {
    upper = _runTimeCounter_OverflowCount;
    lower = __HAL_TIM_GET_COUNTER(&htim23);
  }
  // combine upper and lower 32 bits
  runtime_us = (((uint64_t) upper) << 32) | lower;
  return runtime_us;
}

void TIM23_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
  if(htim == &htim23) {
    _runTimeCounter_OverflowCount++;
  }
}
/* USER CODE END 1 */

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
  printf("[ERROR] Stack overflow of thread: %s\n", pcTaskName);
  configASSERT(0);
}
/* USER CODE END 4 */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* init code for LWIP */
  MX_LWIP_Init();
  /* USER CODE BEGIN StartDefaultTask */

  // Print the header
  printf(header);

  int button_was_pressed = 0;

  /* Infinite loop */
  for(;;)
  {
    osDelay(10);
    if (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_SET) {
      if (!button_was_pressed) {
        _print_threads_status();
        button_was_pressed = 1;
      }
    } else {
      button_was_pressed = 0;
    }
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

void _print_threads_status() {
  // Get the number of tasks
  uint16_t arraySize = uxTaskGetNumberOfTasks();

  // Prepare an Array to get the task descriptions
  TaskStatus_t taskStatusArray[arraySize];

  // Generate raw status information about each task.
  arraySize = uxTaskGetSystemState( taskStatusArray, arraySize, NULL);

  // Print the header
  printf("=============== System stats: ===============\n");
  printf("   Thread Name      Stackbase\tStack Left\n");

  // Print the information for each Task
  for(unsigned int x = 0; x < arraySize; x++ ) {
    TaskStatus_t tsk = taskStatusArray[x];
    printf("   %-16s %p\t%hu\n", tsk.pcTaskName, tsk.pxStackBase, tsk.usStackHighWaterMark);
  }

  // Print the footer
  printf("============ Total: %4hu Threads ============\n", arraySize);
}
/* USER CODE END Application */

