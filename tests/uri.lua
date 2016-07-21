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
local dir = (os.getenv("S") .. "/") or ''

module("uri-tests", package.seeall, lunit.testcase)

-- Test few invalid URIs
function test_invalid()
	local context = sandbox.new("Remote")
	-- This scheme doesn't exist
	assert_exception(function () return uri.new(context, "unknown:bad") end, "bad value")
	-- Check it by calling directly uri()
	assert_exception(function () return uri(context, "unknown:bad") end, "bad value")
end

-- Test if we have get and ok methods
function test_methods()
	function method_assert(u)
		uri.wait(u)
		lunit.assert_function(u.get, "Uri missing get method")
		lunit.assert_function(u.ok, "Uri missing ok method")
	end

	local context = sandbox.new("Local")
	-- Check on success
	local u1 = uri(context, "https://api.turris.cz/", {verification = 'none'})
	-- Missing ca and crl files
	local u2 = uri(context, "https://api.turris.cz/", {ca = "file:///tmp/missing.ca", crl = "file:///tmp/missing.pem", pubkey = "file:///tmp/missing.pub"})
	method_assert(u1)
	method_assert(u2)
end

local function check_sync(level, input, output)
	local context = sandbox.new(level)
	local uri = uri(context, input)
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

local function err_sync(level, input, reason)
	local context = sandbox.new(level)
	local uri = uri(context, input)
	-- It fails right avay, synchronously
	assert(uri.done)
	-- The error is returned
	local ok, result = uri:get()
	assert_false(ok)
	assert_equal('error', result.tp)
	assert_equal(reason, result.reason)
	-- The same goes when requested through the callback
	local called = false
	uri:cback(function (ok, result)
		assert_false(ok)
		assert_equal('error', result.tp)
		assert_equal(reason, result.reason)
		called = true
	end)
	assert(called)
end

-- Test the data scheme
function test_data()
	local function check(input, output)
		check_sync("Restricted", "data:" .. input, output)
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
		err_sync("Restricted", "data:" .. input, "malformed URI")
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

function test_file()
	check_sync("Local", "file:///dev/null", "")
	check_sync("Local", "file://" .. dir .. "tests/data/hello.txt", "hello\n")
	check_sync("Local", "file://" .. dir .. "tests/data/hello%2etxt", "hello\n")
	local context = sandbox.new("Remote")
	assert_exception(function () uri(context, "file:///dev/null") end, "access violation")
	err_sync("Local", "file:something", "malformed URI")
	err_sync("Local", "file://%ZZ", "malformed URI")
	err_sync("Local", "file:///does/not/exist", "unreachable")
end

function test_internal()
	check_sync("Local", "internal:hello_txt", "hello\n")
	check_sync("Local", "internal:hello%5ftxt", "hello\n")
	assert_exception(function () uri(sandbox.new("Remote"), "internal:hello_txt") end, "access violation")
	err_sync("Local", "internal:%ZZ", "malformed URI")
	err_sync("Local", "internal:does_not_exist", "unreachable")
end

function test_https()
	local context = sandbox.new("Remote")
	local u1 = uri(context, "https://api.turris.cz/", {verification = 'none'})
	local u2 = uri(context, "https://api.turris.cz/does/not/exist", {verification = 'none'})
	assert_false(u1.done)
	assert_false(u2.done)
	local called1 = false
	local called2 = false
	u1:cback(function (ok, content)
		called1 = true
		assert(ok)
		assert(content:match("Not for your eyes"))
	end)
	u2:cback(function (ok, err)
		called2 = true
		assert_false(ok)
		assert_equal("error", err.tp)
		assert_equal("unreachable", err.reason)
	end)
	assert_false(called1)
	assert_false(called2)
	local ok, content = u1:get()
	assert(called1)
	assert(ok)
	assert(content:match("Not for your eyes"))
	uri.wait(u1, u2)
	assert(called2)
	local ok = u2:get()
	assert_false(ok)
end

function test_https_cert()
	local context = sandbox.new("Local")
	local ca_file = "file://" .. dir .. "tests/data/updater.pem"
	-- It should succeed with the correct CA
	local u1 = uri(context, "https://api.turris.cz/", {verification = "cert", ca = ca_file})
	-- But should fail with a wrong one
	local u2 = uri(context, "https://api.turris.cz/", {verification = "cert", ca = "file:///dev/null"})
	-- We may specify the ca as a table of possibilities
	local u3 = uri(context, "https://api.turris.cz/", {verification = "cert", ca = {"file:///dev/null", ca_file}})
	local ok1 = u1:get()
	assert(ok1)
	local ok2 = u2:get()
	assert_false(ok2)
	local ok3 = u3:get()
	assert(ok3)
	-- Check we can put the verification stuff into the context
	context.ca = ca_file
	context.verification = "cert"
	u1 = uri(context, "https://api.turris.cz/")
	u2 = uri(context, "https://api.turris.cz/", {ca = "file:///dev/null"})
	ok1 = u1:get()
	ok2 = u2:get()
	assert(ok1)
	assert_false(ok2)
	-- It refuses local URIs inside the ca field if refered from the wrong context
	context = sandbox.new("Remote")
	assert_exception(function () uri(context, "https://api.turris.cz/", {verification = "cert", ca = ca_file}) end, "access violation")
end

function test_restricted()
	local context = sandbox.new("Restricted")
	context.restrict = 'https://api%.turris%.cz/.*'
	local function u(location)
		local result = uri(context, location, {verification = 'none'})
		--[[
		Make sure we wait for the result so we free all relevant memory.
		Yes, it would be better if we freed it automatically when we just
		drop the reference, but the world is not perfect.
		]]
		result:get()
	end
	assert_pass(function () u("https://api.turris.cz/") end)
	assert_pass(function () u("https://api.turris.cz/index.html") end)
	assert_exception(function () u("https://api.turris.cz") end, "access violation")
	assert_exception(function () u("http://api.turris.cz/index.html") end, "access violation")
	assert_exception(function () u("https://www.turris.cz/index.html") end, "access violation")
end

function test_sig()
	local context = sandbox.new("Restricted")
	local key_ok = 'data:,ok'
	local key_bad = 'data:,bad'
	local key_broken = 'data:'
	local sig = 'data:,sig'
	mock_gen("uri.signature_check", function (content, key, signature)
		if key == 'ok' then
			return true
		else
			return false
		end
	end)
	local function ck(key, sig)
		local ok, content = uri(context, "data:,data", {verification = 'sig', sig = sig, pubkey = key}):get()
		return ok
	end
	assert(ck(key_ok, sig))
	assert_false(ck(key_bad, sig))
	assert_false(ck(key_broken, sig))
	-- Check one correct key is enough
	assert(ck({key_bad, key_broken, key_ok}, sig))
	-- Check the default sig uri (it actually works with data uri in a strange way
	assert(ck(key_ok))
	assert_table_equal({
		{f = "uri.signature_check", p = {"data", "ok", "sig"}},
		{f = "uri.signature_check", p = {"data", "bad", "sig"}},
		{f = "uri.signature_check", p = {"data", "bad", "sig"}},
		{f = "uri.signature_check", p = {"data", "ok", "sig"}},
		{f = "uri.signature_check", p = {"data", "ok", "data.sig"}}
	}, mocks_called)
end

-- Check invalid verification mode (a typo) is rejected
function test_vermode()
	local context = sandbox.new("Restricted")
	assert_exception(function () uri(context, "data:,data", {verification = 'typo'}) end, 'bad value')
end
