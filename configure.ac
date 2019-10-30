AC_INIT([updater-ng], [63.1.2], [tech.support@turris.cz])
AC_CONFIG_MACRO_DIRS([m4])

AM_INIT_AUTOMAKE([foreign silent-rules subdir-objects -Wall])
AM_SILENT_RULES([yes])

AC_PROG_CC
AM_PROG_AR
LT_INIT

PKG_CHECK_MODULES([LUA], [lua5.1])
PKG_CHECK_MODULES([LIBEVENT], [libevent >= 2.0])
PKG_CHECK_MODULES([LIBCURL], [libcurl])
PKG_CHECK_MODULES([LIBCRYPTO], [libcrypto])
PKG_CHECK_MODULES([LIBURIPARSER], [liburiparser])
AC_CHECK_HEADERS([b64/cdecode.h],, AC_MSG_ERROR([Missing libb64]))
AC_CHECK_HEADERS([argp.h],, AC_MSG_ERROR([Missing ARGP]))
AC_PATH_PROG([PERL], [perl])
AS_IF([test -z "$PERL"], [AC_MSG_ERROR([Missing perl interpreter])])
AC_PATH_PROG([XXDI], [xxdi.pl],, [$ac_abs_confdir/utils])
AS_IF([test -z "$XXDI"], [AC_MSG_ERROR([Not able to locate xxdi.pl])])

AC_CONFIG_FILES([
 Makefile
 src/Makefile
 src/lib/Makefile
 src/pkgupdate/Makefile
])

AC_OUTPUT
