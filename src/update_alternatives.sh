#!/bin/sh
set -e

if [ $# -gt 0 ]; then
	echo "This script is part of updater and allows user to manually fix alternative links in system." >&2
	exit 0
fi

if [ ! -d /usr/lib/opkg/info ]; then
	echo "OPKG info directory not located. This is OpenWrt system, isn't it?" >&2
	exit 1
fi

sed -n 's/^Alternatives://p' /usr/lib/opkg/info/*.control | \
	tr , '\n' | \
	sed 's/^\ \([^:]*\):\([^:]*\):/\1:\2:/' | \
	sort -n | \
	while IFS=: read PRIO TRG SRC; do
		ln -sf "$SRC" "$TRG"
	done

for applet in $(busybox --list); do
	for prefix in /bin /sbin /usr/bin /usr/sbin; do
		if [ -L "$prefix/$applet" ]; then
			[ -x "$prefix/$applet" ] || ln -sf /bin/busybox "$prefix/$applet"
		fi
	done
done
