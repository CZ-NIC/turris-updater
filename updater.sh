#!/bin/busybox sh

# Copyright (c) 2013, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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

# Switches (to be used in this order if multiple are needed)
# -b: Fork to background.
# -r <Reason>: Restarted. Internal switch, not to be used by other applications.
# -n: Now. Don't wait a random amount of time before doing something.

set -xe

# Load the libraries
LIB_DIR="$(dirname "$0")"
. "$LIB_DIR/updater-worker.sh"

PID_FILE="$STATE_DIR/pid"
LOCK_DIR="$STATE_DIR/lock"
BACKGROUND=false
EXIT_CODE="1"

BASE_PLAN_FILE='/usr/share/updater/plan'

if [ "$1" = "-b" ] ; then
	BACKGROUND=true
	shift
fi

if [ "$1" = '-r' ] ; then
	echo "$2" | logger -t updater -p daemon.info
	shift 2
else
	"$LIB_DIR"/updater-wipe.sh # Remove forgotten stuff, if any

	# Create the state directory, set state, etc.
	mkdir -p "$STATE_DIR"
	if ! mkdir "$LOCK_DIR" ; then
		echo "Already running" >&2
		echo "Already running" | logger -t updater -p daemon.warning
		if [ "$1" = '-n' ] ; then
			# We were asked to run updater now. There's another one running, possibly sleeping. Make it stop.
			touch "$LOCK_DIR/dont_sleep"
		fi
		# For some reason, busybox sh doesn't know how to exit. Use this instead.
		EXIT_CODE="0"
		exit
	fi
	echo "$PID" >"$PID_FILE"
	echo 'startup' >"$STATE_FILE"
	echo 'initial sleep' >"$STATE_FILE"
	rm -f "$LOG_FILE" "$STATE_DIR/last_error"
	touch "$LOG_FILE"
fi

if $BACKGROUND ; then
	# If we background, we do so just after setting up the state dir. Don't remove it in
	# the trap yet and leave it up for the restarted process.
	shift
	"$0" -r "Backgrounded" -n "$@" >/dev/null 2>&1 &
	# Update the PID so updater-wipe does not remove it
	echo $! >"$PID_FILE"
	exit
fi

trap 'rm -rf "$TMP_DIR" "$PID_FILE" "$LOCK_DIR" /usr/share/updater/packages /usr/share/updater/plan; exit "$EXIT_CODE"' EXIT INT QUIT TERM ABRT

# Don't load the server all at once. With NTP-synchronized time, and
# thousand clients, it would make spikes on the CPU graph and that's not
# nice.
if [ "$1" != "-n" ] ; then
	TIME=$(( $(tr -cd 0-9 </dev/urandom | head -c 8 | sed -e 's/^0*//' ) % 120 ))
	for i in $(seq 1 $TIME) ; do
		if [ -f "$LOCK_DIR/dont_sleep" ] ; then
			break;
		fi
		sleep 1
	done
else
	shift
fi

mkdir -p "$TMP_DIR"

echo 'get list' >"$STATE_FILE"
get_list_main list

HAVE_WORK=false
echo 'examine' >"$STATE_FILE"
echo 'PKG_DIR=/usr/share/updater/packages' >"$PLAN_FILE"
prepare_plan list

if $HAVE_WORK ; then
	# Overwrite the restart function
	do_restart() {
		echo 'Update restart requested, complying' | logger -t updater -p daemon.info
		exec "$0" -r "Restarted" -n "$@"
	}

	# Back up the packages to permanent storage, so we can resume on next restart if the power is unplugged
	mv "$PKG_DIR" /usr/share/updater/packages
	mv "$PLAN_FILE" "$BASE_PLAN_FILE"
	sync

	# Run the plan from the permanent storage
	run_plan "$BASE_PLAN_FILE"
fi

echo 'done' >"$STATE_FILE"
echo 'Updater finished' | logger -t updater -p daemon.info

EXIT_CODE="0"
