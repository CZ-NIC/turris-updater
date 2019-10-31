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

local dir = os.getenv("S") or "."
local tmpdir = os.getenv("TMPDIR") or "/tmp"

local lorem_ipsum = "lorem ipsum\n"
local https_lorem_ipsum = "https://applications-test.turris.cz/li.txt"
local ca_lets_encrypts = "file://" .. dir .. "/tests/data/lets_encrypt_roots.pem"
local ca_opentrust_g1 = "file://" .. dir .. "/tests/data/opentrust_ca_g1.pem"

module("uri-tests", package.seeall, lunit.testcase)

-- Test master on its own
function test_master()
	local master = uri:new()
	master:download() -- it whould pass without any uri
end

function test_uri()
	local master = uri.new()
	local u = master:to_buffer("file:///dev/null")
	assert_equal("file:///dev/null", u:uri())
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

function test_https()
	local master = uri.new()
	local u = master:to_buffer(https_lorem_ipsum)
	master:download()
	local dt = u:finish()
	assert_equal(lorem_ipsum, dt)
end

function test_cert_pinning_correct()
	local master = uri.new()
	local u = master:to_buffer(https_lorem_ipsum)
	u:add_ca(ca_lets_encrypts)
	master:download()
	local dt = u:finish()
	assert_equal(lorem_ipsum, dt)
end

function test_cert_no_verify()
	local master = uri.new()
	local u = master:to_buffer(https_lorem_ipsum)
	u:set_ssl_verify(false)
	u:add_ca(ca_opentrust_g1)
	master:download()
	local dt = u:finish()
	assert_equal(lorem_ipsum, dt)
end

-- This is valid usage so test that it is possible
function test_add_nil()
	local master = uri.new()
	local u = master:to_buffer(https_lorem_ipsum)
	u:add_ca(nil)
	u:add_crl(nil)
	u:add_pubkey(nil)
end
