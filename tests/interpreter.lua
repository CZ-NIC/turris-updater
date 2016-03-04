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
require 'utils'

-- Some of the interpreter tests are in C, some are easier written in lua
module("interpreter-tests", package.seeall, lunit.testcase)

local tmp_dirs = {}

-- Test work with working directory
function test_dirs()
	local top = os.getenv("S") or "."
	chdir(top)
	chdir("tests")
	local dir = getcwd()
	-- The current directory should be in tests now
	assert_equal("/tests", dir:sub(-6))
end

-- Test some FS utilities
function test_fsutils()
	local dir = mkdtemp()
	table.insert(tmp_dirs, dir)
	-- ls on empty directory
	assert_table_equal({}, ls(dir))
	-- We can create a directory
	mkdir(dir .. "/d1")
	assert_table_equal({["d1"] = "d"}, ls(dir))
	-- Exists and is a directory
	assert_equal("d", stat(dir .. "/d1"))
	-- Doesn't exist
	assert_table_equal({}, {stat(dir .. "/d2")})
	-- Parent directory doesn't exist
	assert_error(function () mkdir(dir .. "/d2/d3") end)
	-- Already exists
	assert_error(function () mkdir(dir .. "/d1") end)
	move(dir .. "/d1", dir .. "/d2")
	assert_table_equal({["d2"] = "d"}, ls(dir))
	-- Create a file
	local f = io.open(dir .. "/d2/x", "w")
	assert(f)
	f:close()
	-- The file exists
	assert_table_equal({["x"] = "r"}, ls(dir .. "/d2"))
	-- A directory on another file system than tmp (likely)
	local ldir = mkdtemp(getcwd())
	table.insert(tmp_dirs, ldir)
	-- Cross-device move
	move(dir .. "/d2/x", ldir .. "/x")
	assert_table_equal({["x"] = "r"}, ls(ldir))
end

function teardown()
	utils.cleanup_dirs(tmp_dirs)
	tmp_dirs = {}
end
