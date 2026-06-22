
PHP_ARG_ENABLE([laure],
  [whether to enable laure support],
  [AS_HELP_STRING([--enable-laure],
    [Enable laure support])],
  [no])

AS_VAR_IF([PHP_LAURE], [no],, [
  PHP_REQUIRE_CXX()
  CXXFLAGS="$CXXFLAGS -std=c++20 -Wno-c++17-extensions"

  PHP_ADD_INCLUDE($srcdir/deps/libuv)
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
      src/laure.cc
    ],
    [$ext_shared],,
    [-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1])

    dnl 将 libuv.a 添加到扩展的链接依赖
  PHP_ADD_EXTENSION_DEP([laure], [libuv])
  
  dnl 生成构建 libuv 的 Makefile 片段
  cat > $ext_builddir/libuv.mk <<'EOF'
# libuv 构建规则
LIBUV_DIR = $(top_srcdir)/deps/libuv
LIBUV_A = $(LIBUV_DIR)/.libs/libuv.a

# 扩展的 .la 文件依赖于 libuv.a
$(builddir)/laure.la: $(LIBUV_A)

# 编译 libuv 静态库
$(LIBUV_A):
	@echo "Building libuv..."
	cd $(LIBUV_DIR) && \
  sh autogen.sh && \
  ./configure --enable-static --disable-shared CFLAGS="-fPIC" && \
  make

# 确保链接扩展时添加 libuv.a
$(builddir)/laure.lo: $(LIBUV_A)
EOF

  PHP_ADD_MAKEFILE_FRAGMENT([$ext_builddir/libuv.mk])
  
  dnl 将 libuv.a 添加到扩展的共享库链接中
  LAURE_SHARED_LIBADD="$LAURE_SHARED_LIBADD $LIBUV_A"
  PHP_SUBST([LAURE_SHARED_LIBADD])
])
