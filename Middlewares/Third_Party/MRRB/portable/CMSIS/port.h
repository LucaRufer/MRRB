/**
 * @file       port.h
 * @brief      portable macros for CMSIS-compatible processors
 *
 * @author     Luca Rufer, luca.rufer@swissloop.ch
 * @date       23.11.2023
 */

#ifndef __MRRB_PORTABLE_CMSIS_PORT_H
#define __MRRB_PORTABLE_CMSIS_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

#include "cmsis_compiler.h"

#if MRRB_USE_OS
#include "cmsis_os.h"
#endif

/* Exported constants --------------------------------------------------------*/

#if MRRB_USE_MUTEX
#define MRRB_PORT_MUTEX_TYPE osMutexId_t
#else
#define MRRB_PORT_MUTEX_TYPE void
#endif

/* Exported macros -----------------------------------------------------------*/

/* Exported types ------------------------------------------------------------*/

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

static inline void fence(void) {
  __DSB();
  __ISB();
}

#if MRRB_USE_MUTEX
static inline int port_lock_init(osMutexId_t *mutex) {
  *mutex = osMutexNew(NULL);
  return (*mutex != NULL) ? 0 : -1;
}

static inline int port_lock_deinit(osMutexId_t *mutex) {
  return (osMutexDelete(*mutex) == 0) ? 0 : -1;
}

static inline int port_lock(osMutexId_t *mutex) {
  return (osMutexAcquire(*mutex, 0) == osOK) ? 0 : -1;
}

static inline int port_unlock(osMutexId_t *mutex) {
  return (osMutexRelease(*mutex) == osOK) ? 0 : -1;
}
#endif /* MRRB_USE_MUTEX */

static inline int port_disable_interrupts(void) {
  int lock = __get_PRIMASK();
  __set_PRIMASK(0);
  fence();
  return lock;
}

static inline int port_enable_interrupts(int lock) {
  fence();
  __set_PRIMASK(lock);
  return 0;
}

static inline int port_interrupt_active(void) {
  return (__get_IPSR() != 0U);
}

#ifdef __cplusplus
}
#endif

#endif // __MRRB_PORTABLE_CMSIS_PORT_H included
