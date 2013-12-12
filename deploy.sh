#!/bin/sh

set -e

# First, check this commit is signed. That means we are allowed to proceed with deployment.
if [ "$(git log -n1 --pretty='%G?' updater/deploy.sh updater/deploy)" != 'G' ] ; then
	echo 'The last commit on misc is not signed by trusted key, not continuing'
	exit 1
fi

if [ "$(git log -n1 updater/deploy.sh updater/deploy | grep 'Signed-off-by' | wc -l)" -lt 2 ] ; then
	echo 'The last commit is not signed off by at least two people, not continuing'
	exit 1
fi

cd "$HOME"/turris-packages

while read source target hash ; do
	current_hash=$(cat $source/git-hash)
	if [ "$hash" != "$current_hash" ] ; then
		echo "The hash on $source/$target does not match ‒ $hash vs. $current_hash"
		exit 1
	fi
	rm -rf "$target"
	cp -pr "$source" "$target"
	rm "$target"/git-hash
	scp -r "$target" "api.turris.cz:$target-upload"
	ssh api.turris.cz "chmod a+rX '$target-upload' -R && mv 'openwrt-repo/$target' '$target-rm' && mv '$target-upload' 'openwrt-repo/$target' && rm -rf '$target-rm'"
done
