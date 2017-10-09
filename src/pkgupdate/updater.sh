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

config_load updater
config_get_bool DISABLED override disable 0

if [ "$DISABLED" = "1" ] ; then
	echo "Updater disabled" | logger -t daemon.warning
	echo "Updater disabled" >&2
	exit 0
fi

NET_WAIT=10
while [ $NET_WAIT -gt 0 ] && ! ping -c 1 -w 1 api.turris.cz >/dev/null 2>&1; do
	NET_WAIT=$(($NET_WAIT - 1))
	sleep 1 # Note: we wait in ping too (so we wait for 2 seconds), but in some cases (failed dns resolution) ping exits fast so we have to have this sleep too
done

get-api-crl || {
	ptimeout 120 create_notification -s error "Updater selhal: Chybí CRL, pravděpodobně je problém v připojení k internetu." "Updater failed: Missing CRL, possibly broken Internet connection." || \
		echo "Create notification failed" | logger -t updater -p daemon.error
	exit 1
}

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
	APPROVALS="--ask-approval=$APPROVAL_ASK_FILE"
	if [ -f "$APPROVAL_GRANTED_FILE" ]; then
		# Get a threshold time when we grant approval automatically. In case we don't, we set the time to
		# 1, which is long long time ago in the glorious times when automatic updaters were not
		# needed.
		config_get AUTO_GRANT_TIME approvals auto_grant_seconds
		AUTO_GRANT_TRESHOLD=$(expr $(date -u +%s) - $AUTO_GRANT_TIME 2>/dev/null || echo 1)
		APPROVED_HASH="$(tail -1 "$APPROVAL_GRANTED_FILE" | awk '$2 == "granted" || ( $2 == "asked" && $3 <= "'"$AUTO_GRANT_TRESHOLD"'" ) {print $1}')"
		[ -n "$APPROVED_HASH" ] && APPROVALS="$APPROVALS --approve=$APPROVED_HASH"
	fi
}

# Handle new generated approvals request
approvals_request() {
	local HASH
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
			ptimeout 120 create_notification -s update \
				"Updater žádá o autorizaci akcí. Autorizaci můžete přidělit v administračním rozhraní Foris.$LIST" \
				"The updater requests an autorisation of its planned actions. You can grant it in the Foris administrative interface.$LIST" \
				|| echo "Create notification failed" | logger -t updater -p daemon.error
		fi
	fi
}

# Do post-update actions for approvals
approvals_finish() {
	if [ -f "$APPROVAL_ASK_FILE" ] ; then
		approvals_request
	else
		if [ "$EXIT_CODE" = 0 ] ; then
			# When we run successfully and didn't need any further approval, we
			# used up all the current approvals by that (if we ever want to do the
			# same thing again, we need to ask again).
			rm -f "$APPROVAL_GRANTED_FILE"
		fi
	fi
}

# Create notifications
notify_user() {
	if [ -s "$LOG_FILE" ] && grep -q '^[IR]' "$LOG_FILE" ; then
		ptimeout 120 create_notification -s update \
			"$(sed -ne 's/^I \(.*\) \(.*\)/ • Nainstalovaná verze \2 balíku \1/p;s/^R \(.*\)/ • Odstraněn balík \1/p' "$LOG_FILE")" \
			"$(sed -ne 's/^I \(.*\) \(.*\)/ • Installed version \2 of package \1/p;s/^R \(.*\)/ • Removed package \1/p' "$LOG_FILE")" \
			|| echo "Create notification failed" | logger -t updater -p daemon.error
	fi
	if [ "$STATE" != "done" ] ; then
		if [ -s "$ERROR_FILE" ] ; then
			ERROR=$(cat "$ERROR_FILE")
		else
			ERROR="Unknown error"
		fi
		ptimeout 120 create_notification -s error "Updater selhal: $ERROR" "Updater failed: $ERROR" || echo "Create notification failed" | logger -t updater -p daemon.error
	fi
	ptimeout 120 notifier || echo "Notifier failed" | logger -t updater -p daemon.error
}

# Function handling everything about pkgupdate execution
run_updater() {
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
	if [ "$STATE" != "error" ] && ([ "$EXIT_CODE" != "0" ] || [ "$STATE" != "done" ]); then
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

if $BACKGROUND; then
	# When we are backgrounded we can't ask user so force --batch
	PKGUPDATE_ARGS="$PKGUPDATE_ARGS --batch"
	if $RAND_SLEEP; then
		run_delayed >/dev/null 2>&1 </dev/null &
	else
		updater_lock
		run_immediate >/dev/null 2>&1 </dev/null &
	fi
	echo $!>"$PID_FILE" # Make sure the PID is of the process actually doing the work
else
	if $RAND_SLEEP; then
		run_delayed
	else
		updater_lock
		run_immediate
	fi
fi
