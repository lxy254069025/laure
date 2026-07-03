#ifndef LAURE_SERVER_H
#define LAURE_SERVER_H

#include "php_laure.h"
#include "uv.h"

#include <stdint.h>
#include <string.h>

#define LAURE_BACKLOG 511
#define LAURE_READ_BUF 65536
#define LAURE_HB_INTERVAL_MS 30000
#define LAURE_HB_TIMEOUT_MS 60000
#define LAURE_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define LAURE_WS_MAX_FRAME (16 * 1024 * 1024)

typedef enum {
    LAURE_SERVER_TCP = 0,
    LAURE_SERVER_HTTP,
    LAURE_SERVER_HTTPS,
    LAURE_SERVER_WS,
    LAURE_SERVER_WSS
} laure_server_type_t;

typedef struct laure_server_s {
    uv_loop_t          *loop;
    uv_tcp_t            tcp_handle;
    uv_signal_t         sig_int;
    uv_signal_t         sig_term;
    uv_timer_t          timer_tick;
    laure_server_type_t type;
    char               *host;
    int                 port;
    int                 worker_num;
    uint64_t            hb_interval_ms;
    uint64_t            hb_timeout_ms;
    int                 bound_fd;
    void               *tls_ctx;
    zval                cb_connect, cb_receive, cb_close, cb_error, cb_request;
    zval                cb_open, cb_message, cb_ping, cb_pong, cb_tick;
    uint64_t            id_seq;
    zend_object         std;
} laure_server_t;

#define LAURE_SERVER_OBJ(o)                                                    \
    ((laure_server_t *)((char *)(o) - XtOffsetOf(laure_server_t, std)))
#define LAURE_SERVER_P(zv) LAURE_SERVER_OBJ(Z_OBJ_P(zv))

extern zend_class_entry    *laure_server_ce;
extern zend_object_handlers laure_server_obj_handlers;

zend_object *laure_server_new(zend_class_entry *ce);

void laure_register_server_class();
void laure_server_start(laure_server_t *srv);
void laure_server_stop(laure_server_t *srv);
void laure_server_free_cbs(laure_server_t *srv);
void server_ctor(INTERNAL_FUNCTION_PARAMETERS, laure_server_type_t type);
#endif