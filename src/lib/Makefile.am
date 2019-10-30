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
LUA_SOURCES = \
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

interpreter.$(OBJEXT): $(patsubst %,lua/%.h,$(LUA_SOURCES))

lua/%.lua.h: lua/%.lua
	$(AM_V_GEN)$(XXDI) "lua_$*" "$<" "$@"
