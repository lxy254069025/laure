#include "websocket.h"
#include "heartbeat.h"
#include "server.h"
#include "utils/map.h"
#include "utils/string.h"
#include "utils/util.h"
#include <stdio.h>
#include <string.h>

int laure_ws_is_upgrade(laure_conn_t *c) {
    if (!c->http_req_header) {
        return 0;
    }
    const char *v = laure_strmap_get_ci(c->http_req_header, "upgrade");
    return v && strcasestr(v, "websocket") ? 1 : 0;
}

int laure_ws_handshake(laure_conn_t *c) {
    const char *key =
        laure_strmap_get_ci(c->http_req_header, "sec-websocket-key");
    if (!key) {
        return 0;
    }

    size_t klen    = strlen(key);
    size_t guidlen = strlen(LAURE_WS_GUID);
    char  *concat  = emalloc(klen + guidlen + 1);
    memcpy(concat, key, klen);
    memcpy(concat + klen, LAURE_WS_GUID, guidlen);
    concat[klen + guidlen] = '\0';

    char *accept = laure_sha1_b64(concat, klen + guidlen);
    efree(concat);

    char resp[512];
    int  rlen = snprintf(resp, sizeof(resp),
                         "HTTP/1.1 101 Switching Protocols\r\n"
                          "Upgrade: websocket\r\n"
                          "Connection: Upgrade\r\n"
                          "Sec-WebSocket-Accept: %s\r\n",
                         accept);
    efree(accept);

    const char *proto =
        laure_strmap_get_ci(c->http_req_header, "sec-websocket-protocol");
    if (proto && rlen < (int)sizeof(resp) - 64) {
        rlen += snprintf(resp + rlen, sizeof(resp) - rlen,
                         "Sec-WebSocket-Protocol: %s\r\n", proto);
    }

    memcpy(resp + rlen, "\r\n", 2);
    rlen += 2;
    laure_conn_send_raw(c, resp, (size_t)rlen);
    c->state = LAURE_CONN_WS_OPEN;
    {
        laure_server_t *srv = c->server;
        zval            args[2];
        ZVAL_OBJ_COPY(&args[0], &srv->std);
        ZVAL_COPY(&args[1], &c->zconn);
        laure_zval_cb(&srv->cb_open, 2, args);
        zval_ptr_dtor(&args[0]);
        zval_ptr_dtor(&args[1]);
    }

    laure_hb_start(c);
    return 1;
}

static char *build_frame(const char *data, size_t len, laure_ws_opcode_t op,
                         int fin, size_t *out_len) {
    size_t hlen;
    if (len < 126) {
        hlen = 2;
    } else if (len < 65536) {
        hlen = 4;
    } else {
        hlen = 10;
    }

    char *frame = emalloc(len + hlen);
    frame[0]    = (char)((fin ? 0x80 : 0x00) | (uint8_t)op);
    if (len < 126) {
        frame[1] = (char)len;
    } else if (len < 65536) {
        frame[1]   = 126;
        uint16_t n = htons((uint16_t)len);
        memcpy(frame + 2, &n, 2);
    } else {
        frame[1] = 127;
        for (int i = 7; i >= 0; i--) {
            frame[2 + i] = (char)((len >> (i * 8)) & 0xFF);
        }
    }
    if (len) {
        memcpy(frame + hlen, data, len);
    }
    *out_len = len + hlen;
    return frame;
}

void laure_ws_send(laure_conn_t *c, const char *data, size_t len,
                   laure_ws_opcode_t op) {
    if (c->state != LAURE_CONN_WS_OPEN) {
        return;
    }

    size_t fl;
    char  *f = build_frame(data, len, op, 1, &fl);
    laure_conn_send_raw(c, f, fl);
    efree(f);
}

void laure_ws_ping(laure_conn_t *c, const char *payload, size_t len) {
    size_t fl;
    char  *f = build_frame(payload, len, LAURE_WS_OP_PING, 1, &fl);
    laure_conn_send_raw(c, f, fl);
    efree(f);
}

void laure_ws_pong(laure_conn_t *c, const char *payload, size_t len) {
    size_t fl;
    char  *f = build_frame(payload, len, LAURE_WS_OP_PONG, 1, &fl);
    laure_conn_send_raw(c, f, fl);
    efree(f);
}

void laure_ws_on_data(laure_conn_t *c, const char *data, size_t len) {
    laure_server_t *srv = c->server;
    laure_buf_append(&c->rbuf, data, len);

    while (1) {
        if (c->rbuf.len < 2) {
            return;
        }

        const uint8_t *p    = (const uint8_t *)c->rbuf.data;
        int            fin  = (p[0] & 0x80) != 0;
        uint8_t        op   = p[0] & 0x0F;
        int            mask = (p[1] & 0x80) != 0;
        uint64_t       plen = p[1] & 0x7F;
        size_t         hlen = 2;

        if (plen == 126) {
            if (c->rbuf.len < 4) {
                return;
            }
            plen = ntohs(*(uint16_t *)(p + 2));
            hlen = 4;
        } else if (plen == 127) {
            if (c->rbuf.len < 10) {
                return;
            }
            plen = 0;
            for (int i = 0; i < 8; i++) {
                plen = (plen << 8) | p[2 + i];
            }
            hlen = 10;
        }

        if (mask) {
            hlen += 4;
        }

        if (plen > LAURE_WS_MAX_FRAME) {
            laure_ws_close(c, 1009, "Frame too large");
            laure_conn_close(c);
            return;
        }

        size_t total = hlen + plen;
        if (c->rbuf.len < total) {
            return;
        }
        char *payload = (char *)emalloc(plen + 1);
        memcpy(payload, c->rbuf.data + hlen, plen);
        if (mask) {
            const uint8_t *mk = p + hlen - 4;
            for (size_t i = 0; i < plen; i++) {
                payload[i] ^= mk[i & 3];
            }
        }
        payload[plen] = '\0';
        laure_buf_consume(&c->rbuf, total);

        switch (op) {
        case LAURE_WS_OP_TEXT:
        case LAURE_WS_OP_BINARY:
            if (!fin) {
                if (!c->ws_frag_opcode) {
                    c->ws_frag_opcode = op;
                }
                laure_buf_append(&c->ws_frag_buf, payload, plen);
            } else {
                zval args[4];
                ZVAL_OBJ_COPY(&args[0], &srv->std);
                ZVAL_COPY(&args[1], &c->zconn);
                ZVAL_STRINGL(&args[2], payload, plen);
                ZVAL_LONG(&args[3], op);
                laure_zval_cb(&srv->cb_message, 4, args);
                zval_ptr_dtor(&args[0]);
                zval_ptr_dtor(&args[1]);
                zval_ptr_dtor(&args[2]);
                zval_ptr_dtor(&args[3]);
            }
            break;
        case LAURE_WS_OP_CONT:
            laure_buf_append(&c->ws_frag_buf, payload, plen);
            if (fin) {
                zval args[4];
                ZVAL_OBJ_COPY(&args[0], &srv->std);
                ZVAL_COPY(&args[1], &c->zconn);
                ZVAL_STRINGL(&args[2], c->ws_frag_buf.data, c->ws_frag_buf.len);
                ZVAL_LONG(&args[3], c->ws_frag_opcode);
                laure_zval_cb(&srv->cb_message, 4, args);
                zval_ptr_dtor(&args[0]);
                zval_ptr_dtor(&args[1]);
                zval_ptr_dtor(&args[2]);
                zval_ptr_dtor(&args[3]);
                laure_buf_reset(&c->ws_frag_buf);
                c->ws_frag_opcode = 0;
            }
            break;
        case LAURE_WS_OP_PING:
            laure_ws_pong(c, payload, plen);
            if (!Z_ISUNDEF(srv->cb_ping)) {
                zval args[3];
                ZVAL_OBJ_COPY(&args[0], &srv->std);
                ZVAL_COPY(&args[1], &c->zconn);
                ZVAL_STRINGL(&args[2], payload, plen);
                laure_zval_cb(&srv->cb_ping, 3, args);
                zval_ptr_dtor(&args[0]);
                zval_ptr_dtor(&args[1]);
                zval_ptr_dtor(&args[2]);
            }
            break;
        case LAURE_WS_OP_PONG:
            laure_hb_reset(c);
            c->hb_waiting_pong = 0;
            c->hb_last_pong_ms = uv_now(srv->loop);
            if (!Z_ISUNDEF(srv->cb_pong)) {
                zval args[3];
                ZVAL_OBJ_COPY(&args[0], &srv->std);
                ZVAL_COPY(&args[1], &c->zconn);
                ZVAL_STRINGL(&args[2], payload, plen);
                laure_zval_cb(&srv->cb_pong, 3, args);
                zval_ptr_dtor(&args[0]);
                zval_ptr_dtor(&args[1]);
                zval_ptr_dtor(&args[2]);
            }
            break;
        case LAURE_WS_OP_CLOSE: {
            uint16_t    code   = 1000;
            const char *reason = "";
            if (plen >= 2) {
                code   = ntohs(*(uint16_t *)payload);
                reason = payload + 2;
            }
            if (c->state == LAURE_CONN_WS_OPEN) {
                laure_ws_close(c, code, reason);
            }
            laure_conn_close(c);
            efree(payload);
            return;
        }
        default:
            laure_ws_close(c, 1002, "Invalid opcode");
            laure_conn_close(c);
            efree(payload);
            return;
        }
        efree(payload);
    }
}

void laure_ws_close(laure_conn_t *c, uint16_t code, const char *reason) {
    if (c->state != LAURE_CONN_WS_OPEN) {
        return;
    }
    size_t   rlen    = reason ? strlen(reason) : 0;
    char    *payload = emalloc(2 + rlen);
    uint16_t nc      = htons(code);
    memcpy(payload, &nc, 2);
    if (rlen) {
        memcpy(payload + 2, reason, rlen);
    }

    size_t fl;
    char  *f = build_frame(payload, 2 + rlen, LAURE_WS_OP_CLOSE, 1, &fl);
    efree(payload);
    laure_conn_send_raw(c, f, fl);
    efree(f);
    c->state = LAURE_CONN_WS_CLOSING;
}
