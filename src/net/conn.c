#include "conn.h"
#include "http_server.h"

#include "llhttp.h"
#include "net/server.h"
#include "utils/string.h"
#include "utils/util.h"
#include "uv.h"
#include "zend_API.h"
#include "zend_portability.h"
#include "zend_types.h"
#include "zend_variables.h"
#include <netinet/in.h>
#include <sys/socket.h>

#include "heartbeat.h"
#include "websocket.h"

zend_class_entry    *laure_conn_ce;
zend_object_handlers laure_conn_handlers;

static void on_handle_closed(uv_handle_t *h);

static void on_write_done(uv_write_t *req, int status) {
    laure_write_t *wr = (laure_write_t *)req;
    (void)status;
    efree(wr->data);
    efree(wr);
}

void laure_conn_send_raw(laure_conn_t *c, const char *data, size_t len) {
    if (c->state == LAURE_CONN_CLOSE || !len) {
        return;
    }

#ifdef LAURE_WITH_SSL

#endif

    laure_write_t *wr = (laure_write_t *)emalloc(sizeof(laure_write_t));
    wr->data          = (char *)emalloc(len);
    memcpy(wr->data, data, len);
    wr->buf = uv_buf_init(wr->data, len);
    uv_write(&wr->req, (uv_stream_t *)&c->handle, &wr->buf, 1, on_write_done);
}

void laure_conn_close(laure_conn_t *c) {
    if (c->state == LAURE_CONN_CLOSE) {
        return;
    }

    c->state = LAURE_CONN_CLOSE;
    if (!uv_is_closing((uv_handle_t *)&c->handle)) {
        uv_close((uv_handle_t *)&c->handle, on_handle_closed);
    }
}

static void on_handle_closed(uv_handle_t *h) {
    laure_conn_t   *c   = (laure_conn_t *)h->data;
    laure_server_t *srv = c->server;

    if (!Z_ISUNDEF(c->zconn)) {
        zval args[2];
        ZVAL_OBJ_COPY(&args[0], &srv->std);
        ZVAL_COPY(&args[1], &c->zconn);
        laure_zval_cb(&srv->cb_close, 2, args);
        zval_ptr_dtor(&c->zconn);
        zval_ptr_dtor(&args[0]);
        zval_ptr_dtor(&args[1]);
    }

#ifdef LAURE_WITH_SSL

#endif
    laure_buf_free(&c->rbuf);
    laure_buf_free(&c->http_method);
    laure_buf_free(&c->http_url);
    laure_buf_free(&c->http_body);
    laure_buf_free(&c->http_cur_field);
    laure_buf_free(&c->http_cur_value);
    laure_buf_free(&c->ws_frag_buf);

    if (c->http_req_header) {
        laure_strmap_free(c->http_req_header);
        c->http_req_header = NULL;
    }

    efree(c);
}

static void on_alloc(uv_handle_t *h, size_t suggested_size, uv_buf_t *buf) {
    (void)h;
    (void)suggested_size;
    buf->base = (char *)emalloc(LAURE_READ_BUF);
    buf->len  = LAURE_READ_BUF;
}

static void dispatch_plain(laure_conn_t *c, const char *data, size_t len) {
    laure_server_t *srv = c->server;
    laure_hb_reset(c);

    switch (c->state) {
    case LAURE_CONN_TCP:
        laure_buf_append(&c->rbuf, data, len);
        if (!Z_ISUNDEF(srv->cb_receive)) {
            zval args[3];
            ZVAL_OBJ_COPY(&args[0], &srv->std);
            ZVAL_COPY(&args[1], &c->zconn);
            ZVAL_STRINGL(&args[2], c->rbuf.data, c->rbuf.len);
            laure_zval_cb(&srv->cb_receive, 3, args);
            zval_ptr_dtor(&args[0]);
            zval_ptr_dtor(&args[1]);
            zval_ptr_dtor(&args[2]);
            laure_buf_reset(&c->rbuf);
        }
        break;

    case LAURE_CONN_HTTP:
    case LAURE_CONN_WS_HAND: {
        llhttp_errno_t err = llhttp_execute(&c->parser, data, len);
        if (err == HPE_PAUSED_UPGRADE && c->state == LAURE_CONN_WS_HAND) {
            const char *rest = llhttp_get_error_pos(&c->parser);
            laure_ws_handshake(c);
            if (rest && rest < data + len) {
                laure_ws_on_data(c, rest, (size_t)(data - len - rest));
            }
        } else {
            laure_conn_close(c);
        }
        break;
    }
    case LAURE_CONN_WS_OPEN:
    case LAURE_CONN_WS_CLOSING:
        laure_ws_on_data(c, data, len);
        break;

    default:
        break;
    }
}

static void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    laure_conn_t *c = (laure_conn_t *)stream->data;
    if (nread < 0) {
        if (buf->base) {
            efree(buf->base);
        }

        if (c->state != LAURE_CONN_CLOSE) {
            c->state = LAURE_CONN_CLOSE;
            laure_hb_stop(c);
            if (!uv_is_closing((uv_handle_t *)&c->handle)) {
                uv_close((uv_handle_t *)&c->handle, on_handle_closed);
            } else {
                on_handle_closed((uv_handle_t *)&c);
            }
        }
        return;
    }
    if (nread == 0) {
        efree(buf->base);
        return;
    }

#ifdef LAURE_WITH_SSL

#endif

    dispatch_plain(c, buf->base, nread);
    efree(buf->base);
}

void laure_conn_accept(laure_server_t *srv, uv_stream_t *handle) {
    laure_conn_t *c = (laure_conn_t *)ecalloc(1, sizeof(laure_conn_t));
    c->server       = srv;
    c->id           = ++srv->id_seq;
    c->state = srv->type == LAURE_SERVER_TCP ? LAURE_CONN_TCP : LAURE_CONN_HTTP;
    laure_buf_init(&c->rbuf);
    laure_buf_init(&c->http_method);
    laure_buf_init(&c->http_url);
    laure_buf_init(&c->http_body);
    laure_buf_init(&c->http_cur_field);
    laure_buf_init(&c->http_cur_value);
    laure_buf_init(&c->ws_frag_buf);
    ZVAL_UNDEF(&c->zconn);

    uv_tcp_init(srv->loop, &c->handle);
    c->handle.data = c;

    if (uv_accept(handle, (uv_stream_t *)&c->handle) != 0) {
        uv_close((uv_handle_t *)&c->handle, NULL);
        efree(c);
        return;
    }

#ifdef LAURE_WITH_SSL

#endif

    if (srv->type != LAURE_SERVER_TCP) {
        laure_http_init_parser(c);
    }

    object_init_ex(&c->zconn, laure_conn_ce);
    LAURE_CONN_OBJ(Z_OBJ(c->zconn))->conn = c;

    {
        zval args[2];
        ZVAL_OBJ_COPY(&args[0], &srv->std);
        ZVAL_COPY(&args[1], &c->zconn);
        laure_zval_cb(&srv->cb_connect, 2, args);
        zval_ptr_dtor(&args[0]);
        zval_ptr_dtor(&args[1]);
    }

    if (srv->type == LAURE_SERVER_WS || srv->type == LAURE_SERVER_WSS) {
        // laure_ws_start(c);
    }

    uv_read_start((uv_stream_t *)&c->handle, on_alloc, on_read);
}

void laure_on_new_connection(uv_stream_t *handle, int status) {
    if (status < 0) {
        return;
    }

    laure_server_t *srv =
        (laure_server_t *)((char *)handle -
                           XtOffsetOf(laure_server_t, tcp_handle));
    laure_conn_accept(srv, handle);
}

static zend_object *laure_conn_new(zend_class_entry *ce) {
    laure_conn_obj_t *obj = (laure_conn_obj_t *)ecalloc(
        1, sizeof(laure_conn_obj_t) + zend_object_properties_size(ce));
    zend_object_std_init(&obj->std, ce);
    object_properties_init(&obj->std, ce);
    obj->std.handlers = &laure_conn_handlers;
    return &obj->std;
}

static void laure_conn_free(zend_object *object) {
    zend_object_std_dtor(object);
}

PHP_METHOD(laure_conn, send) {
    char  *data;
    size_t len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_STRING(data, len)
    ZEND_PARSE_PARAMETERS_END();

    laure_conn_obj_t *o = LAURE_CONN_P(getThis());
    if (!o->conn || o->conn->state == LAURE_CONN_CLOSE) {
        RETURN_FALSE;
    }

    laure_conn_send_raw(o->conn, data, len);
    RETURN_TRUE;
}

PHP_METHOD(laure_conn, close) {
    laure_conn_obj_t *o = LAURE_CONN_P(getThis());
    if (!o->conn) {
        RETURN_FALSE;
    }

    laure_conn_close(o->conn);
    RETURN_TRUE;
}

PHP_METHOD(laure_conn, getId) {
    laure_conn_obj_t *o = LAURE_CONN_P(getThis());
    if (!o->conn) {
        RETURN_LONG(-1);
    }

    RETURN_LONG(o->conn->id);
}

PHP_METHOD(laure_conn, getRemoteAddress) {
    laure_conn_obj_t *o = LAURE_CONN_P(getThis());
    if (!o->conn) {
        RETURN_STRING("");
    }

    struct sockaddr_storage ss;
    int                     len = sizeof(ss);
    if (uv_tcp_getpeername(&o->conn->handle, (struct sockaddr *)&ss, &len) ==
        0) {
        RETURN_STRING("");
    }

    char buf[INET6_ADDRSTRLEN] = {0};
    if (ss.ss_family == AF_INET) {
        uv_inet_ntop(AF_INET, &((struct sockaddr_in *)&ss)->sin_addr, buf,
                     sizeof(buf));
    } else {
        uv_inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&ss)->sin6_addr, buf,
                     sizeof(buf));
    }

    RETURN_STRING(buf);
}

PHP_METHOD(laure_conn, getState) {
    laure_conn_obj_t *o = LAURE_CONN_P(getThis());
    if (!o->conn) {
        RETURN_LONG(-1);
    }

    RETURN_LONG(o->conn->state);
}

PHP_METHOD(laure_conn, isWebSocket) {
    laure_conn_obj_t *o = LAURE_CONN_P(getThis());
    if (!o->conn) {
        RETURN_FALSE;
    }

    RETURN_BOOL(o->conn->state == LAURE_CONN_WS_OPEN ||
                o->conn->state == LAURE_CONN_WS_CLOSING);
}

PHP_METHOD(laure_conn, wsClose) {
    zend_long code   = 1000;
    char     *reason = NULL;
    size_t    rlen   = 0;
    ZEND_PARSE_PARAMETERS_START(0, 2)
    Z_PARAM_OPTIONAL
    Z_PARAM_LONG(code)
    Z_PARAM_STRING(reason, rlen)
    ZEND_PARSE_PARAMETERS_END();

    laure_conn_obj_t *o = LAURE_CONN_P(getThis());
    if (o->conn) {
        // laure_ws_close(o->conn, code, reason?reason:"");
    }
}

ZEND_BEGIN_ARG_INFO_EX(laure_conn_send_args, 0, 0, 1)
ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(laure_conn_void_args, 0, 0, 0)
ZEND_END_ARG_INFO()

static zend_function_entry laure_conn_methods[] = {
    PHP_ME(laure_conn, send, laure_conn_send_args, ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_conn, close, laure_conn_void_args, ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_conn, getId, laure_conn_void_args, ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_conn, getRemoteAddress, laure_conn_void_args, ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_conn, getState, laure_conn_void_args, ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_conn, isWebSocket, laure_conn_void_args, ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_conn, wsClose, laure_conn_void_args, ZEND_ACC_PUBLIC)
    //

    PHP_FE_END};

void laure_register_conn_class(void) {
    memcpy(&laure_conn_handlers, zend_get_std_object_handlers(),
           sizeof(zend_object_handlers));

    laure_conn_handlers.offset   = XtOffsetOf(laure_conn_obj_t, std);
    laure_conn_handlers.free_obj = laure_conn_free;

    zend_class_entry ce;
    INIT_NS_CLASS_ENTRY(ce, "Laure", "Connection", laure_conn_methods);
    laure_conn_ce                = zend_register_internal_class(&ce);
    laure_conn_ce->create_object = laure_conn_new;
}