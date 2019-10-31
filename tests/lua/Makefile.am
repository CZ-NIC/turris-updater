include $(srcdir)/../usign.mk

TESTS = \
	backend.lua \
	events.lua \
	subprocess.lua \
	interpreter.lua \
	journal.lua \
	planner.lua \
	postprocess.lua \
	requests.lua \
	sandbox.lua \
	transaction.lua \
	utils.lua \
	syscnf.lua \
	cleanup.lua \
	uri.lua \
	picosat.lua \
	updater.lua

# TODO TMPDIR?
# TODO allow SUPPRESS_LOG modification somehow (possibly just # TESTS_ENVIRONMENT="export SUPPRESS_LOG=0;")
# TODO some tests are failing with SUPPRESS_LOG
AM_TESTS_ENVIRONMENT = \
	export SUPPRESS_LOG=1; \
	export DATADIR="$(abs_top_srcdir)/tests/data"; \
	export TOP_SRCDIR="$(top_srcdir)"; \
	$(TESTS_ENV_USIGN)
LOG_COMPILER = $(srcdir)/lunit-launch
AM_LOG_FLAGS =

check_PROGRAMS = lunit-launch
lunit_launch_SOURCES = lunit-launch.c
lunit_launch_CFLAGS =
lunit_launch_LDADD = ../../src/lib/libupdater.la


FILTER = $(foreach v,$(2),$(if $(findstring $(1),$(v)),$(v)))

LUA_FILES_H = lunit-launch.lua.h lunit.lua.h lunit-console.lua.h
CLEANFILES = $(LUA_FILES_H)
$(call FILTER,lunit-launch,$(lunit_launch_OBJECTS)): $(LUA_FILES_H)

lunit-launch.lua.h: lunit-launch.lua
	$(AM_V_GEN)$(XXDI) lunit_launch "$<" "$@"

lunit.lua.h lunit-console.lua.h: %.lua.h: lunit/%.lua
	$(AM_V_GEN)$(XXDI) "$(subst -,_,$*)" "$<" "$@"
