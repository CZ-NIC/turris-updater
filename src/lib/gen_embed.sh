#!/bin/sh

suffix="$1"
shift
iname="$1"
shift
sed -e 's/^/|/' "$1"
shift

while [ "$1" ]; do
	name="$(basename -s "$suffix" "$1" | tr -- '.-' '__')"
	echo "$name" "$1"
	shift
done
echo "idx $iname file_index_element"
