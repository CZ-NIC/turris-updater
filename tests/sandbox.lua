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
local utils = require 'utils'
local backend = require 'backend'

module("sandbox-tests", package.seeall, lunit.testcase)

-- Test creating brand new contexts (no inheritance)
function test_context_new()
	-- Set a state variable override for testing. Check it propagates.
	sandbox.state_vars.model = 'test'
	-- If we specify no parent and no security level, it fails
	assert_error(sandbox.new)
	-- If we specify an invalid security level, it fails
	assert_error(function () sandbox.new('Invalid level') end)
	-- We try creating a context for each level.
	for _, level in pairs({"Full", "Local", "Remote", "Restricted"}) do
		local context = sandbox.new(level, nil, "")
		assert(context:level_check("Restricted"))
		assert(context:level_check(level))
		assert(context:level_check(sandbox.level("Restricted")))
		if level ~= "Full" then
			assert_false(context:level_check("Full"))
		end
		assert_equal("table", type(context))
		assert_equal("table", type(context.env))
		assert_equal("function", type(context.level_check))
		-- There're some common functions in all of them
		assert_equal(pairs, context.env.pairs)
		assert_table_equal(string, context.env.string)
		-- Some are just in some of the contexts
		if level == "Full" then
			assert_equal(io, context.env.io)
			assert_equal(utils, context.env.utils)
			assert_equal(getmetatable, context.env.getmetatable)
		else
			assert_nil(context.env.io)
			assert_nil(context.env.utils)
			assert_nil(context.env.getmetatable)
		end
		assert_equal("test", context.env.model)
		-- While we aren't sure to detect any other architecture, the all one should be there.
		assert_equal("all", context.env.architectures[1])
		-- And the change to the table doesn't propagate outside
		context.env.architectures[1] = 'changed'
		assert_equal(sandbox.state_vars.architectures[1], 'all')
		context.env = nil
		context.level_check = nil
		local expected = {sec_level = sandbox.level(level), tp = "context", flags = {}, name = '', full_name = ''}
		expected.root_parent = expected
		expected.hierarchy = {[''] = expected}
		assert_table_equal(expected, context)
		backend.stored_flags = {}
	end
end

-- Create contexts by inheriting it from a parent
function test_context_inherit()
	local c1 = sandbox.new('Full')
	local c2 = sandbox.new(nil, c1)
	assert_equal(c1, c2.parent)
	assert_equal(sandbox.level('Full'), c2.sec_level)
	c2.parent = nil
	-- The environments are separate instances, but look the same (though some functions are generated, so they can't be compared directly)
	local function env_sanitize(context)
		return utils.map(context.env, function (n, v)
			return n, type(v)
		end)
	end
	assert_not_equal(env_sanitize(c1), env_sanitize(c2))
	c1.env = nil
	c2.env = nil
	c1.level_check = nil
	c2.level_check = nil
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
	local chunk_meta = [[getmetatable({})]]
	local chunk_private = [[utils.private({})]]
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
		if expected then
			assert_table_equal(expected, result)
		end
		assert_equal(result_called, called)
		backend.stored_flags = {}
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
	test_do(chunk_private, "Local", {
		tp = "error",
		reason = "runtime",
		msg = "[string \"Chunk name\"]:1: attempt to index global 'utils' (a nil value)"
	})
	test_do(chunk_private, "Full", nil)
	test_do(chunk_meta, "Local", {
		tp = "error",
		reason = "runtime",
		msg = "[string \"Chunk name\"]:1: attempt to call global 'getmetatable' (a nil value)"
	})
	test_do(chunk_meta, "Full", nil)
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
	local function mofun(result, ...)
		utils.table_merge(result, {...})
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
	local result = sandbox.run_sandboxed([[m = morpher "a"]], "Chunk name", "Restricted", nil, nil, function (c)
		context = c
		context.env.morpher = morpher;
	end)
	assert_equal(context, result)
	assert_nil(getmetatable(context.env.m))
	assert_table_equal({"a"}, context.env.m)
	-- Test these don't break when nested in allowed ways (doesn't work with parenthenless form)
	local m6 = morpher("a", morpher("b", morpher("c")))
	m6:morph()
	assert_table_equal({"a", {"b", {"c"}}}, m6)
	local m7 = morpher "a"
	local m8 = morpher {"b", m7}
	m8:morph()
	assert_table_equal({{"b", {"a"}}}, m8)
end

-- Check the sandbox can't damage a system library
function test_syslib()
	-- Store the original
	local l = string.lower
	mock_gen("string.lower", function (...) return l(...) end)
	local result = sandbox.run_sandboxed([[string.lower = function () return "hello" end]], "Chunk name", "Local")
	assert_equal("context", result.tp, result.err)
	local str = "HI"
	assert_equal("hi", str:lower())
	-- Everything is allowed inside the full security level
	local result = sandbox.run_sandboxed([[string.lower = function () return "hello" end]], "Chunk name 2", "Full")
	assert_equal("context", result.tp, result.err)
	assert_equal("hello", str:lower())
end

-- Test the complex dep descriptions
function test_deps()
	for fun, tp in pairs({And = 'dep-and', Or = 'dep-or', Not = 'dep-not'}) do
		local env
		local result = sandbox.run_sandboxed("res = " .. fun .. "('a', 'b', 'c')", "Chunk name", "Restricted", nil, nil, function (context)
			-- Steal the context, so we can access the data stored there later on.
			env = context.env
		end)
		assert_equal("context", result.tp, result.err)
		backend.stored_flags = {}
		assert_table_equal({
			tp = tp,
			sub = {'a', 'b', 'c'}
		}, env.res)
	end
	-- Test them together
	local env
	local result = sandbox.run_sandboxed([[
		res = Or('pkg1', And(Not('pkg2'), 'pkg3'))
	]], "Chunk name", 'Restricted', nil, nil, function (context)
		env = context.env
	end)
	assert_equal("context", result.tp, result.err)
	assert_table_equal({
		tp = 'dep-or',
		sub = {
			'pkg1',
			{
				tp = 'dep-and',
				sub = {
					{
						tp = 'dep-not',
						sub = {
							'pkg2'
						}
					},
					'pkg3'
				}
			}
		}
	}, env.res)
end

function test_hierarchy()
	local result = sandbox.run_sandboxed([[Script "script" "data:,"]], "toplevel", "Full")
	assert_equal("context", result.tp, result.msg)
	assert_equal("toplevel", result.name)
	assert_equal("toplevel", result.full_name)
	assert_equal(result, result.root_parent)
	assert_equal(result, result.hierarchy["toplevel"])
	local sub = result.hierarchy["toplevel/script"]
	assert(sub)
	assert_not_equal(result, sub)
	assert_equal("context", sub.tp)
	assert_equal("script", sub.name)
	assert_equal("toplevel/script", sub.full_name)
	assert_equal(result, sub.root_parent)
end

function teardown()
	mocks_reset()
	backend.stored_flags = {}
end
