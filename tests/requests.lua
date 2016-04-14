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
local requests = require "requests"
local utils = require "utils"

module("requests-tests", package.seeall, lunit.testcase)

local function run_sandbox_fun(func_code, level)
	local chunk = "result = " .. func_code
	local env
	local err = sandbox.run_sandboxed(chunk, "Test chunk", level or "Restricted", nil, nil, function (context)
		env = context.env
	end)
	print(DataDumper(err))
	assert_nil(err)
	return env.result
end

function test_package()
	local p1 = run_sandbox_fun "Package 'pkg_name'"
	assert_table_equal({
		tp = "package",
		name = "pkg_name"
	}, p1)
	local p2 = run_sandbox_fun "Package 'pkg_name' {replan = true, reboot = true}"
	assert_table_equal({
		tp = "package",
		name = "pkg_name",
		replan = true,
		reboot = true
	}, p2)
	assert_table_equal(utils.exception("bad value", "There's no extra option typo for a package"), sandbox.run_sandboxed("Package 'pkg_name' {typo = true}", "Test chunk", "Restricted"))
	assert_table_equal({p1, p2}, requests.known_packages)
end

function test_repository()
	local r1 = run_sandbox_fun "Repository 'test-repo' 'http://example.org/repo'"
	assert_table_equal({
		tp = "repository",
		name = "test-repo",
		uri = "http://example.org/repo"
	}, r1)
	local r2 = run_sandbox_fun "Repository 'test-repo-2' 'http://example.org/repo-2' {subdirs = {'a', 'b'}}"
	assert_table_equal({
		tp = "repository",
		name = "test-repo-2",
		uri = "http://example.org/repo-2",
		subdirs = {'a', 'b'}
	}, r2)
	assert_table_equal(utils.exception("bad value", "There's no extra option typo for a repository"), sandbox.run_sandboxed("Repository 'test-repo' 'http://example.org/repo' {typo = true}", "Test chunk", "Restricted"))
	assert_table_equal({
		["test-repo"] = r1,
		["test-repo-2"] = r2
	}, requests.known_repositories)
	assert_equal(r2, requests.repository_get("test-repo-2"))
	assert_equal(r2, requests.repository_get(r2))
	assert_nil(requests.repository_get(nil))
	assert_nil(requests.repository_get("does-not-exist"))
end

function teardown()
	requests.known_packages = {}
	requests.known_repositories = {}
end
