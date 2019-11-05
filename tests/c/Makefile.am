TESTS = \
	events \
	util \
	syscnf \
	multiwrite \
	subprocess \
	download \
	uri \
	interpreter

# TODO TMPDIR?
AM_TESTS_ENVIRONMENT = export S="$(top_srcdir)";

check_PROGRAMS = $(TESTS)
AM_CFLAGS = -I../../src/lib
LDADD = libctest.la

check_LTLIBRARIES = libctest.la
libctest_la_SOURCES = ctest.c test_data.c
libctest_la_CFLAGS = $(CHECK_FLAGS) $(AM_CFLAGS)
libctest_la_LIBADD = ../../src/lib/libupdater.la $(CHECK_LIBS)
