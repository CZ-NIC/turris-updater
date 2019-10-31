TESTS = \
	events \
	util \
	syscnf \
	multiwrite \
	subprocess \
	download \
	uri \
	interpreter

check_PROGRAMS = $(TESTS)

SOURCES = ctest.c test_data.c
AM_CFLAGS = $(CHECK_FLAGS) -I../../src/lib
LDADD = ../../src/lib/libupdater.la $(CHECK_LIBS)

events_SOURCE = $(SOURCES) events.c
util_SOURCE = $(SOURCES) util.c
syscnf_SOURCE = $(SOURCES) syscnf.c
multiwrite_SOURCE = $(SOURCES) multiwrite.c
subprocess_SOURCE = $(SOURCES) subprocess.c
download_SOURCE = $(SOURCES) download.c
uri_SOURCE = $(SOURCES) uri.c
interpreter_SOURCE = $(SOURCES) interpreter.c
