TESTS =
check_PROGRAMS =
check_LTLIBRARIES =
TEST_EXTENSIONS = .lua .sys_trans .sys_update

if ENABLE_TESTS
DISTCHECK_CONFIGURE_FLAGS = --enable-tests

include $(srcdir)/%reldir%/c/Makefile.am
include $(srcdir)/%reldir%/lua/Makefile.am
include $(srcdir)/%reldir%/system/Makefile.am


# TODO TMPDIR?
AM_TESTS_ENVIRONMENT = \
	export TOP_SRCDIR="$(srcdir)"; \
	export DATADIR="$(srcdir)/tests/data"; \
	export DATA_DIR="$(abs_srcdir)/tests/data"; \
	export BUILD_DIR="$(abs_builddir)";

LOG_DRIVER = \
	env AM_TAP_AWK='$(AWK)' TEST_OUTPUT_TAP="/dev/stdout" \
	'$(SHELL)' '$(srcdir)/.aux/tap-driver.sh'


## Valgrind
VALGRIND_memcheck_FLAGS = \
	--leak-check=full \
	--show-leak-kinds=definite,indirect,possible \
	--track-fds=yes \
	--track-origins=yes \
	--trace-children=no \
	--child-silent-after-fork=yes
@VALGRIND_CHECK_RULES@

# Rules generated for valgrind are for some reason called *-am. This is just an alias
check-valgrind: check-valgrind-am
define check_valgrind_rule
check-valgrind-$(1): check-valgrind-$(1)-am
endef
$(foreach tool,$(valgrind_tools),$(eval $(call check_valgrind_rule,$(tool))))

## Coverage
CODE_COVERAGE_LCOV_OPTIONS := --no-external
CODE_COVERAGE_GENHTML_OPTIONS := --prefix $(dir $(abs_top_builddir))

include $(srcdir)/aminclude_static.am

clean-local: code-coverage-clean
distclean-local: code-coverage-dist-clean


endif
