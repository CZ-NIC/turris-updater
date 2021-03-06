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

local pairs = pairs
local ipairs = ipairs
local tostring = tostring
local error = error
local pcall = pcall
local next = next
local type = type
local table = table
local string = string
local DBG = DBG
local WARN = WARN
local ERROR = ERROR
local archive = archive
local utils = require "utils"
local backend = require "backend"
local requests = require "requests"

module "postprocess"

-- luacheck: globals get_repos deps_canon conflicts_canon available_packages pkg_aggregate run sort_candidates

local function repo_parse(repo)
	repo.tp = 'parsed-repository'
	repo.content = {}
	local name = repo.name .. "/" .. repo.index_uri:uri()
	-- Get index
	local index = repo.index_uri:finish() -- TODO error?
	if index:sub(1, 2) == string.char(0x1F, 0x8B) then -- compressed index
		DBG("Decompressing index " .. name)
		index = archive.decompress(index)
	end
	-- Parse index
	DBG("Parsing index " .. name)
	local ok, list = pcall(backend.repo_parse, index)
	if not ok then
		local msg = "Couldn't parse the index of " .. name .. ": " .. tostring(list)
		if not repo.optional then
			error(utils.exception('syntax', msg))
		end
		WARN(msg)
		-- TODO we might want to ignore this repository in its fulles instead of this
	end
	for _, pkg in pairs(list) do
		-- Compute the URI of each package (but don't download it yet, so don't create the uri object)
		pkg.uri_raw = repo.repo_uri .. '/' .. pkg.Filename
		pkg.repo = repo
	end
	repo.content = list
end

local function repos_failed_download(uri_fail)
	-- Locate failed repository and check if we can continue
	for _, repo in pairs(requests.known_repositories) do
		if uri_fail == repo.index_uri then
			local message = "Download failed for repository index " ..
				repo.name .. " (" .. repo.index_uri:uri() .. "): " ..
				tostring(repo.index_uri:download_error())
			if not repo.optional then
				error(utils.exception('repo missing', message))
			end
			WARN(message)
			repo.tp = 'failed-repository'
			break
		end
	end
end

function get_repos()
	DBG("Downloading repositories indexes")
	-- Run download
	while true do
		local uri_fail = requests.repositories_uri_master:download()
		if uri_fail then
			repos_failed_download(uri_fail)
		else
			break
		end
	end
	-- Collect indexes and parse them
	for _, repo in pairs(requests.known_repositories) do
		if repo.tp == 'repository' then -- ignore failed repositories
			local ok, err = pcall(repo_parse, repo)
			if not ok then
				-- TODO is this fatal?
				error(err)
			end
		end
	end
end

-- Helper function for deps_canon ‒ handles 0 and 1 item dependencies.
local function dep_size_check(dep)
	if not next(dep.sub) then
		return nil
	elseif #dep.sub == 1 and dep.tp ~= 'dep-not' then
		return dep.sub[1]
	else
		return dep
	end
end

--[[
Canonicize the dependencies somewhat. This does several things:
• Splits dependencies from strings (eg. "a, b, c" becomes a real dep-and object holding "a", "b", "c").
• Splits version limitations from dependency string (ex.: "a (>=1)" becomes { tp="dep-package", name="a", version=">=1" }).
• Table dependencies are turned to real dep-and object.
• Empty dependencies are turned to nil.
• Single dependencies are turned to just the string (except with the not dependency)
]]
function deps_canon(old_deps)
	if type(old_deps) == 'string' then
		if old_deps:match(',') then
			local sub = {}
			for dep in old_deps:gmatch('[^,]+') do
				table.insert(sub, deps_canon(dep))
			end
			return deps_canon({
				tp = 'dep-and',
				sub = sub
			})
		elseif old_deps:match('%s') then
			local name, version = backend.parse_pkg_specifier(old_deps)
			if version then
				return { tp = "dep-package", name = name, version = version }
			else
				-- TODO possibly report error if name is nil?
				return name -- No version detected, use just name.
			end
		elseif old_deps == '' then
			return nil
		else
			return old_deps
		end
	elseif type(old_deps) == 'table' then
		local tp = old_deps.tp
		if tp == nil then
			-- It is an AND-type multi-dependency. Mark it as such.
			return deps_canon({
				tp = 'dep-and',
				sub = old_deps
			})
		elseif tp == 'dep-and' then
			-- Flatten any sub-and dependencies
			local sub = {}
			for _, val in ipairs(old_deps.sub) do
				local new_val = deps_canon(val)
				if new_val and new_val.tp == 'dep-and' then
					utils.arr_append(sub, new_val.sub)
				else
					table.insert(sub, new_val)
				end
			end
			return dep_size_check({
				tp = 'dep-and',
				sub = sub
			})
		elseif tp == 'dep-or' or tp == 'dep-not' then
			-- Run on sub-dependencies
			for i, val in ipairs(old_deps.sub) do
				old_deps.sub[i] = deps_canon(val)
			end
			return dep_size_check(old_deps)
		elseif tp == 'package' or tp == 'dep-package' then
			-- Single package dependency (an object instead of name) ‒ leave it as it is
			return old_deps
		else
			error(utils.exception('bad value', 'Object of type ' .. tp .. ' used as a dependency'));
		end
	elseif old_deps == nil then
		return nil
	else
		error(utils.exception('bad value', 'Bad deps type ' .. type(old_deps)))
	end
end

--[[
Create negative dependencies from Conflicts field.
Argument conflicts is expected to contain string with names of packages.
If generated dependencies don't have explicit version limitation than we use
'~.*'. This should match every version. Specifying it that way we ignore
candidates from other packages (added using Provides). This is required as
'Conflicts' shouldn't affect packages with different name.
]]
function conflicts_canon(conflicts)
	if type(conflicts) ~= "string" and type(conflicts) ~= "nil" then
		error(utils.exception('bad value', 'Bad conflicts type ' .. type(conflicts)))
	end
	-- First canonize as dependency
	local dep = deps_canon(conflicts)
	if type(dep) == "string" then
		dep = { tp = "dep-not", sub = {{ tp = "dep-package", name = dep, version = "~.*" }} }
	elseif type(dep) == "table" and dep.tp then
		-- Note: The only acceptable input to this functions should be string and so the result should be a single package or conjunction of those packages.
		if dep.tp == "dep-package" then
			if not dep.version then
				dep.version = "~.*"
			end
			dep = { tp = "dep-not", sub = {dep} }
		elseif dep.tp == "dep-and" then
			for i, pkg in ipairs(dep.sub) do
				if type(pkg) ~= "string" and pkg.tp ~= "dep-package" then
					error(utils.exception('bad value', 'Bad conflict package deps type ' .. (pkg.tp or type(pkg))))
				end
				if type(pkg) == "string" then
					pkg = { tp = "dep-package", name = pkg }
					dep.sub[i] = pkg
				end
				if not pkg.version then
					pkg.version = "~.*"
				end
			end
			dep.tp = "dep-or"
			dep = { tp = "dep-not", sub = {dep} }
		else
			error(utils.exception('bad value', 'Object of type ' .. dep.tp .. ' used as conflict deps'))
		end
	elseif type(dep) ~= "nil" then -- we pass if dep is nill but anything else is error at this point
		error(utils.exception('bad value', 'Bad conflict deps type ' .. type(dep)))
	end
	return dep
end

--[[
Sort all given candidates according to following criteria.

* Source repository priority
* For packages from same package group compare by version (preferring newer packages)
* Prefer packages from package group we are working on (provided candidates are sorted to end)
* Compare repository order of introduction
* Sort alphabetically by name of package
]]
function sort_candidates(pkgname, candidates)
	local function compare(a, b)
		-- The locally created packages (with content) have no repo, create a dummy one. Get its priority from the Package command, or the default 50
		local a_repo = a.repo or {priority = utils.multi_index(a, "pkg", "priority") or 50, serial = -1, name = ""}
		local b_repo = b.repo or {priority = utils.multi_index(b, "pkg", "priority") or 50, serial = -1, name = ""}
		if a_repo.priority ~= b_repo.priority then -- Check repository priority
			return a_repo.priority > b_repo.priority
		end
		if a.Package == b.Package then -- Don't compare versions for different packages
			local vers_cmp = backend.version_cmp(a.Version, b.Version)
			if vers_cmp ~= 0 then -- Check version of package
				return vers_cmp == 1 -- a is newer version than b
			end
		elseif (a.Package ~= pkgname and b.Package == pkgname) or (a.Package == pkgname and b.Package ~= pkgname) then -- When only one of packages is provided by some other packages candidate
			return a.Package == pkgname -- Prioritize candidates of package it self, not provided ones.
		end
		if a_repo.serial ~= b_repo.serial then -- Check repo order of introduction
			return a_repo.serial < b_repo.serial
		end
		if a.Package ~= b.Package then -- As last resort when packages are not from same and not from provided package group
			return a.Package < b.Package -- Sort alphabetically by package name
		end
		-- As sorting algorithm is quicksort it sometimes compares object to it self. So we can't just strait print warning, but we have to check if it isn't same table.
		if a ~= b then
			WARN("Multiple candidates from same repository (" .. a_repo.name .. ") with same version (" .. a.Version .. ") for package " .. a.Package)
		end
		return false -- Because this is quicksort we have to as last resort return false
	end
	table.sort(candidates, compare)
end

available_packages = {}

--[[
Compute the available_packages variable.

It is a table indexed by the name of packages. Each package has candidates ‒
the sources that can be used to install the package. Also, it has modifiers ‒
list of amending 'package' objects. Afterwards the modifiers are put together
to form single package object.
]]
function pkg_aggregate()
	DBG("Aggregating packages together")
	for _, repo in pairs(requests.known_repositories) do
		if repo.tp == "parsed-repository" then
			-- TODO this content design is invalid as there can be multiple packages of same name in same repository with different versions
			for name, candidate in pairs(repo.content) do
				if not available_packages[name] then
					available_packages[name] = {candidates = {}, modifiers = {}}
				end
				table.insert(available_packages[name].candidates, candidate)
				if candidate.Provides then -- Add this candidate to package it provides
					for p in candidate.Provides:gmatch("[^, ]+") do
						if not available_packages[p] then
							available_packages[p] = {candidates = {}, modifiers = {}}
						end
						if p == name then
							WARN("Package provides itself, ignoring: " .. name)
						else
							table.insert(available_packages[p].candidates, candidate)
						end
					end
				end
			end
		end
	end
	for _, pkg in pairs(requests.known_packages) do
		if not available_packages[pkg.name] then
			available_packages[pkg.name] = {candidates = {}, modifiers = {}}
		end
		local pkg_group = available_packages[pkg.name]
		table.insert(pkg_group.modifiers, pkg)
	end
	for name, pkg_group in pairs(available_packages) do
		-- Merge the modifiers together to form single one.
		local modifier = {
			tp = 'package',
			name = name,
			deps = {},
			order_after = {},
			order_before = {},
			pre_install = {},
			pre_remove = {},
			post_install = {},
			post_remove = {},
			reboot = false,
			replan = false,
			abi_change = {},
			abi_change_deep = {}
		}
		for _, m in pairs(pkg_group.modifiers) do
			m.final = modifier
			--[[
			Merge all the deps together. We use an empty table if there's nothing else, which is OK,
			since it'll get merged into the upper level and therefore won't have any effect during
			the subsequent dependency processing.

			Note that we don't merge the deps from the package sources, since there may be multiple
			candidates and the deps could differ.
			]]
			table.insert(modifier.deps, m.deps or {})
			-- Take a single value or a list from the source and merge it into a set in the destination
			local function set_merge(name)
				local src = m[name]
				if src == nil then
					return
				elseif type(src) == "table" then
					for _, v in pairs(src) do
						modifier[name][v] = true
					end
				else
					modifier[name][src] = true
				end
			end
			set_merge("order_after")
			set_merge("order_before")
			set_merge("pre_install")
			set_merge("pre_remove")
			set_merge("post_install")
			set_merge("post_remove")
			set_merge("abi_change")
			set_merge("abi_change_deep")
			local function flag_merge(name, vals)
				if m[name] and not vals[m[name]] then
					ERROR("Invalid " .. name .. " value " .. m[name] .. " on package " .. m.name)
				elseif (vals[m[name]] or 0) > vals[modifier[name]] then
					-- Pick the highest value (handle the case when there's no flag)
					modifier[name] = m[name]
				end
			end
			flag_merge("reboot", {
				[false] = 0,
				delayed = 1,
				finished = 2,
				immediate = 3
			})
			flag_merge("replan", {
				[false] = 0,
				finished = 1,
				[true] = 2,
				immediate = 2
			})
			if modifier.replan == true then
				-- true is the same as immediate so replace it
				modifier.replan = "immediate"
			end
			modifier.virtual = modifier.virtual or m.virtual
		end
		if modifier.virtual then
			-- virtual packages ignore all candidates
			pkg_group.candidates = {}
		end
		-- Canonize dependencies
		modifier.deps = deps_canon(modifier.deps)
		for _, candidate in ipairs(pkg_group.candidates or {}) do
			candidate.deps = deps_canon(utils.arr_prune({
				candidate.deps, -- deps from updater configuration file
				candidate.Depends, -- Depends from repository
				conflicts_canon(candidate.Conflicts) -- Negative dependencies from repository
			}))
		end
		pkg_group.modifier = modifier
		-- We merged them together, they are no longer needed separately
		pkg_group.modifiers = nil
		-- Sort candidates
		sort_candidates(name, pkg_group.candidates or {})
	end
end

--[[
Canonize request variables
This is effectively here only to support if extra argument that should be
canonized as dependency.
]]
local function canon_requests(all_requests)
	for _, req in pairs(all_requests) do
		req.condition = deps_canon(req.condition)
	end
end

function run()
	get_repos()
	pkg_aggregate()
	canon_requests(requests.content_requests)
end

return _M
