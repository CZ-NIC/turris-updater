AC_INIT([updater-ng], [69.1.3], [tech.support@turris.cz])
AC_CONFIG_MACRO_DIRS([.m4])
AC_CONFIG_AUX_DIR([.aux])

AM_INIT_AUTOMAKE([
  foreign silent-rules subdir-objects
  -Wall -Wno-portability
  dist-zip dist-xz
])
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

PKG_CHECK_MODULES([lua], [lua >= 5.1 lua < 5.2],,
  [PKG_CHECK_MODULES([lua], [lua5.1])])
PKG_CHECK_MODULES([libevent], [libevent >= 2.0])
PKG_CHECK_MODULES([libarchive], [libarchive])
PKG_CHECK_MODULES([libcurl], [libcurl])
PKG_CHECK_MODULES([libcrypto], [libcrypto])
PKG_CHECK_MODULES([liburiparser], [liburiparser >= 0.9])
PKG_CHECK_MODULES([base64c], [base64c])
AC_CHECK_HEADERS([uthash.h], , AC_MSG_ERROR([uthash is required]))
AC_ARG_VAR(argp_LIBS, [linker flags for argp, overriding detection])
AS_IF([test -z "$argp_LIBS"],
	AC_SEARCH_LIBS([argp_parse], [argp], [argp_LIBS="-largp"]))

AC_PATH_PROG([PERL], [perl])
AS_IF([test -z "$PERL"], [AC_MSG_ERROR([Missing perl interpreter])])
AC_PATH_PROG([XXDI], [xxdi.pl],, [$ac_abs_confdir/utils])
AS_IF([test -z "$XXDI"], [AC_MSG_ERROR([Not able to locate xxdi.pl])])


dnl Documentation
AC_ARG_ENABLE([docs], AC_HELP_STRING([--disable-docs], [Disable documentation]))
AM_CONDITIONAL([ENABLE_DOCS], [test "x$enable_docs" != "xno"])

dnl Tests
AC_ARG_ENABLE([tests], AC_HELP_STRING([--enable-tests], [Whether to enable unit tests]))
AM_CONDITIONAL([ENABLE_TESTS], [test "x$enable_tests" = "xyes"])
AS_IF([test "x$enable_tests" = "xyes"], [
  PKG_CHECK_MODULES([CHECK], [check >= 0.11])
  AX_PROG_PERL_MODULES([common::sense], , AC_MSG_WARN(Perl modules File::Slurp is required))
  AC_REQUIRE_AUX_FILE([tap-driver.sh])
])
AX_VALGRIND_CHECK

dnl Linters
AC_ARG_ENABLE([linters], AC_HELP_STRING([--enable-linters], [Whether to enable code linting support (cppcheck, luacheck)]))
AM_CONDITIONAL([ENABLE_LINTERS], [test "x$enable_linters" = "xyes"])
AS_IF([test "x$enable_linters" = "xyes"], [
  AC_PATH_PROG([CPPCHECK], [cppcheck])
  AS_IF([test -z "$CPPCHECK" ], AC_MSG_ERROR([Missing linter cppcheck]))
  AC_PATH_PROG([LUACHECK], [luacheck])
  AS_IF([test -z "$LUACHECK" ], AC_MSG_ERROR([Missing linter luacheck]))
])

dnl Coverage
AX_CODE_COVERAGE


AC_CONFIG_FILES([Makefile])

AC_OUTPUT
