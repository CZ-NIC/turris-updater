noinst_LTLIBRARIES = libupdater.la
libupdater_la_SOURCES = \
	arguments.c \
	inject.c \
	interpreter.c \
	events.c \
	subprocess.c \
	download.c \
	uri.c \
	uri_lua.c \
	journal.c \
	locks.c \
	picosat.c \
	util.c \
	syscnf.c \
	multiwrite.c \
	logging.c \
	picosat-965/picosat.c
LUA_FILES = \
	stacktraceplus.lua \
	utils.lua \
	testing.lua \
	logging.lua \
	cleanup.lua \
	dumper.lua \
	backend.lua \
	transaction.lua \
	requests.lua \
	sandbox.lua \
	postprocess.lua \
	planner.lua \
	updater.lua

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

FILTER_OUT = $(foreach v,$(2),$(if $(findstring $(1),$(v)),,$(v)))

LUA_FILES_H := $(patsubst %,lua/%.h,$(LUA_FILES))
CLEANFILES = $(LUA_FILES_H)
$(call FILTER_OUT,interpreter,$(libupdater_la_OBJECTS)): $(LUA_FILES_H)

lua/%.lua.h: lua/%.lua
	$(AM_V_GEN)$(XXDI) "lua_$*" "$<" "$@"

if BUSYBOX_EMBED

CLEANFILES += busybox_exec.h
$(call FILTER_OUT,events,$(libupdater_la_OBJECTS)): busybox_exec.h

busybox_exec.h: $(BUSYBOX_EMBED)
	$(AM_V_GEN)$(XXDI) busybox_exec "$<" "$@"

endif
