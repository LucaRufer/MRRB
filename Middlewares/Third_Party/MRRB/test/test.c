/**
 * @file        test.c
 * @brief       Tests for MRRB (UNIX version)
 * @version     v0.2
 *
 * @author      Luca Rufer, luca.rufer@swissloop.ch
 * @date        05.12.2023
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

// Std libraries
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// MRRB
#include "mrrb.h"

// Test framework
#include "unity.h"

/* Private defines -----------------------------------------------------------*/

// Timeout macros
#define TEST_TIMEOUT_S 1

// Multi-test definitions
#define TEST_MRRB_BUFFER_LENGTH 128
#define TEST_MRRB_MAX_READERS 25
#define TEST_MRRB_MAX_WRITERS 5
#define TEST_TEXT_LEN 450
#define TEST_MULTI_WRITE_READERS 8
#define TEST_MULTI_WRITE_CONSEC_WRITES 5
#define TEST_MULTI_WRITE_DATA_AMOUNT 1000
#define TEST_MULTI_WRITE_MAX_DATA_SIZE 15

// Other definitions
#define READER_OVERRUN_POLICY_COUNT 3

/* Exported macros -----------------------------------------------------------*/

#define TEST_MRRB_IS_FULL( mrrb ) \
  TEST_ASSERT_EQUAL_UINT(0, mrrb_get_remaining_space(mrrb)); \
  TEST_ASSERT_TRUE(mrrb_is_full(mrrb)); \
  TEST_ASSERT_FALSE(mrrb_is_empty(mrrb))

#define TEST_MRRB_IS_NOT_FULL( mrrb ) \
  TEST_ASSERT_NOT_EQUAL_UINT(0, mrrb_get_remaining_space(mrrb)); \
  TEST_ASSERT_FALSE(mrrb_is_full(mrrb))

#define TEST_MRRB_IS_EMPTY( mrrb ) \
  TEST_ASSERT_EQUAL_UINT(TEST_MRRB_BUFFER_LENGTH, mrrb_get_remaining_space(mrrb)); \
  TEST_ASSERT_FALSE(mrrb_is_full(mrrb)); \
  TEST_ASSERT_TRUE(mrrb_is_empty(mrrb))

#define TEST_MRRB_IS_NOT_EMPTY( mrrb ) \
  TEST_ASSERT_NOT_EQUAL_UINT(TEST_MRRB_BUFFER_LENGTH, mrrb_get_remaining_space(mrrb)); \
  TEST_ASSERT_FALSE(mrrb_is_empty(mrrb))

#define TEST_MRRB_FILL_LEVEL( mrrb, fill_level) \
  TEST_ASSERT_EQUAL_UINT(TEST_MRRB_BUFFER_LENGTH - fill_level, mrrb_get_remaining_space(mrrb)); \
  TEST_ASSERT(mrrb_is_full(mrrb) == (fill_level == TEST_MRRB_BUFFER_LENGTH)); \
  TEST_ASSERT(mrrb_is_empty(mrrb) == (fill_level == 0))

// Utility Macros
#define ARRAY_LENGTH( array ) (sizeof(array) / sizeof(array[0]))
#define SUM( array ) _sum(array, ARRAY_LENGTH(array))
#define SUM2D( array ) _sum2d(ARRAY_LENGTH(array), ARRAY_LENGTH(array[0]), array)

/* Private typedef -----------------------------------------------------------*/

typedef enum read_state_type_e {
  READER_STATE_IMMEDIATE,
  READER_STATE_TRIGGERED
} read_state_type_t;

typedef enum enable_type_e {
  ENABLE_TYPE_ALWAYS_ENABLED,
  ENABLE_TYPE_INITIALLY_ENABLED,
  ENABLE_TYPE_INITIALLY_DISABLED,
  ENABLE_TYPE_ALWAYS_DISABLED,
} enable_type_t;

typedef struct read_state_s {
  read_state_type_t read_type;
  const unsigned int *write_data_lengths;
  unsigned int num_writes;
  unsigned int iteration;
  unsigned int data_received;
  unsigned int split_remaining_data;
  unsigned int outstanding_read_trigger; // Optional
  unsigned int outstanding_abort_trigger;// Optional
  enable_type_t enable_type; // Optional
} read_state_t;

typedef struct multi_write_header_s {
  unsigned int thread_num;
  unsigned int data_length;
} multi_write_header_t;

typedef struct multi_write_read_state_s {
  unsigned int reader_number;
  unsigned int reader_progress[TEST_MULTI_WRITE_READERS];
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  unsigned int seed;
  unsigned int outstanding_completion;
  multi_write_header_t partial_header;
  unsigned int remaining_header_bytes;
  unsigned int remaining_data_bytes;
} multi_write_read_state_t;

typedef struct multi_write_shared_write_state_s {
  pthread_mutex_t mutex;
} multi_write_shared_write_state_t;

typedef struct multi_write_write_state_s {
  multi_write_shared_write_state_t *shared_state;
  unsigned int writer_number;
  unsigned int data_sent;
  unsigned int seed;
} multi_write_write_state_t;

/* Private function prototypes -----------------------------------------------*/

// Read functions
void read_ignore(multi_reader_ring_buffer_t *mrrb,
                 void *handle,
                 const unsigned char *data,
                 unsigned int data_length);
void swsr_immediate_read(multi_reader_ring_buffer_t *mrrb,
                         void *handle,
                         const unsigned char *data,
                         unsigned int data_length);
void swsr_immediate_read_port_failure(multi_reader_ring_buffer_t *mrrb,
                                      void *handle,
                                      const unsigned char *data,
                                      unsigned int data_length);
void swsr_triggered_read(multi_reader_ring_buffer_t *mrrb,
                         void *handle,
                         const unsigned char *data,
                         unsigned int data_length);
void overrun_triggered_read(multi_reader_ring_buffer_t *mrrb,
                            void *handle,
                            const unsigned char *data,
                            unsigned int data_length);
void multi_write_reader_read(multi_reader_ring_buffer_t *mrrb,
                             void *handle,
                             const unsigned char *data,
                             unsigned int data_length);
void multi_write_reader_check_data(multi_write_read_state_t *state,
                                   const unsigned char *data,
                                   unsigned int data_length);

// Abort functions
void abort_ignore(multi_reader_ring_buffer_t *mrrb, void *handle);
void abort_immediate(multi_reader_ring_buffer_t *mrrb, void *handle);
void abort_triggered(multi_reader_ring_buffer_t *mrrb, void *handle);

// Trigger functions
void swsr_triggered_read_trigger(multi_reader_ring_buffer_t *mrrb, void *handle);
void triggered_abort_trigger(multi_reader_ring_buffer_t *mrrb, void *handle);

// Multi-write threads
void *multi_write_writer_thread(void *args);
void *multi_write_reader_thread(void *args);

// Test functions
void test_write_setup(void);
void test_illegal_arguments(void);
void test_single_write_single_read_immediate(void);
void test_single_write_single_read_immediate_port_failure(void);
void test_single_write_single_read_after(void);
void test_consec_write_single_read_after(void);
void test_single_write_multiple_read(void);
void test_overrun(void);
void test_multiple_write_multiple_read(void);

// Misc functions
void *timeout_thread_function(void *args);
unsigned int _sum(const unsigned int array[], const unsigned int length);
unsigned int _sum2d(const unsigned int outer_length,
                    const unsigned int inner_length,
                    const unsigned int array[outer_length][inner_length]);
void _custom_test_abort(void);

// Port mock functions
static void port_mock_fail_next_lock_init(void);
static void port_mock_fail_next_lock_deinit(void);
static void port_mock_fail_nth_lock(int n);
static void port_mock_fail_nth_unlock(int n);
static void port_mock_show_as_interrupt_active(int is_interrupt);

/* Private variables ---------------------------------------------------------*/

// Test setup variables
pthread_t timeout_thread;
pthread_mutex_t timeout_mutex;
pthread_cond_t timeout_cond;
volatile int timeout_ready;

// Multi-test variables
multi_reader_ring_buffer_t mrrb;
ring_buffer_reader_t readers[TEST_MRRB_MAX_READERS];
unsigned char mrrb_buffer[TEST_MRRB_BUFFER_LENGTH];
const unsigned char test_text[TEST_TEXT_LEN] =
"The quick brown fox jumps over the lazy dog, but the lazy dog was too lazy to "
"care about the quick brown fox. Meanwhile, a mischievous squirrel laughed at t"
"he entire situation from a nearby tree, contemplating its next prank involving"
" acorns and unsuspecting passersby. The sun shone brightly, illuminating the p"
"icturesque scene as a curious cat tiptoed in, hoping to join the playful chaos"
" but ended up taking a nap amidst the commotion. -- CHAT GPT";

const unsigned int single_write_data_lengths[] =
  {1, 2, 5, 15, TEST_MRRB_BUFFER_LENGTH - 23, TEST_MRRB_BUFFER_LENGTH, 59, TEST_MRRB_BUFFER_LENGTH};

const unsigned int multi_write_data_lengths[][TEST_MULTI_WRITE_CONSEC_WRITES] = {
  {3, 5},                                           // 2 consec. write
  {1, 2, 3, 4, TEST_MRRB_BUFFER_LENGTH - 8 - 10},   // Fill to buffer edge
  {5, 10, 15, 20, TEST_MRRB_BUFFER_LENGTH - 50},    // Exact buffer fill, edge to edge
  {5, 7, 11, 13, 17},                               // Partially fill the buffer
  {9, 8, 7, 6, TEST_MRRB_BUFFER_LENGTH - 30},       // Exact buffer fill
};

// Read function table
mrrb_reader_notify_data_t read_fn_table[] = {
  swsr_immediate_read,
  swsr_triggered_read,
};

mrrb_reader_notify_data_t overrun_read_fn_table[] = {
  swsr_immediate_read,
  overrun_triggered_read,
};

// Abort function table
mrrb_reader_abort_data_t abort_fn_table[] = {
  NULL,
  abort_immediate,
  abort_triggered,
};

// Port mock variables
int _port_fail_next_lock_init = 0;
int _port_fail_next_lock_deinit = 0;
int _port_fail_nth_lock = 0;
int _port_fail_nth_unlock = 0;
int _port_show_as_interrupt = 0;

/* Exported functions --------------------------------------------------------*/

int main() {
  // Start Testing
  UNITY_BEGIN();

  RUN_TEST(test_write_setup);
  RUN_TEST(test_illegal_arguments);
  RUN_TEST(test_single_write_single_read_immediate);
  RUN_TEST(test_single_write_single_read_immediate_port_failure);
  RUN_TEST(test_single_write_single_read_after);
  RUN_TEST(test_consec_write_single_read_after);
  RUN_TEST(test_single_write_multiple_read);
  RUN_TEST(test_overrun);
  for (unsigned int i = 0; i < 10; i++) {
    RUN_TEST(test_multiple_write_multiple_read);
  }

  // End Testing
  return UNITY_END();
}

void setUp() {
  // Clear the buffer
  memset(mrrb_buffer, 0, TEST_MRRB_BUFFER_LENGTH);

  // Create a timeout thread
  int timeout = 1000;
  timeout_ready = 0;
  TEST_ASSERT_EQUAL_INT(0, pthread_mutex_init(&timeout_mutex, NULL));
  TEST_ASSERT_EQUAL_INT(0, pthread_cond_init(&timeout_cond, NULL));
  TEST_ASSERT_EQUAL_INT(0, pthread_create(&timeout_thread, NULL, timeout_thread_function, &timeout));
  while(!timeout_ready) sched_yield();
}

void tearDown() {
  // Join the timeout thread
  TEST_ASSERT_EQUAL_INT(0, pthread_mutex_lock(&timeout_mutex));
  TEST_ASSERT_EQUAL_INT(0, pthread_cond_signal(&timeout_cond));
  TEST_ASSERT_EQUAL_INT(0, pthread_mutex_unlock(&timeout_mutex));
  TEST_ASSERT_EQUAL_INT(0, pthread_join(timeout_thread, NULL));
  TEST_ASSERT_EQUAL_INT(0, pthread_mutex_destroy(&timeout_mutex));
  TEST_ASSERT_EQUAL_INT(0, pthread_cond_destroy(&timeout_cond));
}

/* Private functions ---------------------------------------------------------*/

void read_ignore(multi_reader_ring_buffer_t *mrrb,
                 void *handle,
                 const unsigned char *data,
                 unsigned int data_length) {
  // Check arguments
  TEST_ASSERT_NOT_NULL(mrrb);
  TEST_ASSERT_NOT_NULL(handle);
  TEST_ASSERT_NOT_NULL(data);
  TEST_ASSERT_GREATER_THAN_INT(0, data_length);
}

void abort_ignore(multi_reader_ring_buffer_t *mrrb, void *handle) {
  // Check arguments
  TEST_ASSERT_NOT_NULL(mrrb);
  TEST_ASSERT_NOT_NULL(handle);
}

void swsr_immediate_read(multi_reader_ring_buffer_t *mrrb,
                         void *handle,
                         const unsigned char *data,
                         unsigned int data_length) {
  // Check arguments
  TEST_ASSERT_NOT_NULL(mrrb);
  TEST_ASSERT_NOT_NULL(handle);
  TEST_ASSERT_NOT_NULL(data);
  TEST_ASSERT_GREATER_THAN_INT(0, data_length);

  // Cast handle
  read_state_t *state = (read_state_t *) handle;
  TEST_ASSERT_LESS_THAN_INT(state->num_writes, state->iteration);

  // Check all data is transmitted
  if (state->split_remaining_data > 0) {
    // The last notification was smaller than expected, expect the rest now
    TEST_ASSERT_EQUAL_UINT(state->split_remaining_data, data_length);
    TEST_ASSERT_EQUAL_MEMORY(test_text + state->data_received, data, data_length);
    state->split_remaining_data = 0;
  } else if (state->write_data_lengths[state->iteration] != data_length){
    // Not all data arrived. Check the arrived data and mark the remainder as split
    TEST_ASSERT_EQUAL_MEMORY(test_text + state->data_received, data, data_length);
    state->split_remaining_data = state->write_data_lengths[state->iteration] - data_length;
  } else {
    // All expected data arrived.
    TEST_ASSERT_EQUAL_MEMORY(test_text + state->data_received, data, data_length);
  }

  // Increment test state
  state->data_received += data_length;
  if (state->split_remaining_data == 0) {
    state->iteration++;
  }

  // Call data complete
  mrrb_read_complete(mrrb, handle);
}

void swsr_immediate_read_port_failure(multi_reader_ring_buffer_t *mrrb,
                                      void *handle,
                                      const unsigned char *data,
                                      unsigned int data_length) {
  TEST_FAIL();
}

void swsr_triggered_read(multi_reader_ring_buffer_t *mrrb,
                         void *handle,
                         const unsigned char *data,
                         unsigned int data_length) {
  // Check arguments
  TEST_ASSERT_NOT_NULL(mrrb);
  TEST_ASSERT_NOT_NULL(handle);
  TEST_ASSERT_NOT_NULL(data);
  TEST_ASSERT_GREATER_THAN_INT(0, data_length);

  // Cast handle
  read_state_t *state = (read_state_t *) handle;
  TEST_ASSERT_LESS_THAN_INT(state->num_writes, state->iteration);

  // Check all data is transmitted
  if (state->split_remaining_data > 0) {
    // The last notification was smaller than expected, expect the rest now
    TEST_ASSERT_EQUAL_UINT(state->split_remaining_data, data_length);
    TEST_ASSERT_EQUAL_MEMORY(test_text + state->data_received, data, data_length);
    state->split_remaining_data = 0;
  } else if (data_length < state->write_data_lengths[state->iteration]){
    // Not all data arrived. Check the arrived data and mark the remainder as split
    TEST_ASSERT_EQUAL_MEMORY(test_text + state->data_received, data, data_length);
    state->split_remaining_data = state->write_data_lengths[state->iteration] - data_length;
  } else {
    // All expected data arrived.
    TEST_ASSERT_EQUAL_UINT(state->write_data_lengths[state->iteration], data_length);
    TEST_ASSERT_EQUAL_MEMORY(test_text + state->data_received, data, data_length);
  }

  // Increment test state
  state->data_received += data_length;
  if (state->split_remaining_data == 0) {
    state->iteration++;
  }
  state->outstanding_read_trigger++;
}

void overrun_triggered_read(multi_reader_ring_buffer_t *mrrb,
                          void *handle,
                          const unsigned char *data,
                          unsigned int data_length) {
  // Check arguments
  TEST_ASSERT_NOT_NULL(mrrb);
  TEST_ASSERT_NOT_NULL(handle);
  TEST_ASSERT_NOT_NULL(data);
  TEST_ASSERT_GREATER_THAN_INT(0, data_length);

  // Cast handle
  read_state_t *state = (read_state_t *) handle;

  // Check all data is transmitted
  TEST_ASSERT_EQUAL_MEMORY(test_text + state->data_received, data, data_length);

  // Increment test state
  state->data_received += data_length;
  state->outstanding_read_trigger++;
}

void abort_immediate(multi_reader_ring_buffer_t *mrrb, void *handle) {
  // Check arguments
  TEST_ASSERT_NOT_NULL(mrrb);
  TEST_ASSERT_NOT_NULL(handle);
  // Cast handle
  mrrb_abort_complete(mrrb, handle);
}

void abort_triggered(multi_reader_ring_buffer_t *mrrb, void *handle) {
  // Check arguments
  TEST_ASSERT_NOT_NULL(mrrb);
  TEST_ASSERT_NOT_NULL(handle);
  // Cast handle
  read_state_t *state = (read_state_t *) handle;
  // Increase outstanding abort triggers
  state->outstanding_abort_trigger++;
  // Call data complete
}

void swsr_triggered_read_trigger(multi_reader_ring_buffer_t *mrrb, void *handle) {
  // Check arguments
  TEST_ASSERT_NOT_NULL(mrrb);
  TEST_ASSERT_NOT_NULL(handle);
  // Cast handle
  read_state_t *state = (read_state_t *) handle;

  // Reduce outstanding read triggers
  TEST_ASSERT_GREATER_THAN(0, state->outstanding_read_trigger--);
  // Call data complete
  mrrb_read_complete(mrrb, handle);
}

void triggered_abort_trigger(multi_reader_ring_buffer_t *mrrb, void *handle) {
  // Check arguments
  TEST_ASSERT_NOT_NULL(mrrb);
  TEST_ASSERT_NOT_NULL(handle);
  // Cast handle
  read_state_t *state = (read_state_t *) handle;
  // Reduce outstanding abort triggers
  TEST_ASSERT_GREATER_THAN(0, state->outstanding_abort_trigger--);
  // Call data complete
  mrrb_abort_complete(mrrb, handle);
}

void *multi_write_writer_thread(void *args) {
  // Check the arguments
  TEST_ASSERT_NOT_NULL(args);
  multi_write_write_state_t *state = (multi_write_write_state_t *) args;
  state->data_sent = 0;

  // Create a buffer for the write message and compute header and data pointers
  unsigned char write_msg[sizeof(multi_write_header_t) + TEST_MULTI_WRITE_MAX_DATA_SIZE];
  multi_write_header_t *header = (multi_write_header_t *) write_msg;
  unsigned char *data = write_msg + sizeof(multi_write_header_t);

  // Initialize header
  header->thread_num = state->writer_number;

  while (state->data_sent < TEST_MULTI_WRITE_DATA_AMOUNT) {
    // Pre-fill the buffer
    for (unsigned int i = 0; i < TEST_MULTI_WRITE_MAX_DATA_SIZE; i++) {
      data[i] = (unsigned char) (state->data_sent + i);
    }

    // Compute maximum writable data while ensuring no buffer overflow
    unsigned int max_sendable_data = mrrb_get_remaining_space(&mrrb);

    // Loop until at least one byte is available
    while (max_sendable_data < (sizeof(multi_write_header_t) + TEST_MULTI_WRITE_MAX_DATA_SIZE) * TEST_MRRB_MAX_WRITERS) {
      // Yield and wait for other threads to release space
      sched_yield();
      // Poll the remaining space again
      max_sendable_data = mrrb_get_remaining_space(&mrrb);
    }

    // Subtract the header
    max_sendable_data -= sizeof(multi_write_header_t);

    // Limit to max write size
    if (max_sendable_data > TEST_MULTI_WRITE_MAX_DATA_SIZE) {
      max_sendable_data = TEST_MULTI_WRITE_MAX_DATA_SIZE;
    }

    // Limit to remaining data
    if (max_sendable_data > TEST_MULTI_WRITE_DATA_AMOUNT - state->data_sent) {
      max_sendable_data = TEST_MULTI_WRITE_DATA_AMOUNT - state->data_sent;
    }

    // Select a random amount of data to be written to the buffer
    header->data_length = (rand_r(&state->seed) % max_sendable_data) + 1;
    TEST_ASSERT_GREATER_THAN_UINT(0, header->data_length);
    TEST_ASSERT_LESS_OR_EQUAL_UINT(TEST_MULTI_WRITE_MAX_DATA_SIZE, header->data_length);
    TEST_ASSERT_LESS_OR_EQUAL_UINT(TEST_MULTI_WRITE_DATA_AMOUNT - state->data_sent, header->data_length);

    // Send the data to the buffer
    TEST_ASSERT_EQUAL_INT((int) (header->data_length + sizeof(multi_write_header_t)),
                          mrrb_write(&mrrb, write_msg, header->data_length + sizeof(multi_write_header_t)));

    // Increase the data sent
    state->data_sent += header->data_length;
  }

  // Sending complete
  return NULL;
}

void *multi_write_reader_thread(void *args) {
  // Check arguments
  TEST_ASSERT_NOT_NULL(args);

  // Cast arguments
  multi_write_read_state_t *state = (multi_write_read_state_t *) args;
  int exit = 0;

  // Enter the event loop
  TEST_ASSERT_EQUAL_INT(0, pthread_mutex_lock(&state->mutex));
  while (!exit) {
    if (state->outstanding_completion == 0) {
      // Wait for the thread so be signaled
      TEST_ASSERT_EQUAL_INT(0, pthread_cond_wait(&state->cond, &state->mutex));
    }

    // Check if condition was signaled from the main thread
    exit = (state->outstanding_completion == 0);
    for (unsigned int i = 0; i < TEST_MRRB_MAX_WRITERS && exit; i++) {
      exit = (state->reader_progress[i] == TEST_MULTI_WRITE_DATA_AMOUNT);
    }
    if (exit) break;

    // After getting the signal, sleep for a random amount before completing the read
    sleep((rand_r(&state->seed) % 10) / 1000.0);

    // Mark the read as completed
    state->outstanding_completion--;

    // Release the mutex while completing the read to prevent a self-deadlock
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_unlock(&state->mutex));
    mrrb_read_complete(&mrrb, state);
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_lock(&state->mutex));

    // Check if reader is complete
    exit = (state->outstanding_completion == 0);
    for (unsigned int i = 0; i < TEST_MRRB_MAX_WRITERS && exit; i++) {
      exit = (state->reader_progress[i] == TEST_MULTI_WRITE_DATA_AMOUNT);
    }
  }
  // Release the mutex again
  TEST_ASSERT_EQUAL_INT(0, pthread_mutex_unlock(&state->mutex));

  // Exit the thread
  return NULL;
}

void multi_write_reader_read(multi_reader_ring_buffer_t *mrrb,
                             void *handle,
                             const unsigned char *data,
                             unsigned int data_length) {
  // Check arguments
  TEST_ASSERT_NOT_NULL(mrrb);
  TEST_ASSERT_NOT_NULL(handle);
  TEST_ASSERT_NOT_NULL(data);
  TEST_ASSERT_GREATER_THAN_UINT(0, data_length);

  // Cast and process handle
  multi_write_read_state_t *state = (multi_write_read_state_t *) handle;

  // Lock the reader mutex
  TEST_ASSERT_EQUAL_INT(0, pthread_mutex_lock(&state->mutex));

  multi_write_reader_check_data(state, data, data_length);

  // Update the outstanding complete counter
  state->outstanding_completion++;

  // Signal the reader thread and unlock
  TEST_ASSERT_EQUAL_INT(0, pthread_cond_signal(&state->cond));
  TEST_ASSERT_EQUAL_INT(0, pthread_mutex_unlock(&state->mutex));
}

void multi_write_reader_check_data(multi_write_read_state_t *state,
                                   const unsigned char *data,
                                   unsigned int data_length) {

  // Process previously incomplete transactions
  if (state->remaining_header_bytes > 0) {
    // Ensure the rest of the header arrives (header may be split at most one time)
    TEST_ASSERT_GREATER_THAN_UINT(state->remaining_header_bytes, data_length);

    // Combine the header and move data pointer
    unsigned char *dest = ((unsigned char *) &state->partial_header)
                           + sizeof(multi_write_header_t)
                           - state->remaining_header_bytes;
    memcpy(dest, data, state->remaining_header_bytes);
    data += state->remaining_header_bytes;
    data_length -= state->remaining_header_bytes;

    // Check the header that all data arrived and thread number is valid
    TEST_ASSERT_LESS_OR_EQUAL_UINT(data_length, state->partial_header.data_length);
    TEST_ASSERT_LESS_THAN_UINT(TEST_MRRB_MAX_WRITERS, state->partial_header.thread_num);

    // Check the integrity of the data
    for (unsigned int i = 0; i < state->partial_header.data_length; i++) {
      unsigned char expected = (unsigned char) (state->reader_progress[state->partial_header.thread_num] & 0xFF);
      TEST_ASSERT_EQUAL_UINT8(expected, *data);
      state->reader_progress[state->partial_header.thread_num]++;
      data++;
    }

    // Reduce the data length
    data_length -= state->partial_header.data_length;
    state->remaining_header_bytes = 0;
  } else if (state->remaining_data_bytes > 0) {

    // Check that all data arrived and thread number is valid
    TEST_ASSERT_LESS_OR_EQUAL_UINT(data_length, state->remaining_data_bytes);
    TEST_ASSERT_LESS_THAN_UINT(TEST_MRRB_MAX_WRITERS, state->partial_header.thread_num);

    // Check the integrity of the data
    for (unsigned int i = 0; i < state->remaining_data_bytes; i++) {
      unsigned char expected = (unsigned char) (state->reader_progress[state->partial_header.thread_num] & 0xFF);
      TEST_ASSERT_EQUAL_UINT8(expected, *data);
      state->reader_progress[state->partial_header.thread_num]++;
      data++;
    }

    // Reduce the data length and update the state
    data_length -= state->remaining_data_bytes;
    state->remaining_data_bytes = 0;
  }

  // Process full packets
  while (data_length > 0) {
    // Check if the header is incomplete
    if (data_length < sizeof(multi_write_header_t)) {
      // Copy the partial header into the state and save remaining byte count
      memcpy(&state->partial_header, data, data_length);
      state->remaining_header_bytes = sizeof(multi_write_header_t) - data_length;
      break;
    }

    // Process the header
    multi_write_header_t *header = (multi_write_header_t *) data;
    data += sizeof(multi_write_header_t);
    data_length -= sizeof(multi_write_header_t);

    // Check the header
    TEST_ASSERT_LESS_OR_EQUAL_UINT(TEST_MRRB_BUFFER_LENGTH, header->data_length);
    TEST_ASSERT_LESS_THAN_UINT(TEST_MRRB_MAX_WRITERS, header->thread_num);

    // Check if not all data arrived
    if (data_length < header->data_length) {
      // Copy the header into the state
      memcpy(&state->partial_header, header, sizeof(multi_write_header_t));
      // Save remaining data byte count into state
      state->remaining_data_bytes = header->data_length - data_length;

      // Check the integrity of the partial data
      for (unsigned int i = 0; i < data_length; i++) {
        unsigned char expected = (unsigned char) (state->reader_progress[state->partial_header.thread_num] & 0xFF);
        TEST_ASSERT_EQUAL_UINT8(expected, *data);
        state->reader_progress[state->partial_header.thread_num]++;
        data++;
      }
      break;
    }
    // Check the integrity of the data
    for (unsigned int i = 0; i < header->data_length; i++) {
      unsigned char expected = (unsigned char) (state->reader_progress[header->thread_num] & 0xFF);
      TEST_ASSERT_EQUAL_UINT8(expected, *data);
      state->reader_progress[header->thread_num]++;
      data++;
    }
    // Reduce the data length
    data_length -= header->data_length;
  }
}

void test_write_setup() {
  // Single write
  // Check every data chunk will fit into the buffer
  for (unsigned int i = 0; i < ARRAY_LENGTH(single_write_data_lengths); i++) {
    TEST_ASSERT_LESS_OR_EQUAL_UINT(TEST_MRRB_BUFFER_LENGTH, single_write_data_lengths[i]);
  }
  unsigned int data_len_sum = SUM(single_write_data_lengths);
  // Check the total data sent is at most the sample text length
  TEST_ASSERT_LESS_OR_EQUAL_UINT(TEST_TEXT_LEN, data_len_sum);
  // Check at least two buffer overflows occur
  TEST_ASSERT_GREATER_THAN_UINT(2 * TEST_MRRB_BUFFER_LENGTH, data_len_sum);

  // Consecutive write
  // Check every data chunk will fit into the buffer
  for (unsigned int i = 0; i < ARRAY_LENGTH(multi_write_data_lengths); i++) {
    unsigned int multi_write_length = _sum(multi_write_data_lengths[i], TEST_MULTI_WRITE_CONSEC_WRITES);
    TEST_ASSERT_LESS_OR_EQUAL_UINT(TEST_MRRB_BUFFER_LENGTH, multi_write_length);
  }
  data_len_sum = SUM2D(multi_write_data_lengths);
  // Check the total data sent is at most the sample text length
  TEST_ASSERT_LESS_OR_EQUAL_UINT(TEST_TEXT_LEN, data_len_sum);
  // Check at least two buffer overflows occur
  TEST_ASSERT_GREATER_THAN_UINT(2 * TEST_MRRB_BUFFER_LENGTH, data_len_sum);
}

void test_illegal_arguments() {
  int state = 0;
  int invalid_state = 0;
  // Reader init
  TEST_ASSERT_EQUAL_INT(-1, mrrb_reader_init(NULL, (void *) &state, MRRB_READER_OVERRUN_SKIP, read_ignore, abort_ignore));
  TEST_ASSERT_EQUAL_INT(-1, mrrb_reader_init(&readers[0], (void *) &state, MRRB_READER_OVERRUN_SKIP, NULL, abort_ignore));
  TEST_ASSERT_EQUAL_INT(-1, mrrb_reader_init(&readers[0], (void *) &state, MRRB_READER_OVERRUN_SKIP, read_ignore, NULL));
  TEST_ASSERT_EQUAL_INT( 0, mrrb_reader_init(&readers[0], (void *) &state, MRRB_READER_OVERRUN_SKIP, read_ignore, abort_ignore));

  // MRRB init
  TEST_ASSERT_EQUAL_INT(-1, mrrb_init(NULL, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, readers, 1));
  TEST_ASSERT_EQUAL_INT(-1, mrrb_init(&mrrb, NULL, TEST_MRRB_BUFFER_LENGTH, readers, 1));
  TEST_ASSERT_EQUAL_INT(-1, mrrb_init(&mrrb, mrrb_buffer, 0, readers, 1));
  TEST_ASSERT_EQUAL_INT(-1, mrrb_init(&mrrb, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, NULL, 1));
  TEST_ASSERT_EQUAL_INT(-1, mrrb_init(&mrrb, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, readers, 0));
  TEST_ASSERT_EQUAL_INT( 0, mrrb_init(&mrrb, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, readers, 1));

  // Remaining Space functions
  TEST_ASSERT_EQUAL_UINT(0, mrrb_get_remaining_space(NULL));
  TEST_ASSERT_EQUAL_UINT(TEST_MRRB_BUFFER_LENGTH, mrrb_get_remaining_space(&mrrb));
  TEST_ASSERT_EQUAL_UINT(0, mrrb_get_overwritable_space(NULL));
  TEST_ASSERT_EQUAL_UINT(TEST_MRRB_BUFFER_LENGTH, mrrb_get_overwritable_space(&mrrb));
  TEST_ASSERT_EQUAL_INT(-1, mrrb_is_empty(NULL));
  TEST_ASSERT_EQUAL_INT( 1, mrrb_is_empty(&mrrb));
  TEST_ASSERT_EQUAL_INT(-1, mrrb_is_full(NULL));
  TEST_ASSERT_EQUAL_INT( 0, mrrb_is_full(&mrrb));

  // Reader disable
  TEST_ASSERT_EQUAL_INT(-1, mrrb_reader_disable(NULL, &readers[0]));
  TEST_ASSERT_EQUAL_INT(-1, mrrb_reader_disable(&mrrb, NULL));
  TEST_ASSERT_EQUAL_INT( 0, mrrb_reader_disable(&mrrb, &readers[0]));

  // Reader enable
  TEST_ASSERT_EQUAL_INT(-1, mrrb_reader_enable(NULL, &readers[0]));
  TEST_ASSERT_EQUAL_INT(-1, mrrb_reader_enable(&mrrb, NULL));
  TEST_ASSERT_EQUAL_INT( 0, mrrb_reader_enable(&mrrb, &readers[0]));

  // MRRB write
  unsigned char buffer[] = "Hello, World!";
  TEST_ASSERT_EQUAL_INT(-1, mrrb_write(NULL, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_INT(-1, mrrb_write(&mrrb, NULL, sizeof(buffer)));
  TEST_ASSERT_EQUAL_INT( 0, mrrb_write(&mrrb, buffer, 0));
  TEST_ASSERT_EQUAL_INT(sizeof(buffer), mrrb_write(&mrrb, buffer, sizeof(buffer)));

  // Read complete
  mrrb_read_complete(NULL, &state);
  mrrb_read_complete(&mrrb, &invalid_state);
  mrrb_read_complete(&mrrb, NULL);
  mrrb_read_complete(&mrrb, &state);

  // Abort complete
  mrrb_abort_complete(NULL, &state);
  mrrb_abort_complete(&mrrb, &invalid_state);
  mrrb_abort_complete(&mrrb, NULL);
  mrrb_abort_complete(&mrrb, &state);

  // MRRB Reader deinit
  TEST_ASSERT_EQUAL_INT(-1, mrrb_reader_deinit(NULL));
  TEST_ASSERT_EQUAL_INT( 0, mrrb_reader_deinit(&readers[0]));

  // MRRB deinit
  TEST_ASSERT_EQUAL_INT(-1, mrrb_deinit(NULL));
  TEST_ASSERT_EQUAL_INT( 0, mrrb_deinit(&mrrb));
}

void test_single_write_single_read_immediate() {
  unsigned int num_writes = ARRAY_LENGTH(single_write_data_lengths);

  // Create test state
  read_state_t reader_state = {
    .write_data_lengths = single_write_data_lengths,
    .num_writes = num_writes
  };

  // Initialize first reader
  mrrb_reader_init(&readers[0], (void *) &reader_state, MRRB_READER_OVERRUN_BLOCKING, swsr_immediate_read, NULL);

  // Initialize MRRB. Check for successful initialization.
  int init_sts = mrrb_init(&mrrb, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, readers, 1);
  TEST_ASSERT_EQUAL_INT(0, init_sts);
  // Check buffer is empty after initialization
  TEST_MRRB_IS_EMPTY(&mrrb);

  // Edge-case test: Send data of length 0
  TEST_ASSERT_EQUAL_INT(0, mrrb_write(&mrrb, test_text, 0));
  TEST_MRRB_IS_EMPTY(&mrrb);

  // Send data
  const unsigned char *pData = test_text;
  for (unsigned int i = 0; i < num_writes; i++) {
    // Write the data and check all sent data is written to the buffer
    TEST_ASSERT_EQUAL_INT((int) single_write_data_lengths[i],
                          mrrb_write(&mrrb, pData, single_write_data_lengths[i]));

    // Increment data pointer
    pData += single_write_data_lengths[i];

    // Immediate reader -> check that buffer is immediately empty after the write.
    TEST_MRRB_IS_EMPTY(&mrrb);

    // Check all data arrived
    TEST_ASSERT_EQUAL_INT(pData - test_text, reader_state.data_received);
  }

  // De-init MRRB and check for success
  TEST_ASSERT_EQUAL_INT(0, mrrb_deinit(&mrrb));
  TEST_ASSERT_EQUAL_INT(0, mrrb_reader_deinit(&readers[0]));
}

void test_single_write_single_read_immediate_port_failure() {
  // List of point failures that are tested
  enum port_write_failure_type {
    PORT_WRITE_FAILURE_LOCK_1,
    PORT_WRITE_FAILURE_LOCK_2,
    PORT_WRITE_FAILURE_UNLOCK,
    PORT_WRITE_FAILURE_COUNT
  };

  // Initialize the reader
  int state = 0;
  mrrb_reader_init(&readers[0], (void *) &state, MRRB_READER_OVERRUN_BLOCKING, swsr_immediate_read_port_failure, NULL);

  // Test MRRB Initialization failure
  port_mock_fail_next_lock_init();
  TEST_ASSERT_EQUAL_INT(-1, mrrb_init(&mrrb, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, readers, 1));

  // Test MRRB Deinitialization failure. Initialize MRRB and check the first deinit fails.
  TEST_ASSERT_EQUAL_INT(0, mrrb_init(&mrrb, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, readers, 1));
  port_mock_fail_next_lock_deinit();
  TEST_ASSERT_EQUAL_INT(-1, mrrb_deinit(&mrrb));
  TEST_ASSERT_EQUAL_INT(0, mrrb_deinit(&mrrb));

  // Run all port failures
  for (enum port_write_failure_type failure = 0; failure < PORT_WRITE_FAILURE_COUNT; failure++) {
    // Initialize MRRB
    TEST_ASSERT_EQUAL_INT(0, mrrb_init(&mrrb, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, readers, 1));

    // Prepare port failure
    switch(failure) {
      case PORT_WRITE_FAILURE_LOCK_1:
        port_mock_fail_nth_lock(1);
        break;
      case PORT_WRITE_FAILURE_LOCK_2:
        port_mock_fail_nth_lock(2);
        break;
      case PORT_WRITE_FAILURE_UNLOCK:
        port_mock_fail_nth_unlock(1);
        break;
      default:
        TEST_FAIL();
    }
    // Check that writing fails
    TEST_ASSERT_EQUAL_INT(-1, mrrb_write(&mrrb, test_text, 10));

    // De-init the MRRB
    TEST_ASSERT_EQUAL_INT(0, mrrb_deinit(&mrrb));
  }

  // Read complete lock failure
  port_mock_fail_nth_lock(1);
  mrrb_read_complete(&mrrb, &state);

  // Abort complete lock failure
  port_mock_fail_nth_lock(1);
  mrrb_abort_complete(&mrrb, &state);

  // reader enable/disable port failure
  for (unsigned int i = 0; i < 2; i++) {
    // Initialize MRRB
    TEST_ASSERT_EQUAL_INT(0, mrrb_init(&mrrb, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, readers, 1));

    // Fail locking
    port_mock_fail_nth_lock(1);

    // Determine enable or disable
    if (i == 0) {
      TEST_ASSERT_EQUAL_INT(-1, mrrb_reader_enable(&mrrb, &readers[0]));
    } else {
      TEST_ASSERT_EQUAL_INT(-1, mrrb_reader_disable(&mrrb, &readers[0]));
    }

    // De-init the MRRB
    TEST_ASSERT_EQUAL_INT(0, mrrb_deinit(&mrrb));
  }

  // Check the write from interrupt failure
  TEST_ASSERT_EQUAL_INT(0, mrrb_init(&mrrb, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, readers, 1));
  port_mock_show_as_interrupt_active(1);
  TEST_ASSERT_EQUAL_INT(0, mrrb_write(&mrrb, test_text, 10));
  port_mock_show_as_interrupt_active(0);
  TEST_ASSERT_EQUAL_INT(0, mrrb_deinit(&mrrb));
}

void test_single_write_single_read_after() {
  // Test settings
  unsigned int num_writes = ARRAY_LENGTH(single_write_data_lengths);

  // Create test state
  read_state_t reader_state = {
    .write_data_lengths = single_write_data_lengths,
    .num_writes = num_writes,
  };

  // Initialize first reader
  mrrb_reader_init(&readers[0], (void *) &reader_state, MRRB_READER_OVERRUN_BLOCKING, swsr_triggered_read, NULL);

  // Initialize MRRB. Check for successful initialization.
  int init_sts = mrrb_init(&mrrb, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, readers, 1);
  TEST_ASSERT_EQUAL_INT(0, init_sts);
  // Check buffer is empty after initialization
  TEST_MRRB_IS_EMPTY(&mrrb);

  // Send data
  const unsigned char *pData = test_text;
  for (unsigned int i = 0; i < num_writes; i++) {
    // Write the data and check all sent data is written to the buffer
    TEST_ASSERT_EQUAL_INT((int) single_write_data_lengths[i],
                          mrrb_write(&mrrb, pData, single_write_data_lengths[i]));

    // Increment data pointer
    pData += single_write_data_lengths[i];

    // Triggered reader: check that buffer is not empty after the write.
    TEST_MRRB_IS_NOT_EMPTY(&mrrb);

    // Check for outstanding trigger
    TEST_ASSERT_NOT_EQUAL_UINT(0, reader_state.outstanding_read_trigger);

    // Trigger
    swsr_triggered_read_trigger(&mrrb, &reader_state);

    // If data was split on the edge of the buffer, trigger again
    if (reader_state.outstanding_read_trigger > 0) {
      swsr_triggered_read_trigger(&mrrb, &reader_state);
    }

    // Check all data arrived
    TEST_ASSERT_EQUAL_INT(pData - test_text, reader_state.data_received);

    // Check no outstanding triggers remain
    TEST_ASSERT_EQUAL_UINT(0, reader_state.outstanding_read_trigger);

    // Triggered reader: check that buffer is empty after the trigger.
    TEST_MRRB_IS_EMPTY(&mrrb);
  }

  // De-init MRRB and check for success
  TEST_ASSERT_EQUAL_INT(0, mrrb_deinit(&mrrb));
  TEST_ASSERT_EQUAL_INT(0, mrrb_reader_deinit(&readers[0]));
}

void test_consec_write_single_read_after() {
  // Test settings
  unsigned int num_writes = ARRAY_LENGTH(multi_write_data_lengths);

  unsigned int write_batch_lengths[2 * num_writes];
  for (unsigned int i = 0; i < num_writes; i++) {
    write_batch_lengths[2*i + 0] = multi_write_data_lengths[i][0];
    write_batch_lengths[2*i + 1] = _sum(&multi_write_data_lengths[i][1],
                                        TEST_MULTI_WRITE_CONSEC_WRITES - 1);
  }

  // Create test state
  read_state_t reader_state = {
    .write_data_lengths = write_batch_lengths,
    .num_writes = 2 * num_writes,
  };

  // Initialize first reader
  mrrb_reader_init(&readers[0], (void *) &reader_state, MRRB_READER_OVERRUN_BLOCKING, swsr_triggered_read, NULL);

  // Initialize MRRB. Check for successful initialization.
  int init_sts = mrrb_init(&mrrb, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, readers, 1);
  TEST_ASSERT_EQUAL_INT(0, init_sts);
  // Check buffer is empty after initialization
  TEST_MRRB_IS_EMPTY(&mrrb);

  // Send data
  const unsigned char *pData = test_text;
  for (unsigned int i = 0; i < num_writes; i++) {
    // Write the data and check all sent data is written to the buffer
    for (unsigned int j = 0; j < TEST_MULTI_WRITE_CONSEC_WRITES; j++) {
      TEST_ASSERT_EQUAL_INT((int) multi_write_data_lengths[i][j],
                            mrrb_write(&mrrb, pData, multi_write_data_lengths[i][j]));
      // Increment data pointer
      pData += multi_write_data_lengths[i][j];
    }

    // Triggered reader: check that buffer is not empty after the write.
    TEST_MRRB_IS_NOT_EMPTY(&mrrb);

    // Check for outstanding trigger
    TEST_ASSERT_NOT_EQUAL_UINT(0, reader_state.outstanding_read_trigger);

    // Trigger (up to three times: first write, to end of ring buffer, spill-over in ring buffer)
    swsr_triggered_read_trigger(&mrrb, &reader_state);
    if (reader_state.outstanding_read_trigger > 0) {
      swsr_triggered_read_trigger(&mrrb, &reader_state);
    }
    if (reader_state.outstanding_read_trigger > 0) {
      swsr_triggered_read_trigger(&mrrb, &reader_state);
    }

    // Check all data arrived
    TEST_ASSERT_EQUAL_INT(pData - test_text, reader_state.data_received);

    // Check no outstanding triggers remain
    TEST_ASSERT_EQUAL_UINT(0, reader_state.outstanding_read_trigger);

    // Triggered reader: check that buffer is empty after the trigger.
    TEST_MRRB_IS_EMPTY(&mrrb);
  }

  // De-init MRRB and check for success
  TEST_ASSERT_EQUAL_INT(0, mrrb_deinit(&mrrb));
  TEST_ASSERT_EQUAL_INT(0, mrrb_reader_deinit(&readers[0]));
}

void test_single_write_multiple_read() {
  // Test settings
  unsigned int num_writes = ARRAY_LENGTH(single_write_data_lengths);

  // Create reader states
  read_state_t reader_states[TEST_MRRB_MAX_READERS];
  memset(reader_states, 0, sizeof(reader_states));

  int all_variants_covered = 0;
  for (unsigned int i = 0; i < TEST_MRRB_MAX_READERS; i++) {
    unsigned int variant = i;

    // Reader initialization options
    read_state_t *reader_state = &reader_states[i];
    read_state_type_t read_type;
    mrrb_reader_overrun_policy_t overrun_policy;
    mrrb_reader_notify_data_t notify_fn;
    mrrb_reader_abort_data_t abort_fn;
    enable_type_t enable_type;

    // select the reader type:
    read_type = variant % ARRAY_LENGTH(read_fn_table);
    variant /= ARRAY_LENGTH(read_fn_table);
    notify_fn = read_fn_table[read_type];

    // Generate a compiler warning if a read type is not covered
    switch(read_type) {
      case READER_STATE_IMMEDIATE:
      case READER_STATE_TRIGGERED:
        ;
    }

    overrun_policy = MRRB_READER_OVERRUN_BLOCKING;
    abort_fn = NULL;

    // Select the enable type
    enable_type = variant % 4;
    variant /= 4;

    switch(enable_type) {
      case ENABLE_TYPE_ALWAYS_ENABLED:
      case ENABLE_TYPE_INITIALLY_ENABLED:
      case ENABLE_TYPE_INITIALLY_DISABLED:
      case ENABLE_TYPE_ALWAYS_DISABLED:
        ;
    }

    // Initialize the remainder of the state
    reader_state->read_type = read_type;
    reader_state->write_data_lengths = single_write_data_lengths;
    reader_state->num_writes = num_writes;
    reader_state->enable_type = enable_type;

    // Initialize the reader
    mrrb_reader_init(&readers[i], reader_state, overrun_policy, notify_fn, abort_fn);

    // Check if all variants covered at least once
    if (variant > 0) {
      all_variants_covered = 1;
    }
  }
  TEST_ASSERT(all_variants_covered);

  // Initialize MRRB. Check for successful initialization.
  int init_sts = mrrb_init(&mrrb, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, readers, TEST_MRRB_MAX_READERS);
  TEST_ASSERT_EQUAL_INT(0, init_sts);
  // Check buffer is empty after initialization
  TEST_MRRB_IS_EMPTY(&mrrb);

  // Initially disable some types
  for (unsigned int i = 0; i < TEST_MRRB_MAX_READERS; i++) {
    if (reader_states[i].enable_type == ENABLE_TYPE_INITIALLY_DISABLED ||
        reader_states[i].enable_type == ENABLE_TYPE_ALWAYS_DISABLED) {
      TEST_ASSERT_EQUAL_INT(0, mrrb_reader_disable(&mrrb, &readers[i]));
    }
  }

  // Send data
  const unsigned char *pData = test_text;
  for (unsigned int i = 0; i < num_writes; i++) {
    // Write the data and check all sent data is written to the buffer
    TEST_ASSERT_EQUAL_INT((int) single_write_data_lengths[i],
                          mrrb_write(&mrrb, pData, single_write_data_lengths[i]));

    // Increment data pointer
    pData += single_write_data_lengths[i];

    // At least one triggered reader: check that buffer is not empty after the write.
    TEST_MRRB_FILL_LEVEL(&mrrb, single_write_data_lengths[i]);

    // Check all readers
    for (unsigned int j = 0; j < TEST_MRRB_MAX_READERS; j++) {
      // update the data received counter for disabled readers
      int enable_type = reader_states[j].enable_type;

      int reader_enabled = (enable_type == ENABLE_TYPE_ALWAYS_ENABLED) ||
                          ((enable_type == ENABLE_TYPE_INITIALLY_ENABLED) && (i % 2 == 0)) ||
                          ((enable_type == ENABLE_TYPE_INITIALLY_DISABLED) && (i % 2 == 1));

      // Manually advance disabled readers
      if (!reader_enabled) {
        reader_states[j].data_received += single_write_data_lengths[i];
        reader_states[j].iteration++;
      }

      // Switch depending on reader
      if (reader_states[j].read_type == READER_STATE_TRIGGERED && reader_enabled) {
        swsr_triggered_read_trigger(&mrrb, &reader_states[j]);
        // If data was split on the edge of the buffer, trigger again
        if (reader_states[j].outstanding_read_trigger > 0) {
          swsr_triggered_read_trigger(&mrrb, &reader_states[j]);
        }
      }

      // Check all data received
      TEST_ASSERT_EQUAL_INT(pData - test_text, reader_states[j].data_received);

      // Update enable/disable of readers
      if (((enable_type == ENABLE_TYPE_INITIALLY_ENABLED) && (i % 2 == 0)) ||
          ((enable_type == ENABLE_TYPE_INITIALLY_DISABLED) && (i % 2 == 1))) {
        TEST_ASSERT_EQUAL_INT(0, mrrb_reader_disable(&mrrb, &readers[j]));
      }
      if (((enable_type == ENABLE_TYPE_INITIALLY_ENABLED) && (i % 2 == 1)) ||
          ((enable_type == ENABLE_TYPE_INITIALLY_DISABLED) && (i % 2 == 0))) {
        TEST_ASSERT_EQUAL_INT(0, mrrb_reader_enable(&mrrb, &readers[j]));
      }
    }

    // Triggered reader: check that buffer is empty after the trigger.
    TEST_MRRB_IS_EMPTY(&mrrb);
  }

  // De-init MRRB and check for success
  TEST_ASSERT_EQUAL_INT(0, mrrb_deinit(&mrrb));
  for (unsigned int i = 0; i < TEST_MRRB_MAX_READERS; i++) {
    TEST_ASSERT_EQUAL_INT(0, mrrb_reader_deinit(&readers[i]));
  }
}

void test_overrun() {
  // Test settings
  const unsigned int overrun_1_data_lengths[] = {
    TEST_MRRB_BUFFER_LENGTH - 10,
    10,
    TEST_MRRB_BUFFER_LENGTH,
    10,
    15,
    TEST_MRRB_BUFFER_LENGTH - 5,
  };
  unsigned int num_writes_1 = ARRAY_LENGTH(overrun_1_data_lengths);

  // Create reader states
  read_state_t reader_states[TEST_MRRB_MAX_READERS];
  memset(reader_states, 0, sizeof(reader_states));

  int all_variants_covered = 0;
  for (unsigned int i = 0; i < TEST_MRRB_MAX_READERS; i++) {
    unsigned int variant = i;

    // Reader initialization options
    read_state_t *reader_state = &reader_states[i];
    read_state_type_t read_type;
    mrrb_reader_overrun_policy_t overrun_policy;
    mrrb_reader_notify_data_t notify_fn;
    mrrb_reader_abort_data_t abort_fn;

    // select the reader type:
    read_type = variant % ARRAY_LENGTH(overrun_read_fn_table);
    variant /= ARRAY_LENGTH(overrun_read_fn_table);
    notify_fn = overrun_read_fn_table[read_type];

    // Generate a compiler warning if a read type is not covered
    switch(read_type) {
      case READER_STATE_IMMEDIATE:
      case READER_STATE_TRIGGERED:
      ;
    }

    // Select the overrun policy
    overrun_policy = variant % READER_OVERRUN_POLICY_COUNT;
    variant /= READER_OVERRUN_POLICY_COUNT;

    // Generate a compiler warning if a policy is not covered
    switch(overrun_policy) {
      case MRRB_READER_OVERRUN_DISABLE:
      case MRRB_READER_OVERRUN_BLOCKING:
      case MRRB_READER_OVERRUN_SKIP:
        ;
    }

    // Select the abort type
    int abort_type = variant % ARRAY_LENGTH(abort_fn_table);
    variant /= ARRAY_LENGTH(abort_fn_table);
    abort_fn = abort_fn_table[abort_type];

    // Prevent illegal combination
    if (overrun_policy == MRRB_READER_OVERRUN_SKIP && abort_fn == NULL) {
      abort_fn = abort_immediate;
    }

    // Initialize the remainder of the state
    reader_state->read_type = read_type;
    reader_state->write_data_lengths = overrun_1_data_lengths;
    reader_state->num_writes = num_writes_1;

    // Initialize the reader
    mrrb_reader_init(&readers[i], reader_state, overrun_policy, notify_fn, abort_fn);

    // Check if all variants covered at least once
    if (variant > 0) {
      all_variants_covered = 1;
    }
  }
  TEST_ASSERT(all_variants_covered);

  // Initialize MRRB. Check for successful initialization.
  int init_sts = mrrb_init(&mrrb, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, readers, TEST_MRRB_MAX_READERS);
  TEST_ASSERT_EQUAL_INT(0, init_sts);
  // Check buffer is empty after initialization
  TEST_MRRB_IS_EMPTY(&mrrb);

  // Send some data
  const unsigned char *pData = test_text;

  // Write the data and check all sent data is written to the buffer
  TEST_ASSERT_EQUAL_INT((int) overrun_1_data_lengths[0],
                        mrrb_write(&mrrb, pData, overrun_1_data_lengths[0]));

  // Increment data pointer
  pData += overrun_1_data_lengths[0];

  // At least one triggered reader: check that buffer is not empty after the write.
  TEST_MRRB_FILL_LEVEL(&mrrb, overrun_1_data_lengths[0]);

  // Check all readers
  for (unsigned int j = 0; j < TEST_MRRB_MAX_READERS; j++) {
    TEST_ASSERT_EQUAL_INT(pData - test_text, reader_states[j].data_received);
  }

  // Without triggering the triggered readers, send more data to cause an overrun
  // As some readers are blocking, the buffer can only be filled, but not all data written
  TEST_ASSERT_EQUAL_INT((int) overrun_1_data_lengths[1],
                        mrrb_write(&mrrb, pData, overrun_1_data_lengths[1] + 10));
  TEST_MRRB_IS_FULL(&mrrb);

  // Increment data pointer
  pData += overrun_1_data_lengths[1];

  // Check all readers
  for (unsigned int j = 0; j < TEST_MRRB_MAX_READERS; j++) {
    switch (reader_states[j].read_type) {
      case READER_STATE_IMMEDIATE:
        TEST_ASSERT(readers[j].status == MRRB_READER_STATUS_IDLE);
        TEST_ASSERT_EQUAL_INT(TEST_MRRB_BUFFER_LENGTH, reader_states[j].data_received);
        break;
      case READER_STATE_TRIGGERED:
        TEST_ASSERT(readers[j].status == MRRB_READER_STATUS_ACTIVE);
        TEST_ASSERT_EQUAL_INT(overrun_1_data_lengths[0], reader_states[j].data_received);
        // Trigger the triggered readers once to complete the first write, but not the second
        swsr_triggered_read_trigger(&mrrb, &reader_states[j]);
        TEST_ASSERT(readers[j].status == MRRB_READER_STATUS_ACTIVE);
        TEST_ASSERT_EQUAL_INT(TEST_MRRB_BUFFER_LENGTH, reader_states[j].data_received);
        break;
    }
  }

  TEST_MRRB_FILL_LEVEL(&mrrb, overrun_1_data_lengths[1]);

  // Triggered readers still have the second write not completed yet.
  // Disable blocking readers to test overrun
  for (unsigned int j = 0; j < TEST_MRRB_MAX_READERS; j++) {
    switch (readers[j].overrun_policy) {
      case MRRB_READER_OVERRUN_BLOCKING:
        // Disable the reader
        TEST_ASSERT_EQUAL_INT(0, mrrb_reader_disable(&mrrb, &readers[j]));
        // Test the abort functions
        if (readers[j].abort_data == abort_immediate || readers[j].abort_data == NULL) {
          TEST_ASSERT_EQUAL(MRRB_READER_STATUS_DISABLED, readers[j].status);
        } else if (readers[j].abort_data == abort_triggered) {
          TEST_ASSERT_EQUAL(MRRB_READER_STATUS_DISABLING, readers[j].status);
          triggered_abort_trigger(&mrrb, &reader_states[j]);
          TEST_ASSERT_EQUAL(MRRB_READER_STATUS_DISABLED, readers[j].status);
        } else {
          TEST_FAIL();
        }
        break;
      case MRRB_READER_OVERRUN_SKIP:
      case MRRB_READER_OVERRUN_DISABLE:
        break;
    }
  }

  // More data still remains in the MRRB
  TEST_MRRB_FILL_LEVEL(&mrrb, overrun_1_data_lengths[1]);

  // Without triggering the triggered readers, send more data to cause an overrun
  // As some no enabled readers are blocking, all data should be written to the buffer
  TEST_ASSERT_EQUAL_INT((int) overrun_1_data_lengths[2],
                        mrrb_write(&mrrb, pData, overrun_1_data_lengths[2]));
  TEST_MRRB_IS_FULL(&mrrb);
  // Increment data pointer
  pData += overrun_1_data_lengths[2];

  // Check the readers
  for (unsigned int j = 0; j < TEST_MRRB_MAX_READERS; j++) {
    switch (readers[j].overrun_policy) {
      case MRRB_READER_OVERRUN_DISABLE:
        switch (reader_states[j].read_type) {
          case READER_STATE_IMMEDIATE:
            TEST_ASSERT_EQUAL(MRRB_READER_STATUS_IDLE, readers[j].status);
            break;
          case READER_STATE_TRIGGERED:
            // Test the abort functions
            if (readers[j].abort_data == abort_immediate || readers[j].abort_data == NULL) {
              TEST_ASSERT_EQUAL(MRRB_READER_STATUS_DISABLED, readers[j].status);
            } else if (readers[j].abort_data == abort_triggered) {
              TEST_ASSERT_EQUAL(MRRB_READER_STATUS_DISABLING, readers[j].status);
              triggered_abort_trigger(&mrrb, &reader_states[j]);
              TEST_ASSERT_EQUAL(MRRB_READER_STATUS_DISABLED, readers[j].status);
            } else {
              TEST_FAIL();
            }
            break;
        }
        break;
      case MRRB_READER_OVERRUN_BLOCKING:
        TEST_ASSERT_EQUAL(MRRB_READER_STATUS_DISABLED, readers[j].status);
        break;
      case MRRB_READER_OVERRUN_SKIP:
        // Test the abort functions
        if (reader_states[j].read_type == READER_STATE_TRIGGERED)
        {
          if (readers[j].abort_data == abort_triggered) {
            TEST_ASSERT_EQUAL(MRRB_READER_STATUS_ABORTING, readers[j].status);
            triggered_abort_trigger(&mrrb, &reader_states[j]);
          }
        }
        break;
    }
  }

  // Now, the triggered readers should still occupy the buffer
  TEST_MRRB_IS_FULL(&mrrb);

  for (unsigned int j = 0; j < TEST_MRRB_MAX_READERS; j++) {
    switch (readers[j].overrun_policy) {
      case MRRB_READER_OVERRUN_DISABLE:
        if (reader_states[j].read_type == READER_STATE_IMMEDIATE) {
          TEST_ASSERT_EQUAL(MRRB_READER_STATUS_IDLE, readers[j].status);
        } else {
          TEST_ASSERT_EQUAL(MRRB_READER_STATUS_DISABLED, readers[j].status);
        }
        break;
      case MRRB_READER_OVERRUN_BLOCKING:
        TEST_ASSERT_EQUAL(MRRB_READER_STATUS_DISABLED, readers[j].status);
        break;
      case MRRB_READER_OVERRUN_SKIP:
        switch (reader_states[j].read_type) {
          case READER_STATE_IMMEDIATE:
            TEST_ASSERT_EQUAL(MRRB_READER_STATUS_IDLE, readers[j].status);
          break;
          case READER_STATE_TRIGGERED:
            TEST_ASSERT_EQUAL(MRRB_READER_STATUS_ACTIVE, readers[j].status);
            swsr_triggered_read_trigger(&mrrb, &reader_states[j]);
            TEST_ASSERT_EQUAL(MRRB_READER_STATUS_IDLE, readers[j].status);
          break;
        }
        break;
    }
  }
  TEST_MRRB_IS_EMPTY(&mrrb);

  // Write more data, two that fit
  TEST_ASSERT_EQUAL_INT((int) overrun_1_data_lengths[3],
                        mrrb_write(&mrrb, pData, overrun_1_data_lengths[3]));
  pData += overrun_1_data_lengths[3];
  TEST_ASSERT_EQUAL_INT((int) overrun_1_data_lengths[4],
                        mrrb_write(&mrrb, pData, overrun_1_data_lengths[4]));
  pData += overrun_1_data_lengths[4];
  // Before sending the overrun data, manually advance readers that
  //  - will SKIP on overrun AND
  //  - have a triggered read (and thus have not read the second write) AND
  //  - have an immediate abort (will thus skip the part of the second write that will be overwritten)
  for (unsigned int j = 0; j < TEST_MRRB_MAX_READERS; j++) {
    if (readers[j].overrun_policy == MRRB_READER_OVERRUN_SKIP &&
        reader_states[j].read_type == READER_STATE_TRIGGERED) {
      // Advance by the amount of data that will be overrun
      reader_states[j].data_received += overrun_1_data_lengths[4] +
                                        overrun_1_data_lengths[5] -
                                        mrrb.buffer_length;
      reader_states[j].iteration += 2;
    }
  }
  TEST_ASSERT_EQUAL_INT((int) overrun_1_data_lengths[5],
                        mrrb_write(&mrrb, pData, overrun_1_data_lengths[5]));
  pData += overrun_1_data_lengths[5];

  // Check the readers
  for (unsigned int j = 0; j < TEST_MRRB_MAX_READERS; j++) {
    switch (readers[j].overrun_policy) {
      case MRRB_READER_OVERRUN_DISABLE:
        if (reader_states[j].read_type == READER_STATE_IMMEDIATE) {
          TEST_ASSERT_EQUAL(MRRB_READER_STATUS_IDLE, readers[j].status);
        } else {
          TEST_ASSERT_EQUAL(MRRB_READER_STATUS_DISABLED, readers[j].status);
        }
        break;
      case MRRB_READER_OVERRUN_BLOCKING:
        TEST_ASSERT_EQUAL(MRRB_READER_STATUS_DISABLED, readers[j].status);
        break;
      case MRRB_READER_OVERRUN_SKIP:
        if (reader_states[j].read_type == READER_STATE_TRIGGERED) {
          if (readers[j].abort_data == abort_triggered) {
            TEST_ASSERT_EQUAL(MRRB_READER_STATUS_ABORTING, readers[j].status);
            triggered_abort_trigger(&mrrb, &reader_states[j]);
            TEST_ASSERT_EQUAL(MRRB_READER_STATUS_ACTIVE, readers[j].status);
          }
          // Two triggers because of the overflow
          swsr_triggered_read_trigger(&mrrb, &reader_states[j]);
          swsr_triggered_read_trigger(&mrrb, &reader_states[j]);
          TEST_ASSERT_EQUAL(MRRB_READER_STATUS_IDLE, readers[j].status);
        } else {
          TEST_ASSERT_EQUAL(MRRB_READER_STATUS_IDLE, readers[j].status);
        }
        break;
    }
  }
  TEST_MRRB_IS_EMPTY(&mrrb);

  // Check all enabled readers received all data
  for (unsigned int j = 0; j < TEST_MRRB_MAX_READERS; j++) {
    if (((readers[j].overrun_policy == MRRB_READER_OVERRUN_DISABLE) &&
         (reader_states[j].read_type == READER_STATE_IMMEDIATE)) ||
        (readers[j].overrun_policy == MRRB_READER_OVERRUN_SKIP)) {
      TEST_ASSERT_EQUAL(SUM(overrun_1_data_lengths), reader_states[j].data_received);
    }
  }

  // De-init MRRB and check for success
  TEST_ASSERT_EQUAL_INT(0, mrrb_deinit(&mrrb));
  for (unsigned int i = 0; i < TEST_MRRB_MAX_READERS; i++) {
    TEST_ASSERT_EQUAL_INT(0, mrrb_reader_deinit(&readers[i]));
  }
}

void test_multiple_write_multiple_read() {
  pthread_t reader_threads[TEST_MULTI_WRITE_READERS];
  pthread_t writer_threads[TEST_MRRB_MAX_WRITERS];
  multi_write_read_state_t reader_states[TEST_MULTI_WRITE_READERS] = { 0 };
  multi_write_write_state_t writer_states[TEST_MRRB_MAX_WRITERS] = { 0 };
  multi_write_shared_write_state_t writer_shared_state = { 0 };

  // Check test settings
  TEST_ASSERT_LESS_OR_EQUAL_UINT(TEST_MRRB_BUFFER_LENGTH, (sizeof(multi_write_header_t) + TEST_MULTI_WRITE_MAX_DATA_SIZE) * TEST_MRRB_MAX_WRITERS);
  TEST_ASSERT_LESS_OR_EQUAL_UINT(TEST_MRRB_MAX_READERS, TEST_MULTI_WRITE_READERS);

  // Create reader states
  for (unsigned int i = 0; i < TEST_MULTI_WRITE_READERS; i++) {
    reader_states[i].reader_number = i;
    reader_states[i].seed = i + 54389277;
    mrrb_reader_init(&readers[i],
                     (void *) &reader_states[i],
                     MRRB_READER_OVERRUN_BLOCKING,
                     multi_write_reader_read,
                     NULL);
  }

  // Initialize MRRB. Check for successful initialization.
  int init_sts = mrrb_init(&mrrb, mrrb_buffer, TEST_MRRB_BUFFER_LENGTH, readers, TEST_MULTI_WRITE_READERS);
  TEST_ASSERT_EQUAL_INT(0, init_sts);
  // Check buffer is empty after initialization
  TEST_MRRB_IS_EMPTY(&mrrb);

  // Create the reader threads
  for (unsigned int i = 0; i < TEST_MULTI_WRITE_READERS; i++) {
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_init(&reader_states[i].mutex, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_cond_init(&reader_states[i].cond, NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_create(reader_threads + i, NULL, multi_write_reader_thread, &reader_states[i]));
  }

  // Initialize the shared state
  TEST_ASSERT_EQUAL_INT(0, pthread_mutex_init(&writer_shared_state.mutex, NULL));

  // Create the writer threads
  for (unsigned int i = 0; i < TEST_MRRB_MAX_WRITERS; i++) {
    writer_states[i].writer_number = i;
    writer_states[i].seed = i + 47239749;
    writer_states[i].shared_state = &writer_shared_state;
    TEST_ASSERT_EQUAL_INT(0, pthread_create(writer_threads + i, NULL, multi_write_writer_thread, &writer_states[i]));
  }

  // -- At this point, the readers and writers execute --

  // Wait for the writer threads to end
  for (unsigned int i = 0; i < TEST_MRRB_MAX_WRITERS; i++) {
    TEST_ASSERT_EQUAL_INT(0, pthread_join(writer_threads[i], NULL));
  }

  // Wait for the reader threads to end
  for (unsigned int i = 0; i < TEST_MULTI_WRITE_READERS; i++) {
    TEST_ASSERT_EQUAL_INT(0, pthread_cond_signal(&reader_states[i].cond));
    TEST_ASSERT_EQUAL_INT(0, pthread_join(reader_threads[i], NULL));
    TEST_ASSERT_EQUAL_INT(0, pthread_mutex_destroy(&reader_states[i].mutex));
    TEST_ASSERT_EQUAL_INT(0, pthread_cond_destroy(&reader_states[i].cond));
  }

  // Check that the mrrb is empty once all writers and readers complete
  TEST_MRRB_IS_EMPTY(&mrrb);

  // De-init MRRB and check for success
  TEST_ASSERT_EQUAL_INT(0, mrrb_deinit(&mrrb));
  for (unsigned int i = 0; i < TEST_MULTI_WRITE_READERS; i++) {
    TEST_ASSERT_EQUAL_INT(0, mrrb_reader_deinit(&readers[i]));
  }
}

void *timeout_thread_function(void *args) {
  // Compute the timeout
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += TEST_TIMEOUT_S;

  // Wait for complete condition or timeout
  pthread_mutex_lock(&timeout_mutex);
  timeout_ready = 1;
  int timed_out = pthread_cond_timedwait(&timeout_cond, &timeout_mutex, &ts);
  pthread_mutex_unlock(&timeout_mutex);

  // Check for timeout or signal
  if (timed_out == ETIMEDOUT) {
    // Fail the test
    TEST_FAIL_MESSAGE("Timed out.");
  }

  return NULL;
}

unsigned int _sum(const unsigned int *array, const unsigned int length) {
  unsigned int sum = 0;
  for( unsigned int i = 0; i < length; i++) {
    sum += array[i];
  }
  return sum;
}

unsigned int _sum2d(const unsigned int outer_length,
                    const unsigned int inner_length,
                    const unsigned int array[outer_length][inner_length]) {
  unsigned int sum = 0;
  for( unsigned int i = 0; i < outer_length; i++) {
    for( unsigned int j = 0; j < inner_length; j++) {
      sum += array[i][j];
    }
  }
  return sum;
}

void _custom_test_abort() {
  // Set a debugger breakpoint here to stop once a test fails
  return;
}

/* Port Mock functions -------------------------------------------------------*/

static inline void port_mock_fail_next_lock_init(void) {
  _port_fail_next_lock_init = 1;
}

static inline void port_mock_fail_next_lock_deinit(void) {
  _port_fail_next_lock_deinit = 1;
}

static inline void port_mock_fail_nth_lock(int n) {
  _port_fail_nth_lock = n;
}

static inline void port_mock_fail_nth_unlock(int n) {
  _port_fail_nth_unlock = n;
}

static inline void port_mock_show_as_interrupt_active(int is_interrupt) {
  _port_show_as_interrupt = is_interrupt;
}

#ifdef __cplusplus
}
#endif
