/* laure extension for PHP */

#ifndef PHP_LAURE_H
#define PHP_LAURE_H

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <uv.h>

#include "php.h"
#include "php_ini.h"

#include "Zend/zend_exceptions.h"
#include "Zend/zend_interfaces.h"
#include "ext/json/php_json.h"
#include "ext/standard/info.h"

#include "zend_errors.h"
#include "zend_types.h"

#include "zend_API.h"
#include "zend_types.h"
#include "zend_variables.h"

#include "zend_errors.h"
#include "zend_frameless_function.h"
#include "zend_property_hooks.h"
#ifdef __cplusplus
}
#endif

extern zend_module_entry laure_module_entry;
#define phpext_laure_ptr &laure_module_entry

#define PHP_LAURE_VERSION "0.1.0"

#if defined(ZTS) && defined(COMPILE_DL_LAURE)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#endif /* PHP_LAURE_H */
