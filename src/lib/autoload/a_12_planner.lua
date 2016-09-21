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

local ipairs = ipairs
local pairs = pairs
local type = type
local tostring = tostring
local error = error
local next = next
local assert = assert
local unpack = unpack
local table = table
local DIE = DIE
local DBG = DBG
local WARN = WARN
local picosat = picosat
local utils = require "utils"
local backend = require "backend"
local requests = require "requests"

module "planner"

-- luacheck: globals candidates_choose required_pkgs filter_required pkg_dep_iterate plan_sorter

-- Choose candidates that complies to version requirement.
function candidates_choose(candidates, version, repository)
	assert(version or repository)
	-- We don't expect that version it self have space in it self, any space is removed.
	local wildmatch, cmp_str, vers = (version or ""):gsub('%s*$', ''):match('^%s*(~?)([<>=]*)%s*(.*)$')
	if wildmatch == '~' then vers = cmp_str .. vers end -- prepend cmd_str to vers if we have wildmatch
	-- repository is table of strings and objects, canonize to objects and add it to set.
	local repos = {}
	for _, repo in pairs(repository or {}) do
		assert(type(repo) == 'string' or type(repo) == 'table')
		if type(repo) == 'string' then
			repos[requests.known_repositories[repo]] = true
		else
			repos[repo] = true
		end
	end

	local compliant = {}
	for _, candidate in pairs(candidates) do
		assert(candidate.Version)
		assert(candidate.repo)
		local cmp = not version or (wildmatch == '~') or backend.version_cmp(vers, candidate.Version)
		if (not version or (
				(wildmatch == '~' and candidate.Version:match(vers)) or
				(cmp_str:find('>', 1, true) and cmp == -1) or
				(cmp_str:find('=', 1, true) and cmp == 0) or
				(cmp_str:find('<', 1, true) and cmp == 1))
			) and (
				not repository or repos[candidate.repo]
			) then
			table.insert(compliant, candidate)
		end
	end
	return compliant
end
--[[
Build dependencies for all touched packages. We do it recursively across
dependencies of requested packages, this makes searched space smaller and building
it faster.

Note that we are not checking if package has some real candidates or if it even
exists. This must be resolved later.
--]]
local function build_deps(sat, satmap, pkgs, requests)
	local dep_traverse -- predefine function as local
	-- Adds penalty variable for given var.
	-- Penalty is added to penalty_group to table on index penalty_group_id.
	-- penalty_group_id can be nil, then new id is created.
	local function penalize(var, penalty_group, penalty_group_id)
		if not penalty_group_id then
			penalty_group_id = #penalty_group + 1
			penalty_group[penalty_group_id] = {}
			return penalty_group_id -- skip first one, it isn't penalized.
		end
		local penalty = sat:var()
		DBG("SAT add penalty variable " .. tostring(penalty) .. " for variable " .. tostring(var))
		-- penalty => not var
		sat:clause(-penalty, -var)
		if #penalty_group[penalty_group_id] ~= 0 then
			-- previous penalty variable implies this one
			local prev = penalty_group[penalty_group_id][#penalty_group[penalty_group_id]]
			sat:clause(-prev, penalty)
		end
		table.insert(penalty_group[penalty_group_id], penalty)
		return penalty_group_id
	end
	-- Returns sat variable for given package group. If it is not yet added, then we create new variable for it and also for all its dependencies and candidates.
	local function dep_pkg_group(name)
		if satmap.pkg2sat[name] then 
			return satmap.pkg2sat[name] -- Already added package group, return its variable.
		end
		-- Create new variable for this package
		local pkg_var = sat:var()
		DBG("SAT add package " .. name .. " with var: " .. tostring(pkg_var))
		satmap.pkg2sat[name] = pkg_var
		local pkg = pkgs[name]
		-- Add candidates for this package group
		local sat_candidates = {}
		local penalty_group_id = nil
		local candidates = (pkg and pkg.candidates) or {}
		-- We expect here that candidates are sorted by their priority.
		-- At first we just add variables for them
		for _, candidate in ipairs(candidates) do
			local var = sat:var()
			DBG("SAT add candidate " .. candidate.Package .. " version:" .. (candidate.Version or "") .. " var:" .. tostring(var))
			satmap.candidate2sat[candidate] = var
			sat:clause(-var, pkg_var) -- candidate implies its package group
			for _, o_cand in pairs(sat_candidates) do
				sat:clause(-var, -o_cand) -- ensure candidates exclusivity
			end
			penalty_group_id = penalize(var, satmap.penalty_candidates, penalty_group_id)
			table.insert(sat_candidates, var)
		end
		-- We solve dependency afterward to ensure that even when they are cyclic, we won't encounter package group in sat that don't have its candidates in sat yet.
		-- Field deps for candidates and modifier of package group should be string or table of type 'dep-*'. nil or empty table means no dependencies.
		for i = 1, #sat_candidates do
			if candidates[i].deps and (type(candidates[i].deps) ~= 'table' or next(candidates[i].deps)) then
				sat:clause(-sat_candidates[i], dep_traverse(candidates[i].deps)) -- candidate implies its dependencies
			end
		end
		if next(sat_candidates) then
			sat:clause(-pkg_var, unpack(sat_candidates)) -- package group implies that at least one candidate is chosen
		else
			satmap.missing[name] = pkg_var -- store that this package group has no candidates
		end
		-- Add dependencies of package group
		local deps = utils.multi_index(pkg, 'modifier', 'deps')
		if deps and (type(deps) ~= 'table' or deps.tp) then
			sat:clause(-pkg_var, dep_traverse(deps)) -- We only do implication here. Equivalence could result in package selection because its dependencies are satisfied.
		end
		-- And return variable for this package
		return pkg_var
	end
	-- Returns sat variable for specified requirements on given package. 
	local function dep(pkg, version, repository)
		local name = pkg.name or pkg
		local group_var = dep_pkg_group(name) -- This also ensures that candidates are in sat
		-- If we specify version then this is request not to whole package group but to some selection of candidates
		if version or repository then
			assert(type(pkg) == 'table') -- If version specified than we should have package not just package group name
			local var = sat:var()
			DBG("SAT add candidate selection " .. name .. " var:" .. tostring(var))
			local chosen_candidates = candidates_choose(pkgs[name].candidates, version, repository)
			if next(chosen_candidates) then
				-- We add here basically or, but without penalizations. Penalization is ensured from dep_pkg_group.
				local vars = utils.map(chosen_candidates, function(i, candidate)
					return i, satmap.candidate2sat[candidate]
				end)
				sat:clause(-var, unpack(vars)) -- imply that at least one of the possible candidates is chosen
			else
				DBG("SAT candidate selection empty")
				satmap.missing[pkg] = var -- store that this variable points to no candidate
			end
			-- Also imply group it self. If we have some candidates, then its just
			-- useless clause. But for no candidates, we ensure that at least some
			-- version of package will be installed if not required one.
			-- Note that that can happen only when we ignore missing dependencies.
			sat:clause(-var, group_var)
			return var
		else
			return group_var
		end
	end
	-- Recursively adds dependency to sat. It returns sat variable for whole dependency.
	function dep_traverse(deps)
		if type(deps) == 'string' or deps.tp == 'package' or deps.tp == 'dep-package' then
			return dep(deps, deps.version)
		end
		if deps.tp == 'dep-not' then
			assert(#deps.sub == 1)
			-- just do negation of var, so 'not' is propagated to upper clause
			return -dep_traverse(deps.sub[1])
		end
		local wvar = sat:var()
		if deps.tp == 'dep-and' then
			DBG("SAT dep and var: " .. tostring(wvar))
			-- wid <=> var for every variable. Result is that they are all in and statement.
			for _, sub in ipairs(deps.sub or deps) do
				local var = dep_traverse(sub)
				sat:clause(-wvar, var)
				sat:clause(-var, wvar)
			end
		elseif deps.tp == 'dep-or' then
			DBG("SAT dep or var: " .. tostring(wvar))
			-- If wvar is true, at least one of sat variables must also be true, so vwar => vars...
			-- Same as if one of vars is true, wvar must be also true, so var => wvar
			local vars = {}
			local penalty_group_id = nil
			for _, sub in ipairs(deps.sub) do
				local var = dep_traverse(sub)
				-- var => wvar
				sat:clause(-var, wvar)
				penalty_group_id = penalize(var, satmap.penalty_or, penalty_group_id)
				-- wvar => vars...
				table.insert(vars, var)
			end
			sat:clause(-wvar, unpack(vars))
		else
			error(utils.exception('bad value', "Invalid dependency description " .. (deps.tp or "<nil>")))
		end
		return wvar
	end

	-- Go trough requests and add them to SAT
	for _, req in ipairs(requests) do
		local req_var = sat:var()
		DBG("SAT add request for " .. req.package.name .. " var:" .. tostring(req_var))
		local target_var = dep(req.package, req.version, req.repository)
		if req.tp == 'install' then
			sat:clause(-req_var, target_var) -- implies true
		elseif req.tp == 'uninstall' then
			sat:clause(-req_var, -target_var) -- implies false
		else
			error(utils.exception('bad value', "Unknown type " .. tostring(req.tp)))
		end
		satmap.req2sat[req] = req_var
	end
end

-- Iterate trough all packages in given dependency tree.
local function pkg_dep_iterate_internal(deps)
	if #deps == 0 then
		return nil
	end
	local d = deps[#deps]
	deps[#deps] = nil
	if type(d) == 'string' or d.tp == 'package' or d.tp == 'dep-package' then
		return deps, d
	else
		assert(type(d) == 'table')
		utils.arr_append(deps, d.sub or d)
		return pkg_dep_iterate_internal(deps)
	end
end
function pkg_dep_iterate(pkg_deps)
	return pkg_dep_iterate_internal, { pkg_deps }
end

--[[
Create new plan, sorted so that packages with dependency on some other installed
package is planned after such package. This is not of course always possible
and so when we encounter cyclic dependencies we just cut circle in some random
point and prints warning for packages that will be inconsistent during
installation process. Exception is critical packages, for those no cycles are
allowed and result would be an error.
If packages has no candidate (so can't be installed) we fail or we print warning
if it should be ignored. We remember whole stack of previous packages to check if
some other planned package won't be affected too.
Function returns sorted plan.
]]--
local function build_plan(pkgs, requests, sat, satmap)
	local plan = {}
	local planned = {} -- Table where key is name of already planned package group and value is index in plan
	local wstack = {} -- array of packages we work on
	local inwstack = {} -- table of all packages we work on where key is name and value is index
	local inconsistent = {} -- Set of potentially inconsistent packages (might fail their post-install scrips)
	local missing_dep = {} -- Set of all packages that depends on some missing dependency
	--[[
	Plans given package (request) and all of its dependencies. Argument plan_pkg is
	"package" or "dep-package" or string (name of package group). ignore_missing
	is extra option of package allowing ignore of missing dependencies.
	ignore_missing_pkg is extra option of package allowing to ignore request if
	there is not target for such package. And parent_str is string used for
	warning and error messages containing information about who requested given
	package.
	--]]
	local function pkg_plan(plan_pkg, ignore_missing, ignore_missing_pkg, parent_str)
		local name = plan_pkg.name or plan_pkg -- it can be object of type "package" or "dep-package" or string containing name of package group
		if not sat[satmap.pkg2sat[name]] then return end -- This package group is not selected, so we ignore it.
		if satmap.missing[plan_pkg] and sat[satmap.missing[plan_pkg]] then -- If missing package or package dependency is selected
			if ignore_missing or ignore_missing_pkg then
				missing_dep[name] = true
				utils.table_merge(missing_dep, utils.arr2set(wstack)) -- Whole working stack is now missing dependency
				WARN(parent_str .. " " .. name .. " that is missing, ignoring as requested.")
			else
				error(utils.exception('inconsistent', parent_str .. " " .. name .. " that is not available."))
			end
		end
		if planned[name] then -- Already in plan, which is OK
			if missing_dep[name] then -- Package was added to plan with ignored missing dependency
				if ignore_missing or ignore_missing_pkg then
					WARN(parent_str .. " " .. name .. " that's missing or misses some dependency. Ignoring as requested")
				else
					error(utils.exception('inconsistent', parent_str .. " " .. name .. " that's missing or misses some dependency. See previous warnings for more info."))
				end
			end
			return plan[planned[name]]
		end
		local pkg = pkgs[name]
		-- Check for cycles --
		if inwstack[name] then -- Already working on it. Found cycle.
			for i = inwstack[name], #wstack, 1 do
				local inc_name = wstack[i]
				if not inconsistent[inc_name] then -- Do not warn again
					WARN("Package " .. inc_name .. " is in cyclic dependency. It might fail its post-install script.")
				end
				inconsistent[inc_name] = true
			end
			return
		end
		-- Found selected candidate for this package group
		local candidate
		for _, cand in pairs((pkg or {}).candidates or {}) do
			if sat[satmap.candidate2sat[cand]] then
				candidate = cand
				break;
			end
		end
		-- Recursively add all packages this package depends on --
		inwstack[name] = #wstack + 1 -- Signal that we are working on this package group.
		table.insert(wstack, name)
		local alldeps = utils.arr_prune({ utils.multi_index(pkg, 'modifier', 'deps'), (candidate or {}).deps })
		for _, p in pkg_dep_iterate(alldeps) do
			pkg_plan(p, ignore_missing or utils.arr2set(utils.multi_index(pkg, 'modifier', 'ignore') or {})["deps"], false, "Package " .. name .. " requires package")
		end
		table.remove(wstack, inwstack[name])
		inwstack[name] = nil -- Our recursive work on this package group ended.
		if not candidate then -- If no candidate, then we have nothing to be planned
			return
		end
		-- And finally plan it --
		planned[name] = #plan + 1
		local r = {
			action = 'require',
			package = candidate,
			modifier = (pkg or {}).modifier or {},
			name = name
		}
		plan[#plan + 1] = r
		return r
	end

	for _, req in pairs(requests) do
		if sat[satmap.req2sat[req]] and req.tp == "install" then -- Plan only if we can satisfy given request and it is install request
			local pln = pkg_plan(req.package, false, utils.arr2set(req.ignore or {})["missing"], 'Requested package')
			-- Note that if pln is nil than we ignored missing package. We have to compute with that here
			if pln and req.reinstall then
				pln.action = 'reinstall'
			end
			if req.critical and inconsistent[req.package.name] then -- Check if critical didn't end up in cyclic dependency
				error(utils.exception('inconsistent', 'Package ' .. req.package.name .. ' is requested as critical. Cyclic dependency is not allowed for critical requests.', { critical = true }))
			end
		end
	end

	return plan
end

--[[
Take list of available packages (in the format of pkg candidate groups
produced in postprocess.available_packages) and list of requests what
to install and remove. Produce list of packages, in the form:
{
  {action = "require"/"reinstall", package = pkg_source, modifier = modifier}
}

The action specifies if the package should be made present in the system (installed
if missing) or reinstalled (installed no matter if it is already present)
• Required to be installed
• Required to be reinstalled even when already present (they ARE part of the previous set)

The pkg_source is the package object (in case it contains the source field or is virtual)
or the description produced from parsing the repository. The modifier is the object
constructed from package objects during the aggregation, holding additional processing
info (hooks, etc).
]]
function required_pkgs(pkgs, requests)
	local sat = picosat.new()
	-- Tables that's mapping packages, requests and candidates with sat variables
	local satmap = {
		pkg2sat = {},
		req2sat = {},
		candidate2sat = {},
		missing = {}, -- Table of all package groups (key is string) and dependencies on specific candidates (key is table), where value is sat variable
		penalty_candidates = {}, -- Array of arrays of penalty variables for candidates. In this case we don't require them to be sorted, but to reuse code for penalty_or, they are.
		penalty_or = {} -- Array of arrays of penalty variables for or dependencies. They are sorted so upper or in dependency is first.
	}
	-- Build dependencies
	build_deps(sat, satmap, pkgs, requests)

	-- Sort all requests to groups by priority
	local reqs_by_priority = {}
	local reqs_critical = {}
	for _, req in pairs(requests) do
		if req.tp == 'install' and req.critical then
			table.insert(reqs_critical, req)
		else
			if not req.priority then req.priority = 50 end
			
			if not reqs_by_priority[req.priority] then reqs_by_priority[req.priority] = {} end
			if req.tp ~= (utils.map(reqs_by_priority[req.priority], function(_, r) return r.package.name, r.tp end)[req.package.name] or req.tp) then
				error(utils.exception('invalid-request', 'Requested both Install and Uninstall with same priority for package ' .. req.package.name))
			end
			table.insert(reqs_by_priority[req.priority], req)
		end
	end
	reqs_by_priority = utils.arr_inv(utils.arr_prune(reqs_by_priority))

	-- Executes sat solver and adds clauses for maximal satisfiable set
	local function clause_max_satisfiable()
		sat:satisfiable()
		local maxassume = sat:max_satisfiable() -- assume only maximal satisfiable set
		for assum, _ in pairs(maxassume) do
			sat:clause(assum)
		end
		sat:satisfiable() -- Reset assumptions (TODO isn't there better solution to reset assumptions?)
	end

	-- Install critical packages requests (set all critical packages to be true)
	DBG("Resolving critical packages")
	for _, req in ipairs(reqs_critical) do
		sat:clause(satmap.req2sat[req])
	end
	if not sat:satisfiable() then
		-- TODO This exception should probably be saying more about why. We can assume variables first and inspect maximal satisfiable set then.
		utils.exception('inconsistent', "Packages marked as critical can't satisfy their dependencies together.")
	end

	-- Install and Uninstall requests.
	DBG("Resolving Install and Uninstall requests")
	for _, reqs in ipairs(reqs_by_priority) do
		for _, req in pairs(reqs) do
			-- Assume all request for this priority
			sat:assume(satmap.req2sat[req])
		end
		clause_max_satisfiable()
	end

	-- Deny any packages missing, without candidates or dependency on missing version if possible
	DBG("Denying packages without any candidate")
	-- TODO add support for virtual packages (we don't want to deny virtual packages)
	for _, var in pairs(satmap.missing) do
		sat:assume(-var)
	end
	clause_max_satisfiable()

	-- Chose alternatives with penalty variables
	DBG("Forcing penalty on expressions with free alternatives")
	local function penalize(penalties)
		for _, pgroup in ipairs(penalties) do
			for _, penalty in pairs(pgroup) do
				sat:assume(penalty)
			end
		end
		clause_max_satisfiable()
	end
	-- Candidates has precedence before dependencies, because we prefer newest possible package.
	penalize(satmap.penalty_candidates)
	penalize(satmap.penalty_or)

	-- Now solve all packages selections from dependencies of already selected packages
	DBG("Deducing minimal set of required packages")
	for _, var in pairs(satmap.pkg2sat) do
		-- We assume false (not selected) for all packages
		sat:assume(-var)
	end
	clause_max_satisfiable()
	-- We call this here again to calculate variables with all new clauses.
	-- Previous call in clause_max_satisfiable is with assumptions, so results
	-- from such calls aren't correct.
	sat:satisfiable() -- Set variables to result values

	return build_plan(pkgs, requests, sat, satmap)
end

--[[
Go through the list of requests on the input. Pass the needed ones through and
leave the extra (eg. requiring already installed package) out. And creates
additional requests with action "remove", such package is present on system, but
is not required any more and should be removed.
]]
function filter_required(status, requests)
	local installed = {}
	for pkg, desc in pairs(status) do
		if not desc.Status or desc.Status[3] == "installed" then
			installed[pkg] = desc.Version or ""
		end
	end
	local unused = utils.clone(installed)
	local result = {}
	-- Go through the requests and look which ones are needed and which ones are satisfied
	for _, request in ipairs(requests) do
		local installed_version = installed[request.name]
		-- TODO: Handle virtual and stand-alone packages
		local requested_version = request.package.Version or ""
		if request.action == "require" then
			if not installed_version or installed_version ~= requested_version then
				DBG("Want to install/upgrade " .. request.name)
				table.insert(result, request)
			else
				DBG("Package " .. request.name .. " already installed")
			end
			unused[request.name] = nil
		elseif request.action == "reinstall" then
			-- Make a shallow copy and change the action requested
			local new_req = utils.shallow_copy(request)
			new_req.action = "require"
			DBG("Want to reinstall " .. request.name)
			table.insert(result, new_req)
			unused[request.name] = nil
		else
			DIE("Unknown action " .. request.action)
		end
	end
	-- Go through the packages that are installed and nobody mentioned them and mark them for removal
	-- TODO: Order them according to dependencies
	for pkg in pairs(unused) do
		DBG("Want to remove left-over package " .. pkg)
		table.insert(result, {
			action = "remove",
			name = pkg,
			package = status[pkg]
		})
	end
	-- If we are requested to replan after some package, wipe the rest of the plan
	local wipe = false
	for i, request in ipairs(result) do
		if wipe then
			result[i] = nil
		elseif request.action == "require" and request.modifier.replan then
			wipe = true
		end
	end
	return result
end

return _M
