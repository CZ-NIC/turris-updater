lib_LTLIBRARIES += libupdater.la

libupdater_la_SOURCES = \
	%reldir%/archive.c \
	%reldir%/arguments.c \
	%reldir%/base64.c \
	%reldir%/download.c \
	%reldir%/embed_types.c \
	%reldir%/events.c \
	%reldir%/inject.c \
	%reldir%/interpreter.c \
	%reldir%/journal.c \
	%reldir%/locks.c \
	%reldir%/logging.c \
	%reldir%/opmode.c \
	%reldir%/path_utils.c \
	%reldir%/picosat.c \
	%reldir%/signature.c \
	%reldir%/subprocess.c \
	%reldir%/syscnf.c \
	%reldir%/uri.c \
	%reldir%/uri_lua.c \
	%reldir%/util.c \
	%reldir%/picosat-965/picosat.c

LIBUPDATER_LUA_FILES = \
	%reldir%/lua/backend.lua \
	%reldir%/lua/cleanup.lua \
	%reldir%/lua/dumper.lua \
	%reldir%/lua/logging.lua \
	%reldir%/lua/planner.lua \
	%reldir%/lua/postprocess.lua \
	%reldir%/lua/requests.lua \
	%reldir%/lua/sandbox.lua \
	%reldir%/lua/stacktraceplus.lua \
	%reldir%/lua/testing.lua \
	%reldir%/lua/transaction.lua \
	%reldir%/lua/updater.lua \
	%reldir%/lua/utils.lua


libupdater_la_CFLAGS = \
	-iquote "$(builddir)/%reldir%" \
	$(LUA_CFLAGS) \
	$(LIBEVENT_CFLAGS) \
	$(LIBARCHIVE_CFLAGS) \
	$(LIBCURL_CLAGS) \
	$(LIBCRYPTO_CFLAGS) \
	$(LIBURIPARSER_CFLAGS) \
	$(CODE_COVERAGE_CFLAGS)
libupdater_la_LDFLAGS = \
	$(LUA_LIBS) \
	$(LIBEVENT_LIBS) \
	$(LIBARCHIVE_LIBS) \
	$(LIBCURL_LIBS) \
	$(LIBCRYPTO_LIBS) \
	$(LIBURIPARSER_LIBS) \
	$(CODE_COVERAGE_LIBS) \
	-ldl

##################################################################################

# picosat-964 files are of external origin so we are not interested in warnings
$(call FILTER,picosat-965/,$(libupdater_la_OBJECTS)): CFLAGS+=-w -DNGETRUSAGE -DTRACE

LUA_FILES_H := $(patsubst %,$(builddir)/%.h,$(LIBUPDATER_LUA_FILES))
CLEANFILES += $(LUA_FILES_H)
# interpreter.c includes all Lua files so add them as dependency
$(call FILTER,interpreter,$(libupdater_la_OBJECTS)): $(LUA_FILES_H)

$(builddir)/%reldir%/%.lua.h: $(builddir)/%reldir%/%.lua
	$(AM_V_at)$(MKDIR_P) "$(@D)"
	$(AM_V_GEN)$(XXDI) "lua_$(notdir $*)" "$<" "$@"


if BUSYBOX_EMBED

CLEANFILES += $(builddir)/%reldir%/busybox_exec.h
$(call FILTER,events,$(libupdater_la_OBJECTS)): $(builddir)/%reldir%/busybox_exec.h

$(builddir)/%reldir%/busybox_exec.h: $(BUSYBOX_EMBED)
	$(AM_V_GEN)$(XXDI) busybox_exec "$<" "$@"

endif

lint_sources += \
	$(filter-out %/picosat.c,$(libupdater_la_SOURCES)) \
	$(filter-out %/dumper.lua %/stacktraceplus.lua,$(LIBUPDATER_LUA_FILES))
