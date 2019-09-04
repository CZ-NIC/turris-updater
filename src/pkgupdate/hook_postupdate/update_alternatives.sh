#!/bin/sh
sed -n 's/^Alternatives://p' "$ROOT_DIR"/usr/lib/opkg/info/*.control | \
	tr , '\n' | \
	sed 's/^\ \([^:]*\):\([^:]*\):/\2:\1:/' | \
	sort | \
	while IFS=: read TRG PRIO SRC; do
		ln -sf "$SRC" "$ROOT_DIR$TRG"
	done
