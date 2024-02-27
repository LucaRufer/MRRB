/**
 * @file        mrrb.c
 * @brief       Multiple Reader Ring Buffer Implementation
 * @version     v0.2
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

unsigned int _mrrb_clear_overrun_space(multi_reader_ring_buffer_t *mrrb,
                                       unsigned int requested_space,
                                       unsigned char *reader_abort_flags);

unsigned char * _mrrb_advance_pointer(const multi_reader_ring_buffer_t *mrrb,
                                      volatile unsigned char *ptr,
                                      unsigned int len);

unsigned int _mrrb_reader_get_continuous_readable_space(const multi_reader_ring_buffer_t *mrrb,
                                                        const ring_buffer_reader_t *reader);
unsigned int _mrrb_reader_get_remaining_space(const multi_reader_ring_buffer_t *mrrb,
                                              const ring_buffer_reader_t *reader);
unsigned int _mrrb_reader_get_overwritable_space(const multi_reader_ring_buffer_t *mrrb,
                                                 const ring_buffer_reader_t *reader);
ring_buffer_reader_t *_mrrb_get_reader_by_handle(const multi_reader_ring_buffer_t *mrrb, void* handle);

int _mrrb_lock(multi_reader_ring_buffer_t *mrrb);
int _mrrb_unlock(multi_reader_ring_buffer_t *mrrb, int lock);

/* Private variables ---------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

/**
 * @brief Initialize a ring buffer reader.
 *
 * @param reader The reader to be initialized.
 * @param handle A pointer to a custom user handle to identify the reader.
 *               May be NULL.
 * @param overrun_policy
 *               The policy for the reader when it is overrun by new data (a
 *               writer tries to write new data, but not enough data can be
 *               allocated because a reader is still reading).
 *               The following policies are available:
 *               - MRRB_READER_OVERRUN_DISABLE:
 *                 Disable the reader when it is overrun.
 *               - MRRB_READER_OVERRUN_BLOCKING:
 *                 The reader blocks data from being overwritten.
 *               - MRRB_READER_OVERRUN_SKIP:
 *                 The @ref abort_data function of the reader is called, new
 *                 data for the writer is allocated, and the reader is
 *                 re-started from the oldest data that is not overwritten.
 *               Note: if MRRB_READER_OVERRUN_SKIP is selected,
 *               @ref abort_data may not be NULL.
 * @param notify_data
 *               A function that will be called by the MRRB when new data is
 *               available for the reader. The function has the MRRB, the
 *               handle, a pointer to data and the length of data as arguments.
 *               Every call of @ref notify_data requires the reader to call
 *               @ref mrrb_read_complete when all of the data was processed,
 *               or @ref mrrb_reader_disable to disable a reader.
 * @param abort_data
 *               A function that will be called by the MRRB when the current
 *               read should be aborted. The function has the MRRB and the
 *               reader handle as arguments.
 *               Every call of @ref abort_data requires the reader to call
 *               @ref mrrb_abort_complete when the read was aborted, or
 *               @ref mrrb_reader_disable to disable a reader.
 *               May be NULL if aborting is not used.
 * @return 0 if the reader is initialized successfully,
 *         -1 if an error occurred.
 */
int mrrb_reader_init(ring_buffer_reader_t *reader,
                     void* handle,
                     mrrb_reader_overrun_policy_t overrun_policy,
                     mrrb_reader_notify_data_t notify_data,
                     mrrb_reader_abort_data_t abort_data) {
  // Check the arguments
  if (reader == NULL ||
      notify_data == NULL ||
      (overrun_policy == MRRB_READER_OVERRUN_SKIP && abort_data == NULL)) {
    return -1;
  }

  // Initialize the reader
  reader->handle = handle;
  reader->overrun_policy = overrun_policy;
  reader->notify_data = notify_data;
  reader->abort_data = abort_data;
  reader->status = MRRB_READER_STATUS_IDLE;
  reader->is_full = 0;

  return 0;
}

/**
 * @brief De-initialize a ring buffer reader.
 *
 * @param reader The reader to be de-initialized.
 * @return 0 if the reader was de-initialized successfully,
 *         -1 if an error occurred during de-initialization.
 */
int mrrb_reader_deinit(ring_buffer_reader_t *reader) {
  // Check the arguments
  if (reader == NULL) {
    return -1;
  }

  int sts = 0;
  return sts;
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
  if (reader->status == MRRB_READER_STATUS_DISABLED ||
      reader->status == MRRB_READER_STATUS_DISABLING) {
    reader->status = MRRB_READER_STATUS_IDLE;
    reader->is_full = 0;
    reader->read_ptr = mrrb->reservation_ptr;
    reader->read_complete_ptr = mrrb->reservation_ptr;
  }

  // Unlock mrrb
  _mrrb_unlock(mrrb, lock);
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
  int abort_reader = 0;

  // Check the arguments
  if (mrrb == NULL || reader == NULL) {
    return -1;
  }

  // Try to acquire lock to modify mrrb
  int lock = _mrrb_lock(mrrb);
  if (lock < 0) {
    return -1;
  }

  // Check if the reader has a abort function set
  if (reader->status == MRRB_READER_STATUS_DISABLING ||
      reader->status == MRRB_READER_STATUS_DISABLED) {
    // Reader already disabled or disabled, nothing to do here
  } else if (reader->abort_data != NULL) {
    // Set the reader into a disabling state and abort any ongoing transfer
    reader->status = MRRB_READER_STATUS_DISABLING;
    abort_reader = 1;
  } else {
    // Disable the reader. Remaining state of the reader is automatically invalid
    reader->status = MRRB_READER_STATUS_DISABLED;
  }

  // Unlock mrrb
  _mrrb_unlock(mrrb, lock);

  // Abort the reader
  if (abort_reader) {
    reader->abort_data(mrrb, reader->handle);
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
    if (mrrb->readers[i].status == MRRB_READER_STATUS_ACTIVE && mrrb->readers[i].is_full) {
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
 * @brief Get the overwritable space in the buffer of an MRRB.
 *
 * @param mrrb The MRRB to be checked.
 * @return the number of Bytes that are available or overwritable in the buffer.
 *
 * @note This function is not thread safe. The state of the MRRB may change
 *       concurrently while the function is being executed and thus the
 *       result of this function may not accurately reflect the state of
 *       the MRRB.
 */
unsigned int mrrb_get_overwritable_space(const multi_reader_ring_buffer_t *mrrb) {
  // Check the arguments
  if (mrrb == NULL) {
    return 0;
  }
  unsigned int overwritable_space = mrrb->buffer_length;
  unsigned int reader_overwritable_space;
  // Get the minimum overwritable space of all readers
  for (unsigned int i = 0; i < mrrb->num_readers; i++) {
    reader_overwritable_space =
      _mrrb_reader_get_overwritable_space(mrrb, mrrb->readers + i);
    if (reader_overwritable_space < overwritable_space) {
      overwritable_space = reader_overwritable_space;
    }
  }
  return overwritable_space;
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

  unsigned int remaining_space, overwritable_space, requested_space, continuous_remaining_space;
  unsigned int write_length, continuous_write_length, spill_write_length;
  unsigned char *write_pointer;
  unsigned int readable_data_length;
  int lock;
  unsigned char reader_notification_flags[(mrrb->num_readers + 7) / 8];
  int abort_readers = 0;

  // Return immediately if data length is 0
  if (data_length == 0) {
    return 0;
  }

#if MRRB_ALLOW_WRITE_FROM_ISR == 0
  if (port_interrupt_active()) {
    return 0;
  }
#endif

  // Initialize the reader notification flags
  memset(reader_notification_flags, 0, sizeof(reader_notification_flags));

  // Try to acquire lock to modify mrrb
  lock = _mrrb_lock(mrrb);
  if (lock < 0) {
    return -1;
  }

  // Check if the requested length fits into the buffer
  remaining_space = mrrb_get_remaining_space(mrrb);
  if (data_length <= remaining_space) {
    write_length = data_length;
  } else {
    // Check if the requested length can be fulfilled with overwriting
    overwritable_space = mrrb_get_overwritable_space(mrrb);
    if (overwritable_space > remaining_space) {
      // Additional space can be gained by clearing some overrun readers
      requested_space = (data_length <= mrrb->buffer_length) ? data_length : mrrb->buffer_length;
      remaining_space = _mrrb_clear_overrun_space(mrrb, requested_space, reader_notification_flags);
      abort_readers = 1;
      write_length = (data_length <= remaining_space) ? data_length : remaining_space;
    } else {
      write_length = remaining_space;
    }
  }

  continuous_remaining_space = mrrb->buffer_length - (mrrb->reservation_ptr - mrrb->buffer);

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
    if (reader->status == MRRB_READER_STATUS_DISABLED ||
        reader->status == MRRB_READER_STATUS_DISABLING) continue;
    // Check if reader was filled with the ongoing write
    reader->is_full = (mrrb->reservation_ptr == reader->read_complete_ptr);
  }

  // Indicate ongoing write
  mrrb->ongoing_writes++;

  // Unlock mrrb
  if (_mrrb_unlock(mrrb, lock) < 0) {
    return -1;
  }

  // Abort readers if an overrun was encountered
  if (abort_readers) {
    for (unsigned int i = 0; i < mrrb->num_readers; i++) {
      ring_buffer_reader_t *reader = mrrb->readers + i;
      if (reader_notification_flags[i / 8] & (1 << (i % 8))) {
        reader->abort_data(mrrb, reader->handle);
      }
    }
    memset(reader_notification_flags, 0, sizeof(reader_notification_flags));
    abort_readers = 0;
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
    // Reserve readers
    for (unsigned int i = 0; i < mrrb->num_readers; i++) {
      ring_buffer_reader_t *reader = mrrb->readers + i;
      if (reader->status == MRRB_READER_STATUS_IDLE) {
        // Set the reader as active
        reader->status = MRRB_READER_STATUS_ACTIVE;
        // Update the read complete pointer
        reader->read_complete_ptr = mrrb->write_ptr;
        // Set the notification flag for this reader
        reader_notification_flags[i / 8] |= 1 << (i % 8);
      } else if (reader->status == MRRB_READER_STATUS_ABORTED) {
        // Set the reader as active
        reader->status = MRRB_READER_STATUS_ACTIVE;
        // Set the notification flag for this reader
        reader_notification_flags[i / 8] |= 1 << (i % 8);
      }
    }

    // Update the write pointer
    mrrb->write_ptr = mrrb->reservation_ptr;

    // Unlock mrrb
    _mrrb_unlock(mrrb, lock);

    // Notify readers
    for (unsigned int i = 0; i < mrrb->num_readers; i++) {
      ring_buffer_reader_t *reader = mrrb->readers + i;
      if (reader_notification_flags[i / 8] & (1 << (i % 8))) {
        // Compute the available data for the reader
        readable_data_length = _mrrb_reader_get_continuous_readable_space(mrrb, reader);
        reader->read_ptr = _mrrb_advance_pointer(mrrb, reader->read_complete_ptr, readable_data_length);
        // Notify the reader
        reader->notify_data(mrrb,
                            reader->handle,
                            (unsigned char *) reader->read_complete_ptr,
                            readable_data_length);
      }
    }
  } else {
    _mrrb_unlock(mrrb, lock);
  }

  // Return the amount of data added to the buffer
  return write_length;
}

/**
 * @brief Indicate that a reader finished reading the data that was passed to it.
 *
 * @param mrrb The MRRB which sent the data to the reader.
 * @param reader_handle The user handle of the reader, from which the reader is
 *                      determined.
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

  // Ignore complete if reader is not active
  if (reader->status == MRRB_READER_STATUS_ACTIVE) {
    // Clear full flag
    reader->is_full = 0;
    // Update the read complete pointer
    reader->read_complete_ptr = reader->read_ptr;
    // Compute remaining length
    readable_data_length = _mrrb_reader_get_continuous_readable_space(mrrb, reader);
    // Check if more data is available
    if (readable_data_length > 0) {
      // Update the read pointer of the current reader
      reader->read_ptr = _mrrb_advance_pointer(mrrb, reader->read_complete_ptr, readable_data_length);
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

/**
 * @brief Indicate that a reader finished aborting the read process.
 *
 * @param mrrb The MRRB which caused the reader to abort.
 * @param reader_handle The user handle of the reader, from which the reader
 *                      is determined.
 */
void mrrb_abort_complete(multi_reader_ring_buffer_t *mrrb,
                         void *reader_handle) {
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

  // Check if the reader is supposed to be disabled
  if (reader->status == MRRB_READER_STATUS_DISABLING) {
    // Disabling the reader is complete
    reader->status = MRRB_READER_STATUS_DISABLED;
  } else if (reader->status == MRRB_READER_STATUS_ABORTING) {
    // Prepare reader restart, compute remaining length
    readable_data_length = _mrrb_reader_get_continuous_readable_space(mrrb, reader);
    // Check if more data is available and the MRRB is not currently being written
    if (readable_data_length > 0 && mrrb->ongoing_writes == 0) {
      // Update the read pointer of the current reader.
      // The read_complete pointere was updated when the reader was set into the aborting state
      reader->read_ptr = _mrrb_advance_pointer(mrrb, reader->read_complete_ptr, readable_data_length);
      // Re-start the reader
      re_start_reader = 1;
      reader->status = MRRB_READER_STATUS_ACTIVE;
    } else {
      // All data was read, set reader to idle
      reader->status = MRRB_READER_STATUS_ABORTED;
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

// NOTE: mrrb must be locked when this function is called
unsigned int _mrrb_clear_overrun_space(multi_reader_ring_buffer_t *mrrb,
                                       unsigned int requested_space,
                                       unsigned char *reader_abort_flags) {
  unsigned int clear_space = mrrb->buffer_length;
  unsigned int reader_clear_space;

  // Iterate over all readers and clear attempt to clear the requested space
  for (unsigned int i = 0; i < mrrb->num_readers; i++) {
    ring_buffer_reader_t *reader = mrrb->readers + i;

    // Skip clearing of diabled or idle readers
    if (reader->status == MRRB_READER_STATUS_DISABLED ||
        reader->status == MRRB_READER_STATUS_DISABLING ||
        reader->status == MRRB_READER_STATUS_IDLE) continue;

    reader_clear_space = _mrrb_reader_get_remaining_space(mrrb, reader);
    // Check if this reader needs to be cleared
    if (reader_clear_space < requested_space) {
      switch (reader->overrun_policy) {
      case MRRB_READER_OVERRUN_BLOCKING:
        // Policy Blocking: Do not clear on overrun. This code should be unreachable.
        break;
      case MRRB_READER_OVERRUN_DISABLE:
        // Policy Disable: Disable reader
        if (reader->abort_data != NULL) {
          // Set the reader into a disabling state and abort any ongoing transfer
          reader->status = MRRB_READER_STATUS_DISABLING;
          reader_abort_flags[i / 8] |= 1 << (i % 8);
        } else {
          // Disable the reader. Remaining state of the reader is automatically invalid
          reader->status = MRRB_READER_STATUS_DISABLED;
        }
        reader_clear_space = mrrb->buffer_length;
        break;
      case MRRB_READER_OVERRUN_SKIP:
        // Policy Skip: Abort the reader
        if (reader->status == MRRB_READER_STATUS_ACTIVE) {
          reader->status = MRRB_READER_STATUS_ABORTING;
          reader_abort_flags[i / 8] |= 1 << (i % 8);
          // mark current read as complete (as we don't know how far it progressed already)
          reader->read_complete_ptr = reader->read_ptr;
          reader->is_full = 0;
        }
        reader_clear_space = _mrrb_reader_get_remaining_space(mrrb, reader);
        if (reader_clear_space < requested_space) {
          // Need to skip over even more date to get enough space
          reader->read_complete_ptr = _mrrb_advance_pointer(mrrb,
                                                            reader->read_complete_ptr,
                                                            (requested_space - reader_clear_space));
          reader_clear_space = requested_space;
        }
        reader->is_full = (reader_clear_space == requested_space);
        break;
      }
    }

    // Update the value for the clear space
    if (reader_clear_space < clear_space) {
      clear_space = reader_clear_space;
    }
  }
  return clear_space;
}

inline unsigned char *_mrrb_advance_pointer(const multi_reader_ring_buffer_t *mrrb,
                                            volatile unsigned char *ptr,
                                            unsigned int len) {
  if ((unsigned int) (ptr - mrrb->buffer) < mrrb->buffer_length - len) {
    return (unsigned char *) ptr + len;
  } else {
    return (unsigned char *) ptr - (mrrb->buffer_length - len);
  }
}

inline unsigned int _mrrb_reader_get_continuous_readable_space(
  const multi_reader_ring_buffer_t *mrrb,
  const ring_buffer_reader_t *reader)
{
  unsigned char *write_ptr = (unsigned char *) mrrb->write_ptr;
  unsigned char *read_complete_ptr = (unsigned char *) reader->read_complete_ptr;
  fence();
  if (read_complete_ptr > write_ptr || reader->is_full) {
    return mrrb->buffer_length - (read_complete_ptr - mrrb->buffer);
  } else {
    return write_ptr - read_complete_ptr;
  }
}

unsigned int _mrrb_reader_get_remaining_space(const multi_reader_ring_buffer_t *mrrb,
                                              const ring_buffer_reader_t *reader) {
  // If reader is disabled, return full buffer length as available
  if (reader->status == MRRB_READER_STATUS_DISABLED ||
      reader->status == MRRB_READER_STATUS_DISABLING) {
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

unsigned int _mrrb_reader_get_overwritable_space(const multi_reader_ring_buffer_t *mrrb,
                                                 const ring_buffer_reader_t *reader) {
  // Check the overrun policy of the reader
  if (reader->overrun_policy == MRRB_READER_OVERRUN_BLOCKING) {
    return _mrrb_reader_get_remaining_space(mrrb, reader);
  } else {
    return mrrb->buffer_length;
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
  fence();
}

inline int _mrrb_unlock(multi_reader_ring_buffer_t *mrrb, int lock) {
  (void) mrrb;
  fence();
#if MRRB_USE_MUTEX
  return port_unlock(&mrrb->mutex);
#else /* MRRB_USE_MUTEX */
  return port_enable_interrupts(lock);
#endif /* MRRB_USE_MUTEX */
}

#ifdef __cplusplus
}
#endif
