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

/* Private defines -----------------------------------------------------------*/

// IP address to integer macro
#define IP_TO_INT(b0, b1, b2, b3) (((b3) << 24) | ((b2) << 16) | ((b1) << 8) | (b0))

// UDP Target settings
#define UDP_TARGET_IP IP_TO_INT(192, 168, 0, 9)
#define UDP_TARGET_PORT 13870

// Update period in milliseconds
#define RTOS_STATS_PERIOD_MS 1000

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

/* Private function prototypes -----------------------------------------------*/

/* Private variables ---------------------------------------------------------*/

const struct sockaddr_in udp_target = {
  .sin_family = AF_INET,
  .sin_port = PP_HTONS(UDP_TARGET_PORT),
  .sin_addr.s_addr = UDP_TARGET_IP,
};

/* Exported functions --------------------------------------------------------*/

/* Private functions ---------------------------------------------------------*/

void RTOS_stats_UDP_thread(void *argument) {
  (void) argument;
  int udp_socket, socket_sts;
  unsigned int num_RTOS_threads;
  void *udp_packet;
  int udp_packet_size;
  RTOS_stats_header_t *stats_header;
  RTOS_task_stats_t *task_stats_udp;
  TaskStatus_t *task_stats_os;
  TickType_t lastWakeTime;

  // Setup the socket
  udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udp_socket < 0) {
    osThreadExit();
  }

  // Enter thread loop
  lastWakeTime = xTaskGetTickCount();
  while (1) {
    // Sleep for the given period
    vTaskDelayUntil(&lastWakeTime, RTOS_STATS_PERIOD_MS / portTICK_PERIOD_MS);

    // Determine the number of running threads
    num_RTOS_threads = uxTaskGetNumberOfTasks();
    udp_packet_size = sizeof(RTOS_stats_header_t) + sizeof(RTOS_task_stats_t) * num_RTOS_threads;

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

    // Send the data
    socket_sts = sendto(udp_socket,
                        udp_packet,
                        udp_packet_size,
                        0,
                        (const struct sockaddr *) &udp_target,
                        sizeof(udp_target));

    // Release the UDP packet memory
    vPortFree(udp_packet);

    // check if data was sent successfully
    if (socket_sts != udp_packet_size) {
      break;
    }
  }

  // Task failed. Close the socket and exit the thread
  close(udp_socket);
  osThreadExit();
}

#ifdef __cplusplus
}
#endif
