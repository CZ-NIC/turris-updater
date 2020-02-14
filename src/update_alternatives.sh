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
	sed 's/^\ \([^:]*\):\([^:]*\):/\2:\1:/' | \
	sort | \
	while IFS=: read TRG PRIO SRC; do
		ln -sf "$SRC" "$TRG"
	done
