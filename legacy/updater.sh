#!/bin/busybox sh

# Copyright (c) 2013-2015, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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
# -w: Wait for network at least this amount of seconds (default 3 minutes)
# -b: Fork to background.
# -r <Reason>: Restarted. Internal switch, not to be used by other applications.
# -n: Now. Don't wait a random amount of time before doing something.

set -xe

if [ "$1" = "-w" ] ; then
	WAIT_FOR_ONLINE="$2"
	shift 2
else
	WAIT_FOR_ONLINE=180
fi

# Load the libraries
LIB_DIR="$(dirname "$0")"
. "$LIB_DIR/updater-worker.sh"

# My own ID
ID="$(uci -q get updater.override.branch || atsha204cmd serial-number || guess_id)"
# We take the hardware revision as "distribution"
REVISION="$(uci -q get updater.override.revision || atsha204cmd hw-rev || guess_revision)"
# Where the things live
GENERATION=$(uci -q get updater.override.generation || sed -e 's/\..*/\//' /etc/turris-version || echo 0/)
BASE_URL=$(uci -q get updater.override.base_url || echo "https://api.turris.cz/updater-repo/")
HASH_URL=$(uci -q get updater.override.hash_url || echo "https://api.turris.cz/hashes/")
BASE_URL="$BASE_URL$GENERATION$REVISION"
LIST_REQ=$(uci -q get updater.override.list_req_url || echo "https://api.turris.cz/getlists.cgi")
DISABLED=$(uci -q get updater.override.disable || echo false)
if $DISABLED ; then
	echo "Updater disabled" | my_logger -p daemon.warning
	exit 0
fi
GENERIC_LIST_URL="$BASE_URL/lists/generic"
SPECIFIC_LIST_URL="$BASE_URL/lists/$ID"
PACKAGE_URL="$BASE_URL/packages"

PID_FILE="$STATE_DIR/pid"
BACKGROUND=false
EXIT_CODE="1"

BASE_PLAN_FILE='/usr/share/updater/plan'

if [ -e '/tmp/offline-update-ready' ] ; then
	echo "Offline update pending, not doing anything else now" | my_logger -p daemon.warning
	exit
fi

if [ "$1" = "-b" ] ; then
	BACKGROUND=true
	shift
fi

# Try to wait for network
PING_TEST_HOST="`echo "$BASE_URL" | sed 's|^https*://\([^/]*\)/.*|\1|'`"
for i in `seq 1 $WAIT_FOR_ONLINE`; do
	if ping -c 1 -w 1 $PING_TEST_HOST > /dev/null 2> /dev/null; then
		break
	else
		sleep 1
	fi
done

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

STABLE_PACKAGES="/usr/share/updater/packages"
STABLE_PLAN="/usr/share/updater/plan"
trap 'rm -rf "$TMP_DIR" "$PID_FILE" "$LOCK_DIR" $STABLE_PACKAGES $STABLE_PLAN; exit "$EXIT_CODE"' EXIT INT QUIT TERM ABRT

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

# Make sure we have a key - now we have network and are sure that we will run
get-api-crl

# Update opkg repositories. Not needed by updater itself, just a convenience for the user.
LAST_UPDATE="$(cat /tmp/opkg-update-timestamp 2> /dev/null || true)"
DATE_NOW="$(date +%s)"
if [ -z "$LAST_UPDATE" ] || [ $(expr $DATE_NOW - $LAST_UPDATE) -gt 86400 ]; then
	opkg update || true
	date +%s > /tmp/opkg-update-timestamp
fi

do_journal

echo 'get list' >"$STATE_FILE"
get_list_pack base core $(uci get updater.pkglists.lists) definitions
get_list base list

HAVE_WORK=false
NEED_OFFLINE_UPDATES=false
echo 'examine' >"$STATE_FILE"
echo 'PKG_DIR=/usr/share/updater/packages' >"$PLAN_FILE"
prepare_plan list

if $HAVE_WORK ; then
	# Make sure the whole plan can go through
	if ! size_check "$PKG_DIR"/* ; then
		die "Not enough space to install whole base plan"
	fi

	# Back up the packages to permanent storage, so we can resume on next restart if the power is unplugged
	rm -rf /usr/share/updater/packages # Remove leftovers
	mv "$PKG_DIR" /usr/share/updater/packages
	mv "$PLAN_FILE" "$BASE_PLAN_FILE"
	sync

	if $NEED_OFFLINE_UPDATES ; then
		# Mark the need for offline updates
		# Schedule the reboot and notify user
		echo done >"$STATE_FILE"
		# Leave the rest be
		STABLE_PACKAGES=
		STABLE_PLAN=
		EXIT_CODE="0"
		touch '/tmp/offline-update-ready'
		timeout 120 create_notification -s restart "Updaty, které nelze nainstalovat za běhu, jsou připraveny pro instalaci při restartu. Tento restart pravděpodobně bude trvat delší dobu." "Some of the downloaded updates need a restart to apply. This restart may take longer than usual." || echo "Create notification failed" | my_logger -p daemon.error
		timeout 120 notifier || echo 'Notifier failed' | my_logger -p daemon.error
		exit
	fi

	# Run the plan from the permanent storage
	run_plan "$BASE_PLAN_FILE"
fi

if $FAILSAFE_MODE ; then
	die "Aborting further update work due to atsha204 failsafe mode"
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

# Run the consolidator, but only in case it is installed - it is possible for it to not exist on the device
if [ -x "$LIB_DIR/updater-consolidate.py" ] ; then
	"$LIB_DIR/updater-consolidate.py" "$TMP_DIR/list" $USER_LIST_FILES || die "Consolidator failed" # Really don't quote this variable, it should be split into parameters
else
	echo 'Missing consolidator' | my_logger -p daemon.warn
fi

# If there's note we would like to check the hashes, do so. But only if the hash checker is installed.
if [ -f /tmp/updater-check-hashes -a "$HASH_URL" != "-" ] ; then
	if [ -x "$LIB_DIR/check-hashes.py" ] ; then
		echo "Running a hash check" | my_logger -p daemon.info
		my_curl "$HASH_URL"c."$REVISION.json.bz2" | bzip2 -dc >"$TMP_DIR/hashes.json" || ( echo "Failed to download hash list" | my_logger -p daemon.error ; false )
		rm -f "$TMP_DIR/hash.reinstall"
		"$LIB_DIR/check-hashes.py" || ( echo "Failed to run the hash checker" | my_logger -p daemon.error; false )
		touch "$TMP_DIR/hash.reinstall"
		. "$TMP_DIR/hash.reinstall"
		rm /tmp/updater-check-hashes
	else
		echo "Hash checker not installed" | my_logger -p daemon.info
		rm /tmp/updater-check-hashes
	fi
fi

# Try running notifier. We don't fail if it does, for one it is not
# critical for updater, for another, it may be not available.
PROGRAM='notifier'
gen_notifies

PROGRAM='updater'

get_list definitions definitions
if ! cmp -s "$TMP_DIR/definitions" /usr/share/updater/definitions ; then
	echo 'Updating user list definitions' | my_logger -p daemon.info
	cp "$TMP_DIR/definitions" /usr/share/updater/definitions
fi

echo 'done' >"$STATE_FILE"
echo 'Updater finished' | my_logger -p daemon.info

EXIT_CODE="0"
