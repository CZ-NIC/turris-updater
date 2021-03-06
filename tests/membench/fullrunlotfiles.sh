#!/bin/bash
# Copyright 2020, CZ.NIC z.s.p.o. (http://www.nic.cz/)
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
BENCH=fullrunlotfiles
src="$(dirname "$0")"
. "$src/common.sh"

prepare_root
pushd "$root_dir"

mkdir repo
pushd repo
for i in $(seq 50); do
	package_template "test_$i"
	for y in $(seq $i); do
		openssl rand -out "test_$i/data/file.$i.$y" 1000000
	done
	package_pack "test_$i"
done
repo_gen
popd

cat >script.lua <<EOF
Repository("repo", "file://$(pwd)/repo")
for i = 1,50,1 do
	Install("test_" .. tostring(i))
end
EOF

popd

pkgupdate --batch "file://$root_dir/script.lua"
