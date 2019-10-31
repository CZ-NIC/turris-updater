AC_INIT([updater-ng], [63.1.2], [tech.support@turris.cz])
AC_CONFIG_MACRO_DIRS([m4])

AM_INIT_AUTOMAKE([foreign silent-rules subdir-objects -Wall -Wno-portability])
AM_SILENT_RULES([yes])

AC_PROG_CC
AM_PROG_AR
LT_INIT

AC_ARG_WITH([embed-busybox], [AC_HELP_STRING([--with-embed-busybox=BUSYBOX], [Embed given busybox binary])])
AM_CONDITIONAL([BUSYBOX_EMBED], [test -n "$with_embed_busybox"])
AC_SUBST([BUSYBOX_EMBED], [$with_embed_busybox])
AS_IF([test -n "$with_embed_busybox"],
  [
   AC_CHECK_FILE([$with_embed_busybox],,[AC_MSG_ERROR([Invalid argument for --with-embed-busybox])])
   AC_DEFINE_UNQUOTED([BUSYBOX_EMBED], [$with_embed_busybox], [Embded Busybox])
  ])

PKG_CHECK_MODULES([LUA], [lua5.1])
PKG_CHECK_MODULES([LIBEVENT], [libevent >= 2.0])
PKG_CHECK_MODULES([LIBCURL], [libcurl])
PKG_CHECK_MODULES([LIBCRYPTO], [libcrypto])
PKG_CHECK_MODULES([LIBURIPARSER], [liburiparser >= 0.9])
AC_CHECK_HEADERS([b64/cdecode.h],, AC_MSG_ERROR([Missing libb64]))
AC_LINK_IFELSE(
   [AC_LANG_PROGRAM([#include <argp.h>],[argp_parse(0,1,NULL,0,0,0);])],,
   AC_CHECK_LIB([argp], [argp_parse], , AC_MSG_ERROR([Missing libargp]))
  )

AC_PATH_PROG([PERL], [perl])
AS_IF([test -z "$PERL"], [AC_MSG_ERROR([Missing perl interpreter])])
AX_PROG_PERL_MODULES([File::Slurp], , AC_MSG_ERROR(Perl modules File::Slurp is required))
AC_PATH_PROG([XXDI], [xxdi.pl],, [$ac_abs_confdir/utils])
AS_IF([test -z "$XXDI"], [AC_MSG_ERROR([Not able to locate xxdi.pl])])

AC_CONFIG_FILES([
 Makefile
 src/Makefile
 src/lib/Makefile
 src/pkgupdate/Makefile
 src/pkgtransaction/Makefile
])

dnl Tests can be disabled so users does need all dependencies
AC_ARG_ENABLE([tests], AS_HELP_STRING([--disable-tests], [Disable tests]))
AS_IF([test "x$enable_tests" != "xno"], [

  PKG_CHECK_MODULES([CHECK], [check >= 0.11])
  AX_PROG_PERL_MODULES([common::sense], , AC_MSG_WARN(Perl modules File::Slurp is required))

  AC_CONFIG_FILES([
   tests/Makefile
   tests/c/Makefile
   tests/lua/Makefile
   tests/system/Makefile
   tests/system/pkgtransaction/Makefile
   tests/system/pkgupdate/Makefile
  ])

])

AC_OUTPUT
