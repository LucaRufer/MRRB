/**
 * @file       mrrb_config.h
 * @brief      Multi Reader Ring Buffer configuration file
 *
 * @author     Luca Rufer, luca.rufer@swissloop.ch
 * @date       23.11.2023
 */

#ifndef __MRRB_CONFIG_H
#define __MRRB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

// ==========  Systems List  ==========

#define MRRB_SYSTEM_UNIX 1
#define MRRB_SYSTEM_CMSIS 2

// ==========  Settings  ==========

// Allow Writes from Interrupts
#ifndef MRRB_ALLOW_WRITE_FROM_ISR
#define MRRB_ALLOW_WRITE_FROM_ISR 0
#endif /* MRRB_ALLOW_WRITE_FROM_ISR */

// Whether to use an OS (Embedded only)
#ifndef MRRB_USE_OS
#define MRRB_USE_OS 0
#endif /* MRRB_USE_OS */

// Processor Type
#ifndef MRRB_SYSTEM
#define MRRB_SYSTEM MRRB_SYSTEM_CMSIS
#endif /* MRRB_SYSTEM */

// ========== Setting-specific Definitions  ==========

#define MRRB_USE_MUTEX ((MRRB_ALLOW_WRITE_FROM_ISR == 0) && (MRRB_USE_OS == 1))

#ifndef MRRB_PORT_PATH

#if MRRB_SYSTEM == MRRB_SYSTEM_CMSIS
#define MRRB_PORT_PATH "portable/CMSIS/port.h"
#endif /* MRRB_SYSTEM_CMSIS */

#if MRRB_SYSTEM == MRRB_SYSTEM_UNIX
#define MRRB_PORT_PATH "portable/UNIX/port.h"
#endif /* MRRB_SYSTEM_UNIX */

#endif /* MRRB_PORT_PATH */

#ifndef MRRB_PORT_PATH
#error "Please select a valid system for MRRB_SYSTEM."
#endif

#ifdef __cplusplus
}
#endif

#endif // __MRRB_CONFIG_H included
