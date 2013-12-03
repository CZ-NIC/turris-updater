#!/bin/sh

set -ex

cd "$HOME"/turris-packages

# TODO: Check the git is signed by known pgp and there is a signed-off-by
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
