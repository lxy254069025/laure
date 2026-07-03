#ifndef LAURE_STRING_H
#define LAURE_STRING_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} laure_buf_t;

static inline void laure_buf_init(laure_buf_t *b) {
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

static inline void laure_buf_free(laure_buf_t *b) {
    if (b->data)
        free(b->data);
    b->data = NULL;
    b->len  = 0;
    b->cap  = 0;
}

static inline int laure_buf_append(laure_buf_t *b, const char *data,
                                   size_t len) {
    if (len == 0)
        return 1;
    if (b->len + len + 1 > b->cap) {
        size_t newcap = b->cap ? b->cap * 2 : 64;
        while (newcap < b->len + len + 1)
            newcap *= 2;
        char *p = b->data ? realloc(b->data, newcap) : malloc(newcap);
        if (!p)
            return 0;
        b->data = p;
        b->cap  = newcap;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = '\0';
    return 1;
}

static inline void laure_buf_reset(laure_buf_t *b) {
    b->len = 0;
    if (b->data)
        b->data[0] = '\0';
}

static inline void laure_buf_consume(laure_buf_t *b, size_t n) {

    if (n >= b->len) {
        laure_buf_reset(b);
        return;
    }

    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
    b->data[b->len] = '\0';
}
#endif