#ifndef LAURE_UTIL_H
#define LAURE_UTIL_H
#include "php_laure.h"

#include "map.h"

void  laure_zval_cb(zval *cb, int argc, zval *argv);
char *laure_base64_encode(const unsigned char *in, size_t n, size_t *out_len);
char *laure_sha1_b64(const char *in, size_t n);
int   hv(char c);
void laure_url_decode(const char *in, size_t ilen, char **out, size_t *out_len);
void laure_parse_qs(const char *qs, size_t qlen, laure_strmap_t *out);
const char *laure_http_status_reason(int code);
#endif