/**
 * @file        mrrb.c
 * @brief       Multiple Reader Ring Buffer Implementation
 *
 * @author      Luca Rufer, luca.rufer@swissloop.ch
 * @date        23.11.2023
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

// Std libraries
#include <string.h>

// Header
#include "mrrb.h"

/* Private defines -----------------------------------------------------------*/

/* Exported macros -----------------------------------------------------------*/

/* Private typedef -----------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/

int _mrrb_lock(multi_reader_ring_buffer_t *mrrb);
int _mrrb_unlock(multi_reader_ring_buffer_t *mrrb, int lock);

unsigned int _mrrb_reader_get_remaining_space(const multi_reader_ring_buffer_t *mrrb,
                                              const ring_buffer_reader_t *reader);
ring_buffer_reader_t *_mrrb_get_reader_by_handle(const multi_reader_ring_buffer_t *mrrb, void* handle);

/* Private variables ---------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize a ring buffer reader.
 *
 * @param reader The reader to be initialized.
 * @param handle A pointer to a custom user handle to identify the reader.
 *               May be NULL.
 * @param notify_data A function that will be called by the MRRB when new data
 *                    is available for the reader. The function has the MRRB,
 *                    the handle, a pointer to data and the length of data as
 *                    arguments. Every call of @ref notify_data requires the
 *                    reader to call @ref mrrb_read_complete when all of the
 *                    data was processed, or @ref mrrb_reader_disable to
 *                    disable a reader.
 * @return 0 if the reader is initialized successfully,
 *         -1 if an error occurred.
 */
int mrrb_reader_init(ring_buffer_reader_t *reader,
                     void* handle,
                     mrrb_reader_notify_data_t notify_data) {
  // Check the arguments
  if (reader == NULL || notify_data == NULL) {
    return -1;
  }
  // Initialize the reader
  reader->handle = handle;
  reader->notify_data = notify_data;
  return 0;
}

/**
 * @brief Enable an MRRB reader.
 *
 * @param mrrb The MRRB the reader belongs to.
 * @param reader The Reader to be enabled.
 * @return 0 if the reader was enabled successfully,
 *         -1 is the reader could not be enabled.
 */
int mrrb_reader_enable(multi_reader_ring_buffer_t *mrrb, ring_buffer_reader_t *reader) {
  // Check the arguments
  if (mrrb == NULL || reader == NULL) {
    return -1;
  }

  // Try to acquire lock to modify mrrb
  int lock = _mrrb_lock(mrrb);
  if (lock < 0) {
    return -1;
  }

  // Set the reader into idle and empty the reader
  reader->status = MRRB_READER_STATUS_IDLE;
  reader->is_full = 0;
  reader->read_ptr = mrrb->reservation_ptr;
  reader->read_complete_ptr = mrrb->reservation_ptr;

  // Unlock mrrb
  if (_mrrb_unlock(mrrb, lock) < 0) {
    return -1;
  }
  return 0;
}

/**
 * @brief Disable an MRRB reader.
 *
 * @param mrrb The MRRB the reader belongs to.
 * @param reader The Reader to be disabled.
 * @return 0 if the reader was disabled successfully,
 *         -1 is the reader could not be disabled.
 */
int mrrb_reader_disable(multi_reader_ring_buffer_t *mrrb, ring_buffer_reader_t *reader) {
  // Check the arguments
  if (mrrb == NULL || reader == NULL) {
    return -1;
  }

  // Try to acquire lock to modify mrrb
  int lock = _mrrb_lock(mrrb);
  if (lock < 0) {
    return -1;
  }

  // Disable the reader. Remaining state of the reader is automatically invalid
  reader->status = MRRB_READER_STATUS_DISABLED;

  // Unlock mrrb
  if (_mrrb_unlock(mrrb, lock) < 0) {
    return -1;
  }
  return 0;
}

/**
 * @brief Initialize a Multiple Reader Ring Buffer.
 *
 * @param mrrb The MRRB to be initialized.
 * @param buffer A pointer to memory the ring buffer may use.
 * @param buffer_length The length of the buffer in Bytes.
 * @param readers An Array of readers that read from the ring buffer.
 * @param num_readers The number of readers, i.e. the length of 'readers'.
 * @return 0 if the reader is initialized successfully,
 *         -1 if a problem occurred during initialization.
 *
 * @note All readers must be initialized individually using @ref mrrb_reader_init
 *       before calling any MRRB function other than the init function.
 */
int mrrb_init(multi_reader_ring_buffer_t *mrrb,
               unsigned char *buffer,
               const unsigned int buffer_length,
               ring_buffer_reader_t readers[],
               const unsigned int num_readers) {
  // Check the arguments
  if (mrrb == NULL || buffer == NULL || buffer_length == 0 || readers == NULL || num_readers == 0) {
    return -1;
  }

  // Initialize the MRRB structure
  mrrb->buffer = buffer;
  mrrb->buffer_length = buffer_length;
  mrrb->readers = readers;
  mrrb->num_readers = num_readers;
  mrrb->write_ptr = mrrb->buffer;
  mrrb->reservation_ptr = mrrb->buffer;
  mrrb->ongoing_writes = 0;

#if MRRB_USE_MUTEX
  // Initialize Mutex
  if (port_lock_init(&mrrb->mutex) < 0) {
    return -1;
  }
#endif /* MRRB_USE_MUTEX */

  // Initialize the readers
  for (unsigned int i = 0; i < mrrb->num_readers; i++) {
    mrrb->readers[i].status = MRRB_READER_STATUS_IDLE;
    mrrb->readers[i].read_ptr = mrrb->buffer;
    mrrb->readers[i].read_complete_ptr = mrrb->buffer;
    mrrb->readers[i].is_full = 0;
  }

  return 0;
}

/**
 * @brief De-initialize a MRRB.
 *
 * @param mrrb The MRRB to be de-initialized.
 * @return 0 if the MRRB was de-initialized successfully,
 *         -1 if an error occured during de-initialization.
 */
int mrrb_deinit(multi_reader_ring_buffer_t *mrrb) {
  // Check the arguments
  if (mrrb == NULL) {
    return -1;
  }

  int sts = 0;
  // De-Initialize Mutex
#if MRRB_USE_MUTEX
  if (port_lock_deinit(&mrrb->mutex) < 0) {
    sts = -1;
  }
#endif /* MRRB_USE_MUTEX */
  return sts;
}

/**
 * @brief Check if the buffer of an MRRB is empty.
 *
 * @param mrrb The MRRB to be checked.
 * @return 1 if the MRRB is empty,
 *         0 if the MRRB is not empty
 *         -1 if an error occurred.
 *
 * @note This function is not thread safe. The state of the MRRB may change
 *       concurrently while the function is being executed and thus the
 *       result of this function may not accurately reflect the state of
 *       the MRRB.
 */
char mrrb_is_empty(const multi_reader_ring_buffer_t *mrrb) {
  // Check the arguments
  if (mrrb == NULL) {
    return -1;
  }
  // Check if the full buffer is available
  return mrrb_get_remaining_space(mrrb) == mrrb->buffer_length;
}

/**
 * @brief Check if the buffer of an MRRB is full.
 *
 * @param mrrb The MRRB to be checked.
 * @return 1 if the MRRB is full,
 *         0 if the MRRB is not full,
 *         -1 if an error occurred.
 *
 * @note This function is not thread safe. The state of the MRRB may change
 *       concurrently while the function is being executed and thus the
 *       result of this function may not accurately reflect the state of
 *       the MRRB.
 */
char mrrb_is_full(const multi_reader_ring_buffer_t *mrrb) {
  // Check the arguments
  if (mrrb == NULL) {
    return -1;
  }
  // Check if any enabled reader is full
  for (unsigned int i = 0; i < mrrb->num_readers; i++) {
    if (mrrb->readers[i].status != MRRB_READER_STATUS_DISABLED && mrrb->readers[i].is_full) {
      return 1;
    }
  }
  return 0;
}

/**
 * @brief Get the remaining space in the buffer of an MRRB.
 *
 * @param mrrb The MRRB to be checked.
 * @return the number of Bytes that are available in the buffer.
 *
 * @note This function is not thread safe. The state of the MRRB may change
 *       concurrently while the function is being executed and thus the
 *       result of this function may not accurately reflect the state of
 *       the MRRB.
 */
unsigned int mrrb_get_remaining_space(const multi_reader_ring_buffer_t *mrrb) {
  // Check the arguments
  if (mrrb == NULL) {
    return 0;
  }
  unsigned int remaining_space = mrrb->buffer_length;
  unsigned int reader_remaining_space;
  // Get the minimum remaining space of all readers
  for (unsigned int i = 0; i < mrrb->num_readers; i++) {
    reader_remaining_space =
      _mrrb_reader_get_remaining_space(mrrb, mrrb->readers + i);
    if (reader_remaining_space < remaining_space) {
      remaining_space = reader_remaining_space;
    }
  }
  return remaining_space;
}

/**
 * @brief Write new data into the MRRB.
 *
 * @param mrrb The MRRB into which the data shall be written.
 * @param data A pointer to the data to be written.
 * @param data_length The number of Bytes to be written to the buffer.
 * @return The number of bytes actually written to the buffer, or
 *         -1 if an error occurred.
 */
int mrrb_write(multi_reader_ring_buffer_t *mrrb,
               const unsigned char *data,
               unsigned int data_length) {
  // Check arguments
  if (mrrb == NULL || data == NULL) {
    return -1;
  }

  unsigned int remaining_space, continuous_remaining_space;
  unsigned int write_length, continuous_write_length, spill_write_length, notification_length;
  unsigned char *write_pointer, *notification_pointer, *notification_end_pointer;
  int lock;
  unsigned char outstanding_readers[(mrrb->num_readers + 7) / 8];

  // Return immediately if data length is 0
  if (data_length == 0) {
    return 0;
  }

#if MRRB_ALLOW_WRITE_FROM_ISR == 0
  if (port_interrupt_active()) {
    return 0;
  }
#endif

  // Initialize the outstanding reader notifications
  memset(outstanding_readers, 0, (mrrb->num_readers + 7) / 8);

  // Try to acquire lock to modify mrrb
  lock = _mrrb_lock(mrrb);
  if (lock < 0) {
    return -1;
  }

  // Truncate data to available buffer space
  remaining_space = mrrb_get_remaining_space(mrrb);
  continuous_remaining_space = mrrb->buffer_length - (mrrb->reservation_ptr - mrrb->buffer);
  write_length = data_length > remaining_space ? remaining_space : data_length;

  // Copy current reservation pointer
  write_pointer = (unsigned char *) mrrb->reservation_ptr;

  // Compute write length and move reservation pointer
  if (write_length >= continuous_remaining_space) {
    // Buffer spill or exact buffer fill
    continuous_write_length = continuous_remaining_space;
    spill_write_length = write_length - continuous_write_length;
    mrrb->reservation_ptr = mrrb->buffer + spill_write_length;
  } else {
    // No buffer spill
    continuous_write_length = write_length;
    spill_write_length = 0;
    mrrb->reservation_ptr += write_length;
  }

  // Update full flag of readers
  for (unsigned int i = 0; i < mrrb->num_readers; i++) {
    ring_buffer_reader_t *reader = mrrb->readers + i;
    // Skip disabled readers
    if (reader->status == MRRB_READER_STATUS_DISABLED) continue;
    // Check if reader was filled with the ongoing write
    reader->is_full = (mrrb->reservation_ptr == reader->read_complete_ptr);
  }

  // Indicate ongoing write
  mrrb->ongoing_writes++;

  // Unlock mrrb
  if (_mrrb_unlock(mrrb, lock) < 0) {
    return -1;
  }

  // Copy write data into the reserved space
  if (continuous_write_length > 0) {
    memcpy(write_pointer, data, continuous_write_length);
  }
  if (spill_write_length > 0) {
    memcpy(mrrb->buffer, data + continuous_write_length, spill_write_length);
  }

  // Acquire lock again
  lock = _mrrb_lock(mrrb);
  if (lock < 0) {
    return -1;
  }

  // check if last ongoing write
  if (--mrrb->ongoing_writes == 0) {

    // compute the notification length
    if (mrrb->reservation_ptr > mrrb->write_ptr) {
      // All new data is in a continuous segment
      notification_length = mrrb->reservation_ptr - mrrb->write_ptr;
    } else {
      // Only issue notification for the continuous segment
      notification_length = mrrb->buffer_length - (mrrb->write_ptr - mrrb->buffer);
    }

    // Compute the notification pointers
    notification_pointer = (unsigned char *) mrrb->write_ptr;
    notification_end_pointer = notification_pointer + notification_length;
    if (notification_end_pointer == mrrb->buffer + mrrb->buffer_length) {
      notification_end_pointer = mrrb->buffer;
    }

    // Update the write pointer
    mrrb->write_ptr = mrrb->reservation_ptr;

    // Reserve readers
    for (unsigned int i = 0; i < mrrb->num_readers; i++) {
      ring_buffer_reader_t *reader = mrrb->readers + i;
      if (reader->status == MRRB_READER_STATUS_IDLE) {
        // Set the reader as active
        reader->status = MRRB_READER_STATUS_ACTIVE;
        // Update the read and read complete pointer
        reader->read_complete_ptr = notification_pointer;
        reader->read_ptr = notification_end_pointer;
        // Set the notification flag for this reader
        outstanding_readers[i / 8] |= 1 << (i % 8);
      }
    }

    // Unlock mrrb
    if (_mrrb_unlock(mrrb, lock) < 0) {
      return -1;
    }

    // Notify readers
    for (unsigned int i = 0; i < mrrb->num_readers; i++) {
      ring_buffer_reader_t *reader = mrrb->readers + i;
      if (outstanding_readers[i / 8] & (1 << (i % 8))) {
        // Notify the reader
        reader->notify_data(mrrb,
                            reader->handle,
                            notification_pointer,
                            notification_length);
      }
    }
  } else {
    // Unlock mrrb
    if (_mrrb_unlock(mrrb, lock) < 0) {
      return -1;
    }
  }

  // Return the amount of data added to the buffer
  return write_length;
}

/**
 * @brief Indicate that a reader finished reading the data that was passed to it.
 *
 * @param mrrb The MRRB which sent the data to the reader.
 * @param reader_handle The user handle if the reader, from which the reader is determined.
 */
void mrrb_read_complete(multi_reader_ring_buffer_t *mrrb, void *reader_handle) {
  int lock;
  unsigned int readable_data_length;
  ring_buffer_reader_t *reader;
  int re_start_reader = 0;

  // Check the arguments
  if (mrrb == NULL || reader_handle == NULL) {
    return;
  }

  // Get the reader by handle
  reader = _mrrb_get_reader_by_handle(mrrb, reader_handle);
  if (reader == NULL) {
    return;
  }

  // Try to acquire lock to modify mrrb
  lock = _mrrb_lock(mrrb);
  if (lock < 0) {
    return;
  }

  // Ignore complete if reader is disabled
  if (reader->status == MRRB_READER_STATUS_ACTIVE) {
    // Clear full flag
    reader->is_full = 0;
    // Update the read complete pointer
    reader->read_complete_ptr = reader->read_ptr;
    // Compute remaining length
    if (reader->read_ptr > mrrb->write_ptr) {
      readable_data_length = mrrb->buffer_length - (reader->read_ptr - mrrb->buffer) ;
    } else {
      readable_data_length = mrrb->write_ptr - reader->read_ptr;
    }
    // Check if more data is available
    if (readable_data_length > 0) {
      // Update the read pointer of the current reader
      reader->read_ptr += readable_data_length;
      if (reader->read_ptr == mrrb->buffer + mrrb->buffer_length) {
        reader->read_ptr = mrrb->buffer;
      }
      // Re-start the reader
      re_start_reader = 1;
    } else {
      // All data was read, set reader to idle
      reader->status = MRRB_READER_STATUS_IDLE;
    }
  }

  // Unlock mrrb
  _mrrb_unlock(mrrb, lock);

  // Re-start reader
  if (re_start_reader) {
    reader->notify_data(mrrb,
                        reader->handle,
                        (unsigned char *) reader->read_complete_ptr,
                        readable_data_length);
  }
}

/* Private functions ---------------------------------------------------------*/

unsigned int _mrrb_reader_get_remaining_space(const multi_reader_ring_buffer_t *mrrb,
                                              const ring_buffer_reader_t *reader) {
  // If reader is disabled, return full buffer length as avaliable
  if (reader->status == MRRB_READER_STATUS_DISABLED) {
    return mrrb->buffer_length;
  }
  // Check if buffer is full
  if (reader->is_full) {
    return 0;
  }
  // Compute difference between read complete and reservation pointer
  if (reader->read_complete_ptr > mrrb->reservation_ptr) {
    return reader->read_complete_ptr - mrrb->reservation_ptr;
  } else {
    return  mrrb->buffer_length - (mrrb->reservation_ptr - reader->read_complete_ptr);
  }
}

ring_buffer_reader_t *_mrrb_get_reader_by_handle(const multi_reader_ring_buffer_t *mrrb, void* handle) {
  // Find reader corresponding to the given handle
  ring_buffer_reader_t *owner = (ring_buffer_reader_t *) 0;
  for (unsigned int i = 0; i < mrrb->num_readers; i++) {
    ring_buffer_reader_t *reader = mrrb->readers + i;
    if (reader->handle == handle) {
      owner = reader;
      break;
    }
  }
  return owner;
}

inline int _mrrb_lock(multi_reader_ring_buffer_t *mrrb) {
  (void) mrrb;
#if MRRB_USE_MUTEX
  return port_lock(&mrrb->mutex);
#else /* MRRB_USE_MUTEX */
  return port_disable_interrupts();
#endif /* MRRB_USE_MUTEX */
}

inline int _mrrb_unlock(multi_reader_ring_buffer_t *mrrb, int lock) {
  (void) mrrb;
#if MRRB_USE_MUTEX
  return port_unlock(&mrrb->mutex);
#else /* MRRB_USE_MUTEX */
  return port_enable_interrupts(lock);
#endif /* MRRB_USE_MUTEX */
}

#ifdef __cplusplus
}
#endif
