#!/bin/sh
cd "$(dirname "$(readlink -f "$0")")"

for file in DockerFile_*; do
	docker push "registry.labs.nic.cz/turris/updater/updater:${file#DockerFile_}"
done
