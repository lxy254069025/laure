
PHP_ARG_ENABLE([laure],
  [whether to enable laure support],
  [AS_HELP_STRING([--enable-laure],
    [Enable laure support])],
  [no])

AS_VAR_IF([PHP_LAURE], [no],, [
  m4_include([deps/libev/libev.m4])

  PHP_ADD_INCLUDE($srcdir/deps/libev)
  PHP_ADD_INCLUDE($srcdir/deps/llhttp)
  PHP_ADD_INCLUDE($srcdir/src)

  AC_DEFINE([HAVE_LAURE], [1],
    [Define to 1 if the PHP extension 'laure' is available.])

  dnl Configure extension sources and compilation flags.
  PHP_NEW_EXTENSION([laure],
    [
      deps/llhttp/api.c   \
      deps/llhttp/http.c  \
      deps/llhttp/llhttp.c  \
      deps/libev/ev.c         \
      src/laure.c
    ],
    [$ext_shared],,
    [-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1])
])
