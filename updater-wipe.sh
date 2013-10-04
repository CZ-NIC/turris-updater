#!/bin/sh

# If run, it checks if updater.sh left the lock and disappeared. If so, removes the lock.

set -ex

if [ -d /tmp/update-state/lock ] ; then
	PID=$(cat /tmp/update-state/pid)
	if ! ps | grep updater.sh | grep -q "$PID" ; then
		rm -r /tmp/update-state/lock /tmp/update-state/pid
		echo 'lost' >/tmp/update-state/state
	fi
fi
