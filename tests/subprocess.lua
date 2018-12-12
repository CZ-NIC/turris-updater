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

module("subproc", package.seeall, lunit.testcase)

function test_exit_code()
	local ok, out = subprocess(LST_HOOK, "Test: true", 1000, "true")
	assert_equal(0, ok)
	assert_equal("", out)

	local ok, out = subprocess(LST_HOOK, "Test: false", 1000, "false")
	assert_not_equal(0, ok)
	assert_equal("", out)
end

function test_output()
	local ok, out = subprocess(LST_HOOK, "Test: echo", 1000, "echo", "hello")
	assert_equal(0, ok)
	assert_equal("hello\n", out)

	local ok, out = subprocess(LST_HOOK, "Test: echo stderr", 1000, "sh", "-c", "echo hello >&2")
	assert_equal(0, ok)
	assert_equal("hello\n", out)
end

function test_timeout()
	subprocess_kill_timeout(0)
	local ok, out = subprocess(LST_HOOK, "Test: sleep", 1000, "sleep", "2")
	assert_not_equal(0, ok)
	assert_equal("", out)
end

function test_callback()
	subprocess_kill_timeout(0)

	--[[
	Note: We intentionally use here callback to just set environment variable.
	Correct functionality of stdout and stderr from callback is tested in
	subprocess.c test suite. We have to just test if we are able to execute lua
	code in callback.
	It is not tested with plain print because test suite seems to somehow detect
	fork and prints test statistics like on beginning of whole tests run. This
	is pretty annoying and solution is to simply not test output. Also note that
	this is only test suite problem so real functionality is not affected and
	prints can be used in callbacks like normal.
	]]
	local ok, out = subprocess(LST_HOOK, "Test: env", 1000, function () setenv("TESTENV", "Hello env") end, "sh", "-c", "echo $TESTENV")
	assert_equal(0, ok)
	assert_equal("Hello env\n", out)
end

