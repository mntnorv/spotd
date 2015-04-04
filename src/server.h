#ifndef _SPOTD_SERVER_H_
#define _SPOTD_SERVER_H_

#include "types.h"

/* --- Types --- */
typedef struct spotd_server_callbacks_t {
  void (*command_received)(const char* command);
} spotd_server_callbacks;

/* --- Functions --- */
spotd_error start_server(int port, spotd_server_callbacks *callbacks);

#endif /* _SPOTD_SERVER_H_ */
