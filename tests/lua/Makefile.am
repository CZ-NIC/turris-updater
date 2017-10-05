TESTS += \
	%reldir%/backend.lua \
	%reldir%/events.lua \
	%reldir%/subprocess.lua \
	%reldir%/interpreter.lua \
	%reldir%/journal.lua \
	%reldir%/pkgsorter.lua \
	%reldir%/planner.lua \
	%reldir%/postprocess.lua \
	%reldir%/requests.lua \
	%reldir%/sandbox.lua \
	%reldir%/transaction.lua \
	%reldir%/utils.lua \
	%reldir%/syscnf.lua \
	%reldir%/cleanup.lua \
	%reldir%/uri.lua \
	%reldir%/picosat.lua \
	%reldir%/updater.lua

LUA_LOG_COMPILER = $(builddir)/%reldir%/lunit-launch

check_PROGRAMS += %reldir%/lunit-launch
%canon_reldir%_lunit_launch_SOURCES = \
	%reldir%/lunit-launch.c
%canon_reldir%_lunit_launch_CFLAGS = \
	-I $(srcdir)/src/lib \
	-I $(builddir)/tests/lua \
	$(libpkgupdater_la_CFLAGS)
%canon_reldir%_lunit_launch_LDADD = libupdater.la


LUNIT_LUA_FILES_H = \
	%reldir%/lunit-launch.lua.h \
	%reldir%/lunit.lua.h \
	%reldir%/lunit-console.lua.h
CLEANFILES += $(LUNIT_LUA_FILES_H)
$(call FILTER,lunit-launch,$(%canon_reldir%_lunit_launch_OBJECTS)): $(LUNIT_LUA_FILES_H)

%reldir%/lunit-launch.lua.h: $(srcdir)/%reldir%/lunit-launch.lua
	$(AM_V_GEN)$(XXDI) lunit_launch "$<" "$@"

%reldir%/%.lua.h: $(srcdir)/%reldir%/lunit/%.lua
	$(AM_V_GEN)$(XXDI) "$(subst -,_,$(notdir $*))" "$<" "$@"
