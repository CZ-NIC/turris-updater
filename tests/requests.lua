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

function test_package()
	local result = sandbox.run_sandboxed([[
		Package('pkg_name')
		Package('pkg_name', {replan = true, reboot = 'delayed', priority = 42})
	]], "test_package_chunk", "Restricted")
	assert_equal("context", result.tp, result.msg)
	assert_table_equal({
		{
			tp = "package",
			name = "pkg_name"
		},
		{
			tp = "package",
			name = "pkg_name",
			replan = true,
			reboot = "delayed",
			priority = 42
		}
	}, requests.known_packages)
end

function test_repository()
	requests.repo_serial = 1
	local result = sandbox.run_sandboxed([[
		Repository('test-repo', 'http://example.org/repo')
		Repository('test-repo-2', 'http://example.org/repo-2', {subdirs = {'a', 'b'}, priority = 60})
		Repository('test-repo-other', 'http://example.org/repo-other', {index = 'https://example.org/repo-other/Packages.gz'})
	]], "test_repository_chunk", "Restricted")
	assert_equal("context", result.tp, result.msg)

	for _, repo in pairs(requests.known_repositories) do
		assert(repo.index_uri)
		repo.index_uri = nil
	end
	assert_table_equal({
		["test-repo"] = {
			tp = "repository",
			name = "test-repo",
			repo_uri = "http://example.org/repo",
			priority = 50,
			serial = 1
		},
		["test-repo-2-a"] = {
			tp = "repository",
			name = "test-repo-2-a",
			repo_uri = "http://example.org/repo-2",
			subdirs = {'a', 'b'},
			priority = 60,
			serial = 2
		},
		["test-repo-2-b"] = {
			tp = "repository",
			name = "test-repo-2-b",
			repo_uri = "http://example.org/repo-2",
			subdirs = {'a', 'b'},
			priority = 60,
			serial = 3
		},
		["test-repo-other"] = {
			tp = "repository",
			name = "test-repo-other",
			repo_uri = "http://example.org/repo-other",
			index = "https://example.org/repo-other/Packages.gz",
			priority = 50,
			serial = 4
		}
	}, requests.known_repositories)
	assert_equal(5, requests.repo_serial)
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
	local result = sandbox.run_sandboxed([[
		Script("file:///does/not/exist", { ignore = {"missing"}, security = "local" })
	]], "test_script_missing_chunk", "Local")
	-- It doesn't produce an error, even when the script doesn't exist
	assert_equal("context", result.tp, result.msg)
end

-- Check we are not allowed to raise the security level by running a script
function test_script_raise_level()
	local err = sandbox.run_sandboxed([[
		Script("data:,", { security = 'Full' })
	]], "test_script_raise_level_chunk", "Restricted")
	assert_table_equal(utils.exception("access violation", "Attempt to raise security level from Restricted to Full"), err)
end

-- Test all the transitions between security levels. Some shall error, some not.
function test_script_level_transition()
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

function test_script_err_propagate()
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

function teardown()
	requests.known_packages = {}
	requests.known_repositories = {}
	requests.content_requests = {}
	utils.cleanup_dirs(tmp_dirs)
	tmp_dirs = {}
end
