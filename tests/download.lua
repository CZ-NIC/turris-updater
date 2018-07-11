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
local download = require "downloader"
local os = os
local utils = utils

module("downloader-tests", package.seeall, lunit.testcase)

local http_url = "http://applications-test.turris.cz"
local http_small =  http_url .. "/li.txt"
local http_big = http_url .. "/lorem_ipsum.txt"
local lorem_ipsum_file = (os.getenv("S") or ".") .. "/tests/data/lorem_ipsum.txt"


function test_download_data()
	local d = download.new()
	d:download_data(http_small)
	d:download_data(http_big)
	assert_nil(d:run())
	assert_string("lorem ipsum\n", d[http_small])
	assert_string(utils.read_file(lorem_ipsum_file), d[http_big])
end


function test_download_file()
	local smallf = (os.getenv("TMPDIR") or "/tmp") .. "/download_small.txt"
	local bigf = (os.getenv("TMPDIR") or "/tmp") .. "/download_big.txt"
	local d = download.new()
	d:download_file(http_small, smallf)
	d:download_file(http_big, bigf)

	assert_nil(d:run())

	assert_true(d[http_small])
	assert_true(d[http_big])
	assert_string("lorem ipsum\n", utils.read_file(smallf))
	assert_string(utils.read_file(lorem_ipsum_file), utils.read_file(bigf))
	os.remove(smallf)
	os.remove(bigf)
end

function test_error()
	local ref = http_url .. "/invalid"
	local d = download.new()
	d:download_data(ref)
	assert_table_equal({
		["url"] = ref,
		["error"] = "The requested URL returned error: 404 Not Found"
		}, d:run())
	-- Note: this error can change on curl version but that is not highly probable
end

function test_certificate()
	local d = download.new()
	d:download_data(http_small, {
		["capath"] = '/dev/null',
		["cacert_file"] = (os.getenv("S") or ".") .. "/tests/data/lets_encrypt_roots.pem"
	})
	assert_nil(d:run())
	assert_string("lorem ipsum\n", d[http_small])
end

function test_invalid_certificate()
	local d = download.new()
	d:download_data(http_small, {
		["capath"] = '/dev/null',
		["cacert_file"] = (os.getenv("S") or ".") .. "/tests/data/opentrust_ca_g1.pem"
	})
	assert_table_equal({
		["url"] = http_small,
		["error"] = "SSL certificate problem: unable to get local issuer certificate"
		}, d:run())
	-- Note: this error can change on curl version but that is not highly probable
end
