TESTS += %reldir%/unittests-lib


check_PROGRAMS += %reldir%/unittests-lib
%canon_reldir%_unittests_lib_SOURCES = \
	%reldir%/unittests.c \
	%reldir%/test_data.h %reldir%/test_data.c \
	%reldir%/archive.c \
	%reldir%/changelog.c \
	%reldir%/download.c \
	%reldir%/interpreter.c \
	%reldir%/path_utils.c \
	%reldir%/signature.c \
	%reldir%/subprocess.c \
	%reldir%/syscnf.c \
	%reldir%/uri.c \
	%reldir%/util.c
%canon_reldir%_unittests_lib_CFLAGS = \
	-isystem '$(srcdir)/src/lib' \
	$(libupdater_la_CFLAGS) \
	$(CHECK_CFLAGS) \
	${CODE_COVERAGE_CFLAGS}
%canon_reldir%_unittests_lib_LDADD = \
	libupdater.la \
	$(CHECK_LIBS) \
	${CODE_COVERAGE_LIBS}

# TODO locks test in locks.c


linted_sources += $(%canon_reldir%_unittests_lib_SOURCES)
