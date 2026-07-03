#ifndef LAURE_HTTP_SERVER_H
#define LAURE_HTTP_SERVER_H

#include "net/conn.h"
#include "server.h"
#include "utils/map.h"

typedef struct {
    char           *method, *url, *path, *query, *body;
    size_t          body_len;
    laure_strmap_t *headers;
    laure_strmap_t *get_params;
    laure_strmap_t *post_params;
    zend_object     std;
} laure_req_obj_t;

typedef struct {
    laure_conn_t   *conn;
    int             status;
    char           *reason;
    laure_strmap_t *resp_header;
    int             header_sent;
    int             ended;
    zend_object     std;
} laure_res_obj_t;

extern zend_class_entry *laure_http_server_ce;

#define LAURE_REQ_OBJ(o)                                                       \
    ((laure_req_obj_t *)((char *)(o) - XtOffsetOf(laure_req_obj_t, std)))
#define LAURE_REQ_P(zv) LAURE_REQ_OBJ(Z_OBJ_P(zv))
#define LAURE_RES_OBJ(o)                                                       \
    ((laure_res_obj_t *)((char *)(o) - XtOffsetOf(laure_res_obj_t, std)))
#define LAURE_RES_P(zv) LAURE_RES_OBJ(Z_OBJ_P(zv))

void  laure_http_init_parser(laure_conn_t *c);
void  laure_http_dispatch(laure_conn_t *c);
char *laure_http_build_response(int status, const char *reason,
                                laure_strmap_t *headers, const char *body,
                                size_t body_len, size_t *out_len);

void laure_register_http_server_class();

#endif