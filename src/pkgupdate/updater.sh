#!/bin/sh

get-api-crl

STATE_DIR=/tmp/update-state
LOCK_DIR="$STATE_DIR/lock"
EXIT_CODE=1

# Prepare a state directory and lock
mkdir -p /tmp/update-state
trap 'rm -rf "$LOCK_DIR"; exit "$EXIT_CODE"' EXIT INT QUIT TERM ABRT
if ! mkdir "$LOCK_DIR" ; then
	echo "Already running" >&2
	echo "Already running" | my_logger -p daemon.warning
	EXIT_CODE=0
	exit
fi
cat /dev/null >"$STATE_DIR"/log2

# Run the actual updater
UPDATER_ENABLE_STATE_LOG=true pkgupdate file:///etc/updater/entry.lua --batch
# Evaluate what has run
EXIT_CODE="$?"
STATE=$(cat "$STATE_DIR"/state)
if [ "$EXIT_CODE" != "0" ] && [ "$STATE" != "error" ] ; then
	echo lost >"$STATE_DIR"/state
fi
if [ "$STATE" != "done" ] && [ "$STATE" != "error" ] ; then
	echo lost >"$STATE_DIR"/state
fi

# Let the trap clean up here
