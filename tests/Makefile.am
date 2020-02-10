TESTS =
check_PROGRAMS =
check_LTLIBRARIES =

# Usign ##########################################################################
usign_src_dir = $(abs_srcdir)/tests/usign
usign_build_dir = $(abs_builddir)/tests/usign
usign_exec = $(usign_build_dir)/usign

$(usign_exec):
	$(AM_V_at)$(MKDIR_P) "$(@D)"
	$(AM_V_GEN)cd "$(@D)" && cmake -S "$(usign_src_dir)"
	$(AM_V_GEN)$(MAKE) -C "$(@D)"

.PHONY: clean-local-usign
clean-local: clean-local-usign
clean-local-usign:
	[ ! -d "$(usign_build_dir)" ] || $(MAKE) -C "$(usign_build_dir)" clean

check_SCRIPTS = $(usign_exec)

# Valgrind #######################################################################
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
	export BUILD_DIR="$(abs_builddir)"; \
	export PATH="$$PATH:$(usign_build_dir)";

TEST_EXTENSIONS = .lua .sys_trans .sys_update

if ENABLE_TESTS
include $(srcdir)/%reldir%/c/Makefile.am
include $(srcdir)/%reldir%/lua/Makefile.am
include $(srcdir)/%reldir%/system/Makefile.am
endif
