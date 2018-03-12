#!/bin/sh

if [ -t 1 ]; then
	echo "updater.sh is obsoleted. Please use pkgupdate directly instead." >&2
	pkgupdate --batch "$@"
else
	updater-supervisor -d
fi
