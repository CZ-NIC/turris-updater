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

require "lunit"
local uri = require "uri"
local sandbox = require "sandbox"

module("uri-tests", package.seeall, lunit.testcase)

-- Test few invalid URIs
function test_invalid()
	local context = sandbox.new("Remote")
	-- This scheme doesn't exist
	assert_exception(function () return uri.new(context, "unknown:bad") end, "bad value")
	-- Check it by calling directly uri()
	assert_exception(function () return uri(context, "unknown:bad") end, "bad value")
end

-- Test the data scheme
function test_data()
	local function check(input, output)
		local context = sandbox.new("Remote")
		local uri = uri(context, "data:" .. input)
		-- No need to wait for this one
		assert(uri.done)
		-- But when we do, we get the right data
		local ok, result = uri:get()
		assert(ok)
		assert_equal(output, result)
		-- When we do it with callback, it gets called and provides the same data
		local called = false
		uri:cback(function (ok, result)
			assert(ok)
			assert_equal(output, result)
			called = true
		end)
		assert(called)
	end
	-- Simple case
	check(",hello", "hello")
	-- Something URL-encoded
	check(",hello%20world", "hello world")
	-- Something base64-encoded
	check("base64,aGVsbG8gd29ybGQ=", "hello world")
	-- We don't damage whatever gets out of base64
	check("base64,aGVsbG8lMjB3b3JsZA==", "hello%20world")
	-- And we properly decode before base64
	check("base64,aGVsbG8lMjB3b3JsZA%3D%3D", "hello%20world")
	-- Other options about the URI are ignored
	check("charset=utf8,hello", "hello")
	check("charset=utf8;base64,aGVsbG8lMjB3b3JsZA%3D%3D", "hello%20world")
	local function malformed(input)
		local context = sandbox.new("Remote")
		local uri = uri(context, "data:" .. input)
		-- It fails right avay, synchronously
		assert(uri.done)
		-- The error is returned
		local ok, result = uri:get()
		assert_false(ok)
		assert_equal('error', result.tp)
		assert_equal("malformed URI", result.reason)
		-- The same goes when requested through the callback
		local called = false
		uri:cback(function (ok, result)
			assert_false(ok)
			assert_equal('error', result.tp)
			assert_equal("malformed URI", result.reason)
			called = true
		end)
		assert(called)
	end
	-- Missing comma
	malformed("data:hello")
	-- Bad URL escape
	malformed("data:,%ZZ")
	--[[
	Note: There are other forms of malformed URIs we don't detect.
	We don't aim at being validating parser of the URIs, so that's
	OK. The goal is to work with whatever is valid and report
	if we don't know what to do with what we got.
	]]
end
