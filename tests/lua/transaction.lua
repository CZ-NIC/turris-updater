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
local B = require 'backend'
local T = require 'transaction'
local utils = require 'utils'
local journal = require 'journal'
local syscnf = require "syscnf"

syscnf.set_root_dir()

module("transaction-tests", package.seeall, lunit.testcase)

local test_status = {
	["pkg-rem"] = {
		Package = "pkg-rem",
		Conffiles = {
			remconf = "12345678901234567890123456789012"
		},
		Status = {"install", "user", "installed"}
	},
	["pkg-name"] = {
		Package = "pkg-name",
		Canary = true,
		Version = "0",
		Conffiles = {
			c = "12345678901234567890123456789012"
		},
		Status = {"install", "user", "installed"}
	}
}

local intro = {
	{
		f = "backend.run_state",
		p = {}
	},
	{
		f = "journal.fresh",
		p = {}
	},
	{
		f = "utils.mkdirp",
		p = {"/usr/share/updater/unpacked/"}
	},
}

local function outro(cleanup_dirs, status)
	return {
		{
			f = "utils.cleanup_dirs",
			p = {cleanup_dirs}
		},
		{
			f = "backend.control_cleanup",
			p = {status}
		},
		{
			f = "backend.status_dump",
			p = {status}
		},
		{
			f = "journal.write",
			p = {journal.CLEANED}
		},
		{
			f = "journal.finish",
			p = {}
		},
		{
			f = "backend.run_state.release",
			p = {}
		}
	}
end

local function tables_join(...)
	local idx = 0
	local result = {}
	for _, param in ipairs({...}) do
		for _, val in ipairs(param) do
			idx = idx + 1
			result[idx] = val
		end
	end
	return result
end

local function mocks_install()
	mock_gen("os.remove")
	mock_gen("utils.dir_ensure")
	mock_gen("utils.mkdirp")
	mock_gen("backend.status_parse", function () fail("Forbidden function backend.status_parse") end)
	mock_gen("backend.run_state", function()
		return {
			release = function ()
				table.insert(mocks_called, {
					f = "backend.run_state.release",
					p = {}
				})
			end,
			status = utils.clone(test_status)
		}
	end)
	mock_gen("backend.pkg_unpack", function () return "pkg_dir" end)
	mock_gen("backend.pkg_examine", function () return {f = true}, {d = true}, {c = "1234567890123456"}, {Package = "pkg-name", files = {f = true}, Conffiles = {c = "1234567890123456"}, Version = "1", Status = {"install", "user", "installed"}} end)
	mock_gen("backend.collision_check", function () return {}, {}, {}  end)
	mock_gen("backend.pkg_merge_files")
	mock_gen("backend.pkg_cleanup_files")
	mock_gen("backend.control_cleanup")
	mock_gen("backend.pkg_merge_control")
	mock_gen("backend.status_dump")
	mock_gen("backend.script_run", function (pkgname, suffix)
		if suffix == "postinst" then
			return false, 1, "Fake failed postinst"
		else
			return true, ""
		end
	end)
	mock_gen("backend.pkg_config_info", function (f, configs)
		return f, false
	end)
	mock_gen("utils.cleanup_dirs")
	mock_gen("journal.fresh")
	mock_gen("journal.finish")
	mock_gen("journal.write")
	mock_gen("locks.acquire", function () fail("Forbidden function locks.acquire") end)
end

-- Test calling empty transaction
function test_perform_empty()
	mocks_install()
	-- Run empty set of operations
	T.perform({})
	local expected = tables_join(intro, {
		{
			f = "journal.write",
			p = {journal.UNPACKED, {}, {}, {}, {}, {}}
		},
		{
			f="utils.cleanup_dirs",
			p={ { "/usr/share/updater/download/" } }
		},
		{
			f = "backend.collision_check",
			p = {test_status, {}, {}}
		},
		{
			f = "journal.write",
			p = {journal.CHECKED, {}, {}}
		},
		{
			f = "journal.write",
			p = {journal.CHANGELOG_START}
		},
		{
			f = "journal.write",
			p = {journal.MOVED, test_status, {}, {}, {}}
		},
		{
			f = "backend.pkg_cleanup_files",
			p = {{}, {}}
		},
		{
			f = "journal.write",
			p = {journal.SCRIPTS, test_status, {}}
		},
		{
			f = "journal.write",
			p = {journal.CHANGELOG_END}
		},
	}, outro({}, test_status))
	assert_table_equal(expected, mocks_called)
end

-- Test a transaction when it goes well
function test_perform_ok()
	mocks_install()
	mock_gen("backend.collision_check", function () return {}, {}, {d2 = true}  end)
	local result = T.perform({
		{
			op = "install",
			file = "<package>"
		}, {
			op = "remove",
			name = "pkg-rem"
		}
	})
	-- No collected errors
	assert_table_equal({
		["pkg-name"] = {
			["postinst"] = "Fake failed postinst"
		}
	}, result)
	local status_mod = utils.clone(test_status)
	status_mod["pkg-name"] = {
		Package = "pkg-name",
		Conffiles = { c = "1234567890123456" },
		Version = "1",
		files = { f = true },
		Status = {"install", "user", "installed"}
	}
	status_mod["pkg-rem"] = nil
	local expected = tables_join(intro, {
		{
			f = "backend.pkg_unpack",
			p = {"<package>"}
		},
		{
			f = "backend.pkg_examine",
			p = {"pkg_dir"}
		},
		{
			f = "journal.write",
			p = {
				journal.UNPACKED,
				{["pkg-name"] = true, ["pkg-rem"] = true},
				{["pkg-name"] = { f = true }},
				{
					{
						configs = { c = "1234567890123456" },
						control = {
							Conffiles = { c = "1234567890123456" },
							Package = "pkg-name",
							Version = "1",
							files = { f = true },
							Status = {"install", "user", "installed"}
						},
						dir = "pkg_dir",
						dirs = { d = true },
						files = { f = true },
						op = "install",
						old_configs = { c = "12345678901234567890123456789012" },
					},
					{ name = "pkg-rem", op = "remove" }
				},
				{"pkg_dir"},
				{}
			}
		},
		{
			f = "utils.cleanup_dirs",
			p = {{syscnf.pkg_download_dir}}
		},
		{
			f = "backend.collision_check",
			p = {
				test_status,
				{
					["pkg-rem"] = true,
					["pkg-name"] = true
				},
				{["pkg-name"] = {f = true}}
			}
		},
		{
			f = "journal.write",
			p = {journal.CHECKED, {["d2"] = true}, { }}
		},
		{
			f = "journal.write",
			p = {journal.CHANGELOG_START}
		},
		{
			f = "backend.pkg_merge_control",
			p = {"pkg_dir/control", "pkg-name", {f = true}}
		},
		{
			f = "backend.script_run",
			p = {"pkg-name", "preinst", true, "upgrade", "0"}
		},
		{
			f = "backend.pkg_merge_files",
			p = {"pkg_dir/data", {d = true}, {f = true}, {c = "12345678901234567890123456789012"}}
		},
		{
			f = "backend.pkg_config_info",
			p = {"remconf", { remconf = "12345678901234567890123456789012" } }
		},
		{
			f = "backend.script_run",
			p = {"pkg-rem", "prerm", false, "remove"}
		},
		{
			f = "journal.write",
			p = {
				journal.MOVED,
				{
					["pkg-name"] = {
						Conffiles = { c = "1234567890123456" },
						Package = "pkg-name",
						Version = "1",
						files = { f = true },
						Status = {"install", "user", "installed"}
					}
				},
				{},
				{c = "12345678901234567890123456789012", remconf = "12345678901234567890123456789012"},
				{ ["pkg-name"]=true }
			}
		},
		{
			f = "backend.pkg_cleanup_files",
			p = {{d2 = true}, {c = "12345678901234567890123456789012", remconf = "12345678901234567890123456789012"}}
		},
		{
			f = "backend.script_run",
			p = {"pkg-name", "postinst", true, "configure"}
		},
		{
			f = "backend.script_run",
			p = {"pkg-rem", "postrm", false, "remove"}
		},
		{
			f = "journal.write",
			p = {
				journal.SCRIPTS,
				{
					["pkg-name"] = {
						Conffiles = { c = "1234567890123456" },
						Package = "pkg-name",
						Version = "1",
						files = { f = true },
						Status = {"install", "user", "installed"}
					}
				},
				{ ["pkg-name"] = { ["postinst"] = "Fake failed postinst" } }
			}
		},
		{
			f = "journal.write",
			p = {journal.CHANGELOG_END}
		},
	}, outro({"pkg_dir"}, status_mod))
	assert_table_equal(expected, mocks_called)
end

-- Test if it stops when it finds collisions
function test_perform_collision()
	mocks_install()
	mock_gen("backend.collision_check", function () return {f = {["<pkg1name>"] = "new", ["<pkg2name>"] = "new", ["other"] = "existing"}}, {} end)
	mock_gen("backend.pkg_unpack", function (data) return data:gsub("file", "dir") end)
	mock_gen("backend.pkg_examine", function (dir) return {f = true}, {d = true}, {c = "1234567890123456"}, {Package = dir:gsub("dir", "name")} end)
	local ok, err = pcall(T.perform, {
		{
			op = "install",
			file = "<pkg1file>"
		},
		{
			op = "install",
			file = "<pkg2file>"
		}
	})
	assert_false(ok)
	-- We can't really check for equality, because the order is not guaranteed. So we check for some snippets
	assert(err:match("Collisions"))
	assert(err:match("f: .*other %(existing%)"))
	local expected = tables_join(intro, {
		{
			f = "backend.pkg_unpack",
			p = {"<pkg1file>"}
		},
		{
			f = "backend.pkg_examine",
			p = {"<pkg1dir>"}
		},
		{
			f = "backend.pkg_unpack",
			p = {"<pkg2file>"}
		},
		{
			f = "backend.pkg_examine",
			p = {"<pkg2dir>"}
		},
		{
			f = "journal.write",
			p = {
				journal.UNPACKED,
				{["<pkg1name>"] = true, ["<pkg2name>"] = true},
				{["<pkg1name>"] = { f = true }, ["<pkg2name>"] = { f = true }},
				{
					{
						configs = { c = "1234567890123456" },
						control = { Package = "<pkg1name>" },
						dir = "<pkg1dir>",
						dirs = { d = true },
						files = { f = true },
						op = "install",
						old_configs = { c = "1234567890123456" },
					},
					{
						configs = { c = "1234567890123456" },
						control = { Package = "<pkg2name>" },
						dir = "<pkg2dir>",
						dirs = { d = true },
						files = { f = true },
						op = "install",
						old_configs = { c = "1234567890123456" },
					}
				},
				{"<pkg1dir>", "<pkg2dir>"},
				{}
			}
		},
		{
			f = "utils.cleanup_dirs",
			p = {{syscnf.pkg_download_dir}}
		},
		{
			f = "backend.collision_check",
			p = {
				test_status,
				{
					["<pkg1name>"] = true,
					["<pkg2name>"] = true
				},
				{
					["<pkg1name>"] = {f = true},
					["<pkg2name>"] = {f = true}
				}
			}
		},
		{
			f = "utils.cleanup_dirs",
			p = {{"<pkg1dir>", "<pkg2dir>"}}
		},
		{
			f = "journal.finish",
			p = {}
		}
	})
	assert_table_equal(expected, mocks_called)
end

--[[
Test the journal recovery.

In this case, the journal contains nothing useful, because it's been
interrupted before even unpacking the packages.

Therefore, the recovery actioun would actually only close the journal
properly, since there's been nothing to recover.
]]
function test_recover_early()
	mocks_install()
	-- The journal contains only the start, not even unpack, therefore there's nothing to restore
	mock_gen("journal.recover", function ()
		return {
			{ type = journal.START, params = {} }
		}
	end)
	assert_table_equal({}, transaction.recover())
	assert_table_equal({
		{ f = "backend.run_state", p = {} },
		{ f = "journal.recover", p = {} },
		{ f = "journal.finish", p = {} },
		{ f = "backend.run_state.release", p = {} }
	}, mocks_called)
end

--[[
Test the journal recovery.

The transaction is almost complete, so nothing from the middle shall be called. Only
the missing cleanup is run.
]]
function test_recover_late()
	mocks_install()
	mock_gen("journal.recover", function ()
		return {
			{ type = journal.START, params = {} },
			{ type = journal.UNPACKED, params = {
				{["pkg-name"] = true, ["pkg-rem"] = true},
				{["pkg-name"] = { f = true } },
				{
					{
						configs = { c = "1234567890123456" },
						control = {
							Conffiles = { c = "1234567890123456" },
							Package = "pkg-name",
							Version = "1",
							files = { f = true },
							Status = {"install", "user", "instatalled"}
						},
						dir = "pkg_dir",
						dirs = { d = true },
						files = { f = true },
						op = "install"
					},
					{ name = "pkg-rem", op = "remove" }
				},
				{"pkg_dir"},
				{}
			} },
			{ type = journal.CHECKED, params = { {["d2"] = true} } },
			{ type = journal.CHANGELOG_START, params = {} },
			{ type = journal.MOVED, params = {
				{
					["pkg-name"] = {
						Conffiles = { c = "1234567890123456" },
						Package = "pkg-name",
						Version = "1",
						files = { f = true },
						Status = {"install", "user", "installed"}
					},
					["pkg-rem"] = {
						Package = "pkg-rem"
					}
				},
				{}
			} },
			{ type = journal.SCRIPTS, params = {
				{
					["pkg-name"] = {
						Conffiles = { c = "1234567890123456" },
						Package = "pkg-name",
						Version = "1",
						files = { f = true },
						Status = {"install", "user", "installed"}
					}
				},
				{ ["pkg-name"] = { ["postinst"] = "Fake failed postinst" } }
			} },
			{ type = journal.CHANGELOG_END, params = {} },
		}
	end)
	assert_table_equal({
		["pkg-name"] = {
			["postinst"] = "Fake failed postinst"
		}
	}, transaction.recover())
	local status_mod = utils.clone(test_status)
	status_mod["pkg-name"] = {
		Package = "pkg-name",
		Conffiles = { c = "1234567890123456" },
		Version = "1",
		files = { f = true },
		Status = {"install", "user", "installed"}
	}
	status_mod["pkg-rem"] = nil
	local intro_mod = utils.clone(intro)
	intro_mod[2].f = "journal.recover"
	intro_mod[4] = {f = "utils.cleanup_dirs", p = {{syscnf.pkg_download_dir}}}
	local expected = tables_join(intro_mod, outro({"pkg_dir"}, status_mod))
	assert_table_equal(expected, mocks_called)
end

function teardown()
	-- A trick to clean up the queue
	mock_gen('transaction.perform', function () return {} end)
	transaction.perform_queue()
	mocks_reset()
end
