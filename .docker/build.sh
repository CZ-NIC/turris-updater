#!/bin/sh
set -e
cd "$(dirname "$(readlink -f "$0")")"
registry="registry.labs.nic.cz/turris/updater/updater"

img() {
	local file="DockerFile_$1"
	local tag="$2"
	shift 2
	docker build "$@" -t "$registry:$tag" - < "$file"
}

. ./images.sh
