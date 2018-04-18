#!/bin/sh

SUPERVISOR_ARGS="--wait-for-network"

WIPE=true
for ARG in "$@"; do
	if $WIPE; then
		shift $#
		WIPE=false
	fi

	case "$ARG" in
		--rand-sleep)
			SUPERVISOR_ARGS="$SUPERVISOR_ARGS --rand-sleep"
			;;
		-b|--background|-w|-n)
			echo "Argument $ARG ignored." >&2
			;;
		*)
			set "$@" "$ARG"
			;;
	esac
done

if [ -t 1 ] ; then
	echo "updater.sh is obsoleted. Please use pkgupdate directly instead." >&2
	if $BACKGROUND; then
		pkgupdate --batch "$@" &
	else
		pkgupdate --batch "$@"
	fi
else
	if which updater-supervisor 2>/dev/null >&2; then
		updater-supervisor -d
	else
		pkgupdate --batch &
	fi
fi
