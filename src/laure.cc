#include "php_laure.h"

PHP_RINIT_FUNCTION(laure) {
#if defined(ZTS) && defined(COMPILE_DL_LAURE)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif

    return SUCCESS;
}
PHP_MINFO_FUNCTION(laure) {
    php_info_print_table_start();
    php_info_print_table_row(2, "laure support", "enabled");
    php_info_print_table_end();
}

PHP_MINIT_FUNCTION(laure) {
    /* If you have INI entries, uncomment these lines
    REGISTER_INI_ENTRIES();
    */

    return SUCCESS;
}

/* 当module被卸载时运行 */
PHP_MSHUTDOWN_FUNCTION(laure) {
    /* uncomment this line if you have INI entries
    UNREGISTER_INI_ENTRIES();
    */
    return SUCCESS;
}

zend_module_entry laure_module_entry = {
    STANDARD_MODULE_HEADER,
    "laure",              /* Extension name */
    NULL,                 /* zend_function_entry */
    PHP_MINIT(laure),     /* PHP_MINIT - Module initialization */
    PHP_MSHUTDOWN(laure), /* PHP_MSHUTDOWN - Module shutdown */
    PHP_RINIT(laure),     /* PHP_RINIT - Request initialization */
    NULL,                 /* PHP_RSHUTDOWN - Request shutdown */
    PHP_MINFO(laure),     /* PHP_MINFO - Module info */
    PHP_LAURE_VERSION,    /* Version */
    STANDARD_MODULE_PROPERTIES};
/* }}} */

#ifdef COMPILE_DL_LAURE
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(laure)
#endif
