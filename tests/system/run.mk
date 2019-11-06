include $(srcdir)/../../usign.mk

AM_TESTS_ENVIRONMENT = \
	export DATA_DIR="$(abs_top_srcdir)/tests/data"; \
	export BUILD_DIR="$(abs_top_builddir)"; \
	$(TESTS_ENV_USIGN)
LOG_COMPILER = %reldir%/run
AM_LOG_FLAGS =
