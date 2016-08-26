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
				group = pkgs.pkg1
			}
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2'
			}
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = "require",
			package = {Package = 'pkg1', deps = {}, repo = def_repo},
			modifier = {},
			name = "pkg1"
		},
		pkg2 = {
			action = "require",
			package = {Package = 'pkg2', repo = def_repo},
			modifier = {},
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
			reinstall = true
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		{
			action = "reinstall",
			package = {Package = 'pkg1', repo = def_repo},
			modifier = {},
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
			}
		},
		{
			-- Just a package in the middle, so we are sure the following reschedule doesn't reorder things.
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2'
			}
		},
		{
			-- Second instance with reinstall. That one should reschedule it as „reinstall“
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1'
			},
			reinstall = true
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = "reinstall",
			package = {Package = 'pkg1', repo = def_repo},
			modifier = {},
			name = 'pkg1'
		},
		pkg2 = {
			action = "require",
			package = {Package = 'pkg2', repo = def_repo},
			modifier = {},
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
				{Package = 'dep1', deps = {}, Version = "1", repo = def_repo},
				{Package = 'dep1', deps = {}, Version = "2", repo = def_repo}
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
			candidates = {{Package = 'pkg1', deps ={}, repo = def_repo}},
			modifier = {
				deps = "dep1"
			}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', deps = {'dep2', 'dep3'}, repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg1',
				group = pkgs.pkg1
			}
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2',
				group = pkgs.pkg2
			}
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		dep1 = {
			action = 'require',
			package = {Package = 'dep1', deps = {}, Version = "2", repo = def_repo},
			modifier = {},
			name = 'dep1'
		},
		dep2 = {
			action = 'require',
			package = {Package = 'dep2', repo = def_repo},
			modifier = {},
			name = 'dep2'
		},
		dep3 = {
			action = 'require',
			package = {Package = 'dep3', repo = def_repo},
			modifier = {
				deps = "dep1"
			},
			name = 'dep3'
		},
		pkg1 = {
			action = 'require',
			package = {Package = 'pkg1', deps ={}, repo = def_repo},
			modifier = {
				deps = "dep1"
			},
			name = 'pkg1'
		},
		pkg2 = {
			action = 'require',
			package = {Package = 'pkg2', deps = {'dep2', 'dep3'}, repo = def_repo},
			modifier = {},
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
			candidates = {{Package = 'pkg', deps = {'nothere'}, repo = def_repo}},
			modifier = {}
		}
	}
	local requests = {
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg',
				group = pkgs.pkg
			}
		}
	}
	assert_exception(function () planner.required_pkgs(pkgs, requests) end, 'inconsistent')
end

-- It is able to solve a circular dependency and doesn't stack overflow
function test_circular_deps()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = {'pkg2'}, repo = def_repo}},
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
			}
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		pkg1 = {
			action = 'require',
			package = {Package = 'pkg1', deps = {'pkg2'}, repo = def_repo},
			modifier = {},
			name = 'pkg1'
		},
		pkg2 = {
			action = 'require',
			package = {Package = 'pkg2', repo = def_repo},
			modifier = {
				deps = "pkg1"
			},
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
			candidates = {{Package = 'critic', deps = {'pkg'}, repo = def_repo}},
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
	assert_exception(function () planner.required_pkgs(pkgs, requests) end, 'inconsistent-critical')
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
			reinstall = true
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
			}
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
			name = "pkg1"
		}
	}
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
			}
		},
		{
			tp = 'uninstall',
			package = {
				tp = 'package',
				name = 'pkg1',
			}
		}
	}
	assert_exception(function() planner.required_pkgs(pkgs, requests) end, 'invalid-request')
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
			Version = "4"
		},
		pkg5 = {
			Version = "5"
		}
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
			modifier = {}
		},
		{
			-- Installed and we want to remove it
			action = "remove",
			name = "pkg4",
			package = {
				Version = "4",
				repo = def_repo
			}
		},
		-- The pkg5 is not mentioned, it shall be uninstalled at the end
		{
			-- Not installed and we want it
			action = "require",
			name = "pkg6",
			package = {
				Version = "6",
				repo = def_repo
			},
			modifier = {}
		}
	}
	local result = planner.filter_required(status, requests)
	local expected = {
		requests[1],
		{
			action = "require",
			name = "pkg3",
			package = {
				Version = "3",
				repo = def_repo
			},
			modifier = {}
		},
		requests[4],
		requests[5],
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
			modifier = {
				replan = true
			}
		},
		{
			action = "require",
			name = "pkg2",
			package = {
				Version = "13",
				repo = def_repo
			},
			modifier = {}
		}
	}
	local result = planner.filter_required({}, requests)
	assert_table_equal({
		requests[1]
	}, result)
end

function test_candidate_choose()
	local t1 = {
		{
			Version = "1",
			repo = def_repo
		},
		{
			Version = "3",
			repo = def_repo
		},
		{
			Version = "2",
			repo = def_repo
		}
	}
	assert_equal(t1[2], planner.candidate_choose(t1, "test"))
	local t2 = {
		{
			Version = "2",
			repo = {
				priority = 40,
				serial = 1
			}
		},
		{
			Version = "1",
			repo = {
				priority = 50,
				serial = 2
			}
		}
	}
	assert_equal(t2[2], planner.candidate_choose(t2, "test"))
	local t3 = {
		{
			Version = "1",
			repo = {
				priority = 50,
				serial = 2
			}
		},
		{
			Version = "1",
			repo = {
				priority = 50,
				serial = 1
			}
		}
	}
	assert_equal(t3[2], planner.candidate_choose(t3, "test"))
	local t4 = {
		{
			Version = "1",
			repo = def_repo
		},
		{
			Version = "1",
			repo = def_repo
		}
	}
	assert_equal(t4[1], planner.candidate_choose(t4, "test"))
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
				group = pkgs.pkg1
			}
		},
		{
			tp = 'install',
			package = {
				tp = 'package',
				name = 'pkg2'
			},
			ignore = {'missing'}
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		{
			action = "require",
			package = {Package = 'pkg1', deps = {}, repo = def_repo},
			modifier = {},
			name = "pkg1"
		}
	}
	assert_table_equal(expected, result)
end

function test_missing_dep_ignore()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', deps = {'pkg2'}, repo = def_repo}},
			modifier = {
				ignore = {"deps"}
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
				group = pkgs.pkg1
			}
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		{
			action = "require",
			package = {Package = 'pkg1', deps = {'pkg2'}, repo = def_repo},
			modifier = {
				ignore = {"deps"}
			},
			name = "pkg1"
		}
	}
	assert_table_equal(expected, result)
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
		group = pkgs.pkg2
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
				group = pkgs.meta
			}
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = utils.map({"pkg1", "pkg2", "pkg3", "pkg4", "pkg7", "meta"}, function (_, name)
		local p = pkgs[name]
		return name, {
			action = "require",
			package = p.candidates[1],
			modifier = p.modifier,
			name = name
		}
	end)
	assert_plan_dep_order(expected, result)
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

-- This checks plan order by checking dependencies and tables in plan are checked agains expected ones by name of package
function assert_plan_dep_order(expected, plan)
	local p2i = {}
	for key, p in ipairs(plan) do
		assert_table_equal(expected[p.name], p)
		expected[p.name] = nil -- We checked this one, by this we ensure that it wont be used again and we can check that all that shoudl be is in plan
		p2i[p.name] = key
	end
	for _, p in ipairs(plan) do
		local alldeps = {p.modifier.deps}
		utils.arr_append(alldeps, p.package.deps or {})
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
	assert_nil(next(expected))
end
