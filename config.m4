
PHP_ARG_ENABLE([laure],
  [whether to enable laure support],
  [AS_HELP_STRING([--enable-laure],
    [Enable laure support])],
  [no])

AS_VAR_IF([PHP_LAURE], [no],, [
  PHP_ADD_INCLUDE($srcdir/deps/libuv/include)
  PHP_ADD_INCLUDE($srcdir/deps/llhttp)
  PHP_ADD_INCLUDE($srcdir/src)

  AC_DEFINE([HAVE_LAURE], [1], [Define to 1 if ...])

  PHP_ADD_LIBRARY([pthread])
  PHP_ADD_LIBRARY([dl])

  ext_shared=yes
  PHP_NEW_EXTENSION([laure],
    [
      deps/llhttp/api.c \
      deps/llhttp/http.c \
      deps/llhttp/llhttp.c \
      src/laure.c \
      src/utils/util.c \
      src/net/server.c \
      src/net/tls.c \
      src/net/tcp_server.c \
      src/net/http_server.c \
      src/net/websocket.c \
      src/net/heartbeat.c \
      src/net/conn.c
    ],
    [$ext_shared],, [-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1])

  dnl 定义 libuv 路径（configure 变量）
  LIBUV_DIR="$srcdir/deps/libuv"
  LIBUV_A="$LIBUV_DIR/.libs/libuv.a"
  AC_SUBST([LIBUV_A])

  dnl 添加 libuv.a 到链接参数
  LAURE_SHARED_LIBADD="$LAURE_SHARED_LIBADD $LIBUV_A"
  PHP_SUBST([LAURE_SHARED_LIBADD])

  dnl 生成构建 libuv 的 Makefile 片段
  cat > $ext_builddir/libuv.mk <<'EOF'
LIBUV_DIR = $(top_srcdir)/deps/libuv
LIBUV_A = $(LIBUV_DIR)/.libs/libuv.a

$(builddir)/laure.la: $(LIBUV_A)

$(LIBUV_A):
	@echo "Building libuv..."
	cd $(LIBUV_DIR) && sh autogen.sh && ./configure --enable-static --disable-shared CFLAGS="-fPIC" && make

$(builddir)/laure.lo: $(LIBUV_A)
EOF

  PHP_ADD_MAKEFILE_FRAGMENT([$ext_builddir/libuv.mk])
])
