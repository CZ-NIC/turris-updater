--[[
Copyright 2019, CZ.NIC z.s.p.o. (http://www.nic.cz/)

This file is part of the Turris Updater.

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

require "lunit"
local uri = require "uri"
local utils = require "utils"
local os = os

local dir = (os.getenv("S") .. "/") or ''
local tmpdir = os.getenv("TMPDIR") or "/tmp"

module("uri-tests", package.seeall, lunit.testcase)

-- Test master on its own
function test_master()
	local master = uri:new()
	master:download() -- it whould pass without any uri
end

function test_to_buffer()
	local master = uri.new()
	local u = master:to_buffer("data:,Hello!")
	local dt = u:finish()
	assert_equal("Hello!", dt)
	assert_nil(u:output_path())
end

function test_to_file()
	local fpath = tmpdir .. "/updater-uri-lua-test"
	local master = uri.new()
	local u = master:to_file("data:,Hello!", fpath)
	assert_nil(u:finish())
	assert_equal(fpath, u:output_path())
	assert_equal("Hello!", utils.read_file(fpath))
	os.remove(u:output_path())
end

function test_to_temp_file()
	local template = tmpdir .. "/updater-uri-lua-XXXXXX"
	local master = uri.new()
	local u = master:to_temp_file("data:,Hello!", template)
	assert_nil(u:finish())
	assert_not_equal(template, u:output_path())
	assert_equal("Hello!", utils.read_file(u:output_path()))
	os.remove(u:output_path())
end

function test_is_local()
	local master = uri.new()
	local function check(struri, should_be_local)
		local u = master:to_buffer(struri)
		assert_equal(should_be_local, u:is_local())
	end
	check("data:,test", true)
	check("https://www.example.com/", false)
end

function test_path()
	local master = uri.new()
	local u = master:to_buffer("file:///dev/null")
	assert_equal("/dev/null", u:path())
end
