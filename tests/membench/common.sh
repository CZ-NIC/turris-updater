# Copyright 2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)
#
# This file is part of the Turris Updater.
#
# Updater is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
# Updater is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Updater.  If not, see <http://www.gnu.org/licenses/>.
set -e

[ -n "$BENCH" ] || {
	echo "BENCH has to be defined before common.sh is included!" >&2
	exit 1
}

# Path to updater repository root
sroot="$(readlink -f "$src/../..")"

_exit_cleanup() {
	if [ -n "$root_dir" ]; then
		if [ -n "$PRESERVE_ROOT" ]; then
			echo "Preserving root: $root_dir" >&2
		else
			rm -rf "$root_dir"
		fi
	fi
}
trap _exit_cleanup EXIT TERM INT QUIT ABRT HUP

_run_updater() {
	local bin="$1"
	shift
	[ -n "$root_dir" ] || {
		echo "Error: you have to use prepare_root before updater!" >&2
		exit 1
	}
	make -C "$sroot" "bin/$bin"
	valgrind --tool=massif --massif-out-file="massif.out.$BENCH.%p" \
		"$sroot/bin/$bin" -R "$root_dir" "$@"
}

pkgupdate() {
	_run_updater pkgupdate "$@"
}

pkgtransaction() {
	_run_updater pkgtransaction "$@"
}

# Creates new root for updater
prepare_root() {
	root_dir="$(mktemp -d)"

	## Create base filesystem for updater
	ln -sf tmp "$root_dir/var"
	# Create lock required by updater
	mkdir -p "$root_dir/tmp/lock"
	# Create opkg status file and info file
	mkdir -p "$root_dir/usr/lib/opkg/info"
	touch "$root_dir/usr/lib/opkg/status"
	# And updater directory
	mkdir -p "$root_dir/usr/share/updater"
}

# Following functions can be used to create new packages and repository

# This copies new package template for package PKGNAME in directory PKGNAME
# Usage: package_template PKGNAME [VERSION}]
package_template() {
	local pkgname="$1"
	local pkgversion="${2:-1.0}"
	cp -r "$sroot/utils/opkg-create/template" "$pkgname"
	sed -i "s#^Package:.*\$#Package: $pkgname#;s#^Version:.*\$#Version: $pkgversion#" "$pkgname/control/control"
	rm "$pkgname/data/file"
}

# Generate ipk from package template
# Usage: package_pack PKGNAME
package_pack() {
	local package="$1"
	"$sroot/utils/opkg-create/opkg-create-package" "$package"
	[ -z "$package" ] || rm -rf "$package"
}

# Generate repository index for all packages in current directory
repo_gen() {
	"$sroot/utils/opkg-create/opkg-create-repo"
	echo "== Repository sizes =="
	du -h *
	echo "======================"
}
