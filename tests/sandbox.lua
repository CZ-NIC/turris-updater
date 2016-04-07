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
local sandbox = require 'sandbox'

module("sandbox-tests", package.seeall, lunit.testcase)

-- Test creating brand new contexts (no inheritance)
function test_context_new()
	-- If we specify no parent and no security level, it fails
	assert_error(sandbox.new)
	-- If we specify an invalid security level, it fails
	assert_error(function () sandbox.new('Invalid level') end)
	-- We try creating a context for each level.
	for _, level in pairs({"Full", "Local", "Remote", "Restricted"}) do
		local context = sandbox.new(level)
		assert_equal("table", type(context))
		assert_equal("table", type(context.env))
		-- There're some common functions in all of them
		assert_equal(pairs, context.env.pairs)
		assert_equal(string, context.env.string)
		-- Some are just in some of the contexts
		if level == "Full" then
			assert_equal(io, context.env.io)
		else
			assert_nil(context.env.io)
		end
		context.env = nil
		assert_table_equal({sec_level = sandbox.level(level), tp = "context"}, context)
	end
end

-- Create contexts by inheriting it from a parent
function test_context_inherit()
	local c1 = sandbox.new('Full')
	local c2 = sandbox.new(nil, c1)
	assert_equal(c1, c2.parent)
	assert_equal(sandbox.level('Full'), c2.sec_level)
	c2.parent = nil
	-- The environments are separate instances, but look the same
	assert_not_equal(c1.env, c2.env)
	assert_table_equal(c1, c2)
	c2 = sandbox.new(nil, c1)
	c2.test_field = "value"
	local c3 = sandbox.new('Remote', c2)
	assert_equal(c2, c3.parent)
	assert_equal(sandbox.level('Remote'), c3.sec_level)
	assert_nil(c3.env.io)
	assert_equal("value", c3.test_field)
	-- The lower-level permissions don't add anything to the higher ones.
	for k in pairs(c3.env) do
		assert(c2.env[k] ~= nil)
	end
end

-- Test running chunks in the sandbox
function test_sandbox_run()
	local chunk_ok = [[call()]]
	local chunk_io = [[io.open("/dev/zero")]]
	local chunk_parse = [[this is invalid lua code!!!!]]
	local chunk_runtime = [[error("Error!")]]
	local function test_do(chunk, sec_level, expected, result_called)
		local called
		local function call()
			called = true
		end
		local result = sandbox.run_sandboxed(chunk, "Chunk name", sec_level, nil, nil, function (context)
			context.env.call = call
		end)
		assert_table_equal(expected, result)
		assert_equal(result_called, called)
	end
	-- We can add a function and it can access the local upvalues
	test_do(chunk_ok, "Restricted", nil, true)
	test_do(chunk_ok, "Full", nil, true)
	-- Some things are possible in some security levels but not on others
	test_do(chunk_io, "Restricted", {
		tp = "error",
		reason = "runtime",
		msg = "[string \"Chunk name\"]:1: attempt to index global 'io' (a nil value)"
	})
	test_do(chunk_io, "Full", nil)
	test_do(chunk_parse, "Full", {
		tp = "error",
		reason = "compilation",
		msg = "[string \"Chunk name\"]:1: '=' expected near 'is'"
	})
	test_do(chunk_runtime, "Full", {
		tp = "error",
		reason = "runtime",
		msg = "[string \"Chunk name\"]:1: Error!"
	})
end

function test_level()
	-- Creation and comparisons
	local l1 = sandbox.level("Full")
	assert_equal("Full", tostring(l1))
	assert_equal(l1, l1)
	assert(l1 <= l1)
	assert_false(l1 ~= l1)
	assert_false(l1 < l1)
	local l2 = sandbox.level("Restricted")
	assert(l2 < l1)
	assert(l2 <= l1)
	assert_false(l1 < l2)
	assert_false(l1 <= l2)
	assert(l1 > l2)
	assert(l1 >= l2)
	-- Level is just passed through if it is already level
	local l3 = sandbox.level(l2)
	assert_equal(l2, l3)
	-- We may pass nil and get nil in return
	assert_nil(sandbox.level(nil))
	-- If it doesn't exist, it throws proper error
	local ok, err = pcall(sandbox.level, "Does not exist")
	assert_false(ok)
	assert_table_equal({
		tp = "error",
		reason = "bad value",
		msg = "No such level Does not exist"
	}, err)
end

-- Test the morphers act somewhat sane (or in the limits of their design insanity)
function test_morpher()
	local function mofun(...)
		local result = {...}
		return {...}
	end
	local function morpher (...)
		return sandbox.morpher(mofun, ...)
	end
	local m1 = morpher "a" "b" "c"
	-- It's not yet morphed
	assert(getmetatable(m1))
	-- But when we try to index it, we get the value
	assert_equal("a", m1[1])
	-- And now it is morphed
	assert_nil(getmetatable(m1))
	assert_table_equal({"a", "b", "c"}, m1)
	-- It works if we call it as a normal function (and acts the same as the previous morpher)
	local m2 = morpher("a", "b", "c")
	assert(getmetatable(m2))
	assert_equal("a", m2[1])
	assert_table_equal({"a", "b", "c"}, m1)
	-- Try to morph explicitly
	local m3 = morpher "a" "b" "c"
	m3:morph()
	assert_table_equal({"a", "b", "c"}, m3)
	-- If we run two morphers in a row, the first should get morphed
	local m4 = morpher "a"
	local m5 = morpher "b"
	assert_nil(getmetatable(m4))
	m5:morph()
	-- When we run morpher in a sandbox, that morpher is morphed by the end of the chunk
	local context;
	assert_nil(sandbox.run_sandboxed([[m = morpher "a"]], "Chunk name", "Restricted", nil, nil, function (c)
		context = c
		context.env.morpher = morpher;
	end))
	assert_nil(getmetatable(context.env.m))
	assert_table_equal({"a"}, context.env.m)
end
