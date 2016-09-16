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
local unpack = unpack
local table = table
local string = string
local events_wait = events_wait
local run_command = run_command
local DBG = DBG
local WARN = WARN
local ERROR = ERROR
local utils = require "utils"
local backend = require "backend"
local requests = require "requests"
local uri = require "uri"

module "postprocess"

-- luacheck: globals get_repos deps_canon available_packages pkg_aggregate run

function get_repos()
	DBG("Getting repos")
	--[[
	The repository index downloads are already in progress since
	the repository objects have been created. We now register
	callback for the arrival of data. This might happen right
	away or later on. Anyway, after we wait, all the indices
	have been downloaded.

	When we get each index, we detect if the data is gzipped
	or not. If it is not, the repository is parsed right away.
	If it is, extraction is run in the background and parsing
	is scheduled for once it finishes. Eventually, we wait for
	all the extractions to finish, and at that point everything
	is parsed.
	]]
	local uris = {} -- The uris we wait for to be downloaded
	local extract_events = {} -- The extractions we wait for
	local errors = {} -- Collect errors as we go
	local fatal = false -- Are any of them a reason to abort?
	--[[
	We don't care about the order in which we register the callbacks
	(which may be different from the order in which they are called
	anyway).
	]]
	for _, repo in pairs(requests.known_repositories_all) do
		repo.tp = 'parsed-repository'
		repo.content = {}
		for subrepo, index_uri in pairs(utils.private(repo).index_uri) do
			local name = repo.name .. "/" .. index_uri.uri
			table.insert(uris, index_uri)
			local function broken(why, extra)
				ERROR("Index " .. name .. " is broken (" .. why .. "): " .. tostring(extra))
				extra.why = why
				extra.repo = name
				repo.content[subrepo] = extra
				table.insert(errors, extra)
				fatal = fatal or not utils.arr2set(repo.ignore or {})[why]
			end
			local function parse(content)
				DBG("Parsing index " .. name)
				local ok, list = pcall(backend.repo_parse, content)
				if ok then
					for _, pkg in pairs(list) do
						-- Compute the URI of each package (but don't download it yet, so don't create the uri object)
						pkg.uri_raw = repo.repo_uri .. subrepo .. '/' .. pkg.Filename
						pkg.repo = repo
					end
					repo.content[subrepo] = {
						tp = "pkg-list",
						list = list
					}
				else
					broken('syntax', utils.exception('repo broken', "Couldn't parse the index of " .. name .. ": " .. tostring(list)))
				end
			end
			local function decompressed(ecode, _, stdout, stderr)
				DBG("Decompression of " .. name .. " done")
				if ecode == 0 then
					parse(stdout)
				else
					broken('syntax', utils.exception('repo broken', "Couldn't decompress " .. name .. ": " .. stderr))
				end
			end
			local function downloaded(ok, answer)
				DBG("Received repository index " .. name)
				if not ok then
					-- Couldn't download
					-- TODO: Once we have validation, this could also mean the integrity is broken, not download
					broken('missing', answer)
				elseif answer:sub(1, 2) == string.char(0x1F, 0x8B) then
					-- It starts with gzip magic - we want to decompress it
					DBG("Index " .. name .. " is compressed, decompressing")
					table.insert(extract_events, run_command(decompressed, nil, answer, -1, -1, '/bin/gzip', '-dc'))
				else
					parse(answer)
				end
			end
			index_uri:cback(downloaded)
		end
		--[[
		We no longer need to keep the uris in there, we
		wait for them here and after all is done, we want
		the contents to be garbage collected.
		]]
		utils.private(repo).index_uri = nil
	end
	-- Make sure everything is downloaded
	uri.wait(unpack(uris))
	-- And extracted
	events_wait(unpack(extract_events))
	-- Process any errors
	local multi = utils.exception('multiple', "Multiple exceptions (" .. #errors .. ")")
	multi.errors = errors
	if fatal then
		error(multi)
	elseif next(errors) then
		return multi
	else
		return nil
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
			-- When there is space in parsed name, then there might be version specified
			local dep = old_deps:gsub('^%s', ''):gsub('%s$', '')
			local name = dep:match('^%S+')
			local version = dep:match('%(.+%)$')
			version = version and version:sub(2,-2):gsub('^%s', ''):gsub('%s$', '')
			if not version or version == "" then
				return name -- No version detected, use just name.
			else
				return { tp = "dep-package", name = name, version = version }
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
		elseif tp == 'package' then
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
	for _, repo in pairs(requests.known_repositories_all) do
		for _, cont in pairs(repo.content) do
			if type(cont) == 'table' and cont.tp == 'pkg-list' then
				for name, candidate in pairs(cont.list) do
					if not available_packages[name] then
						available_packages[name] = {candidates = {}, modifiers = {}}
					end
					table.insert(available_packages[name].candidates, candidate)
				end
			end
		end
	end
	for _, pkg in pairs(requests.known_packages) do
		if not available_packages[pkg.name] then
			available_packages[pkg.name] = {candidates = {}, modifiers = {}}
		end
		local pkg_group = available_packages[pkg.name]
		utils.private(pkg).group = pkg_group
		if pkg.virtual then
			table.insert(pkg_group.candidates, pkg)
			pkg_group.virtual = true
		elseif pkg.content then
			-- If it has content, then it is both modifier AND candidate
			table.insert(pkg_group.modifiers, pkg)
			table.insert(pkg_group.candidates, pkg)
		else
			table.insert(pkg_group.modifiers, pkg)
		end
	end
	for name, pkg_group in pairs(available_packages) do
		-- Check if theres at most one of each virtual package.
		if pkg_group.virtual and #pkg_group.candidates > 1 then
			error(utils.exception("inconsistent", "More than one candidate with a virtual package " .. name))
		end
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
			abi_change = {}
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
			local reboot_vals = {
				[false] = 0,
				delayed = 1,
				finished = 2,
				immediate = 3
			}
			if m.reboot and not reboot_vals[m.reboot] then
				ERROR("Invalid reboot value " .. m.reboot .. " on package " .. m.name)
			elseif (reboot_vals[m.reboot] or 0) > reboot_vals[modifier.reboot] then
				-- Pick the highest value for the reboot (handle the case when there's no reboot flag)
				modifier.reboot = m.reboot
			end
			modifier.replan = modifier.replan or m.replan
		end
		-- Canonize dependencies
		modifier.deps = deps_canon(modifier.deps)
		for _, candidate in ipairs(pkg_group.candidates or {}) do
			local candidate_deps = { candidate.deps } -- deps from updater configuration file
			table.insert(candidate_deps, candidate.Depends) -- Depends from repository)
			candidate.deps = deps_canon(candidate_deps)
		end
		pkg_group.modifier = modifier
		-- We merged them together, they are no longer needed separately
		pkg_group.modifiers = nil
		-- Sort candidates
		if pkg_group.candidates then
			table.sort(pkg_group.candidates, function(a, b)
				if a.repo.priority == b.repo.priority then -- Check repository priority
					local vers_cmp = backend.version_cmp(a.Version, b.Version)
					if vers_cmp == 0 then -- Check version of package
						if a.repo.serial == b.repo.serial then -- Check repo order of introduction
							WARN("Multiple candidates from same repository with same version for package " .. a.Package)
							return false -- lets prioritize a, for no reason, lets make b angry.
						else
							return a.repo.serial < b.repo.serial
						end
					else
						return vers_cmp == 1 -- b is newer version than a
					end
				else
					return a.repo.priority > b.repo.priority
				end
			end)
		end
	end
end

function run()
	local repo_errors = get_repos()
	if repo_errors then
		WARN("Not all repositories are available")
	end
	pkg_aggregate()
end

return _M
