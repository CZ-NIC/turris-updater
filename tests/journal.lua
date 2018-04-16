--[[
Copyright 2016-2017, CZ.NIC z.s.p.o. (http://www.nic.cz/)

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
local J = require 'journal'
local backend = require 'backend'

module("journal-tests", package.seeall, lunit.testcase)

-- This is directory where journal directory should be created
journal_path =  "/usr/share/updater"

local tmp_dirs = {}

-- Test the module is injected correctly
function test_module()
	assert_equal(journal, J)
end

-- Test values of some variables and "constants" ‒ make sure they are there
function test_values()
	local types = {"START", "FINISH", "UNPACKED", "CHECKED", "MOVED", "SCRIPTS", "CLEANED"}
	for i, t in ipairs(types) do
		assert_number(J[t])
		for j, t2 in ipairs(types) do
			if i < j then
				assert(J[t] < J[t2])
			end
		end
	end
end

-- Initialize a temporary directory to use by the journal and point the journal file inside
function dir_init()
	local dir = mkdtemp()
	table.insert(tmp_dirs, dir)
	backend.root_dir = dir
	mkpath = dir
	for dr in journal_path:gmatch('[^/]+') do
		mkpath = mkpath .. '/' .. dr
		mkdir(mkpath)
	end
	return dir .. journal_path
end

-- Check creating a fresh journal
function test_fresh()
	local dir = dir_init()
	assert_table_equal({}, ls(dir))
	-- Create a fresh journal file
	assert_false(J.opened())
	J.fresh()
	assert(J.opened())
	assert_table_equal({journal = "r"}, ls(dir));
	-- The journal disappears once we are finished with it.
	J.finish()
	assert_false(J.opened())
	assert_table_equal({}, ls(dir))
	-- Create a fake journal file
	io.open(dir .. '/journal', "w"):close()
	assert_table_equal({journal = "r"}, ls(dir));
	-- Can't open fresh journal, if there's one already
	assert_error(function () J.fresh() end)
	assert_false(J.opened())
end

-- Check we can read a journal (only the start and finish is there)
function test_recover_empty()
	local dir = dir_init()
	assert_table_equal({}, ls(dir))
	-- Nothing to recover
	assert_nil(J.recover())
	J.fresh()
	-- Keep the journal file
	J.finish(true)
	assert_false(J.opened())
	assert_table_equal({journal = "r"}, ls(dir));
	assert_table_equal({
		{ type = J.START, params = {} },
		{ type = J.FINISH, params = {} }
	}, J.recover())
	assert_table_equal({journal = "r"}, ls(dir));
	assert(J.opened())
	J.finish()
	assert_table_equal({}, ls(dir))
	assert_false(J.opened())
end

-- Same as test_recover_empty, but with more data and parameters
function test_recover_data()
	local dir = dir_init()
	assert_table_equal({}, ls(dir))
	J.fresh()
	J.write(J.UNPACKED, { data = { more_data = "hello" } }, { "x", "y", "z" })
	J.finish(true)
	assert_table_equal({
		{ type = J.START, params = {} },
		{ type = J.UNPACKED, params = { { data = { more_data = "hello" } }, { "x", "y", "z" } } },
		{ type = J.FINISH, params = {} }
	}, J.recover())
end

-- The journal is incomplete, test it can read the complete part
function test_recover_broken()
	dir = dir_init()
	J.fresh()
	J.write(J.UNPACKED, { data = "xyz" }, { "x", "y", "z" })
	J.finish(true)
	-- Now damage the file a little bit
	local content = utils.read_file(dir .. '/journal')
	local f, err = io.open(dir .. '/journal', "w")
	assert(f, err)
	-- Store everything except for the last 3 bytes. That should kill the last FINISH record
	f:write(content:sub(1, -3))
	f:close()
	assert_table_equal({
		{ type = J.START, params = {} },
		{ type = J.UNPACKED, params = { { data = "xyz" }, { "x", "y", "z" } } }
	}, J.recover())
	-- We write something in addition
	J.write(J.CHECKED, "more data")
	J.finish(true)
	-- Read it once more and check there are proper data stored
	assert_table_equal({
		{ type = J.START, params = {} },
		{ type = J.UNPACKED, params = { { data = "xyz" }, { "x", "y", "z" } } },
		{ type = J.CHECKED, params = { "more data" } },
		{ type = J.FINISH, params = {} }
	}, J.recover())
end

function teardown()
	if J.opened() then
		J.finish()
	end
	utils.cleanup_dirs(tmp_dirs)
	tmp_dirs = {}
end
