/**
 * @file        rtos_stats.c
 * @brief       Periodically send RTOS thread stats via UDP
 *
 * @author      Luca Rufer, luca.rufer@swissloop.ch
 * @date        06.01.2024
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

// Operating System for Threading
#include "cmsis_os.h"

// Sockets for UDP
#include "socket.h"

// ADC for internal channel measurements of voltages and temperature
#include "adc.h"

/* Private defines -----------------------------------------------------------*/

// IP address to integer macro
#define IP_TO_INT(b0, b1, b2, b3) (((b3) << 24) | ((b2) << 16) | ((b1) << 8) | (b0))

// UDP Target settings
#define UDP_TARGET_IP IP_TO_INT(192, 168, 0, 9)
#define UDP_TARGET_PORT 13870

// Update period in milliseconds
#define RTOS_STATS_PERIOD_MS 1000

// The number of ADC samples in the group
#define RTOS_SYSTEM_STATS_ADC_SAMPLE_COUNT 3

// Thread flag indicating ADC conversion is complete
#define RTOS_STATS_THREAD_FLAG_ADC_DONE 0x00000001

// ADC timeout: 10 ms
#define RTOS_STATS_THREAD_ADC_TIMEOUT ((10 + (portTICK_PERIOD_MS - 1)) / portTICK_PERIOD_MS)

// ADC Range (12 Bits resolution)
#define RTOS_STATS_ADC_FULL_RANGE ((1 << 12) - 1)

/* Exported macros -----------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

typedef struct {
  // Task Basics
  UBaseType_t taskNumber;
  char taskName[configMAX_TASK_NAME_LEN];
  // Task State
  eTaskState state;
  // Stack Management
  StackType_t *stackBase;
  StackType_t	*stackCurrent;
  StackType_t	*stackTop;
  uint16_t stackHighWaterMark;
  // Priority
  UBaseType_t basePriority;
  UBaseType_t currentPriority;
  // Runtime Counter
  uint32_t runTimeCounter;
} RTOS_task_stats_t;

typedef struct {
  uint32_t num_threads;
  uint32_t total_runtime;
} RTOS_stats_header_t;

typedef struct {
  uint16_t valid;
  uint16_t VDDA_mV;
  uint16_t VBAT_mV;
  uint16_t temp_die_C;
} RTOS_system_stats_t;

typedef struct {
  uint16_t samples[RTOS_SYSTEM_STATS_ADC_SAMPLE_COUNT];
  uint16_t sample_index;
  osThreadId_t thread_id;
} RTOS_system_stats_ADC_context_t;

/* Private function prototypes -----------------------------------------------*/

// Function that handles the ADC conversion complete interrupt
void RTOS_stats_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc);

/* Private variables ---------------------------------------------------------*/

const struct sockaddr_in _udp_target = {
  .sin_family = AF_INET,
  .sin_port = PP_HTONS(UDP_TARGET_PORT),
  .sin_addr.s_addr = UDP_TARGET_IP,
};

// ADC sampling context
RTOS_system_stats_ADC_context_t _rtos_system_stats_ADC_context;

/* Exported functions --------------------------------------------------------*/

/* Private functions ---------------------------------------------------------*/

void RTOS_stats_UDP_thread(void *argument) {
  (void) argument;
  int udp_socket, socket_sts;
  unsigned int num_RTOS_threads;
  void *udp_packet;
  int udp_packet_size;
  int system_stats_valid;
  RTOS_stats_header_t *stats_header;
  RTOS_task_stats_t *task_stats_udp;
  RTOS_system_stats_t *system_stats_udp;
  TaskStatus_t *task_stats_os;
  TickType_t lastWakeTime;

  // Setup the socket
  udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udp_socket < 0) {
    osThreadExit();
  }

  // Calibrate the ADC
  HAL_ADCEx_Calibration_Start(&hadc3, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);

  // Register the Callback if enabled
#if USE_HAL_ADC_REGISTER_CALLBACKS
  HAL_ADC_RegisterCallback(&hadc3, HAL_ADC_CONVERSION_COMPLETE_CB_ID, &RTOS_Stats_ADC_ConvCpltCallback);
#endif /* USE_HAL_ADC_REGISTER_CALLBACKS */

  // Enter thread loop
  lastWakeTime = xTaskGetTickCount();
  while (1) {
    // Sleep for the given period
    vTaskDelayUntil(&lastWakeTime, RTOS_STATS_PERIOD_MS / portTICK_PERIOD_MS);

    // Prepare and start sampling the ADC. The conversions are expected to take 0.25 ms in total.
    // Internal sensors must be sampled for at least 9 us per measurement (260 cycles/conv / 25 MHz = 10.4 us/conv)
    // Total conversion time: 8x oversampling * 3 conversions * 10.4 us/conv = 249.6 us.
    _rtos_system_stats_ADC_context.sample_index = 0;
    _rtos_system_stats_ADC_context.thread_id = osThreadGetId();
    if (HAL_ADC_Start_IT(&hadc3) == HAL_OK) {
      system_stats_valid = 1;
    } else {
      system_stats_valid = 0;
    }

    // Determine the number of running threads
    num_RTOS_threads = uxTaskGetNumberOfTasks();
    udp_packet_size = sizeof(RTOS_stats_header_t) +
                      sizeof(RTOS_task_stats_t) * num_RTOS_threads +
                      sizeof(RTOS_system_stats_t);

    // Reserve space for getting the task information from the OS
    task_stats_os = (TaskStatus_t *) pvPortMalloc(sizeof(TaskStatus_t) * num_RTOS_threads);
    udp_packet = pvPortMalloc(udp_packet_size);

    // Check if space was allocated
    if (task_stats_os == NULL || udp_packet == NULL) {
      vPortFree(task_stats_os);
      vPortFree(udp_packet);
      continue;
    }

    // Calculate header and task stats pointers
    stats_header = (RTOS_stats_header_t *) udp_packet;
    task_stats_udp = (RTOS_task_stats_t *) (udp_packet + sizeof(RTOS_stats_header_t));
    system_stats_udp = (RTOS_system_stats_t *) (&task_stats_udp[num_RTOS_threads]);

    // Collect system state from the OS
    if (uxTaskGetSystemState(task_stats_os, num_RTOS_threads, &stats_header->total_runtime) != num_RTOS_threads) {
      vPortFree(task_stats_os);
      vPortFree(task_stats_udp);
      continue;
    }

    // Complete the header
    stats_header->num_threads = num_RTOS_threads;

    // Copy information from the system state to the udp packet
    for (unsigned int i = 0; i < num_RTOS_threads; i++) {
      task_stats_udp[i].taskNumber = task_stats_os[i].xTaskNumber;
      task_stats_udp[i].state = task_stats_os[i].eCurrentState;

      // Stack management (current stack and stack top accessed unofficially via TCB)
      task_stats_udp[i].stackBase = task_stats_os[i].pxStackBase;
      task_stats_udp[i].stackHighWaterMark = task_stats_os[i].usStackHighWaterMark;
      task_stats_udp[i].stackCurrent = *((uint32_t **) task_stats_os[i].xHandle);
      task_stats_udp[i].stackTop = *(((uint32_t **) &task_stats_os[i].pcTaskName[configMAX_TASK_NAME_LEN]));

      // Priority
      task_stats_udp[i].basePriority = task_stats_os[i].uxBasePriority;
      task_stats_udp[i].currentPriority = task_stats_os[i].uxCurrentPriority;

      // Run time
      task_stats_udp[i].runTimeCounter = task_stats_os[i].ulRunTimeCounter;

      // Copy task name
      memcpy(&task_stats_udp[i].taskName, task_stats_os[i].pcTaskName, configMAX_TASK_NAME_LEN);
    }

    // Release OS stats memory
    vPortFree(task_stats_os);

    // Wait for the system stats to complete the ADC conversions
    if (system_stats_valid) {
      if (osThreadFlagsWait(RTOS_STATS_THREAD_FLAG_ADC_DONE, osFlagsWaitAny, RTOS_STATS_THREAD_ADC_TIMEOUT) & 0x80000000UL) {
        // Error flag was risen, system stats are not valid
        system_stats_valid = 0;
      }
      // Stop the ADC conversion
      (void) HAL_ADC_Stop_IT(&hadc3);
    }

    // Calculate System stats
    system_stats_udp->valid = system_stats_valid;
    if (system_stats_valid) {
      // Determine the Vref+ voltage using the internal voltage reference calibrated value
      system_stats_udp->VDDA_mV = (VREFINT_CAL_VREF * *VREFINT_CAL_ADDR) / _rtos_system_stats_ADC_context.samples[0];
      // Determine Vbat (often connected to VDD) using the internal 1/4 voltage divider
      system_stats_udp->VBAT_mV = (uint32_t) 4UL * system_stats_udp->VDDA_mV * _rtos_system_stats_ADC_context.samples[1] / RTOS_STATS_ADC_FULL_RANGE;
      // Determine the die temperature using the internal temperature sensor (See Reference Manual for formula)
      system_stats_udp->temp_die_C =
      ((((int32_t)((((int32_t) _rtos_system_stats_ADC_context.samples[2]) * system_stats_udp->VDDA_mV) / TEMPSENSOR_CAL_VREFANALOG) - (int32_t) *TEMPSENSOR_CAL1_ADDR))
       * (int32_t)(TEMPSENSOR_CAL2_TEMP - TEMPSENSOR_CAL1_TEMP)) / (int32_t)((int32_t)*TEMPSENSOR_CAL2_ADDR - (int32_t)*TEMPSENSOR_CAL1_ADDR) + TEMPSENSOR_CAL1_TEMP;
    }

    // Send the data
    socket_sts = sendto(udp_socket,
                        udp_packet,
                        udp_packet_size,
                        0,
                        (const struct sockaddr *) &_udp_target,
                        sizeof(_udp_target));

    // Release the UDP packet memory
    vPortFree(udp_packet);

    // check if data was sent successfully
    if (socket_sts != udp_packet_size) {
      break;
    }
  }

  // Task failed. Close the socket, de-init the ADC and exit the thread
  close(udp_socket);
  (void) HAL_ADC_DeInit(&hadc3);
  osThreadExit();
}

void RTOS_stats_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
  // Get the latest value from the ADC
  _rtos_system_stats_ADC_context.samples[_rtos_system_stats_ADC_context.sample_index++] = (uint16_t) HAL_ADC_GetValue(hadc);
  // Check if all samples are completed
  if (_rtos_system_stats_ADC_context.sample_index == RTOS_SYSTEM_STATS_ADC_SAMPLE_COUNT) {
    // Notify the thread that all samples are completed
    osThreadFlagsSet(_rtos_system_stats_ADC_context.thread_id, RTOS_STATS_THREAD_FLAG_ADC_DONE);
  }
}

#if !USE_HAL_ADC_REGISTER_CALLBACKS
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
  if (hadc->Instance == ADC3) {
    RTOS_stats_ADC_ConvCpltCallback(hadc);
  }
}
#endif /* USE_HAL_ADC_REGISTER_CALLBACKS */

#ifdef __cplusplus
}
#endif
