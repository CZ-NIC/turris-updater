TESTS += \
	%reldir%/util \
	%reldir%/syscnf \
	%reldir%/multiwrite \
	%reldir%/subprocess \
	%reldir%/download \
	%reldir%/uri \
	%reldir%/interpreter

check_PROGRAMS += \
	%reldir%/util \
	%reldir%/syscnf \
	%reldir%/multiwrite \
	%reldir%/subprocess \
	%reldir%/download \
	%reldir%/uri \
	%reldir%/interpreter
%canon_reldir%_util_LDADD = %reldir%/libctest.la
%canon_reldir%_syscnf_LDADD = %reldir%/libctest.la
%canon_reldir%_multiwrite_LDADD = %reldir%/libctest.la
%canon_reldir%_subprocess_LDADD = %reldir%/libctest.la
%canon_reldir%_download_LDADD = %reldir%/libctest.la
%canon_reldir%_uri_LDADD = %reldir%/libctest.la
%canon_reldir%_interpreter_LDADD = %reldir%/libctest.la
%canon_reldir%_util_CFLAGS = -I $(srcdir)/src/lib $(libupdater_la_CFLAGS)
%canon_reldir%_syscnf_CFLAGS = -I $(srcdir)/src/lib $(libupdater_la_CFLAGS)
%canon_reldir%_multiwrite_CFLAGS = -I $(srcdir)/src/lib $(libupdater_la_CFLAGS)
%canon_reldir%_subprocess_CFLAGS = -I $(srcdir)/src/lib $(libupdater_la_CFLAGS)
%canon_reldir%_download_CFLAGS = -I $(srcdir)/src/lib $(libupdater_la_CFLAGS)
%canon_reldir%_uri_CFLAGS = -I $(srcdir)/src/lib $(libupdater_la_CFLAGS)
%canon_reldir%_interpreter_CFLAGS = -I $(srcdir)/src/lib $(libupdater_la_CFLAGS)
# TODO possibly integrate all tests to single binary or solve this LDADD somehow

check_LTLIBRARIES += %reldir%/libctest.la
%canon_reldir%_libctest_la_SOURCES = \
	%reldir%/ctest.c \
	%reldir%/test_data.c
%canon_reldir%_libctest_la_CFLAGS = \
	-I $(srcdir)/src/lib \
	$(CHECK_FLAGS)
%canon_reldir%_libctest_la_LIBADD = \
	libupdater.la \
	$(CHECK_LIBS)
