include $(srcdir)/../usign.mk

TESTS = \
	util \
	syscnf \
	multiwrite \
	subprocess \
	download \
	uri \
	interpreter

# TODO TMPDIR?
AM_TESTS_ENVIRONMENT = \
	export SUPPRESS_LOG=1; \
	export DATADIR="$(abs_top_srcdir)/tests/data"; \
	$(TESTS_ENV_USIGN)

check_PROGRAMS = $(TESTS)
AM_CFLAGS = -I$(top_builddir)/src/lib
LDADD = libctest.la

check_LTLIBRARIES = libctest.la
libctest_la_SOURCES = ctest.c test_data.c
libctest_la_CFLAGS = $(CHECK_FLAGS) $(AM_CFLAGS)
libctest_la_LIBADD = $(top_builddir)/src/lib/libupdater.la $(CHECK_LIBS)
