#!/bin/sh
cd "$(dirname "$(readlink -f "$0")")"

for file in DockerFile_*; do
	tag="${file#DockerFile_}"
	docker build -t "registry.labs.nic.cz/turris/updater/updater:$tag" - < "$file"
done
