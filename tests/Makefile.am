TESTS =
check_PROGRAMS =
check_LTLIBRARIES =

# Valgrind #######################################################################
VALGRIND_memcheck_FLAGS = \
	--leak-check=full \
	--show-leak-kinds=definite,indirect,possible \
	--track-fds=yes \
	--track-origins=yes \
	--trace-children=yes \
	--child-silent-after-fork=no
@VALGRIND_CHECK_RULES@

# Rules generated for valgrind are for some reason called *-am. This is just an alias
check-valgrind: check-valgrind-am
define check_valgrind_rule
check-valgrind-$(1): check-valgrind-$(1)-am
endef
$(foreach tool,$(valgrind_tools),$(eval $(call check_valgrind_rule,$(tool))))

# Coverage #######################################################################
include $(srcdir)/aminclude_static.am

clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean

##################################################################################

# TODO TMPDIR?
AM_TESTS_ENVIRONMENT = \
	export TOP_SRCDIR="$(srcdir)"; \
	export DATADIR="$(srcdir)/tests/data"; \
	export DATA_DIR="$(abs_srcdir)/tests/data"; \
	export BUILD_DIR="$(abs_builddir)";

TEST_EXTENSIONS = .lua .sys_trans .sys_update

if ENABLE_TESTS
include $(srcdir)/%reldir%/c/Makefile.am
include $(srcdir)/%reldir%/lua/Makefile.am
include $(srcdir)/%reldir%/system/Makefile.am
endif
