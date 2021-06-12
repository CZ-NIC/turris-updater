bin_PROGRAMS += pkgupdate

pkgupdate_SOURCES = \
	%reldir%/main.c \
	%reldir%/arguments.c

pkgupdate_CFLAGS = \
	$(libupdater_la_CFLAGS)

pkgupdate_LDADD = libupdater.la

linted_sources += $(pkgupdate_SOURCES)
