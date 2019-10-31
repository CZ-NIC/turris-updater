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
local C = require 'coverage'
local debug = debug
local io = io

module("sandbox-tests", package.seeall, lunit.testcase)

function test_line()
	-- This expect specific lines so we do some dummy operations
	local a = 1
	local b = 2
	a = b + a
	for i = 1, 3 do
		b = b + 1
	end
	-- Now lets check that they were recorded (we are coverage module)
	local source = debug.getinfo(1, 'S').source -- What we are?
	local cov = C.coverage_data[source]
	assert_equal(1, cov[29])
	assert_equal(1, cov[30])
	assert_equal(1, cov[31])
	assert_equal(4, cov[32])
	assert_equal(3, cov[33])
end

-- Check that when we call dump, file for this module is created
function test_dump()
	local source = debug.getinfo(1, 'S').source -- What we are?
	local fname = source:gsub('/', '-') .. '.lua_lines'
	local dir = os.getenv("COVERAGEDIR")
	-- Call dump
	C.dump(dir)
	-- Check if file exists
	local f = io.open(dir .. "/" .. fname, "r")
	assert(f)
	io.close(f)
end
