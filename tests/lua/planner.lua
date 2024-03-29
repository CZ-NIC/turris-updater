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

local planner = require 'planner'
local requests = require 'requests'
local utils = require "utils"

module("planner-tests", package.seeall, lunit.testcase)

local def_repo = {priority = 50, serial = 1}

--[[
Test installation plan generation when there are no
dependencies.
]]
function test_no_deps()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			priority = 50
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2'
			},
			priority = 50
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = "require",
			package = {Package = 'pkg1', repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg1"
		},
		pkg2 = {
			action = "require",
			package = {Package = 'pkg2', repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg2"
		}
	}
	assert_plan_dep_order(expected, result)
end

function test_reinstall()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1'
			},
			reinstall = true,
			priority = 50
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		{
			action = "reinstall",
			package = {Package = 'pkg1', repo = def_repo},
			modifier = {},
			critical = false,
			name = 'pkg1'
		}
	}
	assert_table_equal(expected, result)
end

-- Test the reinstall flag works even when we „require“ the package first (this was broken before)
function test_reinstall_upgrade()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			-- First ask for it to get installed. That one schedules it as „require"
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1'
			},
			priority = 50
		},
		{
			-- Just a package in the middle, so we are sure the following reschedule doesn't reorder things.
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2'
			},
			priority = 50
		},
		{
			-- Second instance with reinstall. That one should reschedule it as „reinstall“
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1'
			},
			reinstall = true,
			priority = 50
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = "reinstall",
			package = {Package = 'pkg1', repo = def_repo},
			modifier = {},
			critical = false,
			name = 'pkg1'
		},
		pkg2 = {
			action = "require",
			package = {Package = 'pkg2', repo = def_repo},
			modifier = {},
			critical = false,
			name = 'pkg2'
		}
	}
	-- We don't care about order, just compare that expected packages are there in expected form
	assert_table_equal(expected, utils.map(result, function(_, v)
				return v.name, v
			end
		))
end

--[[
Find some deps. Some are from the modifier, some from the candidate.
There may be multiple candidates. Also, check each dep is brought in
just once.
]]
function test_deps()
	local pkgs = {
		dep1 = {
			candidates = {
				{Package = 'dep1', Version = "2", repo = def_repo},
				{Package = 'dep1', Version = "1", repo = def_repo}
			},
			modifier = {}
		},
		dep2 = {
			candidates = {{Package = 'dep2', repo = def_repo}},
			modifier = {}
		},
		dep3 = {
			candidates = {{Package = 'dep3', repo = def_repo}},
			modifier = {
				deps = "dep1"
			}
		},
		unused = {
			candidates = {{Package = 'unused', repo = def_repo}},
			modifier = {
				deps = "dep1"
			}
		},
		pkg1 = {
			candidates = {{Package = 'pkg1', repo = def_repo}},
			modifier = {
				deps = "dep1"
			}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', deps = {tp = 'dep-and', sub = {'dep2', 'dep3'}}, repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			priority = 50,
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		dep1 = {
			action = 'require',
			package = {Package = 'dep1', Version = "2", repo = def_repo},
			modifier = {},
			critical = false,
			name = 'dep1'
		},
		dep2 = {
			action = 'require',
			package = {Package = 'dep2', repo = def_repo},
			modifier = {},
			critical = false,
			name = 'dep2'
		},
		dep3 = {
			action = 'require',
			package = {Package = 'dep3', repo = def_repo},
			modifier = {
				deps = "dep1"
			},
			critical = false,
			name = 'dep3'
		},
		pkg1 = {
			action = 'require',
			package = {Package = 'pkg1', repo = def_repo},
			modifier = {
				deps = "dep1"
			},
			critical = false,
			name = 'pkg1'
		},
		pkg2 = {
			action = 'require',
			package = {Package = 'pkg2', deps = {tp = 'dep-and', sub = {'dep2', 'dep3'}}, repo = def_repo},
			modifier = {},
			critical = false,
			name = 'pkg2'
		}
	}
	assert_plan_dep_order(expected, result)
end

--[[
A dependency doesn't exist. It should fail.
]]
function test_missing_dep()
	local pkgs = {
		pkg = {
			candidates = {{Package = 'pkg', deps = 'nothere', repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg',
			},
			priority = 50,
		}
	}
	assert_exception(function () planner.required_pkgs(pkgs, requests) end, 'inconsistent')
end

--[[
We shouldn't fail if we don't have repository set, because then it is package
content set in configuration.
]]
function test_content_version()
	local pkgs = {
		pkg = {
			candidates = {
				{Package = 'pkg', Version = "2"},
				{Package = 'pkg', Version = "1"}
			},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg',
			},
			priority = 50,
			version = '=1'
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg = {
			action = 'require',
			package = {Package = 'pkg', Version = "1"},
			modifier = {},
			critical = false,
			name = 'pkg'
		}
	}
	assert_plan_dep_order(expected, result)
end

function test_virtual()
	local pkgs = {
		virt1 = {
			modifier = {
				virtual = true,
				deps = "pkg"
			}
		},
		virt2 = {
			modifier = {
				virtual = true
			}
		},
		pkg = {
			candidates = {{Package = 'pkg', Version = "1", deps = 'virt2', repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'virt1',
			},
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg = {
			action = 'require',
			package = {Package = 'pkg', Version = "1", deps = "virt2", repo = def_repo},
			modifier = {},
			critical = false,
			name = 'pkg'
		}
	}
	assert_plan_dep_order(expected, result)
end

function test_virtual_version()
	local pkgs = {
		virt = {
			modifier = {
				virtual = true,
				deps = "pkg"
			}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'virt',
				version = '1'
			},
			priority = 50,
		}
	}
	assert_exception(function () planner.required_pkgs(pkgs, requests) end, 'inconsistent')
end

function test_virtual_missing()
	local pkgs = {
		virt1 = {
			modifier = {
				virtual = true,
				deps = "pkg"
			}
		},
		virt2 = {
			modifier = {
				virtual = true
			}
		},
		pkg = {
			candidates = {{Package = 'pkg', Version = "1", deps = 'virt2', repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'virt1',
			},
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg = {
			action = 'require',
			package = {Package = 'pkg', Version = "1", deps = "virt2", repo = def_repo},
			modifier = {},
			critical = false,
			name = 'pkg'
		}
	}
	assert_plan_dep_order(expected, result)
end


-- Let's check that if we are ignore missing package that we won't plan it's dependencies
function test_missing_ignore_deps()
	local pkgs = {
		pkg = {
			candidates = {},
			modifier = {
				deps = 'dep'
			}
		},
		dep = {
			candidate = {{Package = 'dep', repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg',
			},
			optional = true,
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	assert_plan_dep_order({}, result)
end

-- It is able to solve a circular dependency and doesn't stack overflow
function test_circular_deps()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = 'pkg2', repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', repo = def_repo}},
			modifier = {
				deps = "pkg1"
			}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1'
			},
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = 'require',
			package = {Package = 'pkg1', deps = 'pkg2', repo = def_repo},
			modifier = {},
			critical = false,
			name = 'pkg1'
		},
		pkg2 = {
			action = 'require',
			package = {Package = 'pkg2', repo = def_repo},
			modifier = {
				deps = "pkg1"
			},
			critical = false,
			name = 'pkg2'
		}
	}
	-- We don't care about order, just compare that expected packages are there in expected form
	assert_table_equal(expected, utils.map(result, function(_, v)
				return v.name, v
			end
		))
end

-- It is able to detect a circular dependency for critival package
function test_circular_deps_critical()
	local pkgs = {
		critic = {
			candidates = {{Package = 'critic', deps = 'pkg', repo = def_repo}},
			modifier = { }
		},
		pkg = {
			candidates = {{Package = 'pkg', repo = def_repo}},
			modifier = {
				deps = "critic"
			}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'critic'
			},
			critical = true
		}
	}
	assert_exception(function () planner.required_pkgs(pkgs, requests) end, 'inconsistent', nil, { critical = true })
end

function test_priority()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = {}, repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			priority = 70
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			reinstall = true,
			priority = 50,
		},
		{
			tp = 'uninstall',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			priority = 40
		},
		{
			tp = 'uninstall',
			package = {
				tp = 'package',
				name = 'pkg2'
			},
			priority = 50,
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2'
			},
			priority = 20
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = "reinstall",
			package = {Package = 'pkg1', deps = {}, repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg1"
		}
	}
	assert_plan_dep_order(expected, result)
end

-- Test situation when there are candidates from other packages group added using "Provides" and requested package has no candidate on its own
function test_provides_only()
	local pkgs = {
		req = {
			candidates = {}, -- candidates are added later, because we require them to be same table as in given package group
			modifier = {}
		},
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = {}, repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', repo = def_repo}},
			modifier = {}
		}
	}
	table.insert(pkgs.req.candidates, pkgs.pkg1.candidates[1])
	table.insert(pkgs.req.candidates, pkgs.pkg2.candidates[1])
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'req',
			},
			priority = 50
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = "require",
			package = {Package = 'pkg1', deps = {}, repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg1"
		}
	}
	assert_plan_dep_order(expected, result)
end

-- Test situation when there are candidates from other packages group added using "Provides", but requested packages has candidate on its own.
function test_provides()
	local pkgs = {
		req = {
			candidates = {{Package = 'req', deps = {}, repo = def_repo}}, -- other candidates are added later, because we require them to be same table as in given package group
			modifier = {}
		},
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = {}, repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', repo = def_repo}},
			modifier = {}
		}
	}
	table.insert(pkgs.req.candidates, pkgs.pkg1.candidates[1])
	table.insert(pkgs.req.candidates, pkgs.pkg2.candidates[1])
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'req',
			},
			priority = 50
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		req = {
			action = "require",
			package = {Package = 'req', deps = {}, repo = def_repo},
			modifier = {},
			critical = false,
			name = "req"
		}
	}
	assert_plan_dep_order(expected, result)
end

function test_provides_other_required()
	local pkgs = {
		req = {
			candidates = {{Package = 'req', deps = {}, repo = def_repo}}, -- other candidates are added later, because we require them to be same table as in given package group
			modifier = {}
		},
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = {}, repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', repo = def_repo}},
			modifier = {}
		}
	}
	table.insert(pkgs.req.candidates, pkgs.pkg1.candidates[1])
	table.insert(pkgs.req.candidates, pkgs.pkg2.candidates[1])
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'req',
			},
			priority = 50
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			priority = 50
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg2 = {
			action = "require",
			package = {Package = 'pkg2', repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg2"
		}
	}
	assert_plan_dep_order(expected, result)
end

function test_provides_critical()
	local pkgs = {
		req = {
			candidates = {{Package = 'req', deps = {}, repo = def_repo}}, -- other candidates are added later, because we require them to be same table as in given package group
			modifier = {}
		},
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = {}, repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', repo = def_repo}},
			modifier = {}
		}
	}
	table.insert(pkgs.req.candidates, pkgs.pkg1.candidates[1])
	table.insert(pkgs.req.candidates, pkgs.pkg2.candidates[1])
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'req',
			},
			critical = true,
			priority = 50
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			priority = 50
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg2 = {
			action = "require",
			package = {Package = 'pkg2', repo = def_repo},
			modifier = {},
			critical = true,
			name = "pkg2"
		}
	}
	assert_plan_dep_order(expected, result)
end

-- This checks that when we have two packages both providing same package and both
-- are going to be installed, than both should have critical when request is
-- critical.
-- Note: This is because we can't say which one of those packages is the one that
-- provides that package, so we do it that they are both threated the same way.
function test_provides_critical_both()
	local pkgs = {
		req = {
			candidates = {{Package = 'req', Version = "1", repo = def_repo}}, -- other candidates are added later, because we require them to be same table as in given package group
			modifier = {}
		},
		pkg1 = {
			candidates = {{Package = 'pkg1', Version = "1", repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', Version = "1", repo = def_repo}},
			modifier = {}
		}
	}
	table.insert(pkgs.req.candidates, pkgs.pkg1.candidates[1])
	table.insert(pkgs.req.candidates, pkgs.pkg2.candidates[1])
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'req'
			},
			critical = true,
			priority = 50
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			priority = 50
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			priority = 50
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = "require",
			package = {Package = 'pkg1', Version = "1", repo = def_repo},
			modifier = {},
			critical = true,
			name = "pkg1"
		},
		pkg2 = {
			action = "require",
			package = {Package = 'pkg2', Version = "1", repo = def_repo},
			modifier = {},
			critical = true,
			name = "pkg2"
		}
	}
	assert_plan_dep_order(expected, result)
	-- Let's also test that if we choose to install explicitly that specific package that then we really install it.
	-- We do it by using exact version match (note: version are compared only with candidates with the same name as package group)
	requests[1].version = "=1"
	local result = planner.required_pkgs(pkgs, requests)
	expected.pkg1.critical = false
	expected.pkg2.critical = false
	expected['req'] = {
		action = "require",
		package = {Package = 'req', Version = "1", repo = def_repo},
		modifier = {},
		critical = true,
		name = "req"
	}
	assert_plan_dep_order(expected, result)
end

function test_condition_false()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			condition = "pkg2",
			priority = 50
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {}
	assert_plan_dep_order(expected, result)
end

function test_condition_true()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			condition = "pkg2",
			priority = 50
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			priority = 50
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = "require",
			package = {Package = 'pkg1', repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg1"
		},
		pkg2 = {
			action = "require",
			package = {Package = 'pkg2', repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg2"
		}
	}
	assert_plan_dep_order(expected, result)
end

function test_condition_unistall_priority_higher()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = "dep", repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', repo = def_repo}},
			modifier = {}
		},
		dep = {
			candidates = {{Package = 'dep', repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			condition = "pkg2",
			priority = 50
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			priority = 50
		},
		{
			tp = 'uninstall',
			package = {
				tp = 'package',
				name = 'dep',
			},
			priority = 51
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg2 = {
			action = "require",
			package = {Package = 'pkg2', repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg2"
		}
	}
	assert_plan_dep_order(expected, result)
end

function test_condition_unistall_priority_lower()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = "dep", repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', repo = def_repo}},
			modifier = {}
		},
		dep = {
			candidates = {{Package = 'dep', repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			condition = "pkg2",
			priority = 50
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			priority = 50
		},
		{
			tp = 'uninstall',
			package = {
				tp = 'package',
				name = 'dep',
			},
			priority = 49
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = "require",
			package = {Package = 'pkg1', deps = "dep", repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg1"
		},
		pkg2 = {
			action = "require",
			package = {Package = 'pkg2', repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg2"
		},
		dep = {
			action = "require",
			package = {Package = 'dep', repo = def_repo},
			modifier = {},
			critical = false,
			name = "dep"
		}
	}
	assert_plan_dep_order(expected, result)
end

function test_dual_condition()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = "dep", repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', deps = "dep", repo = def_repo}},
			modifier = {}
		},
		dep = {
			candidates = {{Package = 'dep', repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			condition = "dep",
			priority = 50
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			condition = "dep",
			priority = 50
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'dep',
			},
			priority = 50
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = "require",
			package = {Package = 'pkg1', deps = "dep", repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg1"
		},
		pkg2 = {
			action = "require",
			package = {Package = 'pkg2', deps = "dep", repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg2"
		},
		dep = {
			action = "require",
			package = {Package = 'dep', repo = def_repo},
			modifier = {},
			critical = false,
			name = "dep"
		}
	}
	assert_plan_dep_order(expected, result)
end

--[[ Points about this test case:
This mirrors case we encountered on real package deployment. The problem we are
fasing is that we have two packages with same priority being conditional on same
package. One of them is requested for removal and that results in all packages
being removed.
]]
function test_dual_condition_unistall()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = "dep", repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', deps = "dep", repo = def_repo}},
			modifier = {}
		},
		dep = {
			candidates = {{Package = 'dep', repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			condition = "dep",
			priority = 50
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			condition = "dep",
			priority = 50
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'dep',
			},
			priority = 50
		},
		{
			tp = 'uninstall',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			priority = 51
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = "require",
			package = {Package = 'pkg1', deps = "dep", repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg1"
		},
		dep = {
			action = "require",
			package = {Package = 'dep', repo = def_repo},
			modifier = {},
			critical = false,
			name = "dep"
		}
	}
	assert_plan_dep_order(expected, result)
end

function test_request_unsat()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = {tp = 'dep-not', sub = {"pkg2"}}, repo = def_repo}},
			modifier = {deps = "dep"}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', deps = {}, repo = def_repo}},
			modifier = {}
		},
		dep = {
			candidates = {{Package = 'dep', deps = {}, repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			priority = 50,
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			priority = 50,
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'dep',
			},
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	-- We should chose pkg1 or pkg2. Not depending on chose of dep.
	local respkgs = utils.map(result, function(_, val) return val.name, true end)
	local expected = {
		dep = {
			action = "require",
			package = {Package = 'dep', deps = {}, repo = def_repo},
			modifier = {},
			critical = false,
			name = "dep"
		}
	}
	if respkgs.pkg1 then
		expected.pkg1 = {
			action = "require",
			package = {Package = 'pkg1', deps = {tp = 'dep-not', sub = {"pkg2"}}, repo = def_repo},
			modifier = {deps = 'dep'},
			critical = false,
			name = "pkg1"
		}
	end
	if respkgs.pkg2 then
		expected.pkg2 = {
			action = "require",
			package = {Package = 'pkg2', deps = {}, repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg2"
		}
	end
	assert_plan_dep_order(expected, result)
end

function test_request_collision()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = {}, repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			priority = 50,
		},
		{
			tp = 'uninstall',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = "require",
			package = {Package = 'pkg1', deps = {}, repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg1"
		}
	}
	assert_plan_dep_order(expected, result)
end

function test_critical_request()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = {tp = "dep-not", sub = {"pkg2"}}, repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', deps = {}, repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			priority = 50,
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			critical = true,
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg2 = {
			action = "require",
			package = {Package = 'pkg2', deps = {}, repo = def_repo},
			modifier = {},
			critical = true,
			name = "pkg2"
		}
	}
	assert_plan_dep_order(expected, result)
end

function test_critical_request_unsat()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = {tp = "dep-not", sub = {"pkg2"}}, repo = def_repo}},
			modifier = {}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', deps = {}, repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			critical = true,
			priority = 50,
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			critical = true,
			priority = 50,
		}
	}
	assert_exception(function () planner.required_pkgs(pkgs, requests) end, 'inconsistent', nil, { critical = true })
end

function test_critical_provide_conflict()
	local pkg_new_candidate = {
		Package = 'pkg_new',
		Version = "1.0",
		deps = {tp = "dep-not", sub = {{tp = 'dep-package', name = "pkg", version = "~.*"}}}, -- This is Conflicts dependency description
		repo = def_repo
	}
	local pkgs = {
		pkg = {
			candidates = {
				{Package = 'pkg', Version = "1.0", deps = {}, repo = def_repo},
				pkg_new_candidate, -- pkg_new provides pkg so this is candidate for pkg as well as for pkg_new
			},
			modifier = {}
		},
		pkg_new = {
			candidates = {pkg_new_candidate},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg_new',
			},
			priority = 50,
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg',
			},
			critical = true,
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg_new = {
			action = "require",
			package = pkg_new_candidate,
			modifier = {},
			critical = true,
			name = "pkg_new"
		}
	}
	assert_plan_dep_order(expected, result)
end

function test_penalty()
	local pkgs = {
		pkg = {
			candidates = {{Package = 'pkg', deps = {}, repo = def_repo}},
			modifier = {
				deps = {
					tp = "dep-or",
					sub = {"dep1", "dep2", "dep3", "dep4"}
				}
			}
		},
		dep1 = {
			candidates = {{Package = 'dep1', deps = {}, repo = def_repo}},
			modifier = {}
		},
		dep2 = {
			candidates = {{Package = 'dep2', deps = {}, repo = def_repo}},
			modifier = {}
		},
		dep3 = {
			candidates = {{Package = 'dep3', deps = {}, repo = def_repo}},
			modifier = {}
		},
		dep4 = {
			candidates = {{Package = 'dep4', deps = {}, repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg',
			},
			priority = 50,
		}
	}
	local expected = {
		pkg = {
			action = "require",
			package = {Package = 'pkg', deps = {}, repo = def_repo},
			modifier = {
				deps = {
					tp = "dep-or",
					sub = {"dep1", "dep2", "dep3", "dep4"}
				}
			},
			critical = false,
			name = "pkg"
		},
		dep1 = {
			action = "require",
			package = {Package = 'dep1', deps = {}, repo = def_repo},
			modifier = {},
			critical = false,
			name = "dep1"
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	assert_plan_dep_order(expected, result)

	table.insert(requests, {
		tp = 'install',
		package = {
			tp = 'package',
			name = 'dep3'
		},
		priority = 50,
	})
	expected['dep1'] = nil
	expected['dep3'] = {
		action = "require",
		package = {Package = 'dep3', deps = {}, repo = def_repo},
		modifier = {},
		critical = false,
		name = 'dep3'
	}
	result = planner.required_pkgs(pkgs, requests)
	assert_plan_dep_order(expected, result)
end

-- Check that we chose common preference
function test_penalty_most_common()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = {}, repo = def_repo}},
			modifier = {
				deps = {
					tp = "dep-or",
					sub = {"dep1", "dep2"}
				}
			}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', deps = {}, repo = def_repo}},
			modifier = {
				deps = {
					tp = "dep-or",
					sub = {"dep2", "dep1"}
				}
			}
		},
		pkg3 = {
			candidates = {{Package = 'pkg3', deps = {}, repo = def_repo}},
			modifier = {
				deps = {
					tp = "dep-or",
					sub = {"dep1", "dep2"}
				}
			}
		},
		dep1 = {
			candidates = {{Package = 'dep1', deps = {}, repo = def_repo}},
			modifier = {}
		},
		dep2 = {
			candidates = {{Package = 'dep2', deps = {}, repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			priority = 50,
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			priority = 50,
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg3',
			},
			priority = 50,
		}
	}
	local expected = {
		pkg1 = {
			action = "require",
			package = {Package = 'pkg1', deps = {}, repo = def_repo},
			modifier = {
				deps = {
					tp = "dep-or",
					sub = {"dep1", "dep2"}
				}
			},
			critical = false,
			name = "pkg1"
		},
		pkg2 = {
			action = "require",
			package = {Package = 'pkg2', deps = {}, repo = def_repo},
			modifier = {
				deps = {
					tp = "dep-or",
					sub = {"dep2", "dep1"}
				}
			},
			critical = false,
			name = "pkg2"
		},
		pkg3 = {
			action = "require",
			package = {Package = 'pkg3', deps = {}, repo = def_repo},
			modifier = {
				deps = {
					tp = "dep-or",
					sub = {"dep1", "dep2"}
				}
			},
			critical = false,
			name = "pkg3"
		},
		dep1 = {
			action = "require",
			package = {Package = 'dep1', deps = {}, repo = def_repo},
			modifier = {},
			critical = false,
			name = "dep1"
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	assert_plan_dep_order(expected, result)
end

function test_penalty_and_missing()
	local pkgs = {
		pkg = {
			candidates = {{Package = 'pkg', deps = {}, repo = def_repo}},
			modifier = {
				deps = {
					tp = "dep-or",
					sub = {"dep1", "dep2", "dep3", "dep4"}
				}
			}
		},
		dep1 = {
			candidates = {},
			modifier = {}
		},
		dep2 = {
			candidates = {{Package = 'dep2', deps = {}, repo = def_repo}},
			modifier = {}
		},
		dep3 = {
			candidates = {{Package = 'dep3', deps = {}, repo = def_repo}},
			modifier = {}
		},
		dep4 = {
			candidates = {},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg',
			},
			priority = 50,
		}
	}
	local expected = {
		pkg = {
			action = "require",
			package = {Package = 'pkg', deps = {}, repo = def_repo},
			modifier = {
				deps = {
					tp = "dep-or",
					sub = {"dep1", "dep2", "dep3", "dep4"}
				}
			},
			critical = false,
			name = "pkg"
		},
		dep2 = {
			action = "require",
			package = {Package = 'dep2', deps = {}, repo = def_repo},
			modifier = {},
			critical = false,
			name = "dep2"
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	assert_plan_dep_order(expected, result)

	pkgs.dep2.candidates = {}
	expected.dep2 = nil
	expected.dep3 = {
		action = 'require',
		package = {Package = 'dep3', deps = {}, repo = def_repo},
		modifier = {},
		critical = false,
		name = "dep3"
	}
	result = planner.required_pkgs(pkgs, requests)
	assert_plan_dep_order(expected, result)
end

-- Check if package with immediate replan is planned as soon as possible
function test_replan_order()
	local pkgs = {
		pkg = {
			candidates = {{Package = 'pkg', deps = {}, repo = def_repo}},
			modifier = {}
		},
		pkgreplan = {
			candidates = {{Package = 'pkgreplan', deps = {}, repo = def_repo}},
			modifier = {
				deps = 'dep',
				replan = "immediate"
			}
		},
		dep = {
			candidates = {{Package = 'dep', deps = {}, repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg',
			},
			priority = 50
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkgreplan',
			},
			priority = 50
		}
	}
	local expected = {
		{
			action = "require",
			package = {Package = 'dep', deps = {}, repo = def_repo},
			modifier = {},
			critical = false,
			name = "dep"
		},
		{
			action = "require",
			package = {Package = 'pkgreplan', deps = {}, repo = def_repo},
			modifier = {
				deps = 'dep',
				replan = "immediate"
			},
			critical = false,
			name = "pkgreplan"
		},
		{
			action = "require",
			package = {Package = 'pkg', deps = {}, repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg"
		}
	}
	assert_table_equal(expected, planner.required_pkgs(pkgs, requests))
end

function test_sort_requests()
	local requests = {
		{
			tp = 'uninstall',
			package = {tp = 'package', name = 'pkg0'},
			priority = 50
		},
		{
			tp = 'install',
			package = {tp = 'package', name = 'pkg1'},
			priority = 50
		},
		{
			tp = 'install',
			package = {tp = 'package', name = 'pkg2'},
			priority = 50
		},
		{
			tp = 'uninstall',
			package = {tp = 'package', name = 'pkg2_cond'},
			condition = "pkg2",
			priority = 50
		},
		{
			tp = 'install',
			package = {tp = 'package', name = 'pkg1_cond'},
			condition = "pkg1",
			priority = 50
		},
		{
			tp = 'install',
			package = {tp = 'package', name = 'pkg3'},
			priority = 51
		},
		{
			tp = 'install',
			package = {tp = 'package', name = 'pkg4'},
			priority = 49
		},
		{
			tp = 'uninstall',
			package = {tp = 'package', name = 'pkg5'},
			priority = 51
		},
		{
			tp = 'install',
			package = {tp = 'package', name = 'pkg6'},
			critical = true,
			priority = 50
		},
		{
			tp = 'install',
			package = {tp = 'package', name = 'pkg7'},
			critical = true,
			priority = 50
		},
		{
			tp = 'install',
			package = {tp = 'package', name = 'pkg8'},
			critical = true,
			priority = 51
		},
		{
			tp = 'install',
			package = {tp = 'package', name = 'pkg8'},
			critical = true,
			priority = 49
		},
	}
	local expected = {
		requests[11],
		requests[9],
		requests[10],
		requests[12],
		requests[6],
		requests[8],
		requests[2],
		requests[3],
		requests[1],
		requests[5],
		requests[4],
		requests[7],
	}
	planner.sort_requests(requests)
	assert_table_equal(expected, requests)
end

function test_filter_required()
	local status = {
		pkg1 = {
			Version = "1"
		},
		pkg2 = {
			Version = "2"
		},
		pkg3 = {
			Version = "3"
		},
		pkg4 = {
			Depends = "pkg5 nonexist",
			Version = "4"
		},
		pkg5 = {
			Version = "5"
		},
		pkg7 = {
			Version = "7",
			LinkSignature = "xxxx"
		},
		pkg8 = {
			Version = "8",
			LinkSignature = "xxxx"
		},
		pkg9 = {
			Version = "9",
			FilesSignature = "xxxx"
		},
		pkg10 = {
			Version = "10",
			FilesSignature = "xxxx"
		},
	}
	local requests = {
		{
			-- Installed, but requires an upgrade
			action = "require",
			name = "pkg1",
			package = {
				Version = "2",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		},
		{
			-- Installed in the right version
			action = "require",
			name = "pkg2",
			package = {
				Version = "2",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		},
		{
			-- Installed, but we explicitly want to reinstall
			action = "reinstall",
			name = "pkg3",
			package = {
				Version = "3",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		},
		-- The pkg4 and pkg5 are not mentioned, they shall be uninstalled at the end
		{
			-- Not installed and we want it
			action = "require",
			name = "pkg6",
			package = {
				Version = "6",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		},
		{
			-- Installed with correct version but different LinkSignature
			action = "require",
			name = "pkg7",
			package = {
				Version = "7",
				LinkSignature = "XXXXXX",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		},
		{
			-- Installed with correct version and same LinkSignature
			action = "require",
			name = "pkg8",
			package = {
				Version = "8",
				LinkSignature = "xxxx",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		},
		{
			-- Installed with correct version but different FilesSignature
			action = "require",
			name = "pkg9",
			package = {
				Version = "9",
				FilesSignature = "XXXXXX",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		},
		{
			-- Installed with correct version and same FilesSignature
			action = "require",
			name = "pkg10",
			package = {
				Version = "10",
				FilesSignature = "xxxx",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		},
	}
	local result = planner.filter_required(status, requests, true)
	local expected = {
		requests[1],
		{
			action = "require",
			name = "pkg3",
			package = {
				Version = "3",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		},
		requests[4],
		requests[5],
		requests[7],
		{
			action = "remove",
			name = "pkg4",
			package = {
				Depends = "pkg5 nonexist",
				Version = "4"
				-- No repo field here, this comes from the status, there are no repositories
			}
		},
		{
			action = "remove",
			name = "pkg5",
			package = {
				Version = "5"
				-- No repo field here, this comes from the status, there are no repositories
			}
		}
	}
	assert_table_equal(expected, result)
end

-- Test we don't schedule anything after a replan package
function test_replan()
	local requests = {
		{
			action = "require",
			name = "pkg1",
			package = {
				Version = "1",
				repo = def_repo
			},
			critical = false,
			modifier = {
				replan = "immediate"
			}
		},
		{
			action = "require",
			name = "pkg2",
			package = {
				Version = "13",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		}
	}
	local result = planner.filter_required({}, requests, true)
	assert_table_equal({
		requests[1]
	}, result)
end

function test_replan_finished()
	local requests = {
		{
			action = "require",
			name = "pkg1",
			package = {
				Version = "1",
				repo = def_repo
			},
			critical = false,
			modifier = {
				replan = "finished"
			}
		},
		{
			action = "require",
			name = "pkg2",
			package = {
				Version = "13",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		}
	}
	local result = planner.filter_required({}, requests, true)
	assert_table_equal({
		requests[1],
		requests[2]
	}, result)
end

function test_abi_change()
	local status = {
		pkg1 = {
			Version = "1"
		},
		pkg2 = {
			Version = "1"
		},
		pkg3 = {
			Version = "1"
		},
		pkg4 = {
			Version = "1"
		},
		pkg5 = {
			Version = "1"
		},
		pkg6 = {
			Version = "1"
		},
		pkg7 = {
			Version = "1"
		}
	}
	local requests = {
		{
			action = "require",
			name = "pkg1",
			package = {
				Version = "2",
				repo = def_repo
			},
			critical = false,
			modifier = {
				abi_change = {[true] = true, ['pkg2'] = true, ['pkg3'] = true}
			}
		},
		-- Not depending on pkg1 but explicitly listed
		{
			action = "require",
			name = "pkg2",
			package = {
				Version = "1",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		},
		-- Depending on pkg1 and also explicitly listed
		{
			action = "require",
			name = "pkg3",
			package = {
				deps = "pkg1",
				Version = "1",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		},
		-- Depending on pkg1
		{
			action = "require",
			name = "pkg4",
			package = {
				deps = "pkg1",
				Version = "1",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		},
		-- Depending on pkg4 so indirectly on pkg1
		{
			action = "require",
			name = "pkg5",
			package = {
				deps = "pkg4",
				Version = "1",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		},
		-- Not depending on pkg1, already installed and abi_change set
		{
			action = "require",
			name = "pkg6",
			package = {
				Version = "1",
				repo = def_repo
			},
			critical = false,
			modifier = {abi_change = {[true] = true}}
		},
		-- Depends on pkg6 but it shouldn't be updated so no abi_change
		{
			action = "require",
			name = "pkg7",
			package = {
				deps = "pkg6",
				Version = "1",
				repo = def_repo
			},
			critical = false,
			modifier = {}
		}
	}
	local result = planner.filter_required(status, requests, true)
	local expected = {
		requests[1],
		requests[2],
		requests[3],
		requests[4]
	}
	assert_table_equal(expected, result)
	-- Update abi_change to abi_change_deep and repeat
	requests[1].modifier.abi_change_deep = requests[1].modifier.abi_change
	table.insert(expected, requests[5])
	result = planner.filter_required(status, requests, true)
	assert_table_equal(expected, result)
end

function test_candidate_choose()
	-- Create dummy repositories in requests module
	requests.known_repositories = {
		repo1 = {priority = 50, serial = 1},
		repo2 = {priority = 50, serial = 2}
	}
	local candidates = {
		{
			Package = "pkg",
			Version = "1",
			repo = requests.known_repositories.repo1
		},
		{
			Package = "pkg",
			Version = "1",
			repo = requests.known_repositories.repo2
		},
		{
			Package = "pkg",
			Version = "3",
			repo = requests.known_repositories.repo1
		},
		{
			Package = "pkg",
			Version = "2",
			repo = requests.known_repositories.repo1
		},
		{
			Package = "pkg",
			Version = "4",
			repo = requests.known_repositories.repo2
		},
		{
			Package = "provided",
			Version = "1",
			repo = requests.known_repositories.repo2
		}
	}
	assert_table_equal({
		candidates[1],
		candidates[2],
		candidates[4]
	}, planner.candidates_choose(candidates, "pkg", "<3"))
	assert_table_equal({
		candidates[1],
		candidates[2],
		candidates[4]
	}, planner.candidates_choose(candidates, "pkg", " < 3 "))
	assert_table_equal({
		candidates[1],
		candidates[4]
	}, planner.candidates_choose(candidates, "pkg", "<3", {'repo1'}))
	assert_table_equal({
		candidates[1],
		candidates[2],
		candidates[4]
	}, planner.candidates_choose(candidates, "pkg", "<3", {'repo1', requests.known_repositories.repo2}))
	assert_table_equal({
		candidates[5]
	}, planner.candidates_choose(candidates, "pkg", ">3"))
	assert_table_equal({
		candidates[3],
		candidates[5]
	}, planner.candidates_choose(candidates, "pkg", ">=3"))
	assert_table_equal({
		candidates[5]
	}, planner.candidates_choose(candidates, "pkg", ">=3", {'repo2'}))
	assert_table_equal({
		candidates[3],
		candidates[5]
	}, planner.candidates_choose(candidates, "pkg", ">=3", {'repo2', 'repo1'}))
	assert_table_equal({
		candidates[3],
		candidates[4],
		candidates[5]
	}, planner.candidates_choose(candidates, "pkg", "=>2"))
	assert_table_equal({
		candidates[3],
		candidates[4]
	}, planner.candidates_choose(candidates, "pkg", "~[23]"))
	assert_table_equal({
		candidates[3],
		candidates[4]
	}, planner.candidates_choose(candidates, "pkg", "~[2 3]"))
	assert_table_equal({
		candidates[2],
		candidates[5],
		candidates[6]
	}, planner.candidates_choose(candidates, "pkg", nil, {'repo2'}))
	assert_table_equal({
		candidates[1],
		candidates[3],
		candidates[4]
	}, planner.candidates_choose(candidates, "pkg", nil, {requests.known_repositories.repo1}))
	-- Both of these should match nothing, because second character should be handled as part of version not compare specification.
	assert_table_equal({}, planner.candidates_choose(candidates, "pkg", "~=1"))
	assert_table_equal({}, planner.candidates_choose(candidates, "pkg", "=~1"))
end

function test_missing_request()
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'missing'
			},
			priority = 50,
		}
	}
	assert_exception(function () planner.required_pkgs({}, requests) end, 'inconsistent')
end

function test_request_no_candidate()
	local pkgs = {
		pkg = {
			candidates = {},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg',
			},
			priority = 50,
		}
	}
	assert_exception(function () planner.required_pkgs(pkgs, requests) end, 'inconsistent')
end

function test_request_no_candidate_ignore()
	local pkgs = {
		pkg = {
			candidates = {},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg',
			},
			optional = true,
			priority = 50,
		}
	}
	assert_table_equal({}, planner.required_pkgs(pkgs, requests))
end

function test_missing_install()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = {}, repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			priority = 50,
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2'
			},
			optional = true,
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		{
			action = "require",
			package = {Package = 'pkg1', deps = {}, repo = def_repo},
			modifier = {},
			critical = false,
			name = "pkg1"
		}
	}
	assert_table_equal(expected, result)
end

function test_missing_dep_ignore()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = 'pkg2', repo = def_repo}},
			modifier = {
				optional = true
			},
			name = "pkg1"
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		{
			action = "require",
			package = {Package = 'pkg1', deps = 'pkg2', repo = def_repo},
			modifier = {
				optional = true
			},
			critical = false,
			name = "pkg1"
		}
	}
	assert_table_equal(expected, result)
end

function test_deps_twoalts()
	local pkgs = {}
	for i = 1, 3 do
		local pkgname = 'pkg' .. tostring(i)
		pkgs[pkgname] = {
			candidates = {
				{Package = pkgname, deps = {tp = 'dep-package', name = "pkg" .. tostring(i + 1), version = "=2"}, Version = "2", repo = def_repo},
				{Package = pkgname, deps = {tp = 'dep-package', name = "pkg" .. tostring(i + 1), version = "=1"}, Version = "1", repo = def_repo}
			},
			modifier = {}
		}
	end
	pkgs['pkg4'] = {
		candidates = {
			{Package = 'pkg4', Version = "2", repo = def_repo},
			{Package = 'pkg4', Version = "1", repo = def_repo}
		},
		modifier = {}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {}
	for i = 1, 4 do
		local pkgname = 'pkg' .. tostring(i)
		expected[pkgname] = {
			action = 'require',
			package = pkgs[pkgname].candidates[1],
			modifier = {},
			critical = false,
			name = pkgname
		}
	end
	assert_plan_dep_order(expected, result)
end

function test_deps_alt2alt()
	local pkgs = {
		pkg1 = {
			candidates = {
				{Package = 'pkg1', Version = '2', repo = def_repo, deps = {
					tp = 'dep-and',
					sub = {
						'pkg2',
						{tp = 'dep-package', name = 'dep', version = '=2'}
					}
				}},
				{Package = 'pkg1', Version = '1', repo = def_repo, deps = {
					tp = 'dep-and',
					sub = {
						'pkg2',
						{tp = 'dep-package', name = 'dep', version = '=1'}
					}
				}}
			},
			modifier = {}
		},
		pkg2 = {
			candidates = {
				{Package = 'pkg2', deps = {tp = 'dep-package', name = 'dep', version = '=2'}, Version = '2', repo = def_repo},
				{Package = 'pkg2', deps = {tp = 'dep-package', name = 'dep', version = '=1'}, Version = '1', repo = def_repo}
			},
			modifier = {}
		},
		dep = {
			candidates = {
				{Package = 'dep', Version = '2', repo = def_repo},
				{Package = 'dep', Version = '1', repo = def_repo}
			},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = 'require',
			package = pkgs['pkg1'].candidates[1],
			modifier = {},
			critical = false,
			name = 'pkg1'
		},
		pkg2 = {
			action = 'require',
			package = pkgs['pkg2'].candidates[1],
			modifier = {},
			critical = false,
			name = 'pkg2'
		},
		dep = {
			action = 'require',
			package = pkgs['dep'].candidates[1],
			modifier = {},
			critical = false,
			name = 'dep'
		}
	}
	assert_plan_dep_order(expected, result)
end

function test_complex_deps()
	local pkgs = {}
	for i = 1, 7 do
		local n = "pkg" .. tostring(i)
		pkgs[n] = {
			candidates = {{Package = n, repo = def_repo}},
			modifier = {},
			name = n
		}
	end
	local pkg2 = {
		tp = "package",
		name = "pkg2",
	}
	pkgs.meta = {
		candidates = {{Package = "meta", repo = def_repo}},
		modifier = {
			deps = {
				tp = "dep-and",
				sub = {
					"pkg1",
					pkg2,
					{
						tp = "dep-or",
						sub = {
							{
								tp = "dep-and",
								sub = {
									"pkg3",
									"pkg4"
								}
							},
							"pkg5",
						}
					},
					{
						tp = "dep-not",
						sub = {
							"pkg6"
						}
					},
					"pkg7"
				}
			}
		},
		name = "meta"
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'meta',
			},
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = utils.map({"pkg1", "pkg2", "pkg3", "pkg4", "pkg7", "meta"}, function (_, name)
		local p = pkgs[name]
		return name, {
			action = "require",
			package = p.candidates[1],
			modifier = p.modifier,
			critical = false,
			name = name
		}
	end)
	assert_plan_dep_order(expected, result)
end

function test_version_request()
	local pkgs = {
		pkg1 = {
			candidates = {
				{Package = 'pkg1', deps = {}, Version = "2", repo = def_repo},
				{Package = 'pkg1', deps = {}, Version = "1", repo = def_repo}
			},
			modifier = {}
		},
		pkg2 = {
			candidates = {
				{Package = 'pkg2', Version = "2", deps = {}, repo = def_repo},
				{Package = 'pkg2', Version = "1", deps = {}, repo = def_repo}
			},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
			},
			version = '>1',
			priority = 50,
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
			},
			version = '=1',
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = 'require',
			package = {Package = 'pkg1', deps = {}, Version = "2", repo = def_repo},
			modifier = {},
			critical = false,
			name = 'pkg1'
		},
		pkg2 = {
			action = 'require',
			package = {Package = 'pkg2', deps = {}, Version = "1", repo = def_repo},
			modifier = {},
			critical = false,
			name = 'pkg2'
		}
	}
	assert_plan_dep_order(expected, result)
end

function test_version_deps()
	local pkg_dep = {
		tp = 'dep-and',
		sub = {
			{
				tp = 'dep-package',
				name = 'dep1',
				version = '>1'
			},
			{
				tp = 'dep-package',
				name = 'dep2',
				version = '<2'
			},
			{
				tp = 'dep-or',
				sub = {
					{
						tp = 'dep-package',
						name = 'dep3',
						version = '=1'
					},
					{
						tp = 'dep-package',
						name = 'dep3',
						version = '=2'
					}
				}
			}
		}
	}
	local pkgs = {
		pkg = {
			candidates = {
				{Package = 'pkg', deps = pkg_dep, Version = "2", repo = def_repo},
				{Package = 'pkg', deps = {}, Version = "1", repo = def_repo}
			},
			modifier = {}
		},
		dep1 = {
			candidates = {
				{Package = 'dep1', Version = "2", deps = {}, repo = def_repo},
				{Package = 'dep1', Version = "1", deps = {}, repo = def_repo}
			},
			modifier = {}
		},
		dep2 = {
			candidates = {
				{Package = 'dep2', Version = "2", deps = {}, repo = def_repo},
				{Package = 'dep2', Version = "1", deps = {}, repo = def_repo}
			},
			modifier = {}
		},
		dep3 = {
			candidates = {
				{Package = 'dep3', Version = "2", deps = {}, repo = def_repo},
				{Package = 'dep3', Version = "1", deps = {}, repo = def_repo}
			},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg',
			},
			priority = 50,
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg = {
			action = 'require',
			package = {Package = 'pkg', deps = pkg_dep, Version = "2", repo = def_repo},
			modifier = {},
			critical = false,
			name = 'pkg'
		},
		dep1 = {
			action = 'require',
			package = {Package = 'dep1', deps = {}, Version = "2", repo = def_repo},
			modifier = {},
			critical = false,
			name = 'dep1'
		},
		dep2 = {
			action = 'require',
			package = {Package = 'dep2', deps = {}, Version = "1", repo = def_repo},
			modifier = {},
			critical = false,
			name = 'dep2'
		},
		dep3 = {
			action = 'require',
			package = {Package = 'dep3', deps = {}, Version = "2", repo = def_repo},
			modifier = {},
			critical = false,
			name = 'dep3'
		}
	}
	assert_plan_dep_order(expected, result)
end

-- Package depends on version we don't have
function test_version_missing_dep()
	local pkgs = {
		pkg = {
			candidates = {{Package = 'pkg', deps = {
				tp = 'dep-package', name = 'dep', version = '>=2'
			}, Version = "1", repo = def_repo}},
			modifier = {}
		},
		dep = {
			candidates = {{Package = 'dep', deps = {}, Version = "1", repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg',
			},
			priority = 50,
		}
	}
	assert_exception(function () planner.required_pkgs(pkgs, requests) end, 'inconsistent')
end

-- Package depends on package that we don't have with version specified
function test_version_no_such_dep()
	local pkgs = {
		pkg = {
			candidates = {{Package = 'pkg', deps = {
				tp = 'dep-package', name = 'dep', version = '>=2'
			}, Version = "1", repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg',
			},
			priority = 50,
		}
	}
	assert_exception(function () planner.required_pkgs(pkgs, requests) end, 'inconsistent')
end

function test_version_missing_request()
	local pkgs = {
		pkg = {
			candidates = {{Package = 'pkg', Version = "1", repo = def_repo}},
			modifier = {}
		},
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg',
			},
			version = '>1',
			priority = 50,
		}
	}
	assert_exception(function () planner.required_pkgs(pkgs, requests) end, 'inconsistent')
end

function test_pkg_dep_iterate()
	local dep = {
		tp = "dep-and",
		sub = {
			{
				tp = 'dep-or',
				sub = { 'pkg1', 'pkg2' }
			},
			{
				tp = 'dep-and',
				sub = { {tp = 'dep-package'}, 'pkg3' }
			},
			'pkg4'
		}
	}
	local expected = {
		pkg1 = true,
		pkg2 = true,
		pkg3 = true,
		pkg4 = true,
	}
	for _, pkg in planner.pkg_dep_iterate(dep) do
		if type(pkg) == 'table' then
			assert_table_equal({tp = 'dep-package'}, pkg)
		else
			assert_true(expected[pkg])
			expected[pkg] = nil
		end
	end
	assert_nil(next(expected))
end

local function sat_dummy()
	local sat = {
		clauses = {},
		v_true = 1,
		v_false = -1,
		varcount = 1
	}
	function sat:var(count)
		assert_nil(count) -- we don't support this now here
		self.varcount = self.varcount + 1
		return self.varcount
	end
	function sat:clause(...)
		table.insert(self.clauses, {...})
	end
	function sat:assume()
		assert(false) -- not supported in here
	end
	function sat:satisfiable()
		assert(false) -- not supported in here
	end
	function sat:max_satisfiable()
		assert(false) -- not supported in here
	end
	return sat
end

function test_sat_penalize()
	local state = {
		sat = sat_dummy()
		-- other fields in state shouldn't be required
	}
	local lastpen = nil
	local penalty_group = {}
	lastpen = planner.sat_penalize(state, state.sat.v_true, state.sat:var(), penalty_group, lastpen)
	assert_equal(0, lastpen)
	lastpen = planner.sat_penalize(state, state.sat.v_true, state.sat:var(), penalty_group, lastpen)
	assert_equal(4, lastpen)
	lastpen = planner.sat_penalize(state, state.sat:var(), state.sat:var(), penalty_group, lastpen)
	assert_equal(7, lastpen)
	assert_equal(7, state.sat.varcount)
	assert_table_equal({{-1, -4, -3}, {-5, -7, -6}, {-5, -4, 7}}, state.sat.clauses)
end

function test_sat_dep_traverse()
	local state = {
		pkg2sat = {
			["pkg1"] = 2,
			["pkg2"] = 3,
			["pkg3"] = 4,
			["pkg4"] = 5
		},
		penalty_or = {},
		sat = sat_dummy()
		-- candidate2sat, req2sat, missing, penalty_candidates and pkgs shouldn't be required
	}
	state.sat.varcount = 5
	local dep = {
		tp = "dep-and",
		sub = {
			{
				tp = "dep-not",
				sub = {"pkg1"}
			},
			{
				tp = "dep-or",
				sub = {
					{
						tp = "dep-package",
						name = "pkg2"
					},
					{
						tp = "dep-and",
						sub = {
							{
								tp = "package",
								name = "pkg3"
							},
							{
								tp = "dep-not",
								sub = {"pkg4"}
							}
						}
					}
				}
			}
		}
	}
	local wvar = planner.sat_dep_traverse(state, state.sat.v_true, dep)
	assert_equal(6, wvar) -- created as fist after call
	assert_table_equal({9}, state.penalty_or)
	assert_table_equal({
		-- and variable implies not pkg1
		{ -1, -6, -2 },
		-- pkg2 or (pkg3 and not pkg4)
		{ -1, -3, 7 }, -- pkg2 implies or variable
		{ -1, -8, 4 }, -- second and variable implies pkg3
		{ -1, -8, -5 }, -- second and variable implies not pkg4
		{ -1, 8, -4, 5 }, -- (pkg3 and not pkg4) implies second and variable
		{ -1, -8, 7 }, -- second and variable implies or variable
		{ -1, -9, -8 }, -- penalty
		{ -1, -7, 3, 8 }, -- or variable implies that pkg2 or second and variable
		{ -1, -6, 7 }, -- and variable implies or variable
		{ -1, 6, 2, -7 } -- not pkg1 and or variable implies and variable
	}, state.sat.clauses)
end

function test_sat_pkg_group()
	local state = {
		pkg2sat = {["otherpkg"] = 2, ["deppkg"] = 3},
		candidate2sat = {},
		penalty_candidates = {},
		pkgs = {
			pkg = {
				candidates = {
					{Package = "pkg", deps = "otherpkg", Version = "2", repo = def_repo},
					{Package = "pkg", Version = "1", repo = def_repo}
				},
				modifier = {deps = "deppkg"}
			}
		},
		sat = sat_dummy()
		-- req2sat, missing and penalty_or shouldn't be required
	}
	state.sat.varcount = 3
	local satvar = planner.sat_pkg_group(state, "pkg")
	assert_equal(4, satvar)
	assert_table_equal({
		["otherpkg"] = 2,
		["deppkg"] = 3,
		["pkg"] = 4
	}, state.pkg2sat)
	assert_table_equal({
		[state.pkgs.pkg.candidates[1]] = 5,
		[state.pkgs.pkg.candidates[2]] = 6
	}, state.candidate2sat)
	assert_table_equal({7}, state.penalty_candidates)
	assert_equal(7, state.sat.varcount)
	assert_table_equal({
		{-5, 4}, -- candidate version 2 implies its package group
		{-6, 4}, -- candidate version 1 implies its package group
		{-6, -5}, -- candidates are exclusive
		{-1, -7, -6}, -- penalize second candidate (active only if pkg group is enabled)
		{-5, 2}, -- candidate version 2 depends on otherpkg
		{-4, 5, 6}, -- package group implies that one of candidates is chosen
		{-4, 3} -- package group implies its dependencies
	}, state.sat.clauses)
end

function test_sat_dep()
	local pkgs = {
		pkg = {
			candidates = {
				{Package = "pkg", Version = "2", repo = def_repo},
				{Package = "pkg", Version = "1", repo = def_repo}
			},
			modifier = {}
		}
	}
	local state = {
		pkg2sat = {
			["pkg"] = 2
		},
		candidate2sat = {
			[pkgs.pkg.candidates[1]] = 3,
			[pkgs.pkg.candidates[2]] = 4
		},
		pkgs = pkgs,
		sat = sat_dummy()
		-- req2sat, missing, penalty_candidates and penalty_or shouldn't be required
	}
	state.sat.varcount = 4
	local var = planner.sat_dep(state, {tp = "package", name = "pkg"}, ">=1")
	assert_equal(5, var)
	assert_table_equal({
		{-5, 2}, -- candidate selection implies target package group
		{-3, 5},
		{-4, 5}, -- both candidates implies its selection variables
		{-5, 3, 4} -- candidate selection variable implies at least one of candidates is chosen
	}, state.sat.clauses)
end

-- This checks plan order by checking dependencies and tables in plan are checked against expected ones by name of package
function assert_plan_dep_order(expected, plan)
	-- Check that plan contains all and only expected entries
	assert_table_equal(expected, utils.map(plan, function(_, p) return p.name, p end))
	-- Check that objects are in correct order
	local p2i = utils.map(plan, function(k, v) return v.name, k end)
	for _, p in ipairs(plan) do
		local alldeps = utils.arr_prune({p.modifier.deps, p.package.deps})
		local pi = p2i[p.name]
		for _, dep in ipairs(alldeps) do
			for _, pkg in planner.pkg_dep_iterate(dep) do
				local name = pkg.name or pkg
				if p2i[name] then -- We ignore packages thats are not in plan
					assert_true(pi > p2i[name])
				end
			end
		end
	end
end
