--[[
Copyright 2016-2017, CZ.NIC z.s.p.o. (http://www.nic.cz/)

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
local TRACE = TRACE
local WARN = WARN
local picosat = picosat
local opmode = opmode
local utils = require "utils"
local backend = require "backend"
local requests = require "requests"
local postprocess = require "postprocess"

module "planner"

-- luacheck: globals required_pkgs sort_requests candidates_choose filter_required pkg_dep_iterate plan_sorter sat_penalize sat_pkg_group sat_dep sat_dep_traverse set_reinstall_all

-- Choose candidates that complies to version requirement.
function candidates_choose(candidates, pkg_name, version, repository)
	assert(version or repository)
	-- repository is table of strings and objects, canonize to objects and add it to set.
	local repos = {}
	for _, repo in pairs(repository or {}) do
		assert(type(repo) == 'string' or type(repo) == 'table')
		if type(repo) == 'string' then
			local rp = requests.known_repositories[repo]
			if rp then
				repos[rp] = true
			else
				WARN("Package " .. pkg_name .. " limit to non-existent repository " .. repo .. " is ignored.")
			end
		else
			repos[repo] = true
		end
	end

	local compliant = {}
	for _, candidate in pairs(candidates) do
		assert(candidate.Version) -- Version have to be there but candidate.repo might not if it is content from configuration not from repository
		-- Add candidates matching version and repository limitation. Package
		-- supplied using content field in configuration has no repository, so it
		-- is never added when repository limitation is specified.
		if (not version or (candidate.Package == pkg_name and backend.version_match(candidate.Version, version))) and
				(not repository or (candidate.repo and repos[candidate.repo])) then
			table.insert(compliant, candidate)
		end
	end
	return compliant
end

-- Adds penalty variable for given var.
function sat_penalize(state, activator, var, penalty_group, lastpen)
	if not lastpen then
		return 0 -- skip first one, it isn't penalized.
	end
	if not activator then
		activator = state.sat.v_true -- if no activator given than it should be always active
	end
	local penalty = state.sat:var()
	TRACE("SAT add penalty variable " .. tostring(penalty) .. " for variable " .. tostring(var))
	-- penalty => not pen
	state.sat:clause(-activator, -penalty, -var)
	if lastpen ~= 0 then
		-- previous penalty variable implies this one
		state.sat:clause(-activator, -lastpen, penalty)
	end
	table.insert(penalty_group, penalty)
	return penalty
end

-- Returns sat variable for package group of given name. If it is not yet added, then we create new variable for it and also for all its dependencies and candidates.
-- Note that this have to work if the group is unknown (dependency on package we don't know)
function sat_pkg_group(state, name)
	if state.pkg2sat[name] then
		return state.pkg2sat[name] -- Already added package group, return its variable.
	end
	-- Create new variable for this package
	local pkg_var = state.sat:var()
	TRACE("SAT add package " .. name .. " with var: " .. tostring(pkg_var))
	state.pkg2sat[name] = pkg_var
	local pkg = state.pkgs[name]
	-- Add candidates for this package group
	local sat_candidates = {}
	local sat_candidates_exclusive = {} -- only candidates with same name as package group are exclusive
	local lastpen = nil
	local candidates = (pkg and pkg.candidates) or {}
	-- We expect here that candidates are sorted by their priority.
	-- At first we just add variables for them
	for _, candidate in ipairs(candidates) do
		local cand
		-- Candidate might exists if it provides some other package
		if not state.candidate2sat[candidate] then
			cand = state.sat:var()
			TRACE("SAT add candidate " .. candidate.Package .. " for group: " .. name .. " version:" .. (candidate.Version or "") .. " var:" .. tostring(cand))
			state.candidate2sat[candidate] = cand
		else
			cand = state.candidate2sat[candidate]
		end
		state.sat:clause(-cand, pkg_var) -- candidate implies its package group
		if candidate.Package == name then -- Only candidates of this package group are exclusive. There is no reason why candidates from other packages should be exclusive (they are in their own package group).
			for _, o_cand in pairs(sat_candidates_exclusive) do
				state.sat:clause(-cand, -o_cand) -- ensure candidates exclusivity
			end
			table.insert(sat_candidates_exclusive, cand)
		end
		lastpen = sat_penalize(state, nil, cand, state.penalty_candidates, lastpen) -- penalize candidates
		table.insert(sat_candidates, cand)
	end
	-- We solve dependency afterward to ensure that even when they are cyclic, we won't encounter package group in sat that don't have its candidates in sat yet.
	-- Candidate from other package might not be processed yet, we ensure here also that its package group is added
	-- Field deps for candidates and modifier of package group should be string or table of type 'dep-*'. nil or empty table means no dependencies.
	for i = 1, #sat_candidates do
		if candidates[i].Package ~= name then
			sat_pkg_group(state, candidates[i].Package) -- Ensure that candidate's package is also added
			-- Note: not processing dependencies here ensures that dependencies are added only once
		elseif candidates[i].deps and (type(candidates[i].deps) ~= 'table' or next(candidates[i].deps)) then
			local dep = sat_dep_traverse(state, sat_candidates[i], candidates[i].deps)
			state.sat:clause(-sat_candidates[i], dep) -- candidate implies its dependencies
		end
	end
	if next(sat_candidates) then
		state.sat:clause(-pkg_var, unpack(sat_candidates)) -- package group implies that at least one candidate is chosen
	else
		if not utils.multi_index(pkg, "modifier", "virtual") then -- For virtual package, no candidates is correct state
			TRACE("SAT group " .. name .. " has no candidate")
			state.missing[name] = pkg_var -- store that this package group has no candidates
		end
	end
	-- Add dependencies of package group
	local deps = utils.multi_index(pkg, 'modifier', 'deps')
	if deps and (type(deps) ~= 'table' or deps.tp) then
		local dep = sat_dep_traverse(state, pkg_var, deps)
		state.sat:clause(-pkg_var, dep)
	end
	-- And return variable for this package
	return pkg_var
end

-- Returns sat variable for specified requirements on given package.
function sat_dep(state, pkg, version, repository)
	local name = pkg.name or pkg
	local group_var = sat_pkg_group(state, name) -- This also ensures that candidates are in sat
	-- If we specify version then this is request not to whole package group but to some selection of candidates
	if version or repository then
		assert(type(pkg) == 'table') -- If version specified than we should have package not just package group name
		local var = state.sat:var()
		TRACE("SAT add candidate selection " .. name .. " var:" .. tostring(var))
		-- Imply group it self. If we have some candidates, then its just
		-- useless clause. But for no candidates, we ensure that at least some
		-- version of package will be installed if not required one.
		-- Note that that can happen only when we ignore missing dependencies.
		state.sat:clause(-var, group_var)
		if utils.multi_index(state.pkgs[name], 'modifier', 'virtual') then
			WARN('Package ' .. name .. ' requested with version or repository, but it is virtual. Resolved as missing.')
			state.missing[pkg] = var
			return var
		end
		local chosen_candidates = candidates_choose(utils.multi_index(state.pkgs[name], 'candidates') or {}, name, version, repository) -- Note: package don't have to exist (dependency on unknown package)
		if next(chosen_candidates) then
			-- We add here basically or, but without penalizations. Penalization is ensured from dep_pkg_group.
			local vars = utils.map(chosen_candidates, function(i, candidate)
				assert(state.candidate2sat[candidate]) -- candidate we require should be already in sat
				state.sat:clause(-state.candidate2sat[candidate], var) -- candidate => var
				return i, state.candidate2sat[candidate]
			end)
			state.sat:clause(-var, unpack(vars)) -- var => (candidate or candidate or ...)
		else
			TRACE("SAT candidate selection empty")
			state.missing[pkg] = var -- store that this package (as object not group) points to no candidate
		end
		return var
	else
		return group_var
	end
end

-- Recursively adds dependency to sat. It returns sat variable for whole dependency.
function sat_dep_traverse(state, activator, deps)
	if type(deps) == 'string' or deps.tp == 'package' or deps.tp == 'dep-package' then
		return sat_dep(state, deps, deps.version)
	end
	if deps.tp == 'dep-not' then
		assert(#deps.sub == 1)
		-- just do negation of var, so 'not' is propagated to upper clause
		return -sat_dep_traverse(state, activator, deps.sub[1])
	end
	local wvar = state.sat:var()
	if deps.tp == 'dep-and' then
		TRACE("SAT dep and var: " .. tostring(wvar))
		-- wid => var for every variable. Result is that they are all in and statement.
		local vars = {}
		for _, sub in ipairs(deps.sub or deps) do
			local var = sat_dep_traverse(state, activator, sub)
			state.sat:clause(-activator, -wvar, var) -- wvar => var
			table.insert(vars, -var)
		end
		state.sat:clause(-activator, wvar, unpack(vars)) -- (var and var and ...) => wvar
	elseif deps.tp == 'dep-or' then
		TRACE("SAT dep or var: " .. tostring(wvar))
		-- If wvar is true, at least one of sat variables must also be true, so vwar => vars...
		local vars = {}
		local lastpen = nil
		for _, sub in ipairs(deps.sub) do
			local var = sat_dep_traverse(state, activator, sub)
			state.sat:clause(-activator, -var, wvar) -- var => wvar
			lastpen = sat_penalize(state, activator, var, state.penalty_or, lastpen)
			table.insert(vars, var)
		end
		state.sat:clause(-activator, -wvar, unpack(vars)) -- wvar => (var and var and ...)
	else
		error(utils.exception('bad value', "Invalid dependency description " .. (deps.tp or "<nil>")))
	end
	return wvar
end

--[[
Build dependencies for all touched packages. We do it recursively across
dependencies of requested packages, this makes searched space smaller and building
it faster.

Note that we are not checking if package has some real candidates or if it even
exists. This must be resolved later.
Initialize and execute sat_build. This returns table containing following fields:
 pkg2sat - Name of package group to associated sat variable
 candidate2sat - Candidate object to associated sat variable
 req2sat - Request object to associated sat variable
 missing - Table of all package groups (key is string) and dependencies on specific candidates (key is table), where value is sat variable
 penalty_candidates - Array of arrays of penalty variables for candidates.
 penalty_or - Array of arrays of penalty variables for or dependencies.
]]
local function sat_build(sat, pkgs, requests)
	local state = {
		pkg2sat = {},
		candidate2sat = {},
		req2sat = {},
		missing = {}, -- This is table where key is either package group (string) or specific package (object) and value is SAT variable (number)
		penalty_candidates = {},
		penalty_or = {},
		pkgs = pkgs, -- pass pkgs to other sat_* functions this way
		sat = sat -- picosat object
	}
	-- Go trough requests and add them to SAT
	for _, req in ipairs(requests) do
		if not pkgs[req.package.name] and not req.optional and not opmode.optional_installs then
			error(utils.exception('inconsistent', "Requested package " .. req.package.name .. " doesn't exists."))
		end
		local req_var = sat:var()
		TRACE("SAT add request for " .. req.package.name .. " var:" .. tostring(req_var))
		local target_var = sat_dep(state, req.package, req.version, req.repository)
		if req.tp == 'uninstall' then
			-- variable is implied negated (as false)
			target_var = -target_var
		elseif req.tp ~= 'install' then
			error(utils.exception('bad value', "Unknown type " .. tostring(req.tp)))
		end
		if req.condition then
			local cond_var = sat_dep_traverse(state, req_var, req.condition)
			TRACE("SAT request condition var:" .. tostring(cond_var))
			sat:clause(-req_var, target_var, -cond_var)
		else
			sat:clause(-req_var, target_var)
		end
		state.req2sat[req] = req_var
	end
	return state
end

-- Iterate trough all packages in given dependency tree.
-- TODO This goes trough all dependencies, so even negative dependencies and
-- packages used only as conditionals are returned. This is harmless for what we
-- are using it for, but would be better return only real dependencies.
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
	there is not target for such package. Argument only_version allows check for
	specific version. Package is not planned if candidate version not matches. And
	parent_str is string used for warning and error messages containing
	information about who requested given package.
	--]]
	local function pkg_plan(plan_pkg, ignore_missing, ignore_missing_pkg, only_version, parent_str)
		local name = plan_pkg.name or plan_pkg -- it can be object of type "package" or "dep-package" or string containing name of package group
		if not sat[satmap.pkg2sat[name]] then -- This package group is not selected, so we ignore it.
			-- Note: In special case when package provides its own dependency package group might not be selected and so we should at least return empty table
			return {}
		end
		local missing_pkg = satmap.missing[plan_pkg] or satmap.missing[name]
		if missing_pkg and sat[missing_pkg] then -- If missing package (name) or package dependency (plan_pkg) is selected
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
			return {plan[planned[name]]}
		end
		local pkg = pkgs[name]

		-- Found selected candidates for this package group
		local candidates = {}
		for _, cand in pairs((pkg or {}).candidates or {}) do
			if sat[satmap.candidate2sat[cand]] then
				if cand.Package == name then
					if only_version and not backend.version_match(cand.Version, only_version) then
						return -- This package should not be planned as candidate not matches version request
					end
					-- If we have candidate that is from this package that use is exclusively.
					candidates = {cand}
					break -- SAT ensures that there is always only the one such candidate so we ignore the rest
				else
					-- Otherwise collect all candidates that provide this package
					-- Note: We can't decide which one is the one providing this package so we plan them all.
					table.insert(candidates, cand)
				end
			end
		end
		if not next(candidates) and not utils.multi_index(pkg, 'modifier', 'virtual') then
			return -- If no candidates, then we have nothing to be planned. Exception is if this is virtual package.
		end
		if only_version and candidates[1].Package ~= name then
			-- Version dependencies apply only on candidates of same name as package group
			-- Any other candidate should not be planned now.
			return
		end

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

		-- Recursively add all packages this package depends on --
		inwstack[name] = #wstack + 1 -- Signal that we are working on this package group.
		table.insert(wstack, name)

		local function plan_deps(deps)
			for _, p in pkg_dep_iterate(deps or {}) do
				pkg_plan(p, ignore_missing or utils.multi_index(pkg, 'modifier', 'optional'), false, utils.multi_index(p, 'version'), "Package " .. name .. " requires package")
			end
		end
		plan_deps(utils.multi_index(pkg, 'modifier', 'deps')) -- plan package group dependencies
		if not next(candidates) then
			return -- We have no candidate, but we passed previous check because it's virtual
		end
		local r = {}
		local no_pkg_candidate = true
		for _, candidate in pairs(candidates) do -- Now plan candidate's dependencies and packages that provides this package
			if candidate.Package ~= name then
				-- If Candidate is from other group, then plan that group instead now.
				utils.arr_append(r, pkg_plan(candidate.Package, ignore_missing, ignore_missing_pkg, nil, parent_str) or {})
				-- Candidate dependencies are planed as part of pkg_plan call here
			else
				no_pkg_candidate = false
				plan_deps(utils.multi_index(candidate, 'deps'))
			end
		end

		table.remove(wstack, inwstack[name])
		inwstack[name] = nil -- Our recursive work on this package group ended.
		if no_pkg_candidate then
			return r -- in r we have candidates providing this package
		end
		-- And finally plan it --
		planned[name] = #plan + 1
		r = {
			action = 'require',
			package = candidates[1],
			modifier = (pkg or {}).modifier or {},
			critical = false,
			name = name
		}
		if opmode.reinstall_all then
			r.action = 'reinstall'
		end
		plan[#plan + 1] = r
		return {r}
	end

	-- We plan packages with immediate replan first to ensure that such replan happens as soon as possible.
	for name, pkg in pairs(pkgs) do
		-- pkgs contains all packages so we have to check if package is in sat at all
		if utils.multi_index(pkg, 'modifier', 'replan') == "immediate" and satmap.pkg2sat[name] and not (satmap.missing[pkg] or satmap.missing[name]) then -- we ignore missing packages, as they wouldn't be planned anyway and error or warning should be given by requests and other packages later on.
			pkg_plan(name, false, false, nil, 'Planned package with replan enabled'); -- we don't expect to see this parent_str because we are planning this first, but it theoretically can happen so this makes at least some what sense.
		end
	end

	for _, req in pairs(requests) do
		if sat[satmap.req2sat[req]] then -- Plan only if we can satisfy given request
			if req.tp == "install" then -- And if it is install request, uninstall requests are resolved by not being planned.
				local pln = pkg_plan(req.package, false, req.optional or opmode.optional_installs, nil, 'Requested package')
				-- Note that if pln is nil than we ignored missing package. We have to compute with that here
				if pln then
					if req.reinstall then
						for _, p in pairs(pln) do
							p.action = 'reinstall'
						end
					end
					if req.critical then
						for _, p in pairs(pln) do
							p.critical = true
							if inconsistent[p.name] then -- Check if critical didn't end up in cyclic dependency (Note name from returned package was used not request because it might have been provided by some other package)
								error(utils.exception('inconsistent', 'Package ' .. req.package.name .. ' is requested as critical. Cyclic dependency is not allowed for critical requests.', { critical = true }))
							end
						end
					end
				end
			end
		else
			-- We don't expect critical. If critical request wasn't satisfied we already failed.
			local str_ver = ""
			if req.version then
				str_ver = " version:" .. tostring(req.package.version)
			end
			local str_repo = ""
			if req.repository then
				str_repo = " repository:" .. tostring(req.repository.name)
			end
			WARN("Request not satisfied to " .. req.tp .. " package: " .. req.package.name .. str_ver .. str_repo)
		end
	end

	return plan
end

--[[
Sort requests based on various conditions. The planned depends on correct request
order for correct functionality.
]]
function sort_requests(requests)
	local function compare(a, b)
		if a.critical ~= b.critical then
			return a.critical
		end
		if a.priority ~= b.priority then
			return a.priority > b.priority
		end
		if (a.condition and b.condition == nil) or (a.condition == nil and b.condition) then
			-- conditional requests are ordered later
			return b.condition
		end
		if a.tp ~= b.tp then -- type can be instal or unistall and we prefer install
			return a.tp == "install"
		end
		-- otherwise we keep it as it was
		return false -- Because this is quicksort we have to as last resort return false
	end
	table.sort(requests, compare)
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
	sort_requests(requests)

	local sat = picosat.new()
	-- Tables that's mapping packages, requests and candidates with sat variables
	local satmap = sat_build(sat, pkgs, requests)

	-- Executes sat solver and adds clauses for maximal satisfiable set
	local function clause_max_satisfiable()
		sat:satisfiable()
		local maxassume = sat:max_satisfiable() -- assume only maximal satisfiable set
		for assum, _ in pairs(maxassume) do
			sat:clause(assum)
		end
		sat:satisfiable() -- Reset assumptions (TODO isn't there better solution to reset assumptions?)
	end

	-- Install and Uninstall requests.
	DBG("Resolving Install and Uninstall requests")
	for _, req in ipairs(requests) do
		TRACE("Assume request to " .. req.tp .. ": " .. req.package.name)
		sat:assume(satmap.req2sat[req])
		if sat:satisfiable() then
			sat:clause(satmap.req2sat[req])
		elseif req.critical then
			error(utils.exception('inconsistent', "Packages request marked as critical can't be satisfied: " .. req.package.name, {critical = true}))
		end
	end

	-- Deny any packages missing, without candidates or dependency on missing version if possible
	DBG("Denying packages without any candidate")
	for _, var in pairs(satmap.missing) do
		sat:assume(-var)
	end
	clause_max_satisfiable()

	-- Chose alternatives with penalty variables
	DBG("Forcing penalty on expressions with free alternatives")
	local function penalize(penalties)
		for _, penalty in ipairs(penalties) do
			sat:assume(penalty)
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
Go trough the list of requests and create list of all packages required to be
installed. Those packages are not on system at all or are in different versions.
]]
local function check_install_version(status, requests)
	local installed = {}
	for pkg, desc in pairs(status) do
		if not desc.Status or desc.Status[3] == "installed" then
			installed[pkg] = true
		end
	end
	local unused = utils.clone(installed)
	local install = {}
	-- Go through the requests and look which ones are needed and which ones are satisfied
	for _, request in ipairs(requests) do
		unused[request.name] = nil
		if request.action == "require" then
			if not installed[request.name] then
				DBG("Want to install " .. request.name)
				install[request.name] = request
			else
				local different = nil
				for _, field in ipairs({"Version", "Architecture", "LinkSignature", "FilesSignature", "Depends", "Conflicts", "Provides"}) do
					local installed_field = status[request.name][field] or ""
					local requested_field = request.package[field] or ""
					if installed_field ~= requested_field then
						different = field
						break
					end
				end
				if different then
					install[request.name] = request
					DBG("Want to reinstall " .. request.name ..
						" because of change in " .. different .. " (" ..
						tostring(status[request.name][different]) .. " -> " ..
						tostring(request.package[different]) .. ")")
				else
					DBG("Package " .. request.name .. " already installed")
				end
			end
		elseif request.action == "reinstall" then
			DBG("Want to reinstall " .. request.name .. " as requested")
			install[request.name] = request
		else
			DIE("Unknown action " .. request.action)
		end
	end
	return install, unused
end

--[[
Creates table containing inverted dependencies for given requests. Returned table
has as key name of package and as value set of all packages depending on it.
Example: A -> B -> C results to {["C"] = {["B"] = true}, ["B"] = {["A"] = true}}
]]
local function invert_dependencies(requests)
	local inv = {}
	for _, req in pairs(requests) do
		local alldeps = utils.arr_prune({ req.package.deps, req.modifier.deps })
		for _, dep in pkg_dep_iterate(alldeps) do
			local dname = dep.name or dep
			if not inv[dname] then inv[dname] = {} end
			inv[dname][req.name] = true
		end
	end
	return inv
end

--[[
Go trough the list of requests and install package if it depends on package that
changed its ABI. And also install additional packages listed in abi_change and
abi_change_deep fields.
]]
local function check_abi_change(requests, install)
	local reqs = utils.map(requests, function(_, v) return v.name, v end)
	-- Build inverted dependencies
	local invdep -- initialized
	local function abi_changed(name, abi_ch, causepkg)
		-- Ignore package that we don't request. Also ignore if no abi change is
		-- passed and package is not going to be installed.
		if not reqs[name] or not (install[name] or abi_ch) then
			return
		end
		if not install[name] then
			DBG("ABI change of " .. causepkg .. " causes reinstall of " .. name)
			install[name] = reqs[name]
		end
		local request = reqs[name]
		local dep_abi_ch
		for p in pairs(request.modifier.abi_change or {}) do
			if type(p) == 'table' or type(p) == 'string' then
				abi_changed(p.name or p, abi_ch or "shallow", name)
			elseif type(p) == 'boolean' then
				-- Note: shallow can be overridden by deep afterwards
				dep_abi_ch = abi_ch or "shallow"
			end
		end
		for p in pairs(request.modifier.abi_change_deep or {}) do
			if type(p) == 'table' or type(p) == 'string' then
				abi_changed(p.name or p, "deep", name)
			elseif type(p) == 'boolean' then
				dep_abi_ch = "deep"
			end
		end
		if abi_ch == "deep" then
			dep_abi_ch = abi_ch
		end
		if dep_abi_ch then
			if not invdep then
				invdep = invert_dependencies(requests)
			end
			for name in pairs(invdep[name] or {}) do
				abi_changed(name, dep_abi_ch, name)
			end
		end
	end
	for reqname, _ in pairs(install) do
		abi_changed(reqname, nil, "")
	end
	return install
end

--[[
Go trough the list of unused installed packages and marks them for removal.
]]
local function check_removal(status, unused)
	-- TODO report cycles in dependencies
	local unused_sorted = {}
	local sort_buff = {}
	local function sort_unused(pkg)
		if not sort_buff[pkg] then
			sort_buff[pkg] = true
			-- Unfortunately we have to go trough all dependencies to ensure correct order.
			local deps = postprocess.deps_canon(utils.multi_index(status, pkg, "Depends") or {})
			for _, deppkg in pkg_dep_iterate(deps) do
				sort_unused(deppkg.name or deppkg)
			end
			if unused[pkg] then
				unused[pkg] = nil
				DBG("Want to remove left-over package " .. pkg)
				table.insert(unused_sorted, {
					action = "remove",
					name = pkg,
					package = status[pkg]
				})
			end
			-- Ignore packages that are used or not installed at all
		end
	end
	for pkg in pairs(unused) do
		sort_unused(pkg)
	end
	return utils.arr_inv(unused_sorted)
end

--[[
Go through the list of requests on the input. Pass the needed ones through and
leave the extra (eg. requiring already installed package) out. And creates
additional requests with action "remove", such package is present on system, but
is not required any more and should be removed.
]]
function filter_required(status, requests, allow_replan)
	local install, unused = check_install_version(status, requests)
	install = check_abi_change(requests, install)
	local result = {}
	local replan = false -- If we are requested to replan after some package, drop the rest of the plan
	for _, request in ipairs(requests) do
		if install[request.name] then
			local req = request
			if request.action == "reinstall" then
				-- Make a shallow copy and change the action requested
				req = utils.shallow_copy(request)
				req.action = "require"
			end
			table.insert(result, req)
			if request.modifier.replan == "immediate" and allow_replan then
				replan = true
				break
			end
		end
	end
	if not replan and not opmode.no_removal then
		-- We don't remove unused packages just yet if we do immediate replan, we do it after the replanning.
		utils.arr_append(result, check_removal(status, unused))
	end
	return result
end

return _M
