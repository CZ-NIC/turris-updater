ACLOCAL_AMFLAGS = -I .m4

bin_PROGRAMS =
lib_LTLIBRARIES =
EXTRA_DIST =
CLEANFILES =
linted_sources =


include $(srcdir)/src/Makefile.am
include $(srcdir)/utils/helpers.mk


if ENABLE_DOCS
dist_doc_DATA = README.md
endif

if ENABLE_LINTERS
include $(srcdir)/utils/lint.am
endif

include $(srcdir)/tests/Makefile.am
