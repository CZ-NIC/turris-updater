#!/bin/sh

if [ -t 1 ] || ! which updater-supervisor 2>/dev/null >&2; then
	echo "updater.sh is obsoleted. Please use pkgupdate directly instead." >&2
	pkgupdate --batch "$@"
else
	updater-supervisor -d
fi
