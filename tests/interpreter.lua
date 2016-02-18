--[[
Copyright 2016, CZ.NIC z.s.p.o. (http://www.nic.cz/)

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

-- Some of the interpreter tests are in C, some are easier written in lua
module("interpreter-tests", package.seeall, lunit.testcase)

function test_dirs()
	local top = os.getenv("S") or "."
	chdir(top)
	chdir("tests")
	local dir = getcwd()
	-- The current directory should be in tests now
	assert_equal("/tests", dir:sub(-6))
end
