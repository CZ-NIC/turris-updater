#!/bin/sh

get-api-crl

STATE_DIR=/tmp/update-state
LOCK_DIR="$STATE_DIR/lock"
LOG_FILE="$STATE_DIR/log2"
EXIT_CODE=1
BACKGROUND=false
BACKGROUNDED=false

while [ "$1" ] ; do
	case "$1" in
		-b)
			BACKGROUND=true
			;;
		-r)
			BACKGROUNDED=true
			;;
		*)
			echo "Unknown parameter $1. Continuing anyway, as a compatibility measure for the old updater."
			;;
	esac
	shift
done

if ! $BACKGROUNDED ; then
	# Prepare a state directory and lock
	mkdir -p /tmp/update-state
	if ! mkdir "$LOCK_DIR" ; then
		echo "Already running" >&2
		echo "Already running" | logger -p daemon.warning
		EXIT_CODE=0
		exit
	fi
	cat /dev/null >"$LOG_FILE"
	echo startup >"$STATE_DIR/state"
fi
if $BACKGROUND ; then
	"$0" -r >/dev/null 2>&1 &
	# Make sure the PID is of the process actually doing the work
	echo $!>"$PID_FILE"
	exit
fi
trap 'rm -rf "$LOCK_DIR"; exit "$EXIT_CODE"' EXIT INT QUIT TERM ABRT

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

if [ -s "$STATE_DIR"/log2 ] ; then
	create_notification -s update "$(sed -ne 's/^I \(.*\) \(.*\)/ • Nainstalovaná verze \2 balíku \1/p;s/^R \(.*\)/ • Odstraněn balík \1/p' "$LOG_FILE")" "$(sed -ne 's/^I \(.*\) \(.*\)/ • Installed version \2 of package \1/p;s/^R \(.*\)/ • Removed package \1/p' "$LOG_FILE")" || echo "Create notification failed" | logger -p daemon.error
fi
if [ "$EXIT_CODE" != 0 ] || [ "$STATE" != "done" ] ; then
	if [ -s "$STATE_DIR/last_error" ] ; then
		ERROR=$(cat "$STATE_DIR/last_error")
	else
		ERROR="Unknown error"
	fi
	create_notification -s error "Updater selhal: $ERROR" "Updater failed: $ERROR" || echo "Create notification failed" | logger -p daemon.error
fi
notifier || echo "Notifier failed" | logger -p daemon.error

# Let the trap clean up here
