#!/bin/sh

# Copyright (c) 2016, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#    * Neither the name of the CZ.NIC nor the
#      names of its contributors may be used to endorse or promote products
#      derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL CZ.NIC BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

DISABLED=$( (uci -q get updater.override.disable || echo false) | sed -e 's/0/false/;s/1/true/')
if $DISABLED ; then
	echo "Updater disabled" | logger -t daemon.warning
	echo "Updater disabled" >&2
	exit 0
fi

get-api-crl

STATE_DIR=/tmp/update-state
LOCK_DIR="$STATE_DIR/lock"
LOG_FILE="$STATE_DIR/log2"
PID_FILE="$STATE_DIR/pid"
EXIT_CODE=1
BACKGROUND=false
BACKGROUNDED=false
TMP_DIR="/tmp/$$.tmp"

timeout() {
	# Let a command run for up to $1 seconds. If it doesn't finishes by then, kill it.
	# The timeout starts it in background. Also, a watcher process is started that'd kill
	# it after the timeout and waits for it to finish. If the program finishes, it kills
	# the watcher.
	TIME="$1"
	PROG="$2"
	shift 2
	"$PROG" "$@" >"$TMP_DIR"/t-output &
	export CPID="$!"
	(
		# Note that Busybox doesn't have sleep as build-in, but as external
		# program. So backgrounded wait can stay running even after script exits
		# if not killed.
		sleep "$TIME"
		echo "Killing $PROG/$CPID after $TIME seconds (stuck?)" | logger -t updater -p daemon.error
		kill "$CPID"
		sleep 5
		kill -9 "$CPID"
		# Wait to be killed by the parrent
		sleep 60
	) &
	WATCHER="$!"
	wait "$CPID"
	RESULT="$?"
	CPID=
	kill "$WATCHER"
	wait "$WATCHER"
	WATCHER=
	return "$RESULT"
}

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
		echo "Already running" | logger -t updater -p daemon.warning
		EXIT_CODE=0
		exit
	fi
	cat /dev/null >"$LOG_FILE"
	echo startup >"$STATE_DIR/state"
	rm -f "$STATE_DIR/last_error" "$STATE_DIR/log2"
	echo $$>"$PID_FILE"
fi
if $BACKGROUND ; then
	"$0" -r >/dev/null 2>&1 &
	# Make sure the PID is of the process actually doing the work
	echo $!>"$PID_FILE"
	exit
fi
mkdir -p "$TMP_DIR"

WATCHER=
CPID=
trap_handler() {
	rm -rf "$LOCK_DIR" "$PID_FILE" "$TMP_DIR"
	[ -n "$WATCHER" ] && kill -9 $WATCHER 2>/dev/null
	[ -n "$CPID" ] && if kill $CPID 2>/dev/null; then
	(
		sleep 5
		kill -9 $CPID 2>/dev/null
	) & fi
	exit $EXIT_CODE
}
trap trap_handler EXIT INT QUIT TERM ABRT

# Run the actual updater
timeout 3000 pkgupdate --batch --state-log --task-log=/usr/share/updater/updater-log
# Evaluate what has run
EXIT_CODE="$?"
STATE=$(cat "$STATE_DIR"/state)
if [ "$EXIT_CODE" != "0" ] && [ "$STATE" != "error" ] ; then
	echo lost >"$STATE_DIR"/state
fi
if [ "$STATE" != "done" ] && [ "$STATE" != "error" ] ; then
	echo lost >"$STATE_DIR"/state
fi

if [ -s "$STATE_DIR"/log2 ] && grep -q '^[IR]' "$STATE_DIR/log2" ; then
	timeout 120 create_notification -s update "$(sed -ne 's/^I \(.*\) \(.*\)/ • Nainstalovaná verze \2 balíku \1/p;s/^R \(.*\)/ • Odstraněn balík \1/p' "$LOG_FILE")" "$(sed -ne 's/^I \(.*\) \(.*\)/ • Installed version \2 of package \1/p;s/^R \(.*\)/ • Removed package \1/p' "$LOG_FILE")" || echo "Create notification failed" | logger -t updater -p daemon.error
fi
if [ "$EXIT_CODE" != 0 ] || [ "$STATE" != "done" ] ; then
	if [ -s "$STATE_DIR/last_error" ] ; then
		ERROR=$(cat "$STATE_DIR/last_error")
	else
		ERROR="Unknown error"
	fi
	timeout 120 create_notification -s error "Updater selhal: $ERROR" "Updater failed: $ERROR" || echo "Create notification failed" | logger -t updater -p daemon.error
fi
timeout 120 notifier || echo "Notifier failed" | logger -t updater -p daemon.error

# Let the trap clean up here
