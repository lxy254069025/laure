#include "php.h"
#include "server.h"
#include "zend_API.h"
#include "zend_types.h"

zend_class_entry *laure_tcp_server_ce;

PHP_METHOD(laure_tcp_server, __construct) {
    server_ctor(INTERNAL_FUNCTION_PARAM_PASSTHRU, LAURE_SERVER_TCP);
}

ZEND_BEGIN_ARG_INFO_EX(laure_tcp_server_construct_arginfo, 0, 0, 2)
ZEND_ARG_INFO(0, host)
ZEND_ARG_INFO(0, port)
ZEND_ARG_ARRAY_INFO(0, opts, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry laure_tcp_server_methods[] = {
    //
    PHP_ME(laure_tcp_server, __construct, laure_tcp_server_construct_arginfo,
           ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
    //
    PHP_FE_END};
void laure_register_tcp_server_class() {
    zend_class_entry ce;
    INIT_NS_CLASS_ENTRY(ce, "Laure", "TcpServer", laure_tcp_server_methods);
    laure_tcp_server_ce = zend_register_internal_class_ex(&ce, laure_server_ce);
    laure_tcp_server_ce->create_object = laure_server_new;
    laure_tcp_server_ce->ce_flags |= ZEND_ACC_FINAL;
}