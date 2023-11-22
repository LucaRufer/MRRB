/**
 * @file       ftp.h
 * @brief      FTP Server implementation
 *
 * @author     Luca Rufer, luca.rufer@swissloop.ch
 * @date       \today
 */

#ifndef __FTP_H
#define __FTP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

// Operating System
#include "cmsis_os.h"

/* Exported constants --------------------------------------------------------*/

#define FTP_SERVER_MAX_PI_NUM             4

#define FTP_SERVER_RESPONSE_MESSAGE       1

#define FTP_SERVER_DEFAULT_CONTROL_PORT   21
#define FTP_SERVER_DEFAULT_DATA_PORT     (FTP_SERVER_DEFAULT_CONTROL_PORT - 1)

#define FTP_SERVER_RECV_BUF_LEN           200
#define FTP_SERVER_SEND_BUF_LEN           200
#define FTP_SERVER_PATH_BUF_LEN           200
#define FTP_SERVER_DTP_BUFFER_LEN         600

#define FTP_SERVER_THREAD_STACKSIZE      1536
#define FTP_SERVER_PI_THREAD_STACKSIZE   2048
#define FTP_SERVER_DTP_THREAD_STACKSIZE  3072

#define FTP_MAX_USERNAME_LEN              16
#define FTP_MAX_PASSWORD_LEN              16
#define FTP_MAX_ACCOUNT_LEN               16

#define FTP_SERVER_DEFAULT_TIMEOUT        50

/* Exported macros -----------------------------------------------------------*/

/* Exported types ------------------------------------------------------------*/

typedef enum {
  FTP_PERM_NONE = 0, /*<! Only allowed to use log-in commands and help */
  FTP_PERM_VIEW,     /*<! Allowed to view the directory and system parameters, but may not download files */
  FTP_PERM_READ,     /*<! Allowed to read files */
  FTP_PERM_WRITE,    /*<! Allowed to write new files and create new directories */
  FTP_PERM_ADMIN,    /*<! Allowed to append to files, rename files, overwrite files and delete directories */
} ftp_permission_t;

typedef enum {
  FTP_LOGIN_RESULT_FAILURE = 0,
  FTP_LOGIN_RESULT_MORE_INFO_REQUIRED = 1,
  FTP_LOGIN_RESULT_SUCCESS = 2,
} ftp_login_result_t;

typedef ftp_login_result_t (*ftp_credentials_check_fn)(char *username, char *password, char *account, ftp_permission_t* perm);

/* Exported variables --------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/

osThreadId_t ftp_server_init(void);

#ifdef __cplusplus
}
#endif

#endif // __FTP_H included
