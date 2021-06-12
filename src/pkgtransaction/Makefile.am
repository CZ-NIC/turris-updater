bin_PROGRAMS += pkgtransaction

pkgtransaction_SOURCES = \
	%reldir%/main.c \
	%reldir%/arguments.c

pkgtransaction_CFLAGS = \
	$(libupdater_la_CFLAGS)

pkgtransaction_LDADD = libupdater.la

linted_sources += $(pkgtransaction_SOURCES)
