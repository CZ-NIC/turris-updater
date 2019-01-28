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
local uri = require "uri"
local backend = require "backend"

module("requests-tests", package.seeall, lunit.testcase)

local tmp_dirs = {}

local sandbox_fun_i = 0

local function run_sandbox_fun(func_code, level)
	local chunk = "result = " .. func_code
	local env
	local result = sandbox.run_sandboxed(chunk, "Function chunk" .. tostring(sandbox_fun_i), level or "Restricted", nil, nil, function (context)
		env = context.env
	end)
	sandbox_fun_i = sandbox_fun_i + 1
	assert_equal("context", result.tp, result.msg)
	return env.result
end

function test_package()
	local p1 = run_sandbox_fun("Package('pkg_name')")
	assert_table_equal({
		tp = "package",
		name = "pkg_name"
	}, p1)
	local p2 = run_sandbox_fun("Package('pkg_name', {replan = true, reboot = 'delayed', priority = 42})")
	assert_table_equal({
		tp = "package",
		name = "pkg_name",
		replan = true,
		reboot = "delayed",
		priority = 42
	}, p2)
	assert_table_equal({p1, p2}, requests.known_packages)
end

function test_repository()
	requests.repo_serial = 1
	local r1 = run_sandbox_fun("Repository('test-repo', 'http://example.org/repo')")
	assert_table_equal({
		tp = "repository",
		name = "test-repo",
		repo_uri = "http://example.org/repo",
		priority = 50,
		serial = 1
	}, r1)
	utils.private(r1).context = nil
	assert_table_equal({
		index_uri = {[""] = {u = "http://example.org/repo/Packages.gz"}}
	}, utils.private(r1))
	local r2 = run_sandbox_fun("Repository('test-repo-2', 'http://example.org/repo-2', {subdirs = {'a', 'b'}, priority = 60})")
	assert_table_equal({
		tp = "repository",
		name = "test-repo-2",
		repo_uri = "http://example.org/repo-2",
		subdirs = {'a', 'b'},
		priority = 60,
		serial = 2
	}, r2)
	utils.private(r2).context = nil
	assert_table_equal({
		index_uri = {["/a"] = {u = "http://example.org/repo-2/a/Packages.gz"}, ["/b"] = {u = "http://example.org/repo-2/b/Packages.gz"}}
	}, utils.private(r2))
	local r3 = run_sandbox_fun("Repository('test-repo-other', 'http://example.org/repo-other', {index = 'https://example.org/repo-other/Packages.gz'})")
	assert_table_equal({
		tp = "repository",
		name = "test-repo-other",
		repo_uri = "http://example.org/repo-other",
		index = "https://example.org/repo-other/Packages.gz",
		priority = 50,
		serial = 3
	}, r3)
	utils.private(r3).context = nil
	assert_table_equal({
		index_uri = {[""] = {u = "https://example.org/repo-other/Packages.gz"}}
	}, utils.private(r3))
	assert_table_equal({
		["test-repo"] = r1,
		["test-repo-2"] = r2,
		["test-repo-other"] = r3
	}, requests.known_repositories)
	assert_equal(r2, requests.repository_get("test-repo-2"))
	assert_equal(r2, requests.repository_get(r2))
	assert_nil(requests.repository_get(nil))
	assert_nil(requests.repository_get("does-not-exist"))
	assert_equal(4, requests.repo_serial)
end

function test_install_uninstall()
	local result = sandbox.run_sandboxed([[
		Install("pkg1", "pkg2", {priority = 45}, "pkg3", {priority = 14}, "pkg4", "pkg5")
		Uninstall("pkg6", {priority = 75}, "pkg7")
		Install("pkg8")
	]], "test_install_uninstall_chunk", "Restricted")
	local function req(num, mode, prio)
		return {
			tp = mode,
			package = {
				tp = "package",
				name = "pkg" .. num
			},
			priority = prio
		}
	end
	assert_table_equal({
		req(1, "install", 45),
		req(2, "install", 45),
		req(3, "install", 14),
		req(4, "install", 50),
		req(5, "install", 50),
		req(6, "uninstall", 75),
		req(7, "uninstall", 50),
		req(8, "install", 50)
	}, requests.content_requests)
	assert_equal("context", result.tp, result.msg)
end

function test_script()
	-- We actually don't want any mocks here, let uri work as expected
	mocks_reset()
	-- The URI contains 'Install "pkg"'
	local result = sandbox.run_sandboxed([[
		Script("data:base64,SW5zdGFsbCAicGtnIgo=", { security = 'Restricted' })
	]], "test_script_chunk", "Restricted")
	assert_equal("context", result.tp, result.msg)
	assert_table_equal({
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg'
			},
			priority = 50
		}
	}, requests.content_requests)
end

-- Test legacy syntax of script
function test_script_legacy()
	-- We actually don't want any mocks here, let uri work as expected
	mocks_reset()
	-- The URI contains 'Install "pkg"'
	local result = sandbox.run_sandboxed([[
		Script("name", "data:base64,SW5zdGFsbCAicGtnIgo=", { security = 'Restricted' })
	]], "test_script_chunk", "Restricted")
	assert_equal("context", result.tp, result.msg)
	assert_table_equal({
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg'
			},
			priority = 50
		}
	}, requests.content_requests)
end

function test_script_missing()
	mocks_reset()
	local result = sandbox.run_sandboxed([[
		Script("file:///does/not/exist", { ignore = {"missing"}, security = "local" })
	]], "test_script_missing_chunk", "Local")
	-- It doesn't produce an error, even when the script doesn't exist
	assert_equal("context", result.tp, result.msg)
end

-- Check we are not allowed to raise the security level by running a script
function test_script_raise_level()
	mocks_reset()
	local err = sandbox.run_sandboxed([[
		Script("data:,", { security = 'Full' })
	]], "test_script_raise_level_chunk", "Restricted")
	assert_table_equal(utils.exception("access violation", "Attempt to raise security level from Restricted to Full"), err)
end

-- Test all the transitions between security levels. Some shall error, some not.
function test_script_level_transition()
	mocks_reset()
	local levels = {'Full', 'Local', 'Remote', 'Restricted'}
	for i, from in ipairs(levels) do
		for j, to in ipairs(levels) do
			local result = sandbox.run_sandboxed([[
				Script("data:,", { security = ']] .. to .. [[' })
			]], "test_script_level_transition_chunk " .. from .. "/" .. to, from)
			if i > j then
				assert_table_equal(utils.exception("access violation", "Attempt to raise security level from " .. from .. " to " .. to), result)
			else
				assert_equal("context", result.tp, result.msg)
			end
		end
	end
end

function test_script_pass_validation()
	mocks_reset()
	local bad_i = 0
	local function bad(opts, msg, exctype)
		local err = sandbox.run_sandboxed([[
			Script("data:,", { security = 'Restricted']] .. opts .. [[ })
		]], "test_script_pass_validation_chunk1" .. tostring(bad_i), "Restricted")
		bad_i = bad_i + 1
		assert_table_equal(utils.exception(exctype or "bad value", msg), err)
	end
	-- Bad uri inside something
	bad(", verification = 'sig', pubkey = 'invalid://'", "Unknown URI schema invalid")
	-- We don't allow this URI in the given context (even if it is not directly used)
	bad(", verification = 'sig', pubkey = 'file:///dev/null'", "At least Local level required for file URI", "access violation")
	-- But we allow it if there's a high enough level
	local result = sandbox.run_sandboxed([[
		Script("data:,", { security = 'Restricted', pubkey = 'file:///dev/null' })
	]], "test_script_pass_validation_chunk2", "Local")
	assert_equal("context", result.tp, result.msg)
end

function test_script_err_propagate()
	mocks_reset()
	local err = sandbox.run_sandboxed([[
		Script("data:,error()")
	]], "test_script_err_propagate_chunk", "Restricted")
	assert_table(err)
	assert_equal("error", err.tp)
end

-- If someone wants to actually download the mock URI object, return an empty document
local uri_meta = {}
function uri_meta:__index(key)
	if key == 'get' then
		return function()
			return true, ''
		end
	else
		return nil
	end
end

function setup()
	-- Don't download stuff now
	--
	mock_gen("uri.new", function (context, u) return setmetatable({u = u}, uri_meta) end, true)
end

function teardown()
	requests.known_packages = {}
	requests.known_repositories = {}
	requests.content_requests = {}
	mocks_reset()
	utils.cleanup_dirs(tmp_dirs)
	tmp_dirs = {}
end
