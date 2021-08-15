#!/bin/sh
set -ex
cd "$(dirname "$(readlink -f "$0")")"
registry="registry.nic.cz/turris/updater/updater"

img() {
	docker push "$registry:$2"
}

. ./images.sh
