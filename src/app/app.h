#ifndef LAURE_APP_H
#define LAURE_APP_H
#include "php_laure.h"

typedef struct {
    char       *host;
    zend_long   port;
    zval        view;
    zval        router;
    zval        server; // server object
    zval        config; // config object
    char        view_path[4096];
    zend_object std;
} laure_app_obj_t;

#define Z_APP_OBJ_P(zv) Z_APP_P(Z_OBJ_P(zv))

#define Z_APP_P(obj)                                                           \
    (laure_app_obj_t *)((char *)(obj) - XtOffsetOf(laure_app_obj_t, std))

void laure_register_app_class(void);

#endif // LAURE_APP_H