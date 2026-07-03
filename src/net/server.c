#include "server.h"

#include "php.h"
#include "utils/util.h"
#include "uv.h"

#include "zend_API.h"
#include "zend_compile.h"
#include "zend_exceptions.h"
#include "zend_execute.h"
#include "zend_object_handlers.h"
#include <string.h>
#include <sys/socket.h>

#include "conn.h"

zend_class_entry    *laure_server_ce;
zend_object_handlers laure_server_obj_handlers;

zend_object *laure_server_new(zend_class_entry *ce) {
    laure_server_t *srv = (laure_server_t *)ecalloc(
        1, sizeof(laure_server_t) + zend_object_properties_size(ce));
    zend_object_std_init(&srv->std, ce);
    object_properties_init(&srv->std, ce);
    srv->std.handlers = &laure_server_obj_handlers;

    ZVAL_UNDEF(&srv->cb_connect);
    ZVAL_UNDEF(&srv->cb_receive);
    ZVAL_UNDEF(&srv->cb_close);
    ZVAL_UNDEF(&srv->cb_error);
    ZVAL_UNDEF(&srv->cb_request);
    ZVAL_UNDEF(&srv->cb_open);
    ZVAL_UNDEF(&srv->cb_message);
    ZVAL_UNDEF(&srv->cb_ping);
    ZVAL_UNDEF(&srv->cb_pong);
    ZVAL_UNDEF(&srv->cb_tick);

    srv->type           = LAURE_SERVER_TCP;
    srv->worker_num     = 1;
    srv->hb_interval_ms = LAURE_HB_INTERVAL_MS;
    srv->hb_timeout_ms  = LAURE_HB_TIMEOUT_MS;

    return &srv->std;
}

void laure_server_free_cbs(laure_server_t *srv) {
#define DROP(z)                                                                \
    do {                                                                       \
        if (!Z_ISUNDEF(z))                                                     \
            zval_ptr_dtor(&z);                                                 \
    } while (0)

    DROP(srv->cb_connect);
    DROP(srv->cb_receive);
    DROP(srv->cb_close);
    DROP(srv->cb_error);
    DROP(srv->cb_request);
    DROP(srv->cb_open);
    DROP(srv->cb_message);
    DROP(srv->cb_ping);
    DROP(srv->cb_pong);
    DROP(srv->cb_tick);
#undef DROP
}

static void laure_server_free_obj(zend_object *object) {
    laure_server_t *srv = LAURE_SERVER_OBJ(object);
    laure_server_free_cbs(srv);

    if (srv->host) {
        efree(srv->host);
    }

    if (srv->loop) {
        uv_loop_close(srv->loop);
        efree(srv->loop);
    }

#ifdef LAURE_WITH_SSL

#endif

    zend_object_std_dtor(object);
}

static void walk_close_all(uv_handle_t *h, void *arg) {
    (void)arg;
    if (!uv_is_closing(h)) {
        uv_close(h, NULL);
    }
}

static void on_signal(uv_signal_t *sig, int signum) {
    (void)signum;
    uv_walk(sig->loop, walk_close_all, NULL);
}

/* Tick callback */
static void on_tick(uv_timer_t *t) {
    laure_server_t *srv = (laure_server_t *)t->data;
    if (!Z_ISUNDEF(srv->cb_tick) && Z_TYPE(srv->cb_tick) != IS_NULL) {
        zval arg;
        ZVAL_OBJ_COPY(&arg, &srv->std);
        laure_zval_cb(&srv->cb_tick, 1, &arg);
        zval_ptr_dtor(&arg);
    }
}

void laure_server_start(laure_server_t *srv) {
    srv->loop = (uv_loop_t *)ecalloc(1, sizeof(uv_loop_t));
    if (uv_loop_init(srv->loop) != 0) {
        zend_throw_exception_ex(zend_ce_exception, 0,
                                "Failed to initialize loop");
        return;
    }

    uv_signal_init(srv->loop, &srv->sig_int);
    uv_signal_init(srv->loop, &srv->sig_term);
    srv->sig_int.data  = srv;
    srv->sig_term.data = srv;
    uv_signal_start(&srv->sig_int, on_signal, SIGINT);
    uv_signal_start(&srv->sig_term, on_signal, SIGTERM);

    uv_timer_init(srv->loop, &srv->timer_tick);
    srv->timer_tick.data = srv;
    uv_timer_start(&srv->timer_tick, on_tick, 1000, 1000);

    uv_tcp_init(srv->loop, &srv->tcp_handle);
    //     /* SO_REUSEADDR avoid EADDRINUSE after restart */
    //     {
    //         uv_os_fd_t fd;
    //         uv_fileno((uv_handle_t *)&srv->tcp_handle, &fd);
    //         int yes = 1;
    //         setsockopt((int)fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    // #ifdef SO_REUSEPORT
    //         setsockopt((int)fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
    // #endif
    //     }

    /* bind */
    struct sockaddr_storage addr;
    memset(&addr, 0, sizeof(addr));
    int r;
    if (strchr(srv->host, ':')) {
        r = uv_ip6_addr(srv->host, srv->port, (struct sockaddr_in6 *)&addr);
    } else {
        r = uv_ip4_addr(srv->host, srv->port, (struct sockaddr_in *)&addr);
    }

    if (r != 0) {
        zend_throw_exception_ex(zend_ce_exception, 0,
                                "laure: invalid address %s:%d", srv->host,
                                srv->port);
        goto cleanup;
    }

    r = uv_tcp_bind(&srv->tcp_handle, (const struct sockaddr *)&addr, 0);
    if (r != 0) {
        zend_throw_exception_ex(zend_ce_exception, 0,
                                "laure: failed to bind %s:%d: %s", srv->host,
                                srv->port, uv_strerror(r));
        goto cleanup;
    }

#ifdef LAURE_WITH_SSL

#endif

    r = uv_listen((uv_stream_t *)&srv->tcp_handle, LAURE_BACKLOG,
                  laure_on_new_connection);
    if (r != 0) {
        zend_throw_exception_ex(zend_ce_exception, 0,
                                "laure: failed to listen %s:%d: %s", srv->host,
                                srv->port, uv_strerror(r));
        goto cleanup;
    }

    uv_fileno((uv_handle_t *)&srv->tcp_handle, &srv->bound_fd);

    php_printf("lxxphp: single-process pid=%d listening on %s:%d\n",
               (int)getpid(), srv->host, srv->port);
    uv_run(srv->loop, UV_RUN_DEFAULT);

    return;

cleanup:
    uv_walk(srv->loop, walk_close_all, NULL);
    uv_run(srv->loop, UV_RUN_DEFAULT);
    uv_loop_close(srv->loop);
    efree(srv->loop);
    srv->loop = NULL;
}

void laure_server_stop(laure_server_t *srv) {
    if (!srv->loop) {
        return;
    }
    uv_walk(srv->loop, walk_close_all, NULL);
    uv_run(srv->loop, UV_RUN_DEFAULT);
}

void server_ctor(INTERNAL_FUNCTION_PARAMETERS, laure_server_type_t type) {
    char     *host;
    size_t    hlen;
    zend_long port;
    zval     *opts = NULL;

    ZEND_PARSE_PARAMETERS_START(2, 3)
    Z_PARAM_STRING(host, hlen)
    Z_PARAM_LONG(port)
    Z_PARAM_OPTIONAL
    Z_PARAM_ARRAY(opts)
    ZEND_PARSE_PARAMETERS_END();

    laure_server_t *srv = LAURE_SERVER_P(getThis());
    srv->host           = estrndup(host, hlen);
    srv->port           = port;
    srv->type           = type;

    int worker_num = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (worker_num < 1) {
        worker_num = 1;
    }

    srv->worker_num = 2; // worker_num;
}

PHP_METHOD(laure_server, on) {
    char  *event;
    size_t elen;
    zval  *cb;

    ZEND_PARSE_PARAMETERS_START(2, 2)
    Z_PARAM_STRING(event, elen)
    Z_PARAM_ZVAL(cb)
    ZEND_PARSE_PARAMETERS_END();

    if (!zend_is_callable(cb, 0, NULL)) {
        zend_throw_exception_ex(zend_ce_exception, 0,
                                "laure: callback is not callable");
        return;
    }

    laure_server_t *srv = LAURE_SERVER_P(getThis());

#define STORE(dest)                                                            \
    do {                                                                       \
        if (!Z_ISUNDEF(dest)) {                                                \
            zval_ptr_dtor(&dest);                                              \
        }                                                                      \
        ZVAL_COPY(&dest, cb);                                                  \
    } while (0)

    if (!strncmp(event, "connect", elen)) {
        STORE(srv->cb_connect);
    } else if (!strncmp(event, "receive", elen)) {
        STORE(srv->cb_receive);
    } else if (!strncmp(event, "close", elen)) {
        STORE(srv->cb_close);
    } else if (!strncmp(event, "error", elen)) {
        STORE(srv->cb_error);
    } else if (!strncmp(event, "request", elen)) {
        STORE(srv->cb_request);
    } else if (!strncmp(event, "open", elen)) {
        STORE(srv->cb_open);
    } else if (!strncmp(event, "message", elen)) {
        STORE(srv->cb_message);
    } else if (!strncmp(event, "ping", elen)) {
        STORE(srv->cb_ping);
    } else if (!strncmp(event, "pong", elen)) {
        STORE(srv->cb_pong);
    } else if (!strncmp(event, "tick", elen)) {
        STORE(srv->cb_tick);
    } else {
        zend_throw_exception_ex(zend_ce_exception, 0,
                                "laure: unknown event: %s", event);
        RETURN_THROWS();
    }
#undef STORE

    RETURN_OBJ_COPY(Z_OBJ_P(getThis()));
}

PHP_METHOD(laure_server, start) {
    laure_server_start(LAURE_SERVER_P(getThis()));
}

PHP_METHOD(laure_server, stop) {
    /* stop server */
    laure_server_stop(LAURE_SERVER_P(getThis()));
}

PHP_METHOD(laure_server, getHost) {
    RETURN_STRING(LAURE_SERVER_P(getThis())->host);
}

PHP_METHOD(laure_server, getPort) {
    RETURN_LONG(LAURE_SERVER_P(getThis())->port);
}

PHP_METHOD(laure_server, setHeartbeat) {
    zend_long interval, timeout = -1;
    ZEND_PARSE_PARAMETERS_START(1, 2)
    Z_PARAM_LONG(interval)
    Z_PARAM_OPTIONAL
    Z_PARAM_LONG(timeout)
    ZEND_PARSE_PARAMETERS_END();

    laure_server_t *srv = LAURE_SERVER_P(getThis());
    srv->hb_interval_ms = interval;
    if (timeout >= 0) {
        srv->hb_timeout_ms = timeout;
    }
    RETURN_OBJ_COPY(Z_OBJ_P(getThis()));
}

ZEND_BEGIN_ARG_INFO_EX(laure_server_void_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(laure_server_on_arginfo, 0, 0, 2)
ZEND_ARG_INFO(0, event)
ZEND_ARG_INFO(0, cb)
ZEND_END_ARG_INFO()

const static zend_function_entry laure_server_methods[] = {
    //
    PHP_ME(laure_server, start, laure_server_void_arginfo, ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_server, stop, laure_server_void_arginfo, ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_server, on, laure_server_on_arginfo, ZEND_ACC_PUBLIC)
    //
    PHP_FE_END
    //
};

void laure_register_server_class() {
    memcpy(&laure_server_obj_handlers, zend_get_std_object_handlers(),
           sizeof(zend_object_handlers));
    laure_server_obj_handlers.offset   = XtOffsetOf(laure_server_t, std);
    laure_server_obj_handlers.free_obj = laure_server_free_obj;

    zend_class_entry ce;
    INIT_NS_CLASS_ENTRY(ce, "Laure", "Server", laure_server_methods);
    laure_server_ce                = zend_register_internal_class(&ce);
    laure_server_ce->create_object = laure_server_new;
    laure_server_ce->ce_flags |= ZEND_ACC_ABSTRACT;
}
