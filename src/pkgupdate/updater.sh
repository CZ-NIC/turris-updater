#!/bin/sh

# Copyright (c) 2016-2017, CZ.NIC, z.s.p.o. (http://www.nic.cz/)
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

. /lib/functions.sh

# Posix and Busybox compatible timeout function
# First argument is time and every other is program with it's arguments.
ptimeout() {
	# Check if we have timeout binary and use it if so
	if which timeout >/dev/null; then
		local T="$1"
		shift
		if [ "$(basename "$(readlink -f "$(which timeout)")")" = "busybox" ]; then
			# We are immediately killing as busybox doesn't support delayed kill
			timeout -t "$T" -s 9 "$@"
		else
			timeout -k "$(($T + 5))" "$T" "$@"
		fi
		return $?
	fi
	# Shell workaround
	# Basic idea is to run watcher subshell and wait for given time and kill it
	# unless it exits till then.
	# Because there is a lot of killing and a lot of potentially nonexistent
	# processes we ignore stderr, but we don't want to dump stderr of process it
	# self so we redirect it to different output and then back to stderror.
	/bin/sh -c '
		(
			sleep $1
			NP="$( pgrep -P $$ -n )"
			kill $NP
			sleep 5
			kill -9 $NP
		) &
		shift
		"$@" 2>&3
		EC=$?
		for P in $( pgrep -P $$); do
			kill $( pgrep -P $P )
			kill $P
		done
		exit $EC
	' -- "$@" 3>&2 2>/dev/null
	# Note that Busybox doesn't have sleep as build-in, but as external program.
	# So backgrounded wait can stay running even after script exits if not killed.
	# So we are killing children of children in for loop.
}

create_notify_message() {
	local level="$1"
	local msg_cz="$2"
	local msg_en="$3"
	ptimeout 120 create_notification -s "$level" "$msg_cz" "$msg_en" || {
		echo 'Create notification failed' | logger -t updater -p daemon.error
	}
}

create_notify_error() {
	create_notify_message 'error' "$1" "$2"
}

create_notify_update() {
	create_notify_message 'update' "$1" "$2"
}


config_load updater
config_get_bool DISABLED override disable 0

if [ "$DISABLED" = "1" ] ; then
	echo "Updater disabled" | logger -t daemon.warning
	echo "Updater disabled" >&2
	exit 0
fi

NET_WAIT=10
while [ $NET_WAIT -gt 0 ] && ! ping -c 1 -w 1 repo.turris.cz >/dev/null 2>&1; do
	NET_WAIT=$(($NET_WAIT - 1))
	sleep 1 # Note: we wait in ping too (so we wait for 2 seconds), but in some cases (failed dns resolution) ping exits fast so we have to have this sleep too
done

STATE_DIR=/tmp/update-state
LOCK_DIR="$STATE_DIR/lock"
LOG_FILE="$STATE_DIR/log2"
PID_FILE="$STATE_DIR/pid"
STATE_FILE="$STATE_DIR/state"
ERROR_FILE="$STATE_DIR/last_error"
APPROVALS=''
APPROVAL_ASK_FILE=/usr/share/updater/need_approval
APPROVAL_GRANTED_FILE=/usr/share/updater/approvals
EXIT_CODE=1
BACKGROUND=false
RAND_SLEEP=false
PKGUPDATE_ARGS=""
# This variable is set throught env variables, we must not redefine it here
# Just make it a non-env variable and set the default value if it is not defined
export -n RUNNING_ON_BACKGROUND
RUNNING_ON_BACKGROUND=${RUNNING_ON_BACKGROUND:-false}
# ARGS for myself when running in background mode
BACKGROUND_UPDATER_ARGS=''


for ARG in "$@"; do
	case "$ARG" in
		-h|--help)
			echo "Usage: updater.sh [OPTION]..."
			echo "-e (ERROR|WARNING|INFO|DBG|TRACE)"
			echo "    Message level printed on stderr. In default set to INFO."
			echo "-b|--background"
			echo "    Run updater in background (detach from terminal)"
			echo "--rand-sleep"
			echo "    Sleep random amount of the time with maximum of half an hour before running updater."
			exit 0
			;;
		-b|--background)
			BACKGROUND=true
			;;
		--rand-sleep)
			RAND_SLEEP=true
			# remember this arg in case of BACKGROUND mode
			BACKGROUND_UPDATER_ARGS="$BACKGROUND_UPDATER_ARGS --rand-sleep"
			;;
		-w|-n)
			echo "Argument $ARG ignored as a compatibility measure for old updater." >&2
			;;
		*) # Pass any other argument to pkgupdate
			PKGUPDATE_ARGS="$PKGUPDATE_ARGS $ARG"
			;;
	esac
	shift
done

# Prepare a state directory and lock
updater_lock() {
	mkdir -p "$STATE_DIR"
	if ! mkdir "$LOCK_DIR" 2>/dev/null ; then
		echo "Already running" >&2
		echo "Already running" | logger -t updater -p daemon.warning
		EXIT_CODE=0
		exit
	fi
	rm -f "$ERROR_FILE" "$LOG_FILE"
	echo startup >"$STATE_FILE"
	echo $$ >"$PID_FILE"
}

# Cleanup to remove various files from state directory on updater exit
trap_handler() {
	rm -rf "$LOCK_DIR" "$PID_FILE"
	exit $EXIT_CODE
}
setup_cleanup() {
	trap trap_handler EXIT INT QUIT TERM ABRT
}

# Execution suspend for random amount of time
rand_suspend() {
	# We don't have $RANDOM and base support in arithmetic mode so we use this instead
	local T_RAND="$( printf %d 0x$(head -c 2 /dev/urandom | hexdump -e '"%x"'))"
	T_RAND="$(( $T_RAND % 1800 ))"
	echo "Suspending updater for $T_RAND seconds" >&2
	echo "Suspending updater for $T_RAND seconds" | logger -t updater -p daemon.info
	sleep $T_RAND
}

# Prepare approvals
# This function sets APPROVALS variable. It contains option for pkgupdate to
# enable approvals and latest approved hash.
approvals_prepare() {
	local APPROVED_HASH
	local AUTO_GRANT_TRESHOLD
	local AUTO_GRANT_TIME

	APPROVALS="--ask-approval=$APPROVAL_ASK_FILE"
	if [ -f "$APPROVAL_GRANTED_FILE" ]; then
		# Get a threshold time when we grant approval automatically. In case we don't, we set the time to
		# 1, which is long long time ago in the glorious times when automatic updaters were not
		# needed.
		config_get AUTO_GRANT_TIME approvals auto_grant_seconds
		AUTO_GRANT_TRESHOLD=$(expr $(date -u +%s) - $AUTO_GRANT_TIME 2>/dev/null || echo 1)

		# only the last line is relevant
		APPROVED_HASH="$( awk -v treshold="$AUTO_GRANT_TRESHOLD" 'END {
						if ( $2 == "granted" || ( $2 == "asked" && $3 <= treshold ) ) {
							print $1
						}
					}' "$APPROVAL_GRANTED_FILE" )"
		[ -n "$APPROVED_HASH" ] && APPROVALS="$APPROVALS --approve=$APPROVED_HASH"
	fi
}

# Handle new generated approvals request
approvals_request() {
	local HASH
	local NOTIFY_APPROVAL
	local LIST

	read HASH <"$APPROVAL_ASK_FILE"
	if ! grep -q "^$HASH" "$APPROVAL_GRANTED_FILE" ; then
		echo "$HASH asked $(date -u +%s)" >"$APPROVAL_GRANTED_FILE"
		config_get_bool NOTIFY_APPROVAL approvals notify 1
		echo "Asking for authorisation $HASH" | logger -t updater -p daemon.info
		if [ "$NOTIFY_APPROVAL" = "1" ] ; then
			# Formating input from "OPERATION VERSION PACKAGE REBOOT" to "OPERATION PACKAGE VERSION"
			# Also OPERATION is lowercase to make it pretty uppercase the first character.
			# TODO do we want to show reboot?
			LIST="$(awk 'NR>1{printf "\n • %s %s %s", toupper(substr($1,1,1))substr($1,2), $3, $2}' "$APPROVAL_ASK_FILE")"

			create_notify_update \
				"Updater žádá o autorizaci akcí. Autorizaci můžete přidělit v administračním rozhraní Foris.$LIST" \
				"The updater requests an autorisation of its planned actions. You can grant it in the Foris administrative interface.$LIST"
		fi
	fi
}

# Do post-update actions for approvals
approvals_finish() {
	if [ -f "$APPROVAL_ASK_FILE" ] ; then
		approvals_request
	else
		if [ "$EXIT_CODE" -eq 0 ] ; then
			# When we run successfully and didn't need any further approval, we
			# used up all the current approvals by that (if we ever want to do the
			# same thing again, we need to ask again).
			rm -f "$APPROVAL_GRANTED_FILE"
		fi
	fi
}

# Create notifications
notify_user() {
	local ERROR

	if [ -s "$LOG_FILE" ] && grep -q '^[IR]' "$LOG_FILE" ; then
		create_notify_update \
			"$(sed -ne 's/^I \(.*\) \(.*\)/ • Nainstalovaná verze \2 balíku \1/p;   s/^R \(.*\)/ • Odstraněn balík \1/p' "$LOG_FILE")" \
			"$(sed -ne 's/^I \(.*\) \(.*\)/ • Installed version \2 of package \1/p; s/^R \(.*\)/ • Removed package \1/p' "$LOG_FILE")"
	fi
	if [ "$STATE" != "done" ] ; then
		if [ -s "$ERROR_FILE" ] ; then
			ERROR=$(cat "$ERROR_FILE")
		else
			ERROR="Unknown error"
		fi

		create_notify_error \
			"Updater selhal: $ERROR" \
			"Updater failed: $ERROR"
	fi
	ptimeout 120 notifier || echo "Notifier failed" | logger -t updater -p daemon.error
}

# Function handling everything about pkgupdate execution
run_updater() {
	local NEED_APPROVAL

	config_get_bool NEED_APPROVAL approvals need 0
	if [ "$NEED_APPROVAL" = "1" ] ; then
		approvals_prepare
	else
		# If approvals aren't enabled then run always in batch mode (don't ask user)
		PKGUPDATE_ARGS="$PKGUPDATE_ARGS --batch"
	fi

	# Run the actual updater
	ptimeout 3000 pkgupdate $PKGUPDATE_ARGS --state-log --task-log=/usr/share/updater/updater-log $APPROVALS
	EXIT_CODE="$?"

	# Evaluate what has run
	STATE=$(cat "$STATE_FILE")
	if [ "$STATE" != "error" -a \( "$EXIT_CODE" -ne "0" -o "$STATE" != "done" \) ]; then
		echo lost >"$STATE_FILE"
	fi

	approvals_finish
	notify_user
}

run_immediate() {
	# updater_lock have to be called before calling this function
	setup_cleanup
	run_updater
}

run_delayed() {
	rand_suspend
	updater_lock
	run_immediate
}

# Foris is running updater.sh on the background throw the NUCI interface
# and there is a bug in it that it does not handle shell functions running on
# background well
# So we need to run whole script again! And do not forget to pass the args
run_backgrounded() {
	# we will set RUNNING_ON_BACKGROUND env variable for the child
	# and of course in batch mode
	RUNNING_ON_BACKGROUND='true' "$0" $BACKGROUND_UPDATER_ARGS $PKGUPDATE_ARGS --batch < /dev/null > /dev/null 2>&1 &

	# Make sure the PID is of the process actually doing the work
	echo $! > "$PID_FILE"
	exit 0
}


# main -------------------------------------------------------------------------
# There are several states of this script which needs to be handled.
# These states are described with following variables:
#   - RAND_SLEEP: to run with random delay (intended for cron jobs)
#   - BACKGROUND: to drop run updater on background
#   - RUNNING_ON_BACKGROUND: already running on background
#
# Not all combinations does make sense (i.e. BACKGROUND && RUNNING_ON_BACKGROUND)
# so following logic handles these states

# 1) Lock updater as soon as possible
#    Do not lock it, if running delayed or if this proces run on background
#    (locked already)
if [ "$RAND_SLEEP" != 'true' -a "$RUNNING_ON_BACKGROUND" != 'true' ]; then
	updater_lock
fi

# 2) Running on background
#    Drop to background, if needed, but not when already on background
if [ "$BACKGROUND" = 'true' -a "$RUNNING_ON_BACKGROUND" != 'true' ]; then
	run_backgrounded
fi

# 3) Finally run the updater
#    Delayed or immediately
if $RAND_SLEEP; then
	run_delayed
else
	run_immediate
fi
