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

module("planner-tests", package.seeall, lunit.testcase)

local def_repo = {priority = 50, serial = 1}

--[[
Test installation plan generation when there are no
dependencies.
]]
function test_no_deps()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', Depends = {}, repo = def_repo}},
			modifier = {
				deps = {}
			}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', repo = def_repo}},
			modifier = {
				deps = {}
			}
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
		{
			action = "require",
			package = {Package = 'pkg1', Depends = {}, repo = def_repo},
			modifier = {
				deps = {}
			},
			name = "pkg1"
		},
		{
			action = "require",
			package = {Package = 'pkg2', repo = def_repo},
			modifier = {
				deps = {}
			},
			name = "pkg2"
		}
	}
	assert_table_equal(expected, result)
end

function test_reinstall()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', repo = def_repo}},
			modifier = {
				deps = {}
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
			reinstall = true
		}
	}
	local result = planner.required_pkgs(pkgs, requests)
	local expected = {
		{
			action = "reinstall",
			package = {Package = 'pkg1', repo = def_repo},
			modifier = {
				deps = {},
			},
			name = 'pkg1'
		}
	}
	assert_table_equal(expected, result)
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
				{Package = 'dep1', Depends = {}, Version = "1", repo = def_repo},
				{Package = 'dep1', Depends = {}, Version = "2", repo = def_repo}
			},
			modifier = {
				deps = {}
			}
		},
		dep2 = {
			candidates = {{Package = 'dep2', repo = def_repo}},
			modifier = {
				deps = {}
			}
		},
		dep3 = {
			candidates = {{Package = 'dep3', repo = def_repo}},
			modifier = {
				deps = {dep1 = true}
			}
		},
		unused = {
			candidates = {{Package = 'unused', repo = def_repo}},
			modifier = {
				deps = {dep1 = true}
			}
		},
		pkg1 = {
			candidates = {{Package = 'pkg1', Depends = {}, repo = def_repo}},
			modifier = {
				deps = {dep1 = true}
			}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', Depends = {'dep2', 'dep3'}, repo = def_repo}},
			modifier = {
				deps = {}
			}
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
		{
			action = "require",
			package = {Package = 'dep1', Depends = {}, Version = "2", repo = def_repo},
			modifier = {
				deps = {}
			},
			name = "dep1"
		},
		{
			action = "require",
			package = {Package = 'pkg1', Depends = {}, repo = def_repo},
			modifier = {
				deps = {dep1 = true}
			},
			name = "pkg1"
		},
		{
			action = "require",
			package = {Package = 'dep2', repo = def_repo},
			modifier = {
				deps = {}
			},
			name = "dep2"
		},
		{
			action = "require",
			package = {Package = 'dep3', repo = def_repo},
			modifier = {
				deps = {dep1 = true}
			},
			name = "dep3"
		},
		{
			action = "require",
			package = {Package = 'pkg2', Depends = {'dep2', 'dep3'}, repo = def_repo},
			modifier = {
				deps = {}
			},
			name = "pkg2"
		}
	}
	assert_table_equal(expected, result)
end

--[[
A dependency doesn't exist. It should fail.
]]
function test_missing_dep()
	local pkgs = {
		pkg = {
			candidates = {{Package = 'pkg', Depends = {'nothere'}, repo = def_repo}},
			modifier = {
				deps = {}
			}
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

-- It is able to detect a circular dependency and doesn't stack overflow
function test_circular_deps()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', Depends = {'pkg2'}, repo = def_repo}},
			modifier = {
				deps = {}
			}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', repo = def_repo}},
			modifier = {
				deps = {pkg1 = true}
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
	assert_exception(function () planner.required_pkgs(pkgs, requests) end, 'inconsistent')
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
			candidates = {{Package = 'pkg1', Depends = {}, repo = def_repo}},
			modifier = {
				deps = {}
			}
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
end
