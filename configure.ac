AC_INIT([updater-ng], [67.0], [tech.support@turris.cz])
AC_CONFIG_MACRO_DIRS([m4])

AM_INIT_AUTOMAKE([foreign silent-rules subdir-objects -Wall -Wno-portability])
AM_SILENT_RULES([yes])

AC_PROG_CC
AC_GNU_SOURCE
AM_PROG_AR
LT_INIT


AX_CHECK_COMPILE_FLAG([-std=c11], , AC_MSG_ERROR([Compiler with C11 standard support is required]))
AX_APPEND_FLAG([-std=c11])

AC_ARG_WITH([embed-busybox], [AC_HELP_STRING([--with-embed-busybox=BUSYBOX], [Embed given busybox binary])])
AM_CONDITIONAL([BUSYBOX_EMBED], [test -n "$with_embed_busybox"])
AC_SUBST([BUSYBOX_EMBED], [$with_embed_busybox])
AS_IF([test -n "$with_embed_busybox"],
  [
   AS_IF([test -f "$with_embed_busybox"],,
	 [AC_MSG_ERROR([Invalid argument for --with-embed-busybox])])
   AC_DEFINE_UNQUOTED([BUSYBOX_EMBED], [$with_embed_busybox], [Embded Busybox])
  ])

PKG_CHECK_MODULES([LUA], [lua >= 5.1 lua < 5.2],,
  [PKG_CHECK_MODULES([LUA], [lua5.1])])
PKG_CHECK_MODULES([LIBEVENT], [libevent >= 2.0])
PKG_CHECK_MODULES([LIBARCHIVE], [libarchive])
PKG_CHECK_MODULES([LIBCURL], [libcurl])
PKG_CHECK_MODULES([LIBCRYPTO], [libcrypto])
PKG_CHECK_MODULES([LIBURIPARSER], [liburiparser >= 0.9])
AC_LINK_IFELSE(
   [AC_LANG_PROGRAM([#include <argp.h>],[argp_parse(0,1,NULL,0,0,0);])],,
   AC_CHECK_LIB([argp], [argp_parse], , AC_MSG_ERROR([Missing libargp]))
  )

AC_PATH_PROG([PERL], [perl])
AS_IF([test -z "$PERL"], [AC_MSG_ERROR([Missing perl interpreter])])
AC_PATH_PROG([XXDI], [xxdi.pl],, [$ac_abs_confdir/utils])
AS_IF([test -z "$XXDI"], [AC_MSG_ERROR([Not able to locate xxdi.pl])])

dnl Documentation
AC_ARG_ENABLE([docs], AC_HELP_STRING([--disable-docs], [Disable documentation]))
AM_CONDITIONAL([ENABLE_DOCS], [test "x$enable_docs" != "xno"])

dnl Tests
AC_ARG_ENABLE([tests], AC_HELP_STRING([--disable-tests], [Disable tests]))
AM_CONDITIONAL([ENABLE_TESTS], [test "x$enable_tests" != "xno"])
AS_IF([test "x$enable_tests" != "xno"], [
  PKG_CHECK_MODULES([CHECK], [check >= 0.11])
  AX_PROG_PERL_MODULES([common::sense], , AC_MSG_WARN(Perl modules File::Slurp is required))
])
AX_VALGRIND_CHECK

dnl Linters
AC_ARG_ENABLE([linters], AC_HELP_STRING([--disable-linters], [Disable linters (cppcheck and luacheck)]))
AM_CONDITIONAL([ENABLE_TESTS], [test "x$enable_linters" != "xno"])
AS_IF([test "x$enable_linters" != "xno"], [
  AC_PATH_PROG([LUACHECK], [luacheck])
  AC_PATH_PROG([CPPCHECK], [cppcheck])
])

dnl Coverage
AX_CODE_COVERAGE


AC_CONFIG_FILES([Makefile])

AC_OUTPUT
