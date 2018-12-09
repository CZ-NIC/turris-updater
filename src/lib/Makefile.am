noinst_LTLIBRARIES = libupdater.la
libupdater_la_SOURCES = \
	arguments.c \
	download.c \
	embed_types.c \
	events.c \
	inject.c \
	interpreter.c \
	journal.c \
	locks.c \
	logging.c \
	multiwrite.c \
	opmode.c \
	picosat.c \
	subprocess.c \
	syscnf.c \
	uri.c \
	uri_lua.c \
	util.c \
	picosat-965/picosat.c
LUA_FILES = \
	backend.lua \
	cleanup.lua \
	dumper.lua \
	logging.lua \
	planner.lua \
	postprocess.lua \
	requests.lua \
	sandbox.lua \
	stacktraceplus.lua \
	testing.lua \
	transaction.lua \
	updater.lua \
	utils.lua

libupdater_la_CFLAGS = \
	$(LUA_CFLAGS) \
	$(LIBEVENT_CFLAGS) \
	$(LIBCURL_CLAGS) \
	$(LIBCRYPTO_CFLAGS) \
	$(LIBURIPARSER_CFLAGS)
libupdater_la_LDFLAGS = \
	$(LUA_LIBS) \
	$(LIBEVENT_LIBS) \
	$(LIBCURL_LIBS) \
	$(LIBCRYPTO_LIBS) \
	$(LIBURIPARSER_LIBS) \
	-lb64 -ldl

FILTER = $(foreach v,$(2),$(if $(findstring $(1),$(v)),$(v)))

# picosat-964 files are of external origin so we are not interested in warnings
$(call FILTER,picosat-965/,$(libupdater_la_OBJECTS)): CFLAGS+=-w

LUA_FILES_H := $(patsubst %,lua/%.h,$(LUA_FILES))
CLEANFILES = $(LUA_FILES_H)
$(call FILTER,interpreter,$(libupdater_la_OBJECTS)): $(LUA_FILES_H)

lua/%.lua.h: lua/%.lua
	$(AM_V_GEN)$(XXDI) "lua_$*" "$<" "$@"
