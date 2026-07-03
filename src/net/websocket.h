#ifndef LAURE_WEBSOCKET_H
#define LAURE_WEBSOCKET_H

#include "conn.h"

typedef enum {
    LAURE_WS_OP_CONT   = 0x0,
    LAURE_WS_OP_TEXT   = 0x1,
    LAURE_WS_OP_BINARY = 0x2,
    LAURE_WS_OP_CLOSE  = 0x8,
    LAURE_WS_OP_PING   = 0x9,
    LAURE_WS_OP_PONG   = 0xA
} laure_ws_opcode_t;

int  laure_ws_is_upgrade(laure_conn_t *c);
int  laure_ws_handshake(laure_conn_t *c);
void laure_ws_on_data(laure_conn_t *c, const char *data, size_t len);
void laure_ws_send(laure_conn_t *c, const char *data, size_t len,
                   laure_ws_opcode_t op);
void laure_ws_close(laure_conn_t *c, uint16_t code, const char *reason);
void laure_ws_ping(laure_conn_t *c, const char *payload, size_t len);
void laure_ws_pong(laure_conn_t *c, const char *payload, size_t len);

#endif