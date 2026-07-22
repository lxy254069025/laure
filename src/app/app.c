#include "app.h"
#include "zend_API.h"

#include "net/http_server.h"

zend_class_entry           *laure_app_ce;
static zend_object_handlers laure_app_obj_handlers;

static zend_object *laure_app_obj_new(zend_class_entry *ce) {
    laure_app_obj_t *app_obj =
        ecalloc(1, sizeof(laure_app_obj_t) + zend_object_properties_size(ce));
    zend_object_std_init(&app_obj->std, ce);
    object_properties_init(&app_obj->std, ce);
    app_obj->std.handlers = &laure_app_obj_handlers;

    ZVAL_UNDEF(&app_obj->view);
    ZVAL_UNDEF(&app_obj->router);
    ZVAL_UNDEF(&app_obj->server);
    ZVAL_UNDEF(&app_obj->config);
    return &app_obj->std;
}

static void laure_app_obj_free(zend_object *object) {
    laure_app_obj_t *app_obj = Z_APP_P(object);

    if (Z_TYPE(app_obj->view) != IS_UNDEF) {
        zval_ptr_dtor(&app_obj->view);
    }
    if (Z_TYPE(app_obj->router) != IS_UNDEF) {
        zval_ptr_dtor(&app_obj->router);
    }
    if (Z_TYPE(app_obj->server) != IS_UNDEF) {
        zval_ptr_dtor(&app_obj->server);
    }
    if (Z_TYPE(app_obj->config) != IS_UNDEF) {
        zval_ptr_dtor(&app_obj->config);
    }

    if (app_obj->host) {
        efree(app_obj->host);
    }

    zend_object_std_dtor(&app_obj->std);
}

static int is_static_file(const char *path, size_t len) {
    for (size_t i = len; i > 0; i--) {
        if (path[i - 1] == '/') {
            break;
        }
        if (path[i - 1] == '.') {
            return 1;
        }
    }
    return 0;
}

static void laure_http_request_cb(zval *server) {}

static void init_server(laure_app_obj_t *app_obj) {
    object_init_ex(&app_obj->server, laure_http_server_ce);
    zval arg[2];
    ZVAL_STRING(&arg[0], app_obj->host);
    ZVAL_LONG(&arg[1], app_obj->port);
    zend_call_method_with_2_params(Z_OBJ(app_obj->server), laure_http_server_ce,
                                   NULL, "__construct", NULL, &arg[0], &arg[1]);
    zval_ptr_dtor(&arg[0]);
    zval_ptr_dtor(&arg[1]);
}

PHP_METHOD(laure_app, __construct) {
    laure_app_obj_t *app_obj = Z_APP_OBJ_P(getThis());
    char            *config, *host;
    size_t           clen, hlen;
    zend_long        port;

    ZEND_PARSE_PARAMETERS_START(2, 3)
    Z_PARAM_STRING(host, hlen)
    Z_PARAM_LONG(port)
    Z_PARAM_OPTIONAL
    Z_PARAM_STRING(config, clen)
    ZEND_PARSE_PARAMETERS_END();

    app_obj->host = estrndup(host, hlen);
    app_obj->port = port;

    init_server(app_obj);
}

PHP_METHOD(laure_app, router) {
    laure_app_obj_t *app_obj = Z_APP_OBJ_P(getThis());

    ZVAL_COPY(return_value, &app_obj->router);
}

PHP_METHOD(laure_app, server) {
    laure_app_obj_t *app_obj = Z_APP_OBJ_P(getThis());

    ZVAL_COPY(return_value, &app_obj->server);
}

PHP_METHOD(laure_app, config) {
    laure_app_obj_t *app_obj = Z_APP_OBJ_P(getThis());

    ZVAL_COPY(return_value, &app_obj->config);
}

PHP_METHOD(laure_app, view) {
    laure_app_obj_t *app_obj = Z_APP_OBJ_P(getThis());

    ZVAL_COPY(return_value, &app_obj->view);
}

PHP_METHOD(laure_app, run) {
    laure_app_obj_t *app_obj = Z_APP_OBJ_P(getThis());

    if (!app_obj->host) {
        zend_throw_exception(NULL, "Host is not set", 0);
        return;
    }

    zend_call_method_with_0_params(Z_OBJ(app_obj->server), laure_http_server_ce,
                                   NULL, "start", return_value);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_laure_app__ct, 0, 0, 1)
ZEND_ARG_INFO(0, config)
ZEND_END_ARG_INFO()

void laure_register_app_class(void) {}