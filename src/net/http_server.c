#include "http_server.h"
#include "conn.h"
#include "llhttp.h"
#include "php.h"
#include "utils/khash.h"
#include "utils/map.h"
#include "utils/string.h"
#include "websocket.h"

#include "Zend/zend_smart_str.h"
#include "utils/util.h"
#include "zend_API.h"
#include "zend_object_handlers.h"
#include "zend_objects_API.h"
#include "zend_portability.h"
#include "zend_types.h"
#include "json/php_json.h"
#include <stddef.h>

zend_class_entry *laure_http_server_ce;

zend_class_entry    *laure_req_ce;
zend_object_handlers laure_req_handlers;

zend_class_entry    *laure_res_ce;
zend_object_handlers laure_res_handlers;

static int cb_url(llhttp_t *p, const char *at, size_t len) {
    return laure_buf_append(&(((laure_conn_t *)p->data)->http_url), at, len)
               ? 0
               : -1;
}

static int cb_method(llhttp_t *p, const char *at, size_t len) {
    return laure_buf_append(&(((laure_conn_t *)p->data)->http_method), at, len)
               ? 0
               : -1;
}

static int cb_header_field(llhttp_t *p, const char *at, size_t len) {
    laure_conn_t *c = (laure_conn_t *)p->data;

    if (c->http_cur_field.len > 0 && c->http_cur_value.len > 0) {
        for (size_t i = 0; i < c->http_cur_field.len; i++) {
            c->http_cur_field.data[i] =
                (char)tolower((unsigned char)c->http_cur_field.data[i]);
        }

        laure_strmap_set(c->http_req_header, c->http_cur_field.data,
                         c->http_cur_value.data);
        laure_buf_reset(&c->http_cur_field);
        laure_buf_reset(&c->http_cur_value);
    }

    if (laure_buf_append(&c->http_cur_field, at, len)) {
        return 0;
    } else {
        return -1;
    }
}

static int cb_header_value(llhttp_t *p, const char *at, size_t len) {
    return laure_buf_append(&(((laure_conn_t *)p->data)->http_cur_value), at,
                            len)
               ? 0
               : -1;
}

static int cb_headers_complete(llhttp_t *p) {
    laure_conn_t *c = (laure_conn_t *)p->data;
    if (c->http_cur_field.len > 0) {
        for (size_t i = 0; i < c->http_cur_field.len; i++) {
            c->http_cur_field.data[i] =
                (char)tolower((unsigned char)c->http_cur_field.data[i]);
        }

        laure_strmap_set(c->http_req_header, c->http_cur_field.data,
                         c->http_cur_value.data);
        laure_buf_reset(&c->http_cur_field);
        laure_buf_reset(&c->http_cur_value);
    }

    c->http_keep_alive = llhttp_should_keep_alive(p);

    if (laure_ws_is_upgrade(c)) {
        c->state = LAURE_CONN_WS_HAND;
        return HPE_PAUSED_UPGRADE;
    }

    return 0;
}

static int cb_body(llhttp_t *p, const char *at, size_t len) {
    return laure_buf_append(&(((laure_conn_t *)p->data)->http_body), at, len)
               ? 0
               : -1;
}

static int cb_message_complete(llhttp_t *p) {
    laure_conn_t *c  = (laure_conn_t *)p->data;
    c->http_msg_done = 1;
    laure_http_dispatch(c);

    laure_buf_reset(&c->http_method);
    laure_buf_reset(&c->http_url);
    laure_buf_reset(&c->http_body);
    laure_strmap_clear(c->http_req_header);
    c->http_msg_done = 0;

    return 0;
}

void laure_http_init_parser(laure_conn_t *c) {
    c->http_req_header = laure_strmap_new();
    llhttp_settings_init(&c->parser_settings);
    c->parser_settings.on_url              = cb_url;
    c->parser_settings.on_method           = cb_method;
    c->parser_settings.on_header_field     = cb_header_field;
    c->parser_settings.on_header_value     = cb_header_value;
    c->parser_settings.on_headers_complete = cb_headers_complete;
    c->parser_settings.on_body             = cb_body;
    c->parser_settings.on_message_complete = cb_message_complete;
    llhttp_init(&c->parser, HTTP_REQUEST, &c->parser_settings);
    c->parser.data = c;
}

char *laure_http_build_response(int status, const char *reason,
                                laure_strmap_t *headers, const char *body,
                                size_t body_len, size_t *out_len) {
    size_t hdr_extra = 0;
    {
        khiter_t k;
        for (k = kh_begin(headers); k != kh_end(headers); ++k) {
            if (!kh_exist(headers, k)) {
                continue;
            }
            hdr_extra +=
                strlen(kh_key(headers, k)) + strlen(kh_val(headers, k)) + 4;
        }
    }

    size_t sz  = 256 + hdr_extra + body_len;
    char  *buf = (char *)emalloc(sz);
    int    off = snprintf(buf, sz,
                          "HTTP/1.1 %d %s\r\n"
                             "Content-Length: %zu\r\n"
                             "Connection: keep-alive\r\n"
                             "Server: Laure/" PHP_LAURE_VERSION "\r\n",
                          status, reason, body_len);
    {
        khiter_t k;
        for (k = kh_begin(headers); k != kh_end(headers); ++k) {
            if (!kh_exist(headers, k)) {
                continue;
            }
            off += snprintf(buf + off, sz - off, "%s: %s\r\n",
                            kh_key(headers, k), kh_val(headers, k));
        }
    }

    off += snprintf(buf + off, sz - off, "\r\n");

    if (body_len) {
        memcpy(buf + off, body, body_len);
        off += body_len;
    }
    *out_len = (size_t)off;
    return buf;
}

void laure_http_dispatch(laure_conn_t *c) {
    laure_server_t *srv = c->server;

    zval zreq;
    object_init_ex(&zreq, laure_req_ce);
    laure_req_obj_t *req = LAURE_REQ_OBJ(Z_OBJ(zreq));

    req->method   = estrdup(c->http_method.data ? c->http_method.data : "");
    req->url      = estrdup(c->http_url.data ? c->http_url.data : "");
    req->body     = estrdup(c->http_body.data ? c->http_body.data : "");
    req->body_len = c->http_body.len;

    req->headers     = laure_strmap_new();
    req->get_params  = laure_strmap_new();
    req->post_params = laure_strmap_new();
    {
        khiter_t k;
        for (k = kh_begin(c->http_req_headers); k != kh_end(c->http_req_header);
             ++k) {
            if (!kh_exist(c->http_req_header, k))
                continue;
            laure_strmap_set(req->headers, kh_key(c->http_req_header, k),
                             kh_val(c->http_req_header, k));
        }
    }

    /* 删除解析url参数，由框架部分解析 */
    // char *qmark = req->url ? strchr(req->url, '?') : NULL;
    // if (qmark) {
    //     char  *decoded;
    //     size_t plen;
    // }

    /* post URL */
    const char *ct = laure_strmap_get(req->headers, "content-type");
    if (ct && strstr(ct, "application/x-www-form-urlencoded")) {
        laure_parse_qs(req->body, req->body_len, req->post_params);
    }

    zval zres;
    object_init_ex(&zres, laure_res_ce);
    laure_res_obj_t *res = LAURE_RES_P(&zres);
    res->conn            = c;
    res->status          = 200;
    res->reason          = estrdup("OK");
    res->resp_header     = laure_strmap_new();

    zval args[2];
    ZVAL_COPY(&args[0], &zreq);
    ZVAL_COPY(&args[1], &zres);
    laure_zval_cb(&srv->cb_request, 2, args);
    zval_ptr_dtor(&args[0]);
    zval_ptr_dtor(&args[1]);
    zval_ptr_dtor(&zreq);
    zval_ptr_dtor(&zres);

    if (!c->http_keep_alive) {
        laure_conn_close(c);
    }
}

static zend_object *laure_http_req_new(zend_class_entry *ce) {
    laure_req_obj_t *o = (laure_req_obj_t *)ecalloc(
        1, sizeof(laure_req_obj_t) + zend_object_properties_size(ce));
    zend_object_std_init(&o->std, ce);
    object_properties_init(&o->std, ce);
    o->std.handlers = &laure_req_handlers;
    return &o->std;
}

static void laure_http_req_free(zend_object *obj) {
    laure_req_obj_t *o = LAURE_REQ_OBJ(obj);

    if (o->method) {
        efree(o->method);
    }
    if (o->url) {
        efree(o->url);
    }
    if (o->path) {
        efree(o->path);
    }
    if (o->query) {
        efree(o->query);
    }
    if (o->body) {
        efree(o->body);
    }

    laure_strmap_free(o->headers);
    laure_strmap_free(o->get_params);
    laure_strmap_free(o->post_params);
    zend_object_std_dtor(obj);
}

/* request */
PHP_METHOD(laure_http_request, getMethod) {
    laure_req_obj_t *o = LAURE_REQ_P(getThis());
    if (o->method) {
        RETURN_STRING(o->method);
    } else {
        RETURN_EMPTY_STRING();
    }
}

PHP_METHOD(laure_http_request, getUrl) {
    laure_req_obj_t *o = LAURE_REQ_P(getThis());
    if (o->url) {
        RETURN_STRING(o->url);
    } else {
        RETURN_EMPTY_STRING();
    }
}

PHP_METHOD(laure_http_request, getPath) {
    laure_req_obj_t *o = LAURE_REQ_P(getThis());
    if (o->path) {
        RETURN_STRING(o->path);
    } else {
        RETURN_EMPTY_STRING();
    }
}

PHP_METHOD(laure_http_request, getQuery) {
    laure_req_obj_t *o = LAURE_REQ_P(getThis());
    if (o->query) {
        RETURN_STRING(o->query);
    } else {
        RETURN_EMPTY_STRING();
    }
}

PHP_METHOD(laure_http_request, getBody) {
    laure_req_obj_t *o = LAURE_REQ_P(getThis());
    if (o->body) {
        RETURN_STRINGL(o->body, o->body_len);
    } else {
        RETURN_EMPTY_STRING();
    }
}

PHP_METHOD(laure_http_request, getHeaders) {
    laure_req_obj_t *o = LAURE_REQ_P(getThis());
    if (o->headers) {
        laure_strmap_to_array(o->headers, return_value);
    } else {
        array_init(return_value);
    }
}

PHP_METHOD(laure_http_request, getHeader) {
    laure_req_obj_t *o = LAURE_REQ_P(getThis());
    char            *key;
    size_t           key_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_STRING(key, key_len)
    ZEND_PARSE_PARAMETERS_END();

    if (o->headers) {
        const char *val = laure_strmap_get_ci(o->headers, key);
        if (val) {
            RETURN_STRING(val);
        }
    }
    RETURN_NULL();
}

PHP_METHOD(laure_http_request, post) {
    laure_req_obj_t *o = LAURE_REQ_P(getThis());
    char            *key;
    size_t           key_len;
    ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_STRING(key, key_len)
    ZEND_PARSE_PARAMETERS_END();

    const char *val = laure_strmap_get(o->post_params, key);
    if (val) {
        RETURN_STRING(val);
    }
    RETURN_NULL();
}

PHP_METHOD(laure_http_request, json) {
    laure_req_obj_t *o = LAURE_REQ_P(getThis());
    zval             zj;
    if (php_json_decode_ex(&zj, o->body, o->body_len, PHP_JSON_OBJECT_AS_ARRAY,
                           512) == FAILURE) {
        RETURN_NULL();
    }
    RETVAL_ZVAL(&zj, 0, 0);
}

ZEND_BEGIN_ARG_INFO_EX(laure_http_void_arginfo, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(laure_http_key_arginfo, 0, 0, 1)
ZEND_ARG_INFO(0, key)
ZEND_END_ARG_INFO()

static const zend_function_entry laure_http_req_methods[] = {
    PHP_ME(laure_http_request, getMethod, laure_http_void_arginfo,
           ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_http_request, getUrl, laure_http_void_arginfo, ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_http_request, getPath, laure_http_void_arginfo,
           ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_http_request, getQuery, laure_http_void_arginfo,
           ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_http_request, getBody, laure_http_void_arginfo,
           ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_http_request, getHeaders, laure_http_void_arginfo,
           ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_http_request, getHeader, laure_http_key_arginfo,
           ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_http_request, post, laure_http_key_arginfo, ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_http_request, json, laure_http_void_arginfo, ZEND_ACC_PUBLIC)
    //
    PHP_FE_END};

static zend_object *laure_http_res_new(zend_class_entry *ce) {
    laure_res_obj_t *o = (laure_res_obj_t *)ecalloc(
        1, sizeof(laure_res_obj_t) + zend_object_properties_size(ce));
    zend_object_std_init(&o->std, ce);
    object_properties_init(&o->std, ce);
    o->std.handlers = &laure_res_handlers;
    return &o->std;
}

static void laure_http_res_free(zend_object *obj) {
    laure_res_obj_t *o = LAURE_RES_OBJ(obj);
    if (o->reason) {
        efree(o->reason);
    }
    laure_strmap_free(o->resp_header);
    zend_object_std_dtor(obj);
}

static void do_end(laure_res_obj_t *o, const char *body, size_t body_len) {
    if (o->ended || !o->conn) {
        return;
    }

    o->ended = 1;
    size_t rlen;
    char *resp = laure_http_build_response(o->status, o->reason, o->resp_header,
                                           body, body_len, &rlen);
    laure_conn_send_raw(o->conn, resp, rlen);
    efree(resp);
}

PHP_METHOD(laure_http_response, status) {
    laure_res_obj_t *o = LAURE_RES_P(getThis());
    zend_long        code;
    ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_LONG(code)
    ZEND_PARSE_PARAMETERS_END();

    o->status = (int)code;
    if (o->reason) {
        efree(o->reason);
    }
    o->reason = estrdup(laure_http_status_reason(o->status));
    RETURN_OBJ_COPY(Z_OBJ_P(getThis()));
}

PHP_METHOD(laure_http_response, header) {
    laure_res_obj_t *o = LAURE_RES_P(getThis());
    char            *key, *val;
    size_t           key_len, val_len;
    ZEND_PARSE_PARAMETERS_START(2, 2)
    Z_PARAM_STRING(key, key_len)
    Z_PARAM_STRING(val, val_len)
    ZEND_PARSE_PARAMETERS_END();

    laure_strmap_set(o->resp_header, key, val);
    RETURN_OBJ_COPY(Z_OBJ_P(getThis()));
}

PHP_METHOD(laure_http_response, end) {
    char  *b  = NULL;
    size_t bl = 0;
    ZEND_PARSE_PARAMETERS_START(0, 1)
    Z_PARAM_OPTIONAL
    Z_PARAM_STRING(b, bl)
    ZEND_PARSE_PARAMETERS_END();
    do_end(LAURE_RES_P(getThis()), b ? b : "", bl);
    RETURN_TRUE;
}

PHP_METHOD(laure_http_response, write) {
    char  *b;
    size_t bl;
    ZEND_PARSE_PARAMETERS_START(1, 1)
    Z_PARAM_STRING(b, bl)
    ZEND_PARSE_PARAMETERS_END();
    laure_res_obj_t *o = LAURE_RES_P(getThis());
    if (o->ended || !o->conn) {
        RETURN_FALSE;
    }

    if (!o->header_sent) {
        size_t rlen;
        char  *resp = laure_http_build_response(o->status, o->reason,
                                                o->resp_header, b, bl, &rlen);
        php_printf("resp: %s\n", resp);
        laure_conn_send_raw(o->conn, resp, rlen);
        efree(resp);
        o->header_sent = 1;
    } else {
        laure_conn_send_raw(o->conn, b, bl);
    }
    RETURN_TRUE;
}

PHP_METHOD(laure_http_response, json) {
    zval     *data;
    zend_long flags = 0;
    ZEND_PARSE_PARAMETERS_START(1, 2)
    Z_PARAM_ZVAL(data)
    Z_PARAM_OPTIONAL
    Z_PARAM_LONG(flags)
    ZEND_PARSE_PARAMETERS_END();

    laure_res_obj_t *o = LAURE_RES_P(getThis());
    if (o->ended || !o->conn) {
        RETURN_FALSE;
    }
    smart_str buf = {0};
    if (php_json_encode(&buf, data, flags) == FAILURE) {
        smart_str_free(&buf);
        zend_throw_exception_ex(zend_ce_exception, 0,
                                "laure: failed to encode JSON");
        RETURN_THROWS();
    }

    smart_str_0(&buf);
    laure_strmap_set(o->resp_header, "Content-Type",
                     "application/json; charset=utf-8");
    do_end(o, ZSTR_VAL(buf.s), ZSTR_LEN(buf.s));
    smart_str_free(&buf);
    RETURN_TRUE;
}

PHP_METHOD(laure_http_response, redirect) {
    char     *url;
    size_t    url_len;
    zend_long code = 302;
    ZEND_PARSE_PARAMETERS_START(1, 2)
    Z_PARAM_STRING(url, url_len)
    Z_PARAM_OPTIONAL
    Z_PARAM_LONG(code)
    ZEND_PARSE_PARAMETERS_END();

    laure_res_obj_t *o = LAURE_RES_P(getThis());
    laure_strmap_set(o->resp_header, "Location", url);
    o->status = code;
    if (o->reason) {
        efree(o->reason);
    }
    o->reason = estrdup(laure_http_status_reason(o->status));
    do_end(o, "", 0);
    RETURN_TRUE;
}

ZEND_BEGIN_ARG_INFO_EX(laure_http_res_status_arginfo, 0, 0, 1)
ZEND_ARG_INFO(0, code)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(laure_http_res_header_arginfo, 0, 0, 2)
ZEND_ARG_INFO(0, key)
ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

static const zend_function_entry laure_http_res_methods[] = {
    PHP_ME(laure_http_response, status, laure_http_res_status_arginfo,
           ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_http_response, header, laure_http_res_header_arginfo,
           ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_http_response, end, laure_http_void_arginfo, ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_http_response, write, laure_http_key_arginfo, ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_http_response, json, laure_http_void_arginfo, ZEND_ACC_PUBLIC)
    //
    PHP_ME(laure_http_response, redirect, laure_http_res_header_arginfo,
           ZEND_ACC_PUBLIC)
    //
    PHP_FE_END
    //
};

PHP_METHOD(laure_http_server, __construct) {
    server_ctor(INTERNAL_FUNCTION_PARAM_PASSTHRU, LAURE_SERVER_HTTP);
}

ZEND_BEGIN_ARG_INFO_EX(laure_http_server_construct_arginfo, 0, 0, 2)
ZEND_ARG_INFO(0, host)
ZEND_ARG_INFO(0, port)
ZEND_ARG_ARRAY_INFO(0, opts, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry laure_http_server_methods[] = {
    //
    PHP_ME(laure_http_server, __construct, laure_http_server_construct_arginfo,
           ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
    //
    PHP_FE_END};

void laure_register_http_server_class() {
    zend_class_entry ce;
    INIT_NS_CLASS_ENTRY(ce, "Laure", "HttpServer", laure_http_server_methods);
    laure_http_server_ce =
        zend_register_internal_class_ex(&ce, laure_server_ce);
    laure_http_server_ce->create_object = laure_server_new;
    laure_http_server_ce->ce_flags |= ZEND_ACC_FINAL;

    /* request */
    memcpy(&laure_req_handlers, zend_get_std_object_handlers(),
           sizeof(zend_object_handlers));
    laure_req_handlers.offset   = XtOffsetOf(laure_req_obj_t, std);
    laure_req_handlers.free_obj = laure_http_req_free;
    {
        zend_class_entry ce;
        INIT_NS_CLASS_ENTRY(ce, "Laure", "HttpRequest", laure_http_req_methods);
        laure_req_ce                = zend_register_internal_class(&ce);
        laure_req_ce->create_object = laure_http_req_new;
    }

    /* res */
    memcpy(&laure_res_handlers, zend_get_std_object_handlers(),
           sizeof(zend_object_handlers));
    laure_res_handlers.offset   = XtOffsetOf(laure_res_obj_t, std);
    laure_res_handlers.free_obj = laure_http_res_free;
    {
        zend_class_entry ce;
        INIT_NS_CLASS_ENTRY(ce, "Laure", "HttpResponse",
                            laure_http_res_methods);
        laure_res_ce                = zend_register_internal_class(&ce);
        laure_res_ce->create_object = laure_http_res_new;
    }
}