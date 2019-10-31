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

--[[
On Debian lunit for some reason prints when invoked in subprocess information
about executed test suite again. This of course taints start of subprocess.
This function removes known problematic two first lines from beginning of output
if pattern is detected.
]]
local function fix_out(out)
	if out:find('Loaded testsuite with', 1, true) ~= nil then
		local _, last = out:find('\n\n', 1, true)
		return out:sub(last + 1)
	end
	return out
end

function test_exit_code()
	local ok, out = subprocess(LST_HOOK, "Test: true", 1000, "true")
	assert_equal(0, ok)
	assert_equal("", fix_out(out))

	local ok, out = subprocess(LST_HOOK, "Test: false", 1000, "false")
	assert_not_equal(0, ok)
	assert_equal("", fix_out(out))
end

function test_output()
	local ok, out = subprocess(LST_HOOK, "Test: echo", 1000, "echo", "hello")
	assert_equal(0, ok)
	assert_equal("hello\n", fix_out(out))

	local ok, out = subprocess(LST_HOOK, "Test: echo stderr", 1000, "sh", "-c", "echo hello >&2")
	assert_equal(0, ok)
	assert_equal("hello\n", fix_out(out))
end

function test_timeout()
	subprocess_kill_timeout(0)
	local ok, out = subprocess(LST_HOOK, "Test: sleep", 1000, "sleep", "2")
	assert_not_equal(0, ok)
	assert_equal("", fix_out(out))
end

function test_callback()
	subprocess_kill_timeout(0)

	local ok, out = subprocess(LST_HOOK, "Test: env", 1000, function () io.stderr:write("Hello callback") end, "true")
	assert_equal(0, ok)
	assert_equal("Hello callback", fix_out(out))
	local ok, out = subprocess(LST_HOOK, "Test: env", 1000, function () setenv("TESTENV", "Hello env callback") end, "sh", "-c", "echo $TESTENV")
	assert_equal(0, ok)
	assert_equal("Hello env callback\n", fix_out(out))
end

