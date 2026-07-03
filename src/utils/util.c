#include "util.h"
#include "php_laure.h"

#include "ext/standard/base64.h"
#include "ext/standard/sha1.h"

void laure_zval_cb(zval *cb, int argc, zval *argv) {
    if (!cb || Z_ISUNDEF_P(cb) || Z_TYPE_P(cb) == IS_NULL)
        return;

    zval retval;
    ZVAL_UNDEF(&retval);
    call_user_function(NULL, NULL, cb, &retval, argc, argv);
    zval_ptr_dtor(&retval);
}

char *laure_base64_encode(const unsigned char *in, size_t n, size_t *out_len) {
    zend_string *zs  = php_base64_encode(in, n);
    char        *res = estrndup(ZSTR_VAL(zs), ZSTR_LEN(zs));
    if (out_len) {
        *out_len = ZSTR_LEN(zs);
    }
    zend_string_release(zs);
    return res;
}

char *laure_sha1_b64(const char *in, size_t n) {
    PHP_SHA1_CTX  ctx;
    unsigned char digest[20];
    PHP_SHA1Init(&ctx);
    PHP_SHA1Update(&ctx, (const unsigned char *)in, n);
    PHP_SHA1Final(digest, &ctx);
    return laure_base64_encode(digest, sizeof(digest), NULL);
}

int hv(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return -1;
}

void laure_url_decode(const char *in, size_t ilen, char **out,
                      size_t *out_len) {
    char  *buf = emalloc(ilen + 1);
    size_t j   = 0;
    for (size_t i = 0; i < ilen; i++) {
        if (in[i] == '+') {
            buf[j++] = ' ';
        } else if (in[i] == '%' && i + 2 < ilen) {
            int h1 = hv(in[i + 1]), lo = hv(in[i + 2]);
            if (h1 >= 0 && lo >= 0) {
                buf[j++] = (char)((h1 << 4) | lo);
                i += 2;
            } else {
                buf[j++] = in[i];
            }
        } else {
            buf[j++] = in[i];
        }
    }
    buf[j]   = '\0';
    *out     = buf;
    *out_len = j;
}

void laure_parse_qs(const char *qs, size_t qlen, laure_strmap_t *out) {
    if (!qs || !qlen) {
        return;
    }

    const char *p   = qs;
    const char *end = qs + qlen;

    while (p < end) {
        const char *amp = memchr(p, '&', (size_t)(end - p));
        if (!amp) {
            amp = end;
        }

        const char *eq = memchr(p, '=', (size_t)(amp - p));
        char       *k, *v;
        size_t      kl, vl;

        if (eq) {
            laure_url_decode(p, (size_t)(eq - p), &k, &kl);
            laure_url_decode(p, (size_t)(amp - eq - 1), &v, &vl);
        } else {
            laure_url_decode(p, (size_t)(amp - p), &k, &kl);
            v  = estrndup("", 0);
            vl = 0;
        }

        if (kl > 0) {
            laure_strmap_set_take(out, k, v);
        } else {
            efree(v);
        }

        efree(k);
        p = amp + 1;
    }
}

const char *laure_http_status_reason(int code) {
    switch (code) {
    case 100:
        return "Continue";
    case 101:
        return "Switching Protocols";
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 304:
        return "Not Modified";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 422:
        return "Unprocessable Entity";
    case 429:
        return "Too Many Requests";
    case 500:
        return "Internal Server Error";
    case 502:
        return "Bad Gateway";
    case 503:
        return "Service Unavailable";
    default:
        return "Unknown";
    }
}