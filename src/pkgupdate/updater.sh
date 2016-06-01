#!/bin/sh

get-api-crl

mkdir -p /tmp/update-state
pkgupdate file:///etc/updater/entry.lua --batch
# This manipulation is not safe against concurrent runs of updater.sh. But the damage in such case is small (just a wrong status visible in nuci for a while) and it should happen only rarely, so we ignore it.
STATE=$(cat /tmp/update-state/state)
if [ "$STATE" != "done" ] && [ "$STATE" != "error" ] ; then
	echo lost >/tmp/update-state/state
fi
