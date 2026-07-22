#ifndef LAURE_TLS_H
#define LAURE_TLS_H
#include "conn.h"
int  laure_tls_ctx_init(void **ctx, const char *cert, const char *key);
void laure_tls_ctx_free(void *ctx);
int  laure_tls_accept(laure_conn_t *c, void *ctx);
int  laure_tls_feed(laure_conn_t *c, const char *enc, size_t n, laure_buf_t *plain); int  lxx_tls_send(laure_conn_t *c, const char *data, size_t n); 
void laure_tls_conn_free(laure_conn_t *c);

#endif