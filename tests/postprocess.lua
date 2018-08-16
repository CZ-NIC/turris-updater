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

local requests = require "requests"
local postprocess = require "postprocess"
local utils = require "utils"
local uri = require "uri"
require "syscnf"

syscnf.set_root_dir()
local dir = (os.getenv("S") .. "/") or ''

module("postprocess-tests", package.seeall, lunit.testcase)

local function repo_fake(name, uri, ok, content)
	local result =  {
		tp = "repository",
		name = name,
		repo_uri = uri
	}
	utils.private(result).index_uri = {
		[""] = {
			tp = "uri",
			uri = uri .. "/Packages",
			cback = function(self, cback)
				cback(ok, content)
			end,
			events = {}
		}
	}
	return result
end

local example_output = {
	{
		content = {
			[""] = {
				list = {
					["6in4"] = {
						Architecture = "all",
						Depends = "libc, kmod-sit",
						Description = [[Provides support for 6in4 tunnels in /etc/config/network.
 Refer to http://wiki.openwrt.org/doc/uci/network for
 configuration details.]],
						Filename = "6in4_21-2_all.ipk",
						["Installed-Size"] = "1558",
						License = "GPL-2.0",
						MD5Sum = "a2a58a05c002cf7b45fbe364794d96a5",
						Maintainer = "Jo-Philipp Wich <xm@subsignal.org>",
						Package = "6in4",
						SHA256sum = "06c3e5630a54a6c2d95ff13945b76e4122ac1a9e533fe4665c501ae26d55933d",
						Section = "net",
						Size = "2534",
						Source = "package/network/ipv6/6in4",
						Version = "21-2",
						uri_raw = "http://example.org/test1/6in4_21-2_all.ipk"
					},
					["6rd"] = {
						Architecture = "all",
						Depends = "libc, kmod-sit",
						Description = [[Provides support for 6rd tunnels in /etc/config/network.
 Refer to http://wiki.openwrt.org/doc/uci/network for
 configuration details.]],
						Filename = "6rd_9-2_all.ipk",
						["Installed-Size"] = "3432",
						License = "GPL-2.0",
						MD5Sum = "2b46cba96c887754f879676be77615e5",
						Maintainer = "Steven Barth <cyrus@openwrt.org>",
						Package = "6rd",
						SHA256sum = "e1081e495d0055f962a0ea4710239447eabf596f7acb06ccf0bd6f06b125fda8",
						Section = "net",
						Size = "4416",
						Source = "package/network/ipv6/6rd",
						Version = "9-2",
						uri_raw = "http://example.org/test1/6rd_9-2_all.ipk"
					}
				},
				tp="pkg-list"
			}
		},
		name="test1",
		repo_uri="http://example.org/test1",
		tp="parsed-repository"
	}
}
example_output[1].content[""].list["6in4"].repo = example_output[1]
example_output[1].content[""].list["6rd"].repo = example_output[1]

function test_get_repos_plain()
	requests.known_repositories_all = {
		repo_fake("test1", "http://example.org/test1", true, [[
Package: 6in4
Version: 21-2
Depends: libc, kmod-sit
Source: package/network/ipv6/6in4
License: GPL-2.0
Section: net
Maintainer: Jo-Philipp Wich <xm@subsignal.org>
Architecture: all
Installed-Size: 1558
Filename: 6in4_21-2_all.ipk
Size: 2534
MD5Sum: a2a58a05c002cf7b45fbe364794d96a5
SHA256sum: 06c3e5630a54a6c2d95ff13945b76e4122ac1a9e533fe4665c501ae26d55933d
Description:  Provides support for 6in4 tunnels in /etc/config/network.
 Refer to http://wiki.openwrt.org/doc/uci/network for
 configuration details.

Package: 6rd
Version: 9-2
Depends: libc, kmod-sit
Source: package/network/ipv6/6rd
License: GPL-2.0
Section: net
Maintainer: Steven Barth <cyrus@openwrt.org>
Architecture: all
Installed-Size: 3432
Filename: 6rd_9-2_all.ipk
Size: 4416
MD5Sum: 2b46cba96c887754f879676be77615e5
SHA256sum: e1081e495d0055f962a0ea4710239447eabf596f7acb06ccf0bd6f06b125fda8
Description:  Provides support for 6rd tunnels in /etc/config/network.
 Refer to http://wiki.openwrt.org/doc/uci/network for
 configuration details.
]])
	}
	assert_nil(postprocess.get_repos())
	assert_table_equal(example_output, requests.known_repositories_all)
end

function test_get_repos_gzip()
	local datadir = (os.getenv("S") or ".") .. "/tests/data"
	local content = utils.read_file(datadir .. "/Packages.gz")
	requests.known_repositories_all = {repo_fake("test1", "http://example.org/test1", true, content)}
	assert_nil(postprocess.get_repos())
	assert_table_equal(example_output, requests.known_repositories_all)
end

local multierror = utils.exception("multiple", "Multiple exceptions (1)")
local sub_err = utils.exception("unreachable", "Fake network is down")
sub_err.why = "missing"
sub_err.repo = "test1/http://example.org/test1/Packages"
multierror.errors = {sub_err}

function test_get_repos_broken_fatal()
	-- When we can't download the thing, it throws
	requests.known_repositories_all = {repo_fake("test1", "http://example.org/test1", false, utils.exception("unreachable", "Fake network is down"))}
	local ok, err = pcall(postprocess.get_repos)
	assert_false(ok)
	assert_table_equal(multierror, err)
end

function test_get_repos_broken_nonfatal()
	requests.known_repositories_all = {repo_fake("test1", "http://example.org/test1", false, utils.exception("unreachable", "Fake network is down"))}
	requests.known_repositories_all[1].ignore = {"missing"}
	assert_table_equal(multierror, postprocess.get_repos())
	assert_table_equal({
		{
			content = {
				[""] = sub_err
			},
			ignore = {"missing"},
			name = "test1",
			repo_uri = "http://example.org/test1",
			tp = "parsed-repository"
		}
	}, requests.known_repositories_all)
end

function test_get_content_pkgs()
	local context = sandbox.new("Local")
	requests.known_content_packages = {{name="updater"}}
	utils.private(requests.known_content_packages[1]).content_uri = uri(context, "file://" .. dir .. "tests/data/updater.ipk")
	local expect = {
		{
			name = "updater",
			candidate = {
				Status = {"install", "user", "installed"},
				Package = "updater",
				Version = "129",
				Depends = "libc, vixie-cron, openssl-util, libatsha204, curl, cert-backup, opkg, bzip2, cznic-cacert-bundle",
				Source = "feeds/turrispackages/cznic/updater",
				Section = "opt",
				Maintainer = "Michal Vaner <michal.vaner@nic.cz>",
				Architecture = "mpc85xx",
				Description = "updater",
				["Installed-Size"] = "14773",
				Conffiles = {["/etc/config/updater"] = "b5cf279732a87011eadfe522a0c163b98682bef2919afc4f96330f9f103a3230"},
				files = {
					["/usr/bin/updater-unstuck.sh"] = true,
					["/etc/config/updater"] = true,
					["/usr/share/updater/keys/standby.pem"] = true,
					["/etc/ssl/updater.pem"] = true,
					["/usr/bin/updater.sh"] = true,
					["/usr/bin/updater-wipe.sh"] = true,
					["/usr/bin/updater-utils.sh"] = true,
					["/etc/init.d/updater"] = true,
					["/usr/bin/updater-worker.sh"] = true,
					["/usr/share/updater/keys/release.pem"] = true,
					["/usr/bin/updater-resume.sh"] = true,
				}
			}
		}
	}
	expect[1].candidate.pkg = expect[1]
	postprocess.get_content_pkgs()
	-- set data to nil. We can't check that easily enough. Same goes for Installed-time
	requests.known_content_packages[1].candidate.data = nil
	requests.known_content_packages[1].candidate["Installed-Time"] = nil
	assert_table_equal(expect, requests.known_content_packages)

end

-- Lest break things and expect exception
function test_get_content_pkgs_missing()
	local context = sandbox.new("Local")
	requests.known_content_packages = {{name="updater"}}
	utils.private(requests.known_content_packages[1]).content_uri = uri(context, "file://" .. dir .. "tests/data/nonexistent.ipk")
	assert_exception(function() postprocess.get_content_pkgs() end, "multiple")
end

-- Lest break things but ignore it
function test_get_content_pkgs_missing_ignore()
	local context = sandbox.new("Local")
	requests.known_content_packages = {{name="updater", ignore={"content"}}}
	utils.private(requests.known_content_packages[1]).content_uri = uri(context, "file://" .. dir .. "tests/data/nonexistent.ipk")
	postprocess.get_content_pkgs()
	assert_table_equal({{name="updater", ignore={"content"}}}, requests.known_content_packages)
end

-- Default values for package modifiers to be added if they are not mentioned
local modifier_def = {
	tp = "package",
	abi_change = {},
	abi_change_deep = {},
	order_after = {},
	order_before = {},
	post_install = {},
	post_remove = {},
	pre_install = {},
	pre_remove = {},
	reboot = false,
	replan = false
}

-- Common tasks used in pkg_merge tests for building data structures
local function common_pkg_merge(exp)
	-- Add repo field
	for _, repo in pairs(requests.known_repositories_all) do
		for _, cont in pairs(repo.content) do
			if cont.tp == 'pkg-list' then
				for _, pkg in pairs(cont.list) do
					pkg.repo = repo
				end
			end
		end
	end
	-- Fill in default values for the ones that are not mentioned above
	for _, pkg in pairs(exp) do
		for name, def in pairs(modifier_def) do
			if pkg.modifier[name] == nil then
				pkg.modifier[name] = def
			end
		end
	end
end

function test_pkg_merge()
	requests.known_repositories_all = {
		{
			content = {
				[""] = {
					tp = 'pkg-list',
					list = {
						xyz = {Package = "xyz", Version = "1"},
						abc = {Package = "abc", Version = "2", Depends = "cde", Conflicts = "xyz"},
						cde = {Package = "cde", Version = "1"},
						fgh = {Package = "fgh", Version = "1", Provides = "cde"}
					}
				}
			}
		},
		{
			content = {
				a = {
					tp = 'pkg-list',
					list = {
						abc = {Package = "abc", Version = "1"}
					}
				},
				b = {
					tp = 'pkg-list',
					list = {
						another = {Package = "another", Version = "4"}
					}
				},
				c = utils.exception("Just an exception", "Just an exception")
			}
		}
	}
	requests.known_packages = {
		{
			tp = 'package',
			order_after = "abc",
			name = 'xyz',
			reboot = 'finished',
			abi_change = {'another'},
			deps = "abc"
		},
		{
			tp = 'package',
			name = 'xyz',
			replan = true,
			abi_change = true,
			deps = {"another", "xyz"}
		},
		{
			tp = 'package',
			name = 'virt',
			virtual = true,
			deps = {"xyz", "abc"}
		}
	}
	-- Build the expected data structure
	local exp = {
		abc = {
			candidates = {
				{Package = "abc", Depends = "cde", Conflicts = "xyz", Version = "2", deps = {
						tp = "dep-and", sub = { "cde", { tp = "dep-not", sub = {
							{ tp = "dep-package", name = "xyz", version = "~.*" }
					}}}}, repo = requests.known_repositories_all[1]},
				{Package = "abc", Version = "1", repo = requests.known_repositories_all[2]}
			},
			modifier = {name = "abc"}
		},
		cde = {
			candidates = {
				{Package = "cde", Version = "1", repo = requests.known_repositories_all[1]},
				{Package = "fgh", Version = "1", Provides = "cde", repo = requests.known_repositories_all[1]}
			},
			modifier = {name = "cde"}
		},
		fgh = {
			candidates = {{Package = "fgh", Version = "1", Provides = "cde", repo = requests.known_repositories_all[1]}},
			modifier = {name = "fgh"}
		},
		another = {
			candidates = {{Package = "another", Version = "4", repo = requests.known_repositories_all[2]}},
			modifier = {name = "another"}
		},
		virt = {
			candidates = {},
			modifier = {
				name = "virt",
				deps = {tp = "dep-and", sub = {"xyz", "abc"}},
				virtual = true
			},
		},
		xyz = {
			candidates = {{Package = "xyz", Version = "1", repo = requests.known_repositories_all[1]}},
			modifier = {
				name = "xyz",
				order_after = {abc = true},
				deps = {
					tp = 'dep-and',
					sub = {"abc", "another", "xyz"}
				},
				reboot = "finished",
				abi_change = {[true] = true, ['another'] = true},
				replan = "immediate"
			}
		}
	}
	common_pkg_merge(exp)
	postprocess.pkg_aggregate()
	assert_table_equal(exp, postprocess.available_packages)
end

function test_pkg_merge_virtual()
	requests.known_repositories_all = {
		{
			content = {
				[""] = {
					tp = 'pkg-list',
					list = {
						pkg = {Package = "pkg", Version = "1", Provides="virt"}
					}
				}
			}
		}
	}
	requests.known_packages = {
		{
			tp = 'package',
			name = 'virt',
			virtual = true
		}
	}
	-- Build the expected data structure
	local exp = {
		pkg = {
			candidates = {{Package = "pkg", Version = "1", Provides="virt", repo = requests.known_repositories_all[1]}},
			modifier = {name = "pkg"}
		},
		virt = {
			candidates = {{Package = "pkg", Version = "1", Provides="virt", repo = requests.known_repositories_all[1]}},
			modifier = {
				name = "virt",
				virtual = true
			},
		}
	}
	common_pkg_merge(exp)
	postprocess.pkg_aggregate()
	assert_table_equal(exp, postprocess.available_packages)
end

function test_pkg_merge_virtual_with_candidate()
	requests.known_repositories_all = {
		{
			content = {
				[""] = {
					tp = 'pkg-list',
					list = {
						pkg = {Package = "pkg", Version = "1", Provides="virt"},
						virt = {Package = "virt", Version = "2"}
					}
				}
			}
		}
	}
	requests.known_packages = {
		{
			tp = 'package',
			name = 'virt',
			virtual = true
		}
	}
	common_pkg_merge({})
	assert_exception(function() postprocess.pkg_aggregate() end, "inconsistent")
end

--[[
Test we handle when a package has a candidate from a repository and from local content.
The local one should be preferred.
]]
function test_local_and_repo()
	requests.known_repositories_all = {
		{
			content = {
				[""] = {
					tp = 'pkg-list',
					list = {
						xyz = {Package = "xyz", Version = "2"},
					}
				}
			},
			priority = 50,
			name = 'repo1',
			sequence = 1
		}
	}
	requests.known_repositories_all[1].content[""].list.xyz.repo = requests.known_repositories_all[1]
	requests.known_packages = {
		xyz = {
			tp = 'package',
			name = 'xyz',
			priority = 60,
			candidate = {Package = "xyz", Version = "1", local_mark = true}, -- Mark the package so we recognize it got through unmodified
			content = "dummy-content"
		}
	}
	requests.known_packages.xyz.candidate.pkg = requests.known_packages.xyz
	postprocess.pkg_aggregate()
	local exp = {
		xyz = {
			candidates = {
				{Package = "xyz", Version = "1", local_mark = true, pkg = requests.known_packages.xyz},
				{Package = "xyz", Version = "2", repo = requests.known_repositories_all[1]}
			},
			modifier = {
				name = "xyz",
				abi_change = {},
				abi_change_deep = {},
				order_after = {},
				order_before = {},
				post_install = {},
				post_remove = {},
				pre_install = {},
				pre_remove = {},
				reboot = false,
				replan = false,
				tp = "package"
			}
		}
	}
	assert_table_equal(exp, postprocess.available_packages)
	-- Try again, but with the same priority ‒ versions should be used to sort them
	postprocess.available_packages = {}
	requests.known_packages.xyz.priority = nil
	exp.xyz.candidates = {exp.xyz.candidates[2], exp.xyz.candidates[1]}
	postprocess.pkg_aggregate()
	assert_table_equal(exp, postprocess.available_packages)
end

function test_deps_canon()
	assert_equal(nil, postprocess.deps_canon(nil))
	assert_equal(nil, postprocess.deps_canon({}))
	assert_equal(nil, postprocess.deps_canon(""))
	assert_equal("x", postprocess.deps_canon("x"))
	assert_equal("x", postprocess.deps_canon(" x "))
	assert_equal("x", postprocess.deps_canon({"x"}))
	assert_equal("x", postprocess.deps_canon({"x", ""}))
	assert_equal("x", postprocess.deps_canon({tp = 'dep-and', sub = {"x"}}))
	assert_equal("x", postprocess.deps_canon({tp = 'dep-or', sub = {"x"}}))
	assert_equal("x", postprocess.deps_canon("x ( )"))
	assert_table_equal({tp = "dep-package", name = "x", version = ">1.25"}, postprocess.deps_canon("x (>1.25)"))
	assert_table_equal({tp = "dep-package", name = "x", version = ">v_12"}, postprocess.deps_canon("x ( >v_12)"))
	assert_table_equal({tp = "dep-not", sub = {"x"}}, postprocess.deps_canon({tp = 'dep-not', sub = {"x"}}))
	assert_table_equal({tp = "dep-and", sub = {"x", "y"}}, postprocess.deps_canon("x, y"))
	assert_table_equal({tp = "dep-and", sub = {"x", "y"}}, postprocess.deps_canon({"x, y"}))
	assert_table_equal({tp = "dep-and", sub = {"x", "y"}}, postprocess.deps_canon({"x", "y"}))
	assert_table_equal({tp = "dep-and", sub = {"x", "y"}}, postprocess.deps_canon({"x", {"y "}}))
	assert_table_equal({tp = "dep-and", sub = {"x", "y"}}, postprocess.deps_canon({"x", {tp = 'dep-and', sub = {"y "}}}))
	assert_table_equal({tp = "dep-or", sub = {"x", "y"}}, postprocess.deps_canon({tp = "dep-or", sub = {"x", {tp = 'dep-or', sub = {"y"}}}}))
	assert_table_equal({tp = "dep-or", sub = {"x", {tp = "dep-or", sub = {"y", "z"}}}}, postprocess.deps_canon({tp = "dep-or", sub = {"x", {tp = 'dep-or', sub = {"y ", "z"}}}}))
	assert_table_equal({tp = "dep-or", sub = {"x", {tp = "dep-and", sub = {"y", "z"}}}}, postprocess.deps_canon({tp = "dep-or", sub = {"x", {"y", "z"}}}))
	assert_table_equal({tp = "package", a = "b"}, postprocess.deps_canon({tp = "package", a = "b"}))
	assert_table_equal({tp = "dep-package", a = "b"}, postprocess.deps_canon({tp = "dep-package", a = "b"}))
end

function test_conflicts_canon()
	assert_equal(nil, postprocess.conflicts_canon(nil))
	assert_equal(nil, postprocess.conflicts_canon(""))
	assert_equal(nil, postprocess.conflicts_canon(" "))
	assert_table_equal({ tp = "dep-not", sub = {
			{ tp = "dep-package", name = "x", version = "~.*" }
		}}, postprocess.conflicts_canon("x"))
	assert_table_equal({ tp = "dep-not", sub = {
			{ tp = "dep-package", name = "x", version = "~.*" }
		}}, postprocess.conflicts_canon(" x "))
	assert_table_equal({ tp = "dep-not", sub = {{ tp = "dep-or", sub = {
			{ tp = "dep-package", name = "x", version = "~.*" },
			{ tp = "dep-package", name = "y", version = "~.*" }
		}}}}, postprocess.conflicts_canon("x, y"))
	assert_table_equal({ tp = "dep-not", sub = {{ tp = "dep-or", sub = {
			{ tp = "dep-package", name = "x", version = ">1" },
			{ tp = "dep-package", name = "y", version = "~.*" }
		}}}}, postprocess.conflicts_canon("x (>1), y"))
end

function test_sort_candidates()
	local candidates = {
		{ Package = "pkg", Version = "3", repo = { name = "main", priority = 50 } },
		{ Package = "nopkg", Version = "1", repo = { name = "main", priority = 50 } },
		{ Package = "pkg", Version = "1", repo = { name = "main", priority = 50 } },
		{ Package = "nopkg", Version = "2", repo = { name = "main", priority = 50 } },
		{ Package = "pkg", Version = "2", repo = { name = "main", priority = 50 } },
		{ Package = "pkg", Version = "1", repo = { name = "other", priority = 60 } },
		{ Package = "nopkg", Version = "1", repo = { name = "other", priority = 60 } },
		{ Package = "pkg", Version = "2", repo = { name = "other", priority = 60 } },
	}
	local result = {
		candidates[8],
		candidates[6],
		candidates[7],
		candidates[1],
		candidates[5],
		candidates[3],
		candidates[4],
		candidates[2],
	}
	postprocess.sort_candidates("pkg", candidates)
	assert_table_equal(candidates, result)
end

function teardown()
	requests.known_repositories_all = {}
	requests.known_packages = {}
	postprocess.available_packages = {}
end
