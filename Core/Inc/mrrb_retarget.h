/**
 * @file       mrrb_retarget.h
 * @brief      Retarget of printf functions using a multiple reader ring buffer
 *
 * @author     Luca Rufer, luca.rufer@swissloop.ch
 * @date       28.12.2023
 */

#ifndef __MRRB_RETARGET_H
#define __MRRB_RETARGET_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

/* Exported constants --------------------------------------------------------*/

// Macro to convert IP address to integer
#define MRRB_RETARGET_IP_TO_INT(b0, b1, b2, b3) (((b3) << 24) | ((b2) << 16) | ((b1) << 8) | (b0))

// Buffer length in Bytes
#define MRRB_RETARGET_BUFFER_LENGTH 1024

// Readers
#define MRRB_RETARGET_UART 1
#define MRRB_RETARGET_ITM 1
#define MRRB_RETARGET_UDP 1

// UART settings
#define MRRB_RETARGET_UART_HANDLE huart3

// UDP settings
#define MRRB_RETARGET_UDP_RECV_PORT 13869
#define MRRB_RETARGET_UDP_RECV_IP MRRB_RETARGET_IP_TO_INT(192, 168, 0, 9)

/* Exported macros -----------------------------------------------------------*/

/* Exported types ------------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

int mrrb_retarget_init(void);
int mrrb_retarget_deinit(void);

/* Inline functions --------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif // __MRRB_RETARGET_H included
