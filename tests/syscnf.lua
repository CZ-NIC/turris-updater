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
local SC = require "syscnf"

module("syscnf-tests", package.seeall, lunit.testcase)

function test_set_root_dir()
	SC.set_root_dir("/dir/")
	assert_equal("/dir/usr/lib/opkg/status", SC.status_file)
	assert_equal("/dir/usr/lib/opkg/info/", SC.info_dir)
	assert_equal("/dir/usr/share/updater/unpacked/", SC.pkg_temp_dir)
	assert_equal("/dir/usr/share/updater/collided/", SC.dir_opkg_collided)
end

function test_set_target()
	SC.set_target("Turris", "unknown")
	assert_equal("Turris", SC.target_model)
	assert_equal("unknown", SC.target_board)
end
