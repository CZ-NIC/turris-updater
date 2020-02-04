ACLOCAL_AMFLAGS = -I m4
include $(srcdir)/utils/helpers.mk

bin_PROGRAMS =
lib_LTLIBRARIES =
CLEANFILES = 

include $(srcdir)/src/lib/Makefile.am
include $(srcdir)/src/pkgtransaction/Makefile.am
include $(srcdir)/src/pkgupdate/Makefile.am

if ENABLE_DOCS
dist_doc_DATA = README.md
endif

include $(srcdir)/tests/Makefile.am
