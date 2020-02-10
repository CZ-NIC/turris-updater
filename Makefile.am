ACLOCAL_AMFLAGS = -I m4
include $(srcdir)/utils/helpers.mk

include $(srcdir)/src/Makefile.am

if ENABLE_DOCS
dist_doc_DATA = README.md
endif

include $(srcdir)/tests/Makefile.am
