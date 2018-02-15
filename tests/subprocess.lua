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
	local ok, out = subprocess(LST_HOOK, "Test: true", 1, "true")
	assert_equal(0, ok)
	assert_equal("", out)

	local ok, out = subprocess(LST_HOOK, "Test: false", 1, "false")
	assert_not_equal(0, ok)
	assert_equal("", out)
end

function test_output()
	local ok, out = subprocess(LST_HOOK, "Test: echo", 1, "echo", "hello")
	assert_equal(0, ok)
	assert_equal("hello\n", out)

	local ok, out = subprocess(LST_HOOK, "Test: echo stderr", 1, "sh", "-c", "echo hello >&2")
	assert_equal(0, ok)
	assert_equal("hello\n", out)
end

function test_timeout()
	local ok, out = subprocess(LST_HOOK, "Test: sleep", 1, "sleep", "2")
	assert_not_equal(0, ok)
	assert_equal("", out)
end

function test_callback()
	subprocess_kill_timeout(0)

	local ok, out = subprocess(LST_HOOK, "Test: env", 1, function () io.stderr:write("Hello callback\n") end, "true")
	assert_equal(0, ok)
	assert_equal("Hello callback\n", out)
	--[[
	We are testing here with stderr for a reason. There seems to be some problem
	with lua's stdout and dup. No lua stdout works after dup2 on f.d. 1 on some
	testing platforms. We are not planning to print messages from lua from
	subprocess on daily base so let's not care.
	]]

	local ok, out = subprocess(LST_HOOK, "Test: env", 1, function () setenv("TESTENV", "Hello env") end, "sh", "-c", "echo $TESTENV")
	assert_equal(0, ok)
	assert_equal("Hello env\n", out)
end

