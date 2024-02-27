/**
 * @file       mrrb.h
 * @brief      Multiple Reader Ring Buffer Header file
 * @version    v0.2
 *
 * @author     Luca Rufer, luca.rufer@swissloop.ch
 * @date       23.11.2023
 */

#ifndef __MRRB_H
#define __MRRB_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

// Config file
#include "mrrb_config.h"

// Portable code
#include MRRB_PORT_PATH

/* Exported constants --------------------------------------------------------*/

/* Exported macros -----------------------------------------------------------*/

/* Exported types ------------------------------------------------------------*/

typedef MRRB_PORT_MUTEX_TYPE mrrb_mutex_t;

typedef struct multi_reader_ring_buffer_s multi_reader_ring_buffer_t;

typedef void (*mrrb_reader_notify_data_t)(multi_reader_ring_buffer_t *mrrb,
                                          void *handle,
                                          const unsigned char *data,
                                          const unsigned int data_length);

typedef void (*mrrb_reader_abort_data_t)(multi_reader_ring_buffer_t *mrrb,
                                         void *handle);

typedef enum {
  MRRB_READER_STATUS_DISABLED,
  MRRB_READER_STATUS_IDLE,
  MRRB_READER_STATUS_ACTIVE,
  MRRB_READER_STATUS_ABORTING,
  MRRB_READER_STATUS_ABORTED,
  MRRB_READER_STATUS_DISABLING,
} mrrb_reader_status_t;

typedef enum {
  MRRB_READER_OVERRUN_BLOCKING,
  MRRB_READER_OVERRUN_DISABLE,
  MRRB_READER_OVERRUN_SKIP,
} mrrb_reader_overrun_policy_t;

typedef struct {
  void *handle;
  mrrb_reader_overrun_policy_t overrun_policy;
  volatile unsigned char *read_ptr;
  volatile unsigned char *read_complete_ptr;
  volatile mrrb_reader_status_t status;
  volatile unsigned char is_full;
  mrrb_reader_notify_data_t notify_data;
  mrrb_reader_abort_data_t abort_data;
} ring_buffer_reader_t;

struct multi_reader_ring_buffer_s {
  unsigned char *buffer;
  unsigned int buffer_length;
  ring_buffer_reader_t *readers;
  unsigned int num_readers;
  volatile unsigned char *write_ptr;
  volatile unsigned char *reservation_ptr;
  volatile unsigned int ongoing_writes;
#if MRRB_USE_MUTEX
  mrrb_mutex_t mutex;
#endif /* MRRB_USE_MUTEX */
};

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

int mrrb_reader_init(ring_buffer_reader_t *reader,
                     void* handle,
                     mrrb_reader_overrun_policy_t overrun_policy,
                     mrrb_reader_notify_data_t notify_data,
                     mrrb_reader_abort_data_t abort_data);
int mrrb_reader_deinit(ring_buffer_reader_t *reader);
int mrrb_reader_enable(multi_reader_ring_buffer_t *mrrb, ring_buffer_reader_t *reader);
int mrrb_reader_disable(multi_reader_ring_buffer_t *mrrb, ring_buffer_reader_t *reader);

int mrrb_init(multi_reader_ring_buffer_t *mrrb,
              unsigned char *buffer,
              const unsigned int buffer_length,
              ring_buffer_reader_t readers[],
              const unsigned int num_readers);
int mrrb_deinit(multi_reader_ring_buffer_t *mrrb);
unsigned int mrrb_get_remaining_space(const multi_reader_ring_buffer_t *mrrb);
unsigned int mrrb_get_overwritable_space(const multi_reader_ring_buffer_t *mrrb);
char mrrb_is_empty(const multi_reader_ring_buffer_t *mrrb);
char mrrb_is_full(const multi_reader_ring_buffer_t *mrrb);
int mrrb_write(multi_reader_ring_buffer_t *mrrb,
               const unsigned char *data,
               unsigned int data_length);
void mrrb_read_complete(multi_reader_ring_buffer_t *mrrb,
                        void *reader_handle);
void mrrb_abort_complete(multi_reader_ring_buffer_t *mrrb,
                         void *reader_handle);

/* Inline functions --------------------------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif // __MRRB_H included
