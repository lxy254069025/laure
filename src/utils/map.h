#ifndef LAURE_MAP_H
#define LAURE_MAP_H

#include "zend_API.h"

#include "khash.h"
#include <stdlib.h>

KHASH_MAP_INIT_STR(laure_strmap, char *)

typedef khash_t(laure_strmap) laure_strmap_t;

static inline laure_strmap_t *laure_strmap_new() {
    return kh_init(laure_strmap);
}

static inline int laure_strmap_set(laure_strmap_t *m, const char *key,
                                   const char *val) {
    int      ret;
    khiter_t k = kh_put(laure_strmap, m, key, &ret);
    if (ret < 0)
        return -1;

    if (ret == 0) {
        free((char *)kh_key(m, k));
        free(kh_val(m, k));
    }

    kh_key(m, k) = strdup(key);
    kh_val(m, k) = strdup(val);
    return 0;
}

static inline int laure_strmap_set_take(laure_strmap_t *m, const char *key,
                                        char *val) {
    int      ret;
    khiter_t k = kh_put(laure_strmap, m, key, &ret);
    if (ret < 0) {
        free(val);
        return -1;
    }

    if (ret == 0) {
        free((char *)kh_key(m, k));
        free(kh_val(m, k));
    }

    kh_key(m, k) = strdup(key);
    kh_val(m, k) = val;

    return 0;
}

static inline char *laure_strmap_get(laure_strmap_t *m, const char *key) {
    khiter_t k = kh_get(laure_strmap, m, key);
    if (k == kh_end(m))
        return NULL;

    return kh_val(m, k);
}

/* case-insensitive get http header */
static inline char *laure_strmap_get_ci(laure_strmap_t *m, const char *key) {
    khiter_t k;
    for (k = kh_begin(m); k != kh_end(m); ++k) {
        if (!kh_exist(m, k)) {
            continue;
        }
        if (strcasecmp(kh_key(m, k), key) == 0)
            return kh_val(m, k);
    }

    return NULL;
}

static inline void laure_strmap_free(laure_strmap_t *m) {
    khiter_t k;
    if (!m) {
        return;
    }
    for (k = kh_begin(m); k != kh_end(m); ++k) {
        if (!kh_exist(m, k)) {
            continue;
        }
        free((char *)kh_key(m, k));
        free(kh_val(m, k));
    }
    kh_destroy(laure_strmap, m);
}

static inline void laure_strmap_clear(laure_strmap_t *m) {
    khiter_t k;
    if (!m) {
        return;
    }
    for (k = kh_begin(m); k != kh_end(m); ++k) {
        if (!kh_exist(m, k)) {
            continue;
        }
        free((char *)kh_key(m, k));
        free(kh_val(m, k));
    }
    kh_clear(laure_strmap, m);
}

static inline void laure_strmap_to_array(laure_strmap_t *m, zval *arr) {
    khiter_t k;
    for (k = kh_begin(m); k != kh_end(m); ++k) {
        if (!kh_exist(m, k)) {
            continue;
        }
        add_assoc_string(arr, kh_key(m, k), kh_val(m, k));
    }
}

#endif