#!/bin/sh
# Expected to be executed in project root and prints out markdown.
# It expects that all documentation is already compiled.

echo "Documentation index"
echo "==================="
echo

for f in docs/*.html; do
	ff="$(basename "$f")"
	ff="${ff%.html}"
	echo "* [$ff]($f)"
done
