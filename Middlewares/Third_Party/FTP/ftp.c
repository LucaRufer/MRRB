/**
 * @file        ftp.c
 * @brief       FTP Server implementation
 *
 * @author      Luca Rufer, luca.rufer@swissloop.ch
 * @date        \today
 *
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/

// Std includes
#include <stdio.h>
#include <string.h>

// Header
#include "ftp.h"

// Sockets
#include "socket.h"

// File system
#include "ff.h"

/* Private defines -----------------------------------------------------------*/

// Debug options
#define FTP_DEBUG_ON               1
#define FTP_DEBUG_LEVEL            1
#define FTP_SERVER_DEBUG_LEVEL     1
#define FTP_SERVER_PI_DEBUG_LEVEL  2
#define FTP_SERVER_DTP_DEBUG_LEVEL 3

// check settings
#if FTP_SERVER_RECV_BUF_LEN < 7
#error "FTP Receive Buffer is too small"
#endif

#if FTP_SERVER_SEND_BUF_LEN < 6
#error "FTP Send Buffer is too small"
#endif

#if FTP_SERVER_DTP_BUFFER_LEN < 50
#warning "FTP File info will not fit onto buffer and filename will be truncated"
#endif

// Other defines
#define LOCAL_IP (netif_default->ip_addr.addr)
#define FTP_MAX_THREAD_NAME_LENGTH configMAX_TASK_NAME_LEN
#define MAX_NUM_PI_ARGS 3

/* Exported macros -----------------------------------------------------------*/

#define FTP_PRINTF(...)      \
  do {                       \
    taskENTER_CRITICAL();    \
    printf(__VA_ARGS__);     \
    taskEXIT_CRITICAL();     \
  } while (0)

#if FTP_DEBUG_ON && FTP_DEBUG_LEVEL
#define FTP_DEBUG(level, ...) do {      \
    if (level <= FTP_DEBUG_LEVEL) {     \
      FTP_PRINTF("[FTP] "__VA_ARGS__);  \
    }                                   \
  } while (0)
#else
#define FTP_DEBUG(...)
#endif

#if FTP_DEBUG_ON && FTP_SERVER_DEBUG_LEVEL
#define FTP_SERVER_DEBUG(level, ...) do {     \
    if (level <= FTP_SERVER_DEBUG_LEVEL) {    \
      FTP_PRINTF("[FTP SERVER] "__VA_ARGS__); \
    }                                         \
  } while (0)
#else
#define FTP_SERVER_DEBUG(...)
#endif

#if FTP_DEBUG_ON && FTP_SERVER_PI_DEBUG_LEVEL
#define FTP_SERVER_PI_DEBUG(level, ...) do {                \
    if (level <= FTP_SERVER_PI_DEBUG_LEVEL) {               \
      taskENTER_CRITICAL();                                 \
      FTP_PRINTF("[%s]", osThreadGetName(osThreadGetId())); \
      FTP_PRINTF(__VA_ARGS__);                              \
      taskEXIT_CRITICAL();                                  \
    }                                                       \
  } while (0)
#else
#define FTP_SERVER_PI_DEBUG(...)
#endif

#if FTP_DEBUG_ON && FTP_SERVER_DTP_DEBUG_LEVEL
#define FTP_SERVER_DTP_DEBUG(level, ...) do {               \
    if (level <= FTP_SERVER_DTP_DEBUG_LEVEL) {              \
      taskENTER_CRITICAL();                                 \
      FTP_PRINTF("[%s]", osThreadGetName(osThreadGetId())); \
      FTP_PRINTF(__VA_ARGS__);                              \
      taskEXIT_CRITICAL();                                  \
    }                                                       \
  } while (0)
#else
#define FTP_SERVER_DTP_DEBUG(...)
#endif

#define APPEND_RESPONSE_DATA( server, data_str )                                   \
  do {                                                                             \
    int max_len = FTP_SERVER_SEND_BUF_LEN - (server)->pi.send_buff_put_offset - 2; \
    char *ptr = (server)->pi.send_buffer + (server)->pi.send_buff_put_offset;      \
    (server)->pi.send_buff_put_offset += strlcpy(ptr, data_str, max_len);          \
  } while (0)

#if FTP_SERVER_RESPONSE_MESSAGE
#define APPEND_RESPONSE_MSG( server, msg_str ) APPEND_RESPONSE_DATA(server, msg_str)
#else
#define APPEND_RESPONSE_MSG( server, msg ) (void) (msg)
#endif

#define SET_RESPONSE( server, code_str, msg_str)   \
  do {                                             \
    memcpy((server)->pi.send_buffer, code_str, 3); \
    (server)->pi.send_buffer[3] = ' ';             \
    (server)->pi.send_buff_put_offset = 4;         \
    APPEND_RESPONSE_MSG(server, msg_str);          \
  } while (0)

#define APPEND_TERM( server ) APPEND_RESPONSE_DATA(server, "\r\n")

#define CLEAR_RESPONSE( server ) ((server)->pi.send_buff_put_offset = 0)

/* Private typedef -----------------------------------------------------------*/

/* Enumerations */

typedef enum {
  LOGIN_INFO_TYPE_USERNAME,
  LOGIN_INFO_TYPE_PASSWORD,
  LOGIN_INFO_TYPE_ACCOUNT,
} login_info_type;

typedef enum {
  WAIT_USER = 0,
  WAIT_PASS,
  WAIT_ACCT,
  LOGGED_IN,
} login_state_t;

typedef enum {
  REP_TYPE_ASCII = 0,
  REP_TYPE_EBCDIC,
  REP_TYPE_IMAGE,
  REP_TYPE_LOCAL_BYTE,
} representation_type_t;

typedef enum {
  REP_SUBTYPE_NON_PRINT = 0,
  REP_SUBTYPE_TELNET,
  REP_SUBTYPE_CARRIAGE_CONTROL,
} representation_subtype_t;

typedef enum {
  STRUCTURE_FILE = 0,
  STRUCTURE_RECORD,
  STRUCTURE_PAGE,
} structure_t;

typedef enum {
  TRANSFER_MODE_STREAM = 0,
  TRANSFER_MODE_BLOCK,
  TRANSFER_MODE_COMPRESSED,
} transfer_mode_t;

typedef enum {
  DTP_MODE_ACTIVE = 0,
  DTP_MODE_PASSIVE
} dtp_mode_t;

typedef enum {
  CMD_USER, CMD_PASS, CMD_ACCT, CMD_CWD,  CMD_CDUP, CMD_SMNT, CMD_REIN, CMD_QUIT,
  CMD_PORT, CMD_PASV, CMD_TYPE, CMD_STRU, CMD_MODE, CMD_RETR, CMD_STOR, CMD_STOU,
  CMD_APPE, CMD_ALLO, CMD_REST, CMD_RNFR, CMD_RNTO, CMD_ABOR, CMD_DELE, CMD_RMD,
  CMD_MKD,  CMD_PWD,  CMD_LIST, CMD_NLST, CMD_SITE, CMD_SYST, CMD_STAT, CMD_HELP,
  CMD_NOOP, NUM_CMD
} cmd_t;

typedef enum {
  FTP_SERVER_DTP_COMMAND_NONE,
  FTP_SERVER_DTP_COMMAND_RETR,
  FTP_SERVER_DTP_COMMAND_STOR,
  FTP_SERVER_DTP_COMMAND_APPE,
  FTP_SERVER_DTP_COMMAND_REST,
  FTP_SERVER_DTP_COMMAND_ABOR,
  FTP_SERVER_DTP_COMMAND_LIST,
  FTP_SERVER_DTP_COMMAND_NLST,
  FTP_SERVER_DTP_COMMAND_CLOSE,
  FTP_SERVER_DTP_COMMAND_NUM
} ftp_server_dtp_command_t;

typedef enum {
  FTP_SERVER_DTP_COMMAND_RESP_ACCEPTED,
  FTP_SERVER_DTP_COMMAND_RESP_REJECTED,
  FTP_SERVER_DTP_COMMAND_RESP_SUPERFLUOUS,
  FTP_SERVER_DTP_COMMAND_RESP_FINISHED,
  FTP_SERVER_DTP_COMMAND_RESP_EXITING_ERROR,
  FTP_SERVER_DTP_COMMAND_RESP_NUM
} ftp_server_dtp_command_response_t;

/* States */

typedef struct {
  char user_name[FTP_MAX_USERNAME_LEN]; /*!< Only valid when login_state > WAIT_USER */
  char account[FTP_MAX_ACCOUNT_LEN];    /*!< Only valid when login_state > WAIT_ACCT */
  ftp_permission_t perm;
  login_state_t login_state;
} user_t;

typedef struct {
  DIR dir;
} pi_fs_state_t;

typedef struct {
  int conn;
  unsigned int pi_index;
  unsigned char *recv_buffer;
  unsigned char *send_buffer;
  unsigned char *path_buffer;
  unsigned int send_buff_put_offset;
  int path_buffer_used;
  osThreadId_t dtp_thread;
  osMessageQueueId_t pi_to_dtp_msg_queue;
  osMessageQueueId_t dtp_to_pi_msg_queue;
  pi_fs_state_t fs;
  cmd_t prev_cmd;
} ftp_server_pi_t;

typedef struct {
  dtp_mode_t mode;
  int passive_sd;
  representation_type_t type;
  representation_subtype_t subtype;
  int num_bits;
  structure_t structure;
  transfer_mode_t transfer_mode;
  struct sockaddr_in server_address;
  struct sockaddr_in client_address;
} ftp_server_dtp_settings_t;

typedef struct {
  osMessageQueueId_t pi_to_dtp_msg_queue;
  osMessageQueueId_t dtp_to_pi_msg_queue;
  ftp_server_dtp_settings_t settings;
  ftp_server_dtp_command_t active_cmd;
  int conn;
  FIL current_file;
  DIR current_dir;
  FILINFO current_info;
  int list_file_only;
  char buff[FTP_SERVER_DTP_BUFFER_LEN];
  int buff_len_used;
  int buff_offset;
  int finish_pending;
} ftp_server_dtp_channel_t;

typedef struct {
  user_t user;
  ftp_credentials_check_fn credentials_check_fn;
  ftp_server_pi_t pi;
  ftp_server_dtp_settings_t dtp_settings;
} ftp_server_t;

/* Thread Arguments */

typedef struct {
  unsigned int pi_index;
  int conn;
  struct sockaddr_in client;
} server_pi_args_t;

typedef struct {
  osMessageQueueId_t pi_to_dtp_msg_queue;
  osMessageQueueId_t dtp_to_pi_msg_queue;
  ftp_server_dtp_settings_t *settings;
} server_dtp_args_t;

/* Message Queue Types */

typedef struct {
  ftp_server_dtp_command_t command;
  char *filename_buff;
} ftp_server_pi_to_dtp_msg_t;

typedef struct {
  ftp_server_dtp_command_response_t cmd_resp;
} ftp_server_dtp_to_pi_msg_t;

/* Private function prototypes -----------------------------------------------*/

// Thread functions
void _ftp_server_thread(void *);
void _ftp_server_pi_thread(void *);
void _ftp_server_dtp_thread(void *);

// PI functions
int _send_status_msg(ftp_server_t *server);
int _receive_and_process_ctrl_msg(ftp_server_t *server, int blocking);
int _check_dtp_response(ftp_server_t *server);
int _process_ctrl_msg(ftp_server_t *server, char *buff, int len);
int _check_global_permission(ftp_server_t *server, cmd_t cmd);
void _check_login_credentials(ftp_server_t *server, login_info_type type, const char *str, unsigned int str_len);
void _set_type(ftp_server_t *server, int num_args, char *args[], unsigned int arglens[]);
void _set_structure(ftp_server_t *server, int num_args, char *args[], unsigned int arglens[]);
void _set_transfer_mode(ftp_server_t *server, int num_args, char *args[], unsigned int arglens[]);
void _set_passive(ftp_server_t *server);
void _set_data_port(ftp_server_t *server, char *arg, unsigned int arglen);
void _get_stat(ftp_server_t *server, char *path, unsigned int arglen);
void _execute_fs_command(ftp_server_t *server, ftp_server_dtp_command_t fs_cmd, char *path, unsigned int arglen);

int _open_dtp_channel(ftp_server_t *server);
int _close_dtp_channel(ftp_server_t *server);

// DTP functions
int _dtp_execute_command(ftp_server_dtp_channel_t *dtp,
                         ftp_server_dtp_command_t dtp_cmd,
                         char *args,
                         ftp_server_dtp_to_pi_msg_t *resp);
int _dtp_send_receive(ftp_server_dtp_channel_t *dtp);
int _dtp_listitem_fat(char *buff, FILINFO* info, unsigned int buff_length);
int _dtp_listitem_unix(char *buff, FILINFO* info, unsigned int buff_length);

// Other functions
void _dump_binary(const unsigned char *buff, const unsigned int len);

ftp_login_result_t _default_credentials_check_fn(
  char *username, char *password, char *account, ftp_permission_t* perm);

/* Private variables ---------------------------------------------------------*/

const char *const fat_result_msg_table[FR_INVALID_PARAMETER + 1] = {
#if FTP_SERVER_RESPONSE_MESSAGE
/* FR_OK                  */ "Succeeded",
/* FR_DISK_ERR            */ "A hard error occurred in the low level disk I/O layer",
/* FR_INT_ERR             */ "Assertion failed",
/* FR_NOT_READY           */ "The physical drive cannot work",
/* FR_NO_FILE             */ "Could not find the file",
/* FR_NO_PATH             */ "Could not find the path",
/* FR_INVALID_NAME        */ "The path name format is invalid",
/* FR_DENIED              */ "Access denied due to prohibited access or directory full",
/* FR_EXIST               */ "Access denied due to prohibited access",
/* FR_INVALID_OBJECT      */ "The file/directory object is invalid",
/* FR_WRITE_PROTECTED     */ "The physical drive is write protected",
/* FR_INVALID_DRIVE       */ "The logical drive number is invalid",
/* FR_NOT_ENABLED         */ "The volume has no work area",
/* FR_NO_FILESYSTEM       */ "There is no valid FAT volume",
/* FR_MKFS_ABORTED        */ "The f_mkfs() aborted due to any problem",
/* FR_TIMEOUT             */ "Could not access the volume within defined period",
/* FR_LOCKED              */ "The operation is rejected according to the file sharing policy",
/* FR_NOT_ENOUGH_CORE     */ "LFN working buffer could not be allocated",
/* FR_TOO_MANY_OPEN_FILES */ "Number of open files > _FS_LOCK",
/* FR_INVALID_PARAMETER   */ "Given parameter is invalid",
#else
"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
#endif
};

const char *const cmd_str[NUM_CMD] = {
  "USER", "PASS", "ACCT", "CWD",  "CDUP", "SMNT", "REIN", "QUIT",
  "PORT", "PASV", "TYPE", "STRU", "MODE", "RETR", "STOR", "STOU",
  "APPE", "ALLO", "REST", "RNFR", "RNTO", "ABOR", "DELE", "RMD",
  "MKD", "PWD", "LIST", "NLST", "SITE", "SYST", "STAT", "HELP",
  "NOOP"
};

const unsigned char cmd_min_num_args[NUM_CMD] = {
  1, 1, 1, 1, 0, 1, 0, 0,
  1, 0, 1, 1, 1, 1, 1, 0,
  1, 1, 1, 1, 1, 0, 1, 1,
  1, 0, 0, 0, 1, 0, 0, 0,
  0,
};

const unsigned char cmd_num_opt_args[NUM_CMD] = {
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 1, 0, 0, 0, 0, 0,
  0, 2, 0, 0, 0, 0, 0, 0,
  0, 0, 1, 1, 0, 0, 1, 1,
  0,
};

const ftp_permission_t cmd_perm_req[NUM_CMD] = {
  FTP_PERM_NONE,  FTP_PERM_NONE,  FTP_PERM_NONE,  FTP_PERM_VIEW,
  FTP_PERM_VIEW,  FTP_PERM_VIEW,  FTP_PERM_NONE,  FTP_PERM_NONE,
  FTP_PERM_VIEW,  FTP_PERM_VIEW,  FTP_PERM_VIEW,  FTP_PERM_VIEW,
  FTP_PERM_VIEW,  FTP_PERM_READ,  FTP_PERM_ADMIN, FTP_PERM_WRITE,
  FTP_PERM_ADMIN, FTP_PERM_WRITE, FTP_PERM_VIEW,  FTP_PERM_ADMIN,
  FTP_PERM_ADMIN, FTP_PERM_VIEW,  FTP_PERM_ADMIN, FTP_PERM_ADMIN,
  FTP_PERM_WRITE, FTP_PERM_VIEW,  FTP_PERM_VIEW,  FTP_PERM_VIEW,
  FTP_PERM_VIEW,  FTP_PERM_VIEW,  FTP_PERM_VIEW,  FTP_PERM_NONE,
  FTP_PERM_NONE,
};

const ftp_server_dtp_settings_t _ftp_dtp_default_settings = {
  .mode = DTP_MODE_ACTIVE,
  .passive_sd = 0,
  .type = REP_TYPE_ASCII,
  .subtype = REP_SUBTYPE_NON_PRINT,
  .num_bits = 8,
  .structure = STRUCTURE_FILE,
  .transfer_mode = TRANSFER_MODE_STREAM,
  .client_address = { 0 },
  .server_address = { 0 },
};

const char *const dtp_cmd_str[FTP_SERVER_DTP_COMMAND_NUM] = {
  "NONE", "RETR", "STOR", "APPE", "REST", "ABOR", "LIST", "NLST",
  "CLOSE",
};

const char *const dtp_cmd_resp_str[FTP_SERVER_DTP_COMMAND_RESP_NUM] = {
  "ACCEPTED", "REJECTED", "SUPERFLUOUS", "FINISHED", "EXITING_ERROR",
};

const char *const dtp_month_str[16] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", "???", "???", "???", "???"
};

/* Exported functions --------------------------------------------------------*/

osThreadId_t ftp_server_init() {

  osThreadId_t taskHandle;
  const osThreadAttr_t task_attributes = {
    .name = "FTP_Thread",
    .stack_size = FTP_SERVER_THREAD_STACKSIZE,
    .priority = (osPriority_t) osPriorityLow,
  };

  // Create a thread for the FTP server
  taskHandle = osThreadNew(_ftp_server_thread, NULL, &task_attributes);

  // Check if task was created
  if (taskHandle == 0) {
    FTP_DEBUG(1, "Failed to create FTP Server.\n");
  } else {
    FTP_DEBUG(1, "Created FTP Server.\n");
  }
  return taskHandle;
}

/* Private functions ---------------------------------------------------------*/

void _ftp_server_thread(void *args) {
  (void) args;

  // Connection-related
  int sd, size;
  struct sockaddr_in address;

  // List of PI threads
  osThreadId_t *pi_task = NULL;
  osThreadId_t pi_threads[FTP_SERVER_MAX_PI_NUM] = { 0 };
  unsigned int pi_task_index;

  // Arguments used for protocol interpreter creation
  osThreadAttr_t pi_task_attributes;
  server_pi_args_t pi_args;
  char pi_thread_name[FTP_MAX_THREAD_NAME_LENGTH];

  // Initialize PI attributes
  memset(&pi_task_attributes, 0x00, sizeof(pi_task_attributes));
  pi_task_attributes.name = pi_thread_name;
  pi_task_attributes.stack_size = FTP_SERVER_PI_THREAD_STACKSIZE;

  // Create a TCP socket
  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    FTP_SERVER_DEBUG(1, "Failed to open socket.\n");
    return;
  }

  // Setup the port information
  address.sin_family = AF_INET;
  address.sin_port = htons(FTP_SERVER_DEFAULT_CONTROL_PORT);
  address.sin_addr.s_addr = INADDR_ANY;

  // Bind the port
  if (bind(sd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    FTP_SERVER_DEBUG(1, "Failed to bind socket to port %u.\n", FTP_SERVER_DEFAULT_CONTROL_PORT);
    return;
  }

  // Start listening for incomming connections
  FTP_SERVER_DEBUG(2, "Started listening for incomming connections.\n");
  listen(sd, 1);
  size = sizeof(pi_args.client);

  while (1) {
    // Start accepting connections
    pi_args.conn = accept(sd, (struct sockaddr *)&pi_args.client, (socklen_t *)&size);

    // Check if any PI task is available
    pi_task = NULL;
    for (pi_task_index = 0; pi_task_index < FTP_SERVER_MAX_PI_NUM; pi_task_index++) {
      if (pi_threads[pi_task_index] == NULL ||
          osThreadGetState(pi_threads[pi_task_index]) == osThreadTerminated) {
        pi_task = &pi_threads[pi_task_index];
        break;
      }
    }
    if (pi_task == NULL) {
      FTP_SERVER_DEBUG(1, "Cannot accept any new server connection. No PIs available.\n");
      close(pi_args.conn);
      continue;
    }
    FTP_SERVER_DEBUG(2, "Accepted new Server connection.\n");

    // Set the PI name
    snprintf(pi_thread_name, sizeof(pi_thread_name), "FTP_S_%03u_PI", pi_task_index);

    // Set the PI priority greater than this priority for instant cration
    pi_task_attributes.priority = (osPriority_t) osThreadGetPriority(osThreadGetId()) + 1;
    pi_args.pi_index = pi_task_index;

    // Create a new task for the PI
    *pi_task = osThreadNew(_ftp_server_pi_thread, (void*) &pi_args, &pi_task_attributes);

    if (*pi_task == NULL) {
      FTP_SERVER_DEBUG(1, "Failed to create new FTP PI thread.\n");
      close(pi_args.conn);
    } else {
      FTP_SERVER_DEBUG(1, "Created new FTP PI thread.\n");
    }
  }

  // On thermination, close the socket (this should never happen)
  close(sd);
}

void _ftp_server_pi_thread(void *args) {
  // Argument parsing
  server_pi_args_t *pi_args = (server_pi_args_t *)args;

  // Server related information
  ftp_server_t server;
  unsigned char recv_buffer[FTP_SERVER_RECV_BUF_LEN];
  unsigned char send_buffer[FTP_SERVER_SEND_BUF_LEN];
  unsigned char path_buffer[FTP_SERVER_PATH_BUF_LEN];
  int sts = 0;
  int blocking = 0;

  // Initialize the server state
  memset(&server, 0x00, sizeof(server));
  server.credentials_check_fn = _default_credentials_check_fn;
  server.pi.recv_buffer = recv_buffer;
  server.pi.send_buffer = send_buffer;
  server.pi.path_buffer = path_buffer;
  server.pi.path_buffer_used = 0;
  server.pi.prev_cmd = CMD_NOOP;
  memcpy(&server.dtp_settings, &_ftp_dtp_default_settings, sizeof(ftp_server_dtp_settings_t));

  // Copy arguments
  server.dtp_settings.client_address.sin_addr.s_addr = pi_args->client.sin_addr.s_addr;
  server.dtp_settings.client_address.sin_port = pi_args->client.sin_port;
  server.pi.pi_index = pi_args->pi_index;
  server.pi.conn = pi_args->conn;

  // Initialize the file system
  f_chdir("/");

  // Created and initialized FTP server
  FTP_SERVER_PI_DEBUG(1, "Created new Protocol Interpreter for FTP Server.\n");

  // Send a welcome message
  SET_RESPONSE( &server, "220", "awaiting input.");
  sts = _send_status_msg(&server);

  // Enter the command cycle
  while (sts >= 0) {
    blocking = (server.pi.dtp_thread == NULL);

    // Read new data and execute command
    if (_receive_and_process_ctrl_msg(&server, blocking) < 0) {
      break;
    }

    // Check for responses from the DTP
    if (_check_dtp_response(&server) < 0) {
      break;
    }

    // Yield at the end of the command cycle
    osThreadYield();
  }
  // Close the connection
  close(server.pi.conn);
  FTP_SERVER_PI_DEBUG(2, "Closed connection.\n");

  // Exit the thread
  osThreadExit();
}

void _ftp_server_dtp_thread(void *args) {
  // Argument parsing
  server_dtp_args_t *dtp_args = (server_dtp_args_t *)args;

  // DTP informantion
  ftp_server_dtp_channel_t dtp;
  ftp_server_pi_to_dtp_msg_t pi_to_dtp_msg;
  ftp_server_dtp_to_pi_msg_t dtp_to_pi_msg;

  // Socket-related
  int sd = -1;
  int sts = 0;

  // Other state
  uint32_t timeout;
  osStatus_t q_sts;

  // Copy arguments
  dtp.dtp_to_pi_msg_queue = dtp_args->dtp_to_pi_msg_queue;
  dtp.pi_to_dtp_msg_queue = dtp_args->pi_to_dtp_msg_queue;
  memcpy(&dtp.settings, dtp_args->settings, sizeof(ftp_server_dtp_settings_t));
  dtp.active_cmd = FTP_SERVER_DTP_COMMAND_NONE;

  // Try to initialize DTP connection
  do {
    // Check DTP mode
    if (dtp.settings.mode == DTP_MODE_ACTIVE) {
      // Active mode: Establish connection to client
      // Open a new socket
      if ((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        FTP_SERVER_DTP_DEBUG(1, "Failed to open socket.\n");
        sts = -1;
        break;
      }
      dtp.conn = connect(sd, (struct sockaddr *) &dtp.settings.client_address, sizeof(dtp.settings.client_address));
    } else {
      // Passive mode: Wait for User to establish connection
      FTP_SERVER_DTP_DEBUG(1, "Waiting for user to establish connection.\n");
      dtp.conn = accept(dtp.settings.passive_sd, NULL, NULL);
    }

    // Check the connection
    if (dtp.conn < 0) {
      FTP_SERVER_DTP_DEBUG(1, "Failed to connect to client address.\n");
      sts = -1;
      break;
    }
  } while (0);

  FTP_SERVER_DTP_DEBUG(1, "Initialized DTP.\n");

  // Loop until broken
  while(sts >= 0) {
    // Check for new command from the PI
    timeout = (dtp.active_cmd == FTP_SERVER_DTP_COMMAND_NONE) ? osWaitForever : 0;
    q_sts = osMessageQueueGet(dtp.pi_to_dtp_msg_queue, &pi_to_dtp_msg, NULL, timeout);
    // Check if we actually got a command
    if (q_sts == osErrorParameter) {
      FTP_SERVER_DTP_DEBUG(1, "Failed to receive control message from PI.\n");
      sts = -1;
      break;
    } else if (q_sts == osOK) {
      // Execute the command (prepare for sending/receiving)
      sts = _dtp_execute_command(&dtp, pi_to_dtp_msg.command, pi_to_dtp_msg.filename_buff, &dtp_to_pi_msg);
      if (sts < 0) break;

      // Send a response to the PI. Always wait for the PI to read the messages
      if (osMessageQueuePut(dtp.dtp_to_pi_msg_queue, &dtp_to_pi_msg, 0, osWaitForever) != osOK) {
        FTP_SERVER_DTP_DEBUG(1, "Failed to send response message to PI.\n");
      }
    }

    // check send/recieve functions depending on active command
    sts = _dtp_send_receive(&dtp);
    if (sts != 0) break;

    // Always yield at the end of the execution cycle
    osThreadYield();
  }

  FTP_SERVER_DTP_DEBUG(1, "Exited command cycle with sts %i.\n", sts);

  // Make sure to close files/folders in case they are open
  f_close(&dtp.current_file);
  f_closedir(&dtp.current_dir);

  // Send the exiting state to the PI
  if (sts > 0) {
    dtp_to_pi_msg.cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_FINISHED;
  } else {
    dtp_to_pi_msg.cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_EXITING_ERROR;
  }
  if (osMessageQueuePut(dtp.dtp_to_pi_msg_queue, &dtp_to_pi_msg, 0, osWaitForever) != osOK) {
    FTP_SERVER_DTP_DEBUG(1, "Could not send exiting message to PI.\n");
  }

  // Close the sockets
  if (dtp.conn >= 0) close(dtp.conn);
  if (sd >= 0) close(sd);

  // Exit the DTP Thread
  FTP_SERVER_DTP_DEBUG(1, "Exiting...\n");
  osThreadExit();
}

int _send_status_msg(ftp_server_t *server) {
  int send_len;

  // Include the CRLN
  strcpy(server->pi.send_buffer + server->pi.send_buff_put_offset, "\r\n");
  send_len = server->pi.send_buff_put_offset + 2;
  FTP_SERVER_PI_DEBUG(2, "Sending Control Data: %.*s", send_len, server->pi.send_buffer);

  // Send the data
  if (send(server->pi.conn, server->pi.send_buffer, send_len, 0) != send_len) {
    FTP_SERVER_PI_DEBUG(1, "Failed to send Control Data.\n");
    return -1;
  }

  // Clear the response buffer
  CLEAR_RESPONSE(server);
  return 0;
}

int _receive_and_process_ctrl_msg(ftp_server_t *server, int blocking) {
  int recv_len;
  int sts;

  recv_len = recv(server->pi.conn, server->pi.recv_buffer, FTP_SERVER_RECV_BUF_LEN, blocking ? 0 : MSG_DONTWAIT);
  if (recv_len < 0) {
    if (blocking || errno != EWOULDBLOCK) {
      FTP_SERVER_PI_DEBUG(1, "Failed to read data.\n");
      return -1;
    } else {
      sts = 0;
    }
  } else if (recv_len == 0) {
    if (!blocking) {
      FTP_SERVER_PI_DEBUG(1, "Connection closed by Client.\n");
      return -1;
    }
  } else {
    FTP_SERVER_PI_DEBUG(2, "Received Control Data: %.*s", recv_len, server->pi.recv_buffer);

    // Zero-terminate the receive buffer
    server->pi.recv_buffer[recv_len == FTP_SERVER_RECV_BUF_LEN ? (recv_len - 1) : recv_len] = '\0';

    // Correctly received a message. Process it
    sts = _process_ctrl_msg(server, server->pi.recv_buffer, recv_len);

    // If processing set a response, send it
    if (server->pi.send_buff_put_offset != 0) {
      if (_send_status_msg(server) < 0) {
        sts = -1;
      }
    }
  }
  return sts;
}

int _check_dtp_response(ftp_server_t *server) {
  ftp_server_dtp_to_pi_msg_t dtp_to_pi_msg;
  int sts = 0;

  // Check if the DTP sent a response
  if (osMessageQueueGetCount(server->pi.dtp_to_pi_msg_queue) == 0) {
    return 0;
  }

  // Retrieve the response
  if (osMessageQueueGet(server->pi.dtp_to_pi_msg_queue, &dtp_to_pi_msg, 0, 0) != osOK) {
    FTP_SERVER_PI_DEBUG(1, "Failed to get response from DTP.\n");
    return -1;
  }

  FTP_SERVER_PI_DEBUG(2, "Received Response from DTP: %s.\n", dtp_cmd_resp_str[dtp_to_pi_msg.cmd_resp]);

  // Set response depending on DTP response
  switch (dtp_to_pi_msg.cmd_resp) {
    case FTP_SERVER_DTP_COMMAND_RESP_ACCEPTED:
      SET_RESPONSE(server, "150", "File status okay; about to open data connection.");
      break;
    case FTP_SERVER_DTP_COMMAND_RESP_REJECTED:
      SET_RESPONSE(server, "450", "Requested file action not taken.");
      break;
    case FTP_SERVER_DTP_COMMAND_RESP_SUPERFLUOUS:
    case FTP_SERVER_DTP_COMMAND_RESP_FINISHED:
      SET_RESPONSE(server, "250", "Requested file action okay, completed.");
      break;
    case FTP_SERVER_DTP_COMMAND_RESP_EXITING_ERROR:
      SET_RESPONSE(server, "451", "Requested action aborted: local error in processing.");
      break;
    default:
      sts = -1;
      break;
  }

  // Check if DTP was closed
  switch (dtp_to_pi_msg.cmd_resp) {
    case FTP_SERVER_DTP_COMMAND_RESP_FINISHED:
    case FTP_SERVER_DTP_COMMAND_RESP_EXITING_ERROR:
      // Mark the thread as closed and clean up the connection
      server->pi.dtp_thread = NULL;
      _close_dtp_channel(server);
    break;
    default:
    break;
  }

  // If the response requires sending a response to the user, send it
  if (server->pi.send_buff_put_offset != 0) {
    if (_send_status_msg(server) < 0) {
      sts = -1;
    }
  }

  return sts;
}

int _process_ctrl_msg(ftp_server_t *server, char *buff, int len) {
  char *ptr = buff;
  unsigned int incr;
  cmd_t cmd = NUM_CMD;

  FTP_SERVER_PI_DEBUG(3, "Received Command: %s", buff);
  CLEAR_RESPONSE(server);

  // Check buffer termination
  if (len < 2 || !(ptr[len-2] == '\r' && ptr[len-1] == '\n')) {
    // Unknown Command
    FTP_SERVER_PI_DEBUG(1, "Invalid Command Termination:%s", buff);
    SET_RESPONSE(server, "500", "Syntax Error: Command too long or invalid termination.");
    return 0;
  }
  // remove CRLN
  ptr[len-2] = '\0';
  len -= 2;

  // Find the command
  for (cmd = 0; cmd < NUM_CMD; cmd++) {
    if (strncmp(ptr, cmd_str[cmd], strlen(cmd_str[cmd])) == 0) break;
  }

  // Check if command is valid
  if (cmd >= NUM_CMD) {
    // Unknown Command
    FTP_SERVER_PI_DEBUG(1, "Unknown Command:%s", buff);
    SET_RESPONSE(server, "500", "Syntax Error: Command unrecognized.");
    return 0;
  }

  // Advance the buffer by the command length
  incr = strlen(cmd_str[cmd]);
  ptr += incr;
  len -= incr;

  // Remove spaces
  while (*ptr == ' ' && len > 0) {
    ptr++;
    len--;
  }

  // Unpack the arguments
  unsigned int num_args = cmd_min_num_args[cmd] + cmd_num_opt_args[cmd];
  char *args[MAX_NUM_PI_ARGS];
  unsigned int arglens[MAX_NUM_PI_ARGS];
  unsigned int i;
  if (num_args > MAX_NUM_PI_ARGS) num_args = MAX_NUM_PI_ARGS;
  // Extract all arguments
  for (i = 0; i < num_args; i++) {
    arglens[i] = strcspn(ptr, " \0");
    if (arglens[i] == 0) {
      args[i] = NULL;
      break;
    }
    args[i] = ptr;
    ptr += arglens[i];
    len -= arglens[i];
  }
#if FTP_SERVER_PI_DEBUG_LEVEL >= 3
  FTP_SERVER_PI_DEBUG(3, "Unpacked Command: %s with arguments:\n", cmd_str[cmd]);
  for (unsigned int ii = 0; ii < i; ii++) {
    FTP_SERVER_PI_DEBUG(3, "%.*s\n", arglens[ii], args[ii]);
  }
#endif

  // Check that at least the minimum number of arguments were sent
  if (i < cmd_min_num_args[cmd]) {
    FTP_SERVER_PI_DEBUG(1, "Not enough arguments provided. Required %u, got %u :%s", cmd_min_num_args[cmd], i, buff);
    SET_RESPONSE(server, "501", "Not enough arguments provided.");
    return 0;
  }
  // Fill the remaining args with NULL
  for (; i < num_args; i++) {
    args[i] = NULL;
    arglens[i] = 0;
  }
  // Check that the whole command string was consumed
  if (len != 0) {
    FTP_SERVER_PI_DEBUG(1, "Too many arguments provided: %s", buff);
    SET_RESPONSE(server, "501", "Too many arguments provided.");
    return 0;
  }

  // Check if the user is allowed to execute the command
  if (_check_global_permission(server, cmd) < 0) {
    FTP_SERVER_PI_DEBUG(1, "The current user does not have permission to execute :%s", buff);
    if (server->user.login_state == LOGGED_IN) {
      SET_RESPONSE(server, "530", "User not permitted to take action.");
    } else {
      SET_RESPONSE(server, "530", "Not logged in.");
    }
    return 0;
  }

  // Execute command. Commands will set server response.
  switch(cmd) {
    case CMD_USER:
      _check_login_credentials(server, LOGIN_INFO_TYPE_USERNAME, args[0], arglens[0]);
      break;
    case CMD_PASS:
      if (server->pi.prev_cmd == CMD_USER) {
        _check_login_credentials(server, LOGIN_INFO_TYPE_PASSWORD, args[0], arglens[0]);
      } else {
        SET_RESPONSE(server, "503", "Bad sequence of commands.");
      }
      break;
    case CMD_ACCT:
      _check_login_credentials(server, LOGIN_INFO_TYPE_ACCOUNT, args[0], arglens[0]);
      break;
    case CMD_CWD:
    case CMD_SMNT:
      if (f_chdir(args[0]) == FR_OK) {
        SET_RESPONSE(server, "250", "Requested file action okay, completed.");
      } else {
        SET_RESPONSE(server, "550", "Request action not taken");
      }
      break;
    case CMD_CDUP:
      if (f_chdir("/") == FR_OK) {
        SET_RESPONSE(server, "200", "Command successful.");
      } else {
        SET_RESPONSE(server, "550", "Request action not taken");
      }
      break;
    case CMD_REIN:
      server->user.login_state = WAIT_USER;
      server->user.perm = FTP_PERM_NONE;
      SET_RESPONSE(server, "200", "Command successful.");
      break;
    case CMD_QUIT:
      SET_RESPONSE(server, "200", "Command successful.");
      return -1;
      break;
    case CMD_PORT:
      _set_data_port(server, args[0], arglens[0]);
      break;
    case CMD_PASV:
      _set_passive(server);
      break;
    case CMD_TYPE:
      _set_type(server, num_args, args, arglens);
      break;
    case CMD_STRU:
      _set_structure(server, num_args, args, arglens);
      break;
    case CMD_MODE:
      _set_transfer_mode(server, num_args, args, arglens);
      break;
    case CMD_RETR:
      _execute_fs_command(server, FTP_SERVER_DTP_COMMAND_RETR, args[0], arglens[0]);
      break;
    case CMD_STOR:
      _execute_fs_command(server, FTP_SERVER_DTP_COMMAND_STOR, args[0], arglens[0]);
      break;
    case CMD_STOU:
      SET_RESPONSE(server, "502", "Command not implemented.");
      break;
    case CMD_APPE:
      _execute_fs_command(server, FTP_SERVER_DTP_COMMAND_APPE, args[0], arglens[0]);
      break;
    case CMD_REST:
      _execute_fs_command(server, FTP_SERVER_DTP_COMMAND_REST, args[0], arglens[0]);
      break;
    case CMD_RNFR:
      if (server->pi.path_buffer_used) {
        FTP_SERVER_PI_DEBUG(1, "Cannot execute FS Command: Not enough buffer.\n");
        SET_RESPONSE(server, "451", "Requested action aborted: Not enough buffer.");
      } else {
        server->pi.path_buffer_used = 1;
        strncpy(server->pi.path_buffer, args[0], FTP_SERVER_PATH_BUF_LEN);
        SET_RESPONSE(server, "350", "Requested file action pending further information.");
      }
      break;
    case CMD_RNTO:
      if (server->pi.prev_cmd == CMD_RNFR) {
        if (f_rename(server->pi.path_buffer, args[0]) == FR_OK) {
          SET_RESPONSE(server, "250", "Requested file action okay, completed.");
        } else {
          SET_RESPONSE(server, "553", "File name not allowed.");
        }
        server->pi.path_buffer_used = 0;
      } else {
        SET_RESPONSE(server, "503", "Bad sequence of commands.");
      }
      break;
    case CMD_ABOR:
      _execute_fs_command(server, FTP_SERVER_DTP_COMMAND_ABOR, NULL, 0);
      break;
    case CMD_DELE:
      if (f_unlink(args[0]) == FR_OK) {
        SET_RESPONSE(server, "250", "Requested file action okay, completed.");
      } else {
        SET_RESPONSE(server, "550", "Request action not taken");
      }
      break;
    case CMD_RMD:
      if (f_rmdir(args[0]) == FR_OK) {
        SET_RESPONSE(server, "250", "Requested file action okay, completed.");
      } else {
        SET_RESPONSE(server, "550", "Request action not taken");
      }
      break;
    case CMD_MKD:
      if (f_mkdir(args[0]) == FR_OK) {
        SET_RESPONSE(server, "250", "Requested file action okay, completed.");
      } else {
        SET_RESPONSE(server, "550", "Request action not taken");
      }
      break;
    case CMD_PWD:
      if (server->pi.path_buffer_used) {
        FTP_SERVER_PI_DEBUG(1, "Cannot execute FS Command: Not enough buffer.\n");
        SET_RESPONSE(server, "451", "Requested action aborted: Not enough buffer.");
      } else {
        if (f_getcwd(server->pi.path_buffer, FTP_SERVER_PATH_BUF_LEN) == FR_OK) {
          SET_RESPONSE(server, "250", "");
          APPEND_RESPONSE_DATA(server, server->pi.path_buffer);
        } else {
          SET_RESPONSE(server, "550", "Request action not taken");
        }
      }
      break;
    case CMD_LIST:
      _execute_fs_command(server, FTP_SERVER_DTP_COMMAND_LIST, args[0], arglens[0]);
      break;
    case CMD_NLST:
      _execute_fs_command(server, FTP_SERVER_DTP_COMMAND_NLST, args[0], arglens[0]);
      break;
    case CMD_SITE:
      SET_RESPONSE(server, "202", "Command not implemented.");
      break;
    case CMD_SYST:
      SET_RESPONSE(server, "215", "ELF system type.");
      break;
    case CMD_STAT:
      _get_stat(server, args[0], arglens[0]);
      break;
    case CMD_HELP:
      SET_RESPONSE(server, "211", "For Help, consult the offical FTP documentation.");
      break;
    case CMD_ALLO:
    case CMD_NOOP:
      SET_RESPONSE(server, "200", "Command okay.");
      break;
    default:
      FTP_SERVER_PI_DEBUG(1, "Client requested unimplemented command: %s.\n", cmd_str[cmd]);
      SET_RESPONSE(server, "502", "Command not implemented.");
      break;
  }

  // clear the path buffer after an Rename From command
  if (server->pi.prev_cmd == CMD_RNFR) {
    server->pi.path_buffer_used = 0;
  }
  server->pi.prev_cmd = cmd;
  return 0;
}

int _check_global_permission(ftp_server_t* server, cmd_t cmd) {
  // Check arguments
  if (server == NULL || cmd >= NUM_CMD) return -1;
  // Check if current user permission is >= required permission for command
  return (int)(server->user.perm) - cmd_perm_req[cmd];
}

void _check_login_credentials(ftp_server_t *server, login_info_type type, const char *str, unsigned int str_len) {
  // Check arguments
  if (server == NULL || type > LOGIN_INFO_TYPE_ACCOUNT || str == NULL || str_len == 0) {
    FTP_SERVER_PI_DEBUG(1, "Illegal credential parameters.\n");
    SET_RESPONSE(server, "504", "Command not implemented for that parameter.");
    return;
  }

  // Variables
  char pw_buffer[FTP_MAX_PASSWORD_LEN];
  ftp_login_result_t retr = FTP_LOGIN_RESULT_FAILURE;
  ftp_permission_t perm;

  if (server->credentials_check_fn == NULL) {
    FTP_SERVER_PI_DEBUG(1, "No Credential check function set.\n");
    SET_RESPONSE(server, "451", "Credentials cannot be confirmed.");
    return;
  }

  // Check string length
  unsigned int max_length = 0;
  switch (type) {
    case LOGIN_INFO_TYPE_USERNAME:
      max_length = FTP_MAX_USERNAME_LEN;
      break;
    case LOGIN_INFO_TYPE_PASSWORD:
      max_length = FTP_MAX_PASSWORD_LEN;
      break;
    case LOGIN_INFO_TYPE_ACCOUNT:
      max_length = FTP_MAX_ACCOUNT_LEN;
      break;
  }

  if (str_len >= max_length) {
    FTP_SERVER_PI_DEBUG(1, "Credentials information do not fit into buffer.\n");
    SET_RESPONSE(server, "504", "Argument too long.");
    return;
  }

  // Check login state
  if ((type == LOGIN_INFO_TYPE_PASSWORD && server->user.login_state != WAIT_PASS) ||
      (type == LOGIN_INFO_TYPE_ACCOUNT  && server->user.login_state != WAIT_ACCT)) {
    FTP_SERVER_PI_DEBUG(1, "Bad sequence of Login commands.\n");
    SET_RESPONSE(server, "503", "Bad Sequence of commands.");
    return;
  }

  // Check credentials
  switch (type) {
    case LOGIN_INFO_TYPE_USERNAME:
      memcpy(server->user.user_name, str, str_len);
      server->user.user_name[str_len] = '\0';
      retr = server->credentials_check_fn(server->user.user_name, NULL, NULL, &perm);
      break;
    case LOGIN_INFO_TYPE_PASSWORD:
      memcpy(pw_buffer, str, str_len);
      pw_buffer[str_len] = '\0';
      retr = server->credentials_check_fn(server->user.user_name, pw_buffer, NULL, &perm);
      break;
    case LOGIN_INFO_TYPE_ACCOUNT:
      memcpy(server->user.account, str, str_len);
      server->user.account[str_len] = '\0';
      retr = server->credentials_check_fn(server->user.user_name, NULL, server->user.account, &perm);
      break;
  }

  // formulate response depending on retr
  switch (retr) {
    case FTP_LOGIN_RESULT_MORE_INFO_REQUIRED:
      switch (server->user.login_state) {
        case WAIT_USER:
        case LOGGED_IN:
          server->user.login_state = WAIT_PASS;
          SET_RESPONSE(server, "331", "User name okay, need password.");
          break;
        case WAIT_PASS:
          server->user.login_state = WAIT_ACCT;
          SET_RESPONSE(server, "332", "Need account for login.");
          break;
        case WAIT_ACCT:
        default:
          SET_RESPONSE(server, "451", "Requested action aborted: local error in processing.");
          server->user.login_state = WAIT_USER;
          break;
      }
      break;
    case FTP_LOGIN_RESULT_SUCCESS:
      server->user.login_state = LOGGED_IN;
      SET_RESPONSE(server, "230", "User logged in, proceed.");
      break;
    case FTP_LOGIN_RESULT_FAILURE:
    default:
      server->user.login_state = WAIT_USER;
      SET_RESPONSE(server, "532", "Login Failed.");
      break;
  }

  // Safety cleanup
  if (server->user.login_state == LOGGED_IN) {
    server->user.perm = perm;
  } else {
    server->user.perm = FTP_PERM_NONE;
  }
  FTP_SERVER_PI_DEBUG(2, "Set permission level to %i.\n", (int) server->user.perm);
}

void _set_type(ftp_server_t *server, int num_args, char *args[], unsigned int arglens[]) {
  representation_type_t type;
  representation_subtype_t subtype;
  int syntax_error = 0;
  int parameter_error = 0;
  int not_supported = 0;
  int num_bits = 8;

  if (arglens[0] != 1) {
    syntax_error = 1;
  } else {
    switch (*args[0]) {
      case 'A':
        type = REP_TYPE_ASCII;
        break;
      case 'E':
        type = REP_TYPE_EBCDIC;
        not_supported = 1;
        break;
      case 'I':
        type = REP_TYPE_IMAGE;
        break;
      case 'L':
        type = REP_TYPE_LOCAL_BYTE;
        not_supported = 1;
        break;
      default:
        parameter_error = 1;
    }
    if (type == REP_TYPE_ASCII || type == REP_TYPE_EBCDIC) {
      if (num_args == 2) {
        if (arglens[0] != 1){
          syntax_error = 1;
        } else {
          switch (*args[1]) {
            case 'N':
              subtype = REP_SUBTYPE_NON_PRINT;
              break;
            case 'T':
              subtype = REP_SUBTYPE_TELNET;
              not_supported = 1;
              break;
            case 'C':
              subtype = REP_SUBTYPE_CARRIAGE_CONTROL;
              not_supported = 1;
              break;
          }
        }
      }
    }
    if (type == REP_TYPE_LOCAL_BYTE) {
      if (num_args != 2) {
        syntax_error = 1;
      } else {
        num_bits = atoi(args[1]);
        not_supported = 1;
      }
    }
  }
  if (syntax_error || parameter_error) {
    SET_RESPONSE(server, "501", "Syntax error in parameters or arguments.");
  } else if (not_supported) {
    SET_RESPONSE(server, "504", "Command not implemented for that parameter.");
  } else {
    server->dtp_settings.type = type;
    server->dtp_settings.subtype = subtype;
    server->dtp_settings.num_bits = num_bits;
    SET_RESPONSE(server, "200", "Command okay.");
  }
  return;
}

void _set_structure(ftp_server_t *server, int num_args, char *args[], unsigned int arglens[]) {
  structure_t structure;
  int syntax_error = 0;
  int parameter_error = 0;
  int not_supported = 0;

  if (arglens[0] != 1) {
    syntax_error = 1;
  } else {
    switch (*args[0]) {
      case 'F':
        structure = STRUCTURE_FILE;
        break;
      case 'R':
        structure = STRUCTURE_RECORD;
        not_supported = 1;
        break;
      case 'P':
        structure = STRUCTURE_PAGE;
        not_supported = 1;
        break;
      default:
        parameter_error = 1;
    }
  }
  if (syntax_error || parameter_error) {
    SET_RESPONSE(server, "501", "Syntax error in parameters or arguments.");
  } else if (not_supported) {
    SET_RESPONSE(server, "504", "Command not implemented for that parameter.");
  } else {
    server->dtp_settings.structure = structure;
    SET_RESPONSE(server, "200", "Command okay.");
  }
  return;
}

void _set_transfer_mode(ftp_server_t *server, int num_args, char *args[], unsigned int arglens[]) {
  transfer_mode_t mode;
  int syntax_error = 0;
  int parameter_error = 0;
  int not_supported = 0;

  if (arglens[0] != 1) {
    syntax_error = 1;
  } else {
    switch (*args[0]) {
      case 'F':
        mode = TRANSFER_MODE_STREAM;
        break;
      case 'R':
        mode = TRANSFER_MODE_BLOCK;
        not_supported = 1;
        break;
      case 'P':
        mode = TRANSFER_MODE_COMPRESSED;
        not_supported = 1;
        break;
      default:
        parameter_error = 1;
    }
  }
  if (syntax_error || parameter_error) {
    SET_RESPONSE(server, "501", "Syntax error in parameters or arguments.");
  } else if (not_supported) {
    SET_RESPONSE(server, "504", "Command not implemented for that parameter.");
  } else {
    server->dtp_settings.transfer_mode = mode;
    SET_RESPONSE(server, "200", "Command okay.");
  }
  return;
}

void _set_passive(ftp_server_t *server) {
  // Buffer for response of form (h1,h2,h3,h4,p1,p2)
  char buff[26];
  unsigned char ip[4];
  unsigned char port[2];
  unsigned long len;

  // Check if socket already is in passive mode
  if (server->dtp_settings.mode != DTP_MODE_PASSIVE) {
    // Setup the port information
    server->dtp_settings.server_address.sin_family = AF_INET;
    server->dtp_settings.server_address.sin_port = 0;
    server->dtp_settings.server_address.sin_addr.s_addr = INADDR_ANY;

    if ((server->dtp_settings.passive_sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
      FTP_SERVER_PI_DEBUG(1, "Failed to create passive DTP socket.\n");
      SET_RESPONSE(server, "425", "Cannot create socket.");
      return;
    }

    // Bind the port
    if (bind(server->dtp_settings.passive_sd,
             (struct sockaddr *)&server->dtp_settings.server_address,
             sizeof(server->dtp_settings.server_address)) < 0) {
      FTP_SERVER_PI_DEBUG(1, "Failed to bind passive DTP socket to port.\n");
      SET_RESPONSE(server, "425", "Cannot bind port.");
      return;
    }

    // Start listening for incomming connections
    listen(server->dtp_settings.passive_sd, 1);

    // Get the newly bound port
    len = sizeof(server->dtp_settings.server_address);
    if (getsockname(server->dtp_settings.passive_sd,
        (struct sockaddr *)&server->dtp_settings.server_address,
        &len) < 0) {
      FTP_SERVER_PI_DEBUG(1, "Failed to get socketname of passive DTP socket.\n");
      SET_RESPONSE(server, "425", "Cannot get port.");
      return;
    }

    // Update DTP mode
    server->dtp_settings.mode = DTP_MODE_PASSIVE;

    // Close the DTP thread if one is alreay open
    if (server->pi.dtp_thread != NULL) {
      _close_dtp_channel(server);
    }
  }

  // Create the DTP thread so the client can connect to it
  if (_open_dtp_channel(server) < 0) {
    SET_RESPONSE(server, "421", "Service not available.");
    return;
  };

  // Extract IP
  ip[0] = (LOCAL_IP >>  0) & 0xFF;
  ip[1] = (LOCAL_IP >>  8) & 0xFF;
  ip[2] = (LOCAL_IP >> 16) & 0xFF;
  ip[3] = (LOCAL_IP >> 24) & 0xFF;

  // Extract Port
  port[0] = (server->dtp_settings.server_address.sin_port >> 0) & 0xFF;
  port[1] = (server->dtp_settings.server_address.sin_port >> 8) & 0xFF;

  // Create the response
  snprintf(buff, sizeof(buff), "(%03u,%03u,%03u,%03u,%03u,%03u)", ip[0], ip[1], ip[2], ip[3], port[0], port[1]);
  SET_RESPONSE(server, "227", "Entering Passive Mode ");
  APPEND_RESPONSE_DATA(server, buff);
  return;
}

void _set_data_port(ftp_server_t *server, char *arg, unsigned int arglen) {
  // Convert argument into IP and port
  unsigned int ip[4];
  unsigned int port[2];

  // consume the optional '('
  if (arg[0] == ')') {
    arg++;
    arglen--;
  }

  // Scan for IP and Port
  if (sscanf(arg, "%u,%u,%u,%u,%u,%u", ip, ip + 1, ip + 2, ip + 3, port, port + 1) != 6) {
    FTP_SERVER_PI_DEBUG(1, "Unable to parse data port string from :%s\n", arg);
    SET_RESPONSE(server, "501", "Syntax error in parameters or arguments.");
    return;
  }

  // Overwrite the client address structure
  server->dtp_settings.client_address.sin_family = AF_INET;
  server->dtp_settings.client_address.sin_port = (port[1] << 8) | port[0];
  server->dtp_settings.client_address.sin_addr.s_addr = (ip[3] << 24) | (ip[2] << 16) | (ip[1] << 8) | ip[0];
  server->dtp_settings.mode = DTP_MODE_ACTIVE;
  SET_RESPONSE(server, "200", "Command successful.");
  FTP_SERVER_PI_DEBUG(1, "Set client data port to :%u\n", server->dtp_settings.client_address.sin_port);
}

void _get_stat(ftp_server_t *server, char *path, unsigned int arglen) {
  SET_RESPONSE(server, "502", "Command not implemented.");
}

void _execute_fs_command(ftp_server_t *server, ftp_server_dtp_command_t fs_cmd, char *path, unsigned int arglen) {

  // check arguments
  if (server == NULL || fs_cmd >= FTP_SERVER_DTP_COMMAND_NUM) return;

  // Required vaiables
  int path_exists = 0;
  ftp_server_pi_to_dtp_msg_t pi_to_dtp_msg;

  // Check if the path exists
  if (path != NULL) {
    if (f_stat(path, NULL) == FR_OK || strncmp(path, "/", arglen) == 0) {
      path_exists = 1;
    }
  }

  // Test if the command requires the file/path to exist
  switch (fs_cmd) {
    case FTP_SERVER_DTP_COMMAND_LIST:
      if (path == NULL) {
        break;
      }
      __attribute__((fallthrough));
    case FTP_SERVER_DTP_COMMAND_RETR:
    case FTP_SERVER_DTP_COMMAND_NLST:
      if (!path_exists) {
        FTP_SERVER_PI_DEBUG(2, "Cannot execute FS Command: File/Path '%s' not found.\n", path);
        SET_RESPONSE(server, "550", "File/Path not found.");
        return;
      }
      break;
    default:
      break;
  }

  // Prepare the command to the DTP channel
  pi_to_dtp_msg.command = fs_cmd;
  pi_to_dtp_msg.filename_buff = NULL;

  // If a path was specified, copy the path
  if (path != NULL) {
    if (server->pi.path_buffer_used || strlen(path) >= FTP_SERVER_PATH_BUF_LEN) {
      FTP_SERVER_PI_DEBUG(1, "Cannot execute FS Command: Not enough buffer.\n");
      SET_RESPONSE(server, "451", "Requested action aborted: Not enough buffer.");
      return;
    } else {
      pi_to_dtp_msg.filename_buff = server->pi.path_buffer;
      strncpy(server->pi.path_buffer, path, FTP_SERVER_PATH_BUF_LEN);
      server->pi.path_buffer_used = 1;
    }
  }

  // Open a server if not already open
  if (server->pi.dtp_thread == NULL) {
    if (_open_dtp_channel(server) < 0) {
      SET_RESPONSE(server, "425", "Can't open data connection.");
      return;
    }
  }

  // Send the command
  if (osMessageQueuePut(server->pi.pi_to_dtp_msg_queue, &pi_to_dtp_msg, 0, FTP_SERVER_DEFAULT_TIMEOUT) != osOK) {
    FTP_SERVER_PI_DEBUG(1, "Could not send message to DTP.\n");
    SET_RESPONSE(server, "451", "Requested action aborted: local error in processing.");
    return;
  }

  if (path == NULL) {
    FTP_SERVER_PI_DEBUG(2, "Sent FS command '%s' without a path to DTP.\n", dtp_cmd_str[fs_cmd]);
  } else {
    FTP_SERVER_PI_DEBUG(2, "Sent FS command '%s' with path '%s' to DTP.\n", dtp_cmd_str[fs_cmd], path);
  }
}

int _open_dtp_channel(ftp_server_t *server) {
  // Check arguments
  if (server == NULL) return -1;

  // Arguments used for DTP thread creation
  osThreadAttr_t dtp_thread_attributes;
  char dtp_thread_name[FTP_MAX_THREAD_NAME_LENGTH];
  server_dtp_args_t dtp_args;

  // Check if server is already open
  if (server->pi.dtp_thread != NULL) {
    FTP_SERVER_PI_DEBUG(2, "Cannot open DTP channel: already open.\n");
    return 0;
  }

  // Create Message Queues between PI and DTP threads
  server->pi.pi_to_dtp_msg_queue = osMessageQueueNew(1, sizeof(ftp_server_pi_to_dtp_msg_t), NULL);
  server->pi.dtp_to_pi_msg_queue = osMessageQueueNew(1, sizeof(ftp_server_dtp_to_pi_msg_t), NULL);

  // Initialize DTP attributes
  memset(&dtp_thread_attributes, 0x00, sizeof(dtp_thread_attributes));
  dtp_thread_attributes.name = dtp_thread_name;
  dtp_thread_attributes.stack_size = FTP_SERVER_DTP_THREAD_STACKSIZE;

  // Set DTP thread name
  snprintf(dtp_thread_name, sizeof(dtp_thread_name), "FTP_S_%03u_DTP", server->pi.pi_index);

  // Set up the DTP arguments
  dtp_args.settings = &server->dtp_settings;
  dtp_args.pi_to_dtp_msg_queue = server->pi.pi_to_dtp_msg_queue;
  dtp_args.dtp_to_pi_msg_queue = server->pi.dtp_to_pi_msg_queue;

  // Create the DTP task
  server->pi.dtp_thread = osThreadNew(_ftp_server_dtp_thread, (void*) &dtp_args, &dtp_thread_attributes);

  if (server->pi.dtp_thread == NULL) {
    FTP_SERVER_PI_DEBUG(1, "Failed to create new FTP DTP thread.\n");
    return -1;
  } else {
    FTP_SERVER_PI_DEBUG(2, "Created new FTP DTP thread.\n");
  }

  return 0;
}

int _close_dtp_channel(ftp_server_t *server) {
  // Check Arguments
  if (server == NULL) return -1;

  ftp_server_dtp_to_pi_msg_t dtp_to_pi_msg;
  ftp_server_pi_to_dtp_msg_t pi_to_dtp_msg;
  int stat = 0;
  int exited = 0;
  int exit_tries = 2;

  // Check if a DTP channel is open
  if (server->pi.dtp_thread != NULL) {
    // Make sure the Message Queue to the DTP is clear
    osMessageQueueReset(server->pi.pi_to_dtp_msg_queue);

    // Send a CLOSE command to the DTP
    pi_to_dtp_msg.command = FTP_SERVER_DTP_COMMAND_CLOSE;
    if (osMessageQueuePut(server->pi.pi_to_dtp_msg_queue, &pi_to_dtp_msg, 0, 0) != osOK) {
      FTP_SERVER_PI_DEBUG(1, "Could not send message to close DTP.\n");
      stat = -1;
    }

    // Wait for the DTP to send a response. Try twice in case an old item was left in the queue
    do {
      if (osMessageQueueGet(server->pi.dtp_to_pi_msg_queue, &dtp_to_pi_msg, NULL, FTP_SERVER_DEFAULT_TIMEOUT) != osOK) {
        stat = -1;
      }
      if (dtp_to_pi_msg.cmd_resp == FTP_SERVER_DTP_COMMAND_RESP_EXITING_ERROR ||
          dtp_to_pi_msg.cmd_resp == FTP_SERVER_DTP_COMMAND_RESP_FINISHED) {
        exited = 1;
      }
    } while (stat >= 0 && --exit_tries > 0 && !exited);

    if (!exited) {
      // If it has not exited yet, kill the thread. The socket will be closed by timeout.
      FTP_SERVER_PI_DEBUG(1, "DTP Thread did not exit, terminating thread.\n");
      osThreadTerminate(server->pi.dtp_thread);
    }

    // delete the reference to the DTP
    server->pi.dtp_thread = NULL;
  }

  // Cleanup Message Queues
  if (server->pi.dtp_to_pi_msg_queue != NULL) {
    if (osMessageQueueDelete(server->pi.dtp_to_pi_msg_queue) != osOK) {
      FTP_SERVER_PI_DEBUG(1, "Failed to delete message queues.\n");
      stat = -1;
    }
    server->pi.dtp_to_pi_msg_queue = NULL;
  }

  if (server->pi.pi_to_dtp_msg_queue != NULL) {
    if (osMessageQueueDelete(server->pi.pi_to_dtp_msg_queue) != osOK) {
      FTP_SERVER_PI_DEBUG(1, "Failed to delete message queues.\n");
      stat = -1;
    }
    server->pi.pi_to_dtp_msg_queue = NULL;
  }

  // Misc cleanup
  server->pi.path_buffer_used = 0;
  FTP_SERVER_PI_DEBUG(1, "Closed DTP.\n");

  return stat;
}

int _dtp_execute_command(ftp_server_dtp_channel_t *dtp,
                         ftp_server_dtp_command_t dtp_cmd,
                         char *args,
                         ftp_server_dtp_to_pi_msg_t *resp)
{
  // check parameters
  if (dtp == NULL || resp == NULL) return -1;

  // Set default response parameters
  resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_REJECTED;

  if (dtp_cmd >= FTP_SERVER_DTP_COMMAND_NUM) {
    FTP_SERVER_DTP_DEBUG(1, "Received illegal command from PI: %u.\n", dtp_cmd);
    return 0;
  }

  // Send debug message
  if (args == NULL) {
    FTP_SERVER_DTP_DEBUG(2, "Received PI command %s without args.\n", dtp_cmd_str[dtp_cmd]);
  } else {
    FTP_SERVER_DTP_DEBUG(2, "Received PI command %s with args %s.\n", dtp_cmd_str[dtp_cmd], args);
  }
  FTP_SERVER_DTP_DEBUG(2, "Active command is %s.\n", dtp_cmd_str[dtp->active_cmd]);

  // Take action depending on command
  switch (dtp_cmd) {
    case FTP_SERVER_DTP_COMMAND_NONE:
      resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_ACCEPTED;
      break;
    case FTP_SERVER_DTP_COMMAND_RETR:
      // Check if command is allowed
      if (dtp->active_cmd != FTP_SERVER_DTP_COMMAND_NONE) {
        resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_REJECTED;
      } else {
        // Attempt to read the file
        if (f_open(&dtp->current_file, args, FA_READ) == FR_OK) {
          resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_ACCEPTED;
          dtp->active_cmd = dtp_cmd;
        } else {
          resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_REJECTED;
        }
      }
      break;
    case FTP_SERVER_DTP_COMMAND_STOR:
      // Check if command is allowed
      if (dtp->active_cmd != FTP_SERVER_DTP_COMMAND_NONE) {
        resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_REJECTED;
      } else {
        // Attempt to read the file
        if (f_open(&dtp->current_file, args, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
          resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_ACCEPTED;
          dtp->active_cmd = dtp_cmd;
        } else {
          resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_REJECTED;
        }
      }
      break;
    case FTP_SERVER_DTP_COMMAND_APPE:
      // Check if command is allowed
      if (dtp->active_cmd != FTP_SERVER_DTP_COMMAND_NONE) {
        resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_REJECTED;
      } else {
        // Attempt to read the file
        if (f_open(&dtp->current_file, args, FA_OPEN_APPEND | FA_WRITE) == FR_OK) {
          resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_ACCEPTED;
          dtp->active_cmd = dtp_cmd;
        } else {
          resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_REJECTED;
        }
      }
      break;
    case FTP_SERVER_DTP_COMMAND_REST:
      // Check if command is allowed
      switch (dtp->active_cmd) {
        case FTP_SERVER_DTP_COMMAND_RETR:
        case FTP_SERVER_DTP_COMMAND_STOR:
        case FTP_SERVER_DTP_COMMAND_APPE:
          if (f_lseek(&dtp->current_file, atoi(args)) == FR_OK) {
            resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_ACCEPTED;
          } else {
            resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_REJECTED;
          }
          break;
        case FTP_SERVER_DTP_COMMAND_LIST:
        case FTP_SERVER_DTP_COMMAND_NLST:
          resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_REJECTED;
          break;
        default:
          resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_SUPERFLUOUS;
      }
      break;
    case FTP_SERVER_DTP_COMMAND_ABOR:
      switch (dtp->active_cmd) {
        case FTP_SERVER_DTP_COMMAND_RETR:
        case FTP_SERVER_DTP_COMMAND_STOR:
        case FTP_SERVER_DTP_COMMAND_APPE:
          f_close(&dtp->current_file);
          f_closedir(&dtp->current_dir);
          resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_ACCEPTED;
          break;
        case FTP_SERVER_DTP_COMMAND_LIST:
        case FTP_SERVER_DTP_COMMAND_NLST:
          resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_ACCEPTED;
          break;
        default:
          resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_SUPERFLUOUS;
      }
      dtp->active_cmd = FTP_SERVER_DTP_COMMAND_NONE;
      break;
    case FTP_SERVER_DTP_COMMAND_LIST:
      // If no argument is set, set the default argument (PWD)
      if (args == NULL) {
        args = ".";
      }
      // Check if path exists
      FRESULT fres = f_stat(args, &dtp->current_info);
      if (fres == FR_OK || fres == FR_INVALID_NAME) {
        // Check if file or directory
        if (fres == FR_INVALID_NAME || dtp->current_info.fattrib & AM_DIR) {
          // Open Directory and extract first item in directory
          if (f_opendir(&dtp->current_dir, args) == FR_OK && f_readdir(&dtp->current_dir, &dtp->current_info) == FR_OK) {
            resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_ACCEPTED;
            dtp->list_file_only = 0;
          } else {
            resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_REJECTED;
            break;
          }
        } else {
          // File only
          dtp->list_file_only = 1;
          resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_ACCEPTED;
        }
      } else {
        resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_REJECTED;
        break;
      }
      dtp->active_cmd = FTP_SERVER_DTP_COMMAND_LIST;
      break;
    case FTP_SERVER_DTP_COMMAND_NLST:
      if (dtp->active_cmd != FTP_SERVER_DTP_COMMAND_NONE) {
        resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_REJECTED;
      } else {
        // Attempt to read the file
        if (f_opendir(&dtp->current_dir, args) == FR_OK) {
          resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_ACCEPTED;
          dtp->active_cmd = FTP_SERVER_DTP_COMMAND_NLST;
        } else {
          resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_REJECTED;
        }
      }
      break;
    case FTP_SERVER_DTP_COMMAND_CLOSE:
      resp->cmd_resp = FTP_SERVER_DTP_COMMAND_RESP_ACCEPTED;
      __attribute((fallthrough));
    default:
      // Close the file and directory if one is open
      f_close(&dtp->current_file);
      f_closedir(&dtp->current_dir);
      // Indicate the close
      return -1;
  }

  // Flush the buffer if command accpected
  if (resp->cmd_resp == FTP_SERVER_DTP_COMMAND_RESP_ACCEPTED) {
    dtp->buff_len_used = 0;
    dtp->buff_offset = 0;
    dtp->buff[0] = '\0';
    dtp->finish_pending = 0;
  }

  FTP_SERVER_DTP_DEBUG(2, "Processed PI command with response %s.\n", dtp_cmd_resp_str[resp->cmd_resp]);
  FTP_SERVER_DTP_DEBUG(2, "Active command is now %s.\n", dtp_cmd_str[dtp->active_cmd]);
  return 0;
}

int _dtp_send_receive(ftp_server_dtp_channel_t *dtp) {
  // set up the buffer
  int ret = 0;
  int sock_sts = 0;
  int bytes_written = 0;

  // check arguments
  if (dtp == NULL || dtp->active_cmd >= FTP_SERVER_DTP_COMMAND_NUM) return -1;

  // fill buffer if send command
  if (dtp->buff_len_used == 0) {
    switch (dtp->active_cmd) {
      case FTP_SERVER_DTP_COMMAND_RETR:
        // Read from the file
        if (f_read(&dtp->current_file, dtp->buff, FTP_SERVER_DTP_BUFFER_LEN, &dtp->buff_len_used) != FR_OK) {
          FTP_SERVER_DTP_DEBUG(1, "Failed to read file from FS.\n");
          ret = -1;
        } else {
          // Check if finished reading file
          if (dtp->buff_len_used < FTP_SERVER_DTP_BUFFER_LEN) {
            dtp->finish_pending = 1;
          }
        }
        break;
      case FTP_SERVER_DTP_COMMAND_LIST:
        // send the info stored in f_info
        if (dtp->list_file_only) {
          // File only
          bytes_written = _dtp_listitem_unix(dtp->buff, &dtp->current_info, FTP_SERVER_DTP_BUFFER_LEN);
          dtp->finish_pending = 1;
        } else {
          while(1) {
            // Attempt to write the current entry into the buffer
            bytes_written = _dtp_listitem_unix(dtp->buff + dtp->buff_len_used, &dtp->current_info, FTP_SERVER_DTP_BUFFER_LEN - dtp->buff_len_used);
            dtp->buff_len_used += bytes_written;
            // Check if the entry was written into buffer
            if (bytes_written == 0) {
              break;
            }
            // Get the next entry in the directory
            if (f_readdir(&dtp->current_dir, &dtp->current_info) != FR_OK) {
              FTP_SERVER_DTP_DEBUG(1, "Failed to read directory from FS.\n");
              ret = -1;
            }
            // Check if end of directory reached
            if (*dtp->current_info.fname == '\0') {
              dtp->finish_pending = 1;
              break;
            }
          }
        }
        break;
      case FTP_SERVER_DTP_COMMAND_NLST:
        // Loop while enough space is available
        while(dtp->buff_len_used + (_USE_LFN ? _MAX_LFN : 12) + 3 < FTP_SERVER_DTP_BUFFER_LEN) {
          // Get the next entry in the directory
          if (f_readdir(&dtp->current_dir, &dtp->current_info) != FR_OK) {
            FTP_SERVER_DTP_DEBUG(1, "Failed to read directory from FS.\n");
            ret = -1;
          }
          // Check if end of directory reached
          if (*dtp->current_info.fname == '\0') {
            dtp->finish_pending = 1;
            break;
          }
          // Copy the directory name and add CRLF
          dtp->buff_len_used += strlcpy(dtp->buff + dtp->buff_len_used,
                                        dtp->current_info.fname,
                                        FTP_SERVER_DTP_BUFFER_LEN - dtp->buff_len_used);
          dtp->buff_len_used += strlcpy(dtp->buff + dtp->buff_len_used,
                                        "\r\n",
                                        FTP_SERVER_DTP_BUFFER_LEN - dtp->buff_len_used);
        }
        break;
      default:
        // Not a send command
        break;
    }
    // Check if data has been added to the buffer
    if (dtp->buff_len_used != 0) {
      FTP_SERVER_DTP_DEBUG(2, "Added %u Byte to be sent.\n", dtp->buff_len_used);
    }
  }

  // Send or receive data
  if (ret >= 0) {
    switch (dtp->active_cmd) {
      case FTP_SERVER_DTP_COMMAND_STOR:
      case FTP_SERVER_DTP_COMMAND_APPE:
        if (dtp->buff_len_used == 0) {
          sock_sts = recv(dtp->conn, dtp->buff, FTP_SERVER_DTP_BUFFER_LEN, MSG_DONTWAIT);
          if (sock_sts < 0) {
            if (errno != EWOULDBLOCK) {
              FTP_SERVER_DTP_DEBUG(1, "Failed to receive data from socket.\n");
              ret = -1;
            } else {
              dtp->buff_len_used = 0;
            }
          } else if (sock_sts == 0) {
            FTP_SERVER_DTP_DEBUG(1, "Receive connection closed.\n");
            dtp->finish_pending = 1;
          } else {
            dtp->buff_len_used = sock_sts;
            FTP_SERVER_DTP_DEBUG(2, "Received %u Bytes.\n", dtp->buff_len_used);
#if FTP_SERVER_DTP_DEBUG_LEVEL >= 3
            _dump_binary(dtp->buff, dtp->buff_len_used);
#endif
          }
        }
        break;
      case FTP_SERVER_DTP_COMMAND_RETR:
      case FTP_SERVER_DTP_COMMAND_LIST:
      case FTP_SERVER_DTP_COMMAND_NLST:
        if (dtp->buff_len_used > 0) {
          sock_sts = send(dtp->conn, dtp->buff + dtp->buff_offset, dtp->buff_len_used - dtp->buff_offset, MSG_DONTWAIT);
          if (sock_sts < 0) {
            if (errno != EWOULDBLOCK) {
              FTP_SERVER_DTP_DEBUG(1, "Failed to send data to socket.\n");
              ret = -1;
            } else {
              // Cannot send at the moment, wait until later
            }
          } else if (sock_sts == 0) {
            FTP_SERVER_DTP_DEBUG(1, "Send connection closed unexpectedly.\n");
            ret = -1;
          } else {
            FTP_SERVER_DTP_DEBUG(2, "Sent %u Bytes.\n", sock_sts);
#if FTP_SERVER_DTP_DEBUG_LEVEL >= 3
            _dump_binary(dtp->buff, dtp->buff_len_used);
#endif
            if (sock_sts >= dtp->buff_len_used - dtp->buff_offset) {
              // All data was sent
              dtp->buff_len_used = 0;
              dtp->buff_offset = 0;
            } else {
              // Not all data was sent
              dtp->buff_offset += sock_sts;
              FTP_SERVER_DTP_DEBUG(2, "%u Bytes remain to be sent.\n", dtp->buff_len_used - dtp->buff_offset);
            }
          }
        }
        break;
      default:
        // Noting to send or receive
        break;
    }
  }

  // Write back data to fs
  if (ret >= 0 && dtp->buff_len_used >= 0) {
    switch (dtp->active_cmd) {
      case FTP_SERVER_DTP_COMMAND_STOR:
      case FTP_SERVER_DTP_COMMAND_APPE:
        if (f_write(&dtp->current_file,
                    dtp->buff + dtp->buff_offset,
                    dtp->buff_len_used - dtp->buff_offset,
                    &bytes_written) != FR_OK) {
          FTP_SERVER_DTP_DEBUG(1, "Could not write buffered data to file.\n");
          ret = -1;
          break;
        }
        dtp->buff_offset += bytes_written;
        if (dtp->buff_offset == dtp->buff_len_used) {
          dtp->buff_offset = 0;
          dtp->buff_len_used = 0;
        }
        break;
      default:
        // Not a receive command
        break;
    }
  }

  // Check if finish is pending and all data has been processed
  if (ret >= 0 && dtp->buff_len_used == 0 && dtp->finish_pending) {
    FTP_SERVER_DTP_DEBUG(1, "Finished current process.\n");
    dtp->finish_pending = 0;
    ret = 1;
    f_close(&dtp->current_file);
    f_closedir(&dtp->current_dir);
  }

  return ret;
}

int _dtp_listitem_fat(char *buff, FILINFO* info, unsigned int buff_length) {
  // Check if enough space is avaliable
  if (37 + strlen(info->fname) + 3 > buff_length) {
    return 0;
  }
  unsigned int len_used = snprintf(buff, buff_length,
    "%c%c%c%c%c %10u %4u/%2u/%2u %2u:%2u:%2u %s\r\n",
    (info->fattrib & AM_RDO) ? '-' : 'W',
    (info->fattrib & AM_HID) ? 'H' : '-',
    (info->fattrib & AM_SYS) ? 'S' : '-',
    (info->fattrib & AM_DIR) ? 'D' : '-',
    (info->fattrib & AM_ARC) ? 'A' : '-',
    (unsigned int) info->fsize,
    ((info->fdate >>  9) & 0x7F) + 1980,
    ((info->fdate >>  5) & 0x0F),
    ((info->fdate >>  0) & 0x1F),
    ((info->ftime >> 11) & 0x1F),
    ((info->ftime >>  5) & 0x3F),
    ((info->ftime >>  0) & 0x1F) * 2,
    info->fname);
  return len_used;
}

int _dtp_listitem_unix(char *buff, FILINFO* info, unsigned int buff_length) {
  // Check if enough space is avaliable
  if (59 + strlen(info->fname) + 3 > buff_length) {
    return 0;
  }
  unsigned int len_used = snprintf(buff, buff_length,
    "%cr%c%cr%c%cr%c%c 1 anonymous  anonymous  %10u %3s %02u %02u:%02u %s\r\n",
    (info->fattrib & AM_DIR) ? 'd' : '-',
    (info->fattrib & AM_RDO) ? '-' : 'w',
    (info->fattrib & AM_DIR) ? 'x' : '-',
    (info->fattrib & AM_RDO) ? '-' : 'w',
    (info->fattrib & AM_DIR) ? 'x' : '-',
    (info->fattrib & AM_RDO) ? '-' : 'w',
    (info->fattrib & AM_DIR) ? 'x' : '-',
    (unsigned int) info->fsize,
    dtp_month_str[((info->fdate >>  5) & 0x0F)],
    ((info->fdate >>  0) & 0x1F),
    ((info->ftime >> 11) & 0x1F),
    ((info->ftime >>  5) & 0x3F),
    info->fname);
  return len_used;
}

void _dump_binary(const unsigned char *buff, unsigned int len) {
  while(len > 0) {
    unsigned int i;
    // Dump 16 Bytes in hex format
    for (i = 0; i < 16 && i < len; i++) {
      FTP_PRINTF("%02x ", buff[i]);
    }
    for (; i < 16; i++) {
      FTP_PRINTF("   ");
    }
    FTP_PRINTF(" | ");
    // Dump 16 Bytes as characters
    for (i = 0; i < 16 && i < len; i++) {
      if (buff[i] == '\0') FTP_PRINTF("\\0");
      else if (buff[i] == '\n') FTP_PRINTF("\\n");
      else if (buff[i] == '\r') FTP_PRINTF("\\r");
      else if (buff[i] == '\t') FTP_PRINTF("\\t");
      else if (buff[i] == 127) FTP_PRINTF("<<");
      else if (buff[i] < 32) FTP_PRINTF("??");
      else FTP_PRINTF("%c ", buff[i]);
    }
    FTP_PRINTF("\r\n");
    // update length
    if (len < 16) {
      len = 0;
    } else {
      len -= 16;
      buff += 16;
    }
  }
}

// Note: This is a VERY unsave way to check passwords, but FTP is inherently unsecure.
// Note: This credential check ignores login with account
__attribute__((weak)) ftp_login_result_t _default_credentials_check_fn(
  char *username, char *password, char *account, ftp_permission_t *perm) {

  // Table containing default login information
  struct login_table {
    const char *username;
    const char *password;
    const char *account;
    const ftp_permission_t perm;
  };

  const struct login_table login_table[2] = {
    {
      .username = "anonymous",
      .perm = FTP_PERM_READ,
    },
    {
      .username = "admin",
      .password = "password",
      .perm = FTP_PERM_ADMIN,
    }
  };

  // At least the user name is required
  if (username == NULL) return FTP_LOGIN_RESULT_FAILURE;
  (void) account;

  // Check in the default table if login is allowed
  for (unsigned int i = 0; i < sizeof(login_table)/sizeof(struct login_table); i++) {
    // Check user name
    if (strcmp(login_table[i].username, username) != 0) continue;
    // Found username in table, check password
    if (login_table[i].password == NULL) {
      // Login allowed without Password
      *perm = login_table[i].perm;
      return FTP_LOGIN_RESULT_SUCCESS;
    }
    if (password == NULL) {
      // Password required, but no password given. Request password
      return FTP_LOGIN_RESULT_MORE_INFO_REQUIRED;
    }
    if (strcmp(login_table[i].password, password) != 0) {
      // Incorrect password
      return FTP_LOGIN_RESULT_FAILURE;
    }
    // Password correct
    *perm = login_table[i].perm;
    return FTP_LOGIN_RESULT_SUCCESS;
  }
  // No matching login found
  return FTP_LOGIN_RESULT_FAILURE;
}

#ifdef __cplusplus
}
#endif
