/*
 * HTTP API Server for CKPool
 * Provides read-only access to pool statistics via HTTP endpoints
 */

#ifndef API_SERVER_H
#define API_SERVER_H

#include <stdint.h>

/* Initialize HTTP API server
 * Returns 0 on success, -1 on failure
 * port: Port to listen on (e.g., 8080)
 * 
 * Note: Server runs in separate thread and does not block
 */
int api_server_init(int port);

/* Stop HTTP API server
 * Gracefully shuts down the server
 */
void api_server_stop(void);

/* Set the log directory path for reading ckpool.log
 * This must be called before api_server_init()
 * log_dir: Path to directory containing ckpool.log (e.g., "/data/ckpool/log")
 */
void api_server_set_log_dir(const char *log_dir);

#endif /* API_SERVER_H */
