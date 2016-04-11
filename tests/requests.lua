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
-- The request parts are inside sandbox. Therefore, we use the sandbox as an entry point.
local sandbox = require "sandbox"
local utils = require "utils"

module("requests-tests", package.seeall, lunit.testcase)

local function run_sandbox_fun(func_code, level)
	local chunk = "result = " .. func_code
	local env
	assert_nil(sandbox.run_sandboxed(chunk, "Test chunk", level or "Restricted", nil, nil, function (context)
		env = context.env
	end))
	return env.result
end

function test_package()
	assert_table_equal({
		tp = "package",
		name = "pkg_name"
	}, run_sandbox_fun "Package 'pkg_name'")
	assert_table_equal({
		tp = "package",
		name = "pkg_name",
		replan = true,
		reboot = true
	}, run_sandbox_fun "Package 'pkg_name' {replan = true, reboot = true}")
	assert_table_equal(utils.exception("bad value", "There's no extra option typo for a package"), sandbox.run_sandboxed("Package 'pkg_name' {typo = true}", "Test chunk", "Restricted"))
end
