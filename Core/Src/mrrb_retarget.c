/**
 * @file        mrrb_retarget.c
 * @brief       Retarget of printf functions using a multiple reader ring buffer
 *
 * @author      Luca Rufer, luca.rufer@swissloop.ch
 * @date        28.12.2023
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

// Header
#include "mrrb_retarget.h"

// Ring buffer
#include "mrrb.h"

// Operating System for Threading
#include "cmsis_os.h"

// HAL Driver for UART
#include "stm32h7xx_hal.h"

// Sockets for UDP
#include "socket.h"

/* Private defines -----------------------------------------------------------*/

#define MRRB_RETARGET_NUM_READERS (((MRRB_RETARGET_UART) == 0 ? 0 : 1) + \
                                   ((MRRB_RETARGET_ITM)  == 0 ? 0 : 1) + \
                                   ((MRRB_RETARGET_UDP)  == 0 ? 0 : 1))

#define MRRB_RETARGET_UDP_FLAG_NEW_DATA 0x0001
#define MRRB_RETARGET_UDP_FLAG_EXIT     0x0002

#if MRRB_RETARGET_NUM_READERS == 0
#warning "MRRB Retarget: No readers enabled"
#endif /* MRRB_RETARGET_NUM_READERS == 0 */

/* Exported macros -----------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

typedef struct {
  osThreadId_t udp_thread;
  osMessageQueueId_t queue;
  ring_buffer_reader_t *reader;
} retarget_udp_state_t;

typedef struct {
  const unsigned char *data;
  unsigned int data_length;
} retarget_udp_message_t;

/* Private function prototypes -----------------------------------------------*/

void _retarget_udp_thread(void *args);

void _retarget_uart_data_notify(multi_reader_ring_buffer_t *mrrb,
                                void *handle,
                                const unsigned char *data,
                                const unsigned int data_length);

void _retarget_itm_data_notify(multi_reader_ring_buffer_t *mrrb,
                               void *handle,
                               const unsigned char *data,
                               const unsigned int data_length);

void _retarget_udp_data_notify(multi_reader_ring_buffer_t *mrrb,
                               void *handle,
                               const unsigned char *data,
                               const unsigned int data_length);

#if USE_HAL_UART_REGISTER_CALLBACKS
void _retarget_uart_TxCpltCallback(UART_HandleTypeDef *huart);
#endif /* USE_HAL_UART_REGISTER_CALLBACKS */

/* Private variables ---------------------------------------------------------*/

// UART Handle
#if MRRB_RETARGET_UART
extern UART_HandleTypeDef MRRB_RETARGET_UART_HANDLE;
#endif /* MRRB_RETARGET_UART */

// ITM Handle
#if MRRB_RETARGET_ITM
int ITM_handle;
#endif /* MRRB_RETARGET_ITM */

// UDP Handle
#if MRRB_RETARGET_UDP
retarget_udp_state_t udp_state;
#endif /* MRRB_RETARGET_UDP */

// Multiple Reader Ring Buffer
multi_reader_ring_buffer_t retarget_mrrb;
unsigned char retarget_buffer[MRRB_RETARGET_BUFFER_LENGTH];
ring_buffer_reader_t retarget_mrrb_readers[MRRB_RETARGET_NUM_READERS];

#if MRRB_RETARGET_UDP
// UDP thread attributes
const osThreadAttr_t retarget_udp_thread_attr = {
  .priority = osPriorityLow,
  .stack_size = 256 * 4,
  .name = "retarget_udp"
};

// UDP remote address
struct sockaddr_in retarget_udp_remote = {
  .sin_family = AF_INET,
  .sin_port = PP_HTONS(MRRB_RETARGET_UDP_RECV_PORT),
  .sin_addr.s_addr = MRRB_RETARGET_UDP_RECV_IP,
};
#endif /* MRRB_RETARGET_UDP */

/* Exported functions --------------------------------------------------------*/

int mrrb_retarget_init() {
  // Misc initialization functions
#if MRRB_RETARGET_UART
#if USE_HAL_UART_REGISTER_CALLBACKS
  if(HAL_UART_RegisterCallback(&MRRB_RETARGET_UART_HANDLE,
                                HAL_UART_TX_COMPLETE_CB_ID,
                                _retarget_uart_TxCpltCallback) != HAL_OK) {
    return -1;
  }
#endif /* USE_HAL_UART_REGISTER_CALLBACKS */
#endif /* MRRB_RETARGET_UART */

#if MRRB_RETARGET_UDP
  // Create Queue for UDP thread message passing
  udp_state.queue = osMessageQueueNew(1,
                                      sizeof(retarget_udp_message_t),
                                      NULL);
  if (udp_state.queue == NULL) {
    return -1;
  }
  // Create thread for retargeting via UDP
  udp_state.udp_thread = osThreadNew(_retarget_udp_thread,
                                     &udp_state,
                                     &retarget_udp_thread_attr);
  if (udp_state.udp_thread == NULL) {
    return -1;
  }
#endif /* MRRB_RETARGET_UDP */

  // Initialize Readers
  ring_buffer_reader_t *current_reader = retarget_mrrb_readers;
#if MRRB_RETARGET_UART
  if(mrrb_reader_init(current_reader++,
                      &MRRB_RETARGET_UART_HANDLE,
                      _retarget_uart_data_notify)) {
    return -1;
  }
#endif /* MRRB_RETARGET_UART */
#if MRRB_RETARGET_ITM
  if(mrrb_reader_init(current_reader++,
                      &ITM_handle,
                      _retarget_itm_data_notify)) {
    return -1;
  }
#endif /* MRRB_RETARGET_ITM */
#if MRRB_RETARGET_UDP
  udp_state.reader = &retarget_mrrb_readers[2];
  if(mrrb_reader_init(current_reader++,
                      &udp_state,
                      _retarget_udp_data_notify)) {
    return -1;
  }
#endif /* MRRB_RETARGET_UDP */

  // Initialize MRRB
  if(mrrb_init(&retarget_mrrb,
               retarget_buffer,
               MRRB_RETARGET_BUFFER_LENGTH,
               retarget_mrrb_readers,
               MRRB_RETARGET_NUM_READERS) < 0) {
    return -1;
  }
  return 0;
}

int mrrb_retarget_deinit() {
  int sts = 0;
#if MRRB_RETARGET_UDP
  // Terminate UDP thread
  uint32_t flags = MRRB_RETARGET_UDP_FLAG_NEW_DATA | MRRB_RETARGET_UDP_FLAG_EXIT;
  osThreadFlagsSet(udp_state.udp_thread, flags);
  if (osMessageQueueDelete(udp_state.queue) != osOK) {
    sts = -1;
  }
#endif /* MRRB_RETARGET_UDP */

  // Deinit MRRB
  if (mrrb_deinit(&retarget_mrrb) < 0) {
    sts = -1;
  }

#if MRRB_RETARGET_UART
  // Deinit peripherals
  if (HAL_UART_DeInit(&MRRB_RETARGET_UART_HANDLE) != HAL_OK) {
    sts = -1;
  }
#endif /* MRRB_RETARGET_UART */

  return sts;
}

#ifdef __GNUC__
int __io_putchar (int ch)
#else
int fputc (int ch, FILE *f)
#endif /* __GNUC__ */
{
  (void) mrrb_write(&retarget_mrrb, (unsigned char *) &ch, 1);
  return ch;
}

int _write(int file, char *ptr, int len) {
  return mrrb_write(&retarget_mrrb, (unsigned char *) ptr, len);
}

#if MRRB_RETARGET_UART
#if USE_HAL_UART_REGISTER_CALLBACKS
void _retarget_uart_TxCpltCallback(UART_HandleTypeDef *huart)
#else /* USE_HAL_UART_REGISTER_CALLBACKS */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
#endif /* USE_HAL_UART_REGISTER_CALLBACKS */
{
  if (huart == &MRRB_RETARGET_UART_HANDLE) {
    mrrb_read_complete(&retarget_mrrb, huart);
  }
}
#endif /* MRRB_RETARGET_UART */

/* Private functions ---------------------------------------------------------*/

#if MRRB_RETARGET_UDP
void _retarget_udp_thread(void *args) {
  // Cast arguments
  retarget_udp_state_t *state = (retarget_udp_state_t *) args;

  // Setup the socket
  int udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (udp_socket < 0) {
    mrrb_reader_disable(&retarget_mrrb, state->reader);
    osThreadExit();
  }
  // Enter thread loop
  while (1) {
    // Wait for new data
    uint32_t flags = osThreadFlagsWait(MRRB_RETARGET_UDP_FLAG_NEW_DATA,
                                       osFlagsWaitAny,
                                       osWaitForever);
    // Check if flags contain an error or exit flag is set
    if (((int32_t) flags) < 0 || flags & MRRB_RETARGET_UDP_FLAG_EXIT) {
      close(udp_socket);
      mrrb_reader_disable(&retarget_mrrb, state->reader);
      osThreadExit();
    }

    // Get the data pointer and length
    retarget_udp_message_t msg;
    if (osMessageQueueGet(state->queue, &msg, NULL, 1) == osOK) {
      // Send the data from the message
      int sock_sts = sendto(udp_socket,
                            msg.data,
                            msg.data_length,
                            0,
                            (struct sockaddr *) &retarget_udp_remote,
                            sizeof(retarget_udp_remote));
      if (sock_sts == (int) msg.data_length) {
        // Notify read complete
        mrrb_read_complete(&retarget_mrrb, state);
      } else {
        // Sending data failed, disable the UDP reader
        mrrb_reader_disable(&retarget_mrrb, state->reader);
      }
    } else {
      // Getting the msg from the queue failed, disable the UDP reader
      mrrb_reader_disable(&retarget_mrrb, state->reader);
    }
  }
}
#endif /* MRRB_RETARGET_UDP */

#if MRRB_RETARGET_UART
void _retarget_uart_data_notify(multi_reader_ring_buffer_t *mrrb,
                                void *handle,
                                const unsigned char *data,
                                const unsigned int data_length) {
  HAL_UART_Transmit_IT(handle, data, data_length);
}
#endif /* MRRB_RETARGET_UART */

#if MRRB_RETARGET_ITM
void _retarget_itm_data_notify(multi_reader_ring_buffer_t *mrrb,
                               void *handle,
                               const unsigned char *data,
                               const unsigned int data_length) {
  // Send all data
  for(unsigned int i = 0; i < data_length; i++) {
    uint32_t ch = (uint32_t) *(data++);
    ITM_SendChar(ch);
  }
  // Notify read complete
  mrrb_read_complete(mrrb, handle);
}
#endif /* MRRB_RETARGET_ITM */

#if MRRB_RETARGET_UDP
void _retarget_udp_data_notify(multi_reader_ring_buffer_t *mrrb,
                               void *handle,
                               const unsigned char *data,
                               const unsigned int data_length) {
  // Cast arguments
  retarget_udp_state_t *state = (retarget_udp_state_t *) handle;
  // Create Queue Item
  retarget_udp_message_t msg = {
    .data = data,
    .data_length = data_length,
  };
  // Add notification to the queue
  if (osMessageQueuePut(state->queue, &msg, 0, 1) == osOK) {
    // Set new data flag for udp thread
    osThreadFlagsSet(state->udp_thread, MRRB_RETARGET_UDP_FLAG_NEW_DATA);
  } else {
    // Putting a message in the Queue failed, disable the UDP reader
    mrrb_reader_disable(&retarget_mrrb, state->reader);
  }
}
#endif /* MRRB_RETARGET_UDP */

#ifdef __cplusplus
}
#endif
