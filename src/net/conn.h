#ifndef LAURE_CONN_H
#define LAURE_CONN_H

#include "llhttp.h"
#include "server.h"
#include "utils/map.h"
#include "utils/string.h"
#include <stdint.h>

typedef enum {
    LAURE_CONN_TCP = 0,
    LAURE_CONN_HTTP,
    LAURE_CONN_WS_HAND,
    LAURE_CONN_WS_OPEN,
    LAURE_CONN_WS_CLOSING,
    LAURE_CONN_CLOSE
} laure_conn_state_t;

typedef struct {
    uv_write_t req;
    uv_buf_t   buf;
    char      *data;
} laure_write_t;

typedef struct {
    uv_tcp_t           handle;
    laure_server_t    *server;
    uint64_t           id;
    laure_conn_state_t state;
    laure_buf_t        rbuf;
    llhttp_t           parser;
    llhttp_settings_t  parser_settings;
    laure_buf_t        http_method;
    laure_buf_t        http_url;
    laure_buf_t        http_body;
    laure_buf_t        http_cur_field;
    laure_buf_t        http_cur_value;
    int                http_keep_alive;
    int                http_msg_done;
    laure_strmap_t    *http_req_header;
    laure_buf_t        ws_frag_buf;
    uint8_t            ws_frag_opcode;
    uv_timer_t         hb_timer;
    int                hb_active;
    int                hb_waiting_pong;
    uint64_t           hb_last_pong_ms;
    void              *ssl;
    void              *ssl_rbio;
    void              *ssl_wbio;
    int                tls_hs_done;
    zval               zconn;
} laure_conn_t;

typedef struct {
    laure_conn_t *conn;
    zend_object   std;
} laure_conn_obj_t;

void laure_conn_accept(laure_server_t *srv, uv_stream_t *handle);
void laure_conn_send_raw(laure_conn_t *c, const char *data, size_t len);
void laure_conn_close(laure_conn_t *c);
void laure_on_new_connection(uv_stream_t *handle, int status);
void laure_register_conn_class(void);

#define LAURE_CONN_OBJ(o)                                                      \
    ((laure_conn_obj_t *)((char *)(o) - XtOffsetOf(laure_conn_obj_t, std)))
#define LAURE_CONN_P(zv) LAURE_CONN_OBJ(Z_OBJ_P(zv))

extern zend_class_entry    *laure_conn_ce;
extern zend_object_handlers laure_conn_handlers;
#endif