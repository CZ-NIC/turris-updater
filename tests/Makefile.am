# TODO solve valgrind tests execution somehow
# TODO coverage somehow
# TODO for valgrind runs we should optionally build purity openssl

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
