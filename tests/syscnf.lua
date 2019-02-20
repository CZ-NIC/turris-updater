--[[
Copyright 2018, CZ.NIC z.s.p.o. (http://www.nic.cz/)

This file is part of the turris updater.

Updater is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Updater is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Updater.  If not, see <http://www.gnu.org/licenses/>.
]]--

require 'lunit'
local sc = require "syscnf"

local sdir = os.getenv("S") or "."

module("syscnf-tests", package.seeall, lunit.testcase)

function test_set_root_dir()
	sc.set_root_dir("/dir/")
	assert_equal("/dir/", sc.root_dir)
	assert_equal("/dir/usr/lib/opkg/status", sc.status_file)
	assert_equal("/dir/usr/lib/opkg/info/", sc.info_dir)
	assert_equal("/dir/usr/share/updater/unpacked/", sc.pkg_unpacked_dir)
	assert_equal("/dir/usr/share/updater/download/", sc.pkg_download_dir)
	assert_equal("/dir/usr/share/updater/collided/", sc.opkg_collided_dir)
end

function test_os_release()
	sc.set_root_dir(sdir .. "/tests/data/sysinfo_root/mox")
	sc.system_detect()
	local osr = sc.os_release()
	assert_equal("TurrisOS", osr.NAME);
	assert_equal("4.0-alpha2", osr.VERSION);
	assert_equal("turrisos", osr.ID);
	assert_equal("TurrisOS 4.0-alpha2", osr.PRETTY_NAME);
	sc.set_root_dir()
end
