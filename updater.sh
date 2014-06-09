#!/bin/busybox sh

# Copyright (c) 2013-2014, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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

ping -c1 -w10 api.turris.cz || true # Start up resolution inside turris.cz. It seems unbound sometimes takes a long time, caching part of the path may help.

# Load the libraries
LIB_DIR="$(dirname "$0")"
. "$LIB_DIR/updater-worker.sh"

# My own ID
ID="$(atsha204cmd serial-number || guess_id)"
# We take the hardware revision as "distribution"
REVISION="$(atsha204cmd hw-rev || guess_revision)"
# Where the things live
BASE_URL="https://api.turris.cz/updater-repo/$REVISION"
LIST_REQ="https://api.turris.cz/getlists.cgi"
GENERIC_LIST_URL="$BASE_URL/lists/generic"
SPECIFIC_LIST_URL="$BASE_URL/lists/$ID"
PACKAGE_URL="$BASE_URL/packages"

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
	echo "$2" | my_logger -p daemon.info
	shift 2
else
	"$LIB_DIR"/updater-wipe.sh # Remove forgotten stuff, if any

	# Create the state directory, set state, etc.
	mkdir -p "$STATE_DIR"
	if ! mkdir "$LOCK_DIR" ; then
		echo "Already running" >&2
		echo "Already running" | my_logger -p daemon.warning
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
get_list_pack base core $(uci get updater.pkglists.lists)
get_list base list

HAVE_WORK=false
echo 'examine' >"$STATE_FILE"
echo 'PKG_DIR=/usr/share/updater/packages' >"$PLAN_FILE"
prepare_plan list

if $HAVE_WORK ; then
	# Make sure the whole plan can go through
	if ! size_check "$PKG_DIR"/* ; then
		die "Not enough space to install whole base plan"
	fi

	# Overwrite the restart function
	do_restart() {
		echo 'Update restart requested, complying' | my_logger -p daemon.info
		exec "$0" -r "Restarted" -n "$@"
	}

	# Back up the packages to permanent storage, so we can resume on next restart if the power is unplugged
	rm -rf /usr/share/updater/packages # Remove leftovers
	mv "$PKG_DIR" /usr/share/updater/packages
	mv "$PLAN_FILE" "$BASE_PLAN_FILE"
	sync

	# Run the plan from the permanent storage
	run_plan "$BASE_PLAN_FILE"
fi

execute_list() {
	echo 'get list' >"$STATE_FILE"
	get_list "$1" "user_lists/$1"
	USER_LIST_FILES="$USER_LIST_FILES $TMP_DIR/user_lists/$1"
	echo 'examine' >"$STATE_FILE"
	rm -f "$PLAN_FILE"
	touch "$PLAN_FILE"
	prepare_plan "user_lists/$1"
	run_plan "$PLAN_FILE"
}

# The rest of the base packages that are not considered critical.
mkdir -p "$TMP_DIR/user_lists"
USER_LIST_FILES=""
execute_list "core"

PROGRAM='updater-user'
for USER_LIST in $(uci get updater.pkglists.lists) ; do
	execute_list "$USER_LIST"
done
PROGRAM='updater'

my_opkg --conf /dev/null configure || die "Configure of stray packages failed"

pwd

# Run the consolidator, but only in case it is installed - it is possible for it to not exist on the device
if [ -x "$LIB_DIR/updater-consolidate.py" ] ; then
	"$LIB_DIR/updater-consolidate.py" "$TMP_DIR/list" $USER_LIST_FILES || die "Consolidator failed" # Really don't quote this variable, it should be split into parameters
else
	echo 'Missing consolidator' | my_logger -p daemon.warn
fi

# Try running notifier. We don't fail if it does, for one it is not
# critical for updater, for another, it may be not available.
PROGRAM='notifier'

if [ -s "$LOG_FILE" ] ; then
	timeout 120 create_notification -s update "$(sed -e 's/^I \(.*\) \(.*\)/ • Nainstalovaná verze \2 balíku \1/;s/^R \(.*\)/ • Odstraněn balík \1/' "$LOG_FILE")"
fi
timeout 120 notifier || echo 'Notifier failed' | my_logger -p daemon.error

echo 'done' >"$STATE_FILE"
echo 'Updater finished' | my_logger -p daemon.info

EXIT_CODE="0"
