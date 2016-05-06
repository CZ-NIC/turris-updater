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

local deps = require 'deps'

module("deps-tests", package.seeall, lunit.testcase)

--[[
Test installation plan generation when there are no
dependencies.
]]
function test_no_deps()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', Depends = {}}},
			modifier = {
				deps = {}
			}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2'}},
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
	local result = deps.required_pkgs(pkgs, requests)
	local expected = {
		{
			action = "require",
			package = {Package = 'pkg1', Depends = {}},
			modifier = {
				deps = {}
			}
		},
		{
			action = "require",
			package = {Package = 'pkg2'},
			modifier = {
				deps = {}
			}
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
				{Package = 'dep1', Depends = {}, Version = 1},
				{Package = 'dep1', Depends = {}, Version = 2}
			},
			modifier = {
				deps = {}
			}
		},
		dep2 = {
			candidates = {{Package = 'dep2'}},
			modifier = {
				deps = {}
			}
		},
		dep3 = {
			candidates = {{Package = 'dep3'}},
			modifier = {
				deps = {dep1 = true}
			}
		},
		unused = {
			candidates = {{Package = 'unused'}},
			modifier = {
				deps = {dep1 = true}
			}
		},
		pkg1 = {
			candidates = {{Package = 'pkg1', Depends = {}}},
			modifier = {
				deps = {dep1 = true}
			}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2', Depends = {'dep2', 'dep3'}}},
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
	local result = deps.required_pkgs(pkgs, requests)
	local expected = {
		{
			action = "require",
			package = {Package = 'dep1', Depends = {}, Version = 1},
			modifier = {
				deps = {}
			}
		},
		{
			action = "require",
			package = {Package = 'pkg1', Depends = {}},
			modifier = {
				deps = {dep1 = true}
			}
		},
		{
			action = "require",
			package = {Package = 'dep2'},
			modifier = {
				deps = {}
			}
		},
		{
			action = "require",
			package = {Package = 'dep3'},
			modifier = {
				deps = {dep1 = true}
			}
		},
		{
			action = "require",
			package = {Package = 'pkg2', Depends = {'dep2', 'dep3'}},
			modifier = {
				deps = {}
			}
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
			candidates = {{Package = 'pkg', Depends = {'nothere'}}},
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
	assert_exception(function () deps.required_pkgs(pkgs, requests) end, 'inconsistent')
end

-- It is able to detect a circular dependency and doesn't stack overflow
function test_circular_deps()
	local pkgs = {
		pkg1 = {
			candidates = {{Package = 'pkg1', Depends = {'pkg2'}}},
			modifier = {
				deps = {}
			}
		},
		pkg2 = {
			candidates = {{Package = 'pkg2'}},
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
	assert_exception(function () deps.required_pkgs(pkgs, requests) end, 'inconsistent')
end
