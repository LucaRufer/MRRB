/**
 * @file       port.h
 * @brief      portable macros for UNIX systems adapted for testing
 *
 * @author     Luca Rufer, luca.rufer@swissloop.ch
 * @date       15.12.2023
 */

#ifndef __MRRB_PORTABLE_UNIX_TEST_PORT_H
#define __MRRB_PORTABLE_UNIX_TEST_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

// Include std libraries
#include <stdlib.h>
#include <pthread.h>

// Check configuration
#if MRRB_USE_OS != 1
#error "UNIX System requires MRRB_USE_OS to be set to 1"
#endif

#if MRRB_ALLOW_WRITE_FROM_ISR != 0
#error "UNIX System does not allow writes from ISR. Set MRRB_ALLOW_WRITE_FROM_ISR to 0"
#endif

/* Exported types ------------------------------------------------------------*/

#define MRRB_PORT_MUTEX_TYPE pthread_mutex_t

/* Exported variables --------------------------------------------------------*/

// Variables to fail port functions
extern int _port_fail_next_lock_init;
extern int _port_fail_next_lock_deinit;
extern int _port_fail_nth_lock;
extern int _port_fail_nth_unlock;
extern int _port_show_as_interrupt;

/* Port functions ------------------------------------------------------------*/

static inline int port_lock_init(pthread_mutex_t *mutex) {
  if (_port_fail_next_lock_init) {
    _port_fail_next_lock_init = 0;
    return -1;
  } else {
    return (pthread_mutex_init(mutex, NULL) == 0) ? 0 : -1;
  }
}

static inline int port_lock_deinit(pthread_mutex_t *mutex) {
  if (_port_fail_next_lock_deinit) {
    _port_fail_next_lock_deinit = 0;
    return -1;
  } else {
    (void) pthread_mutex_unlock(mutex);
    return (pthread_mutex_destroy(mutex) == 0) ? 0 : -1;
  }
}

static inline int port_lock(pthread_mutex_t *mutex) {
  if ((_port_fail_nth_lock) > 0 && (--_port_fail_nth_lock == 0)) {
    return -1;
  } else {
    return (pthread_mutex_lock(mutex) == 0) ? 0 : -1;
  }
}

static inline int port_unlock(pthread_mutex_t *mutex) {
  if ((_port_fail_nth_unlock) > 0 && (--_port_fail_nth_unlock == 0)) {
    return -1;
  } else {
    return (pthread_mutex_unlock(mutex) == 0) ? 0 : -1;
  }
}

static inline int port_disable_interrupts(void) {
  exit(-1);
  return 1;
}

static inline int port_enable_interrupts(int lock) {
  (void) lock;
  exit(-1);
  return 1;
}

static inline int port_interrupt_active() {
  return _port_show_as_interrupt;
}

#ifdef __cplusplus
}
#endif

#endif // __MRRB_PORTABLE_UNIX_TEST_PORT_H included
