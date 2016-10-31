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

--[[
This module prepares and manipulates contexts and environments for
the configuration scripts to be run in.
]]

local pairs = pairs
local ipairs = ipairs
local type = type
local error = error
local require = require
local tostring = tostring
local table = table
local utils = require "utils"
local uri = require "uri"
local backend = require "backend"
local DBG = DBG
local WARN = WARN

module "requests"

-- luacheck: globals known_packages package_wrap known_repositories known_repositories_all repo_serial repository repository_get content_requests install uninstall script store_flags

-- Create a set of allowed names of extra options.
local allowed_package_extras = utils.arr2set({
	"virtual",
	"deps",
	"order_after",
	"order_before",
	"pre_inst",
	"post_inst",
	"pre_rm",
	"post_rm",
	"reboot",
	"replan",
	"abi_change",
	"abi_change_deep",
	"content",
	"verification",
	"sig",
	"pubkey",
	"ca",
	"crl",
	"ignore"
})

--[[
We simply store all package promises, so they can be taken
into account when generating the real packages. Note that
there might be multiple package promises for a single package.
We just store them in an array for future processing.
]]
known_packages = {}

--[[
This package is just a promise of a real package in the future. It holds the
name and possibly some additional info for the package. Once we go through
the requests (Install and Uninstall), we gather all package objects with the
same name and merge them somehow together, and look it up in a repository (or
repositories). Then a real package is created from that. But the configuration
language never sees these (they are created after the configuration scripts
has been run).

The package has no methods, it's just a stupid structure.
]]
function package(result, _, pkg, extra)
	extra = extra or {}
	-- Minimal typo verification. Further verification is done when actually using the package.
	for name, value in pairs(extra) do
		if not allowed_package_extras[name] then
			WARN("There's no extra option " .. name .. " for a package")
		end
		-- TODO make top level type check for all extra fields
		if name == "abi_change" then
			if not value then
				ERROR("Invalid value of extra option abi_change " .. tostring(value) .. ", package " .. pkg)
				extra[name] = nil
			elseif type(value) == "table" then
				for _, v in pairs(value) do
					if type(v) ~= "table" and type(v) ~= "string" and type(v) ~= "boolean" then
						ERROR("Invalid type in extra option abi_change " .. type(v) .. ", package " .. pkg)
					elseif type(v) == "boolean" and not v then
						ERROR("Invalid value in extra option abi_change " .. tostring(v) .. ", package " .. pkg)
					elseif type(v) == "table" and v.tp ~= "package" then
						ERROR("Invalid type in extra option abi_change " .. tostring(v.tp) .. ", package " .. pkg)
					end
				end
			end
		end
		-- TODO: Validate the types etc of extra options
	end
	utils.table_merge(result, extra)
	result.name = pkg
	result.tp = "package"
	table.insert(known_packages, result)
end

--[[
Either create a new package of that name (if string is passed) or
pass the provided package.
]]

function package_wrap(context, pkg)
	if type(pkg) == "table" and pkg.tp == "package" then
		-- It is already a package object
		return pkg
	else
		local result = {}
		package(result, context, pkg)
		return result
	end
end

-- List of allowed extra options for a Repository command
local allowed_repository_extras = utils.arr2set({
	"subdirs",
	"index",
	"ignore",
	"priority",
	"verification",
	"sig",
	"pubkey",
	"ca",
	"crl"
})

--[[
The repositories we already created. If there are multiple repos of the
same name, we are allowed to provide any of them. Therefore, this is
indexed by their names.
]]
known_repositories = {}
-- One with all the repositories, even if there are name collisions
known_repositories_all = {}

-- Order of the repositories as they are parsed
repo_serial = 1

--[[
Promise of a future repository. The repository shall be downloaded after
all the configuration scripts are run, parsed and used as a source of
packages. Then it shall mutate into a parsed repository object, but
until then, it is just a stupid data structure without any methods.
]]
function repository(result, context, name, repo_uri, extra)
	extra = extra or {}
	-- Catch possible typos
	for name in pairs(extra) do
		if not allowed_repository_extras[name] then
			WARN("There's no extra option " .. name .. " for a repository")
		end
		-- TODO: Validate the types etc of extra options
	end
	utils.table_merge(result, extra)
	result.repo_uri = repo_uri
	utils.private(result).context = context
	--[[
	Start the download. This way any potential access violation is reported
	right away. It also allows for some parallel downloading while we process
	the configs.

	Pass result as the validation parameter, as all validation info would be
	part of the extra.

	We do some mangling with the sig URI, since they are not at Package.gz.sig, but at
	Package.sig only.
	]]
	if extra.subdirs then
		utils.private(result).index_uri = {}
		for _, sub in pairs(extra.subdirs) do
			sub = "/" .. sub
			local u = repo_uri .. sub .. '/Packages.gz'
			local params = utils.table_overlay(result)
			params.sig = repo_uri .. sub .. '/Packages.sig'
			utils.private(result).index_uri[sub] = uri(context, u, params)
		end
	else
		local u = result.index or repo_uri .. '/Packages.gz'
		local params = utils.table_overlay(result)
		params.sig = params.sig or u:gsub('%.gz$', '') .. '.sig'
		utils.private(result).index_uri = {[""] = uri(context, u, params)}
	end
	result.priority = result.priority or 50
	result.serial = repo_serial
	repo_serial = repo_serial + 1
	result.name = name
	result.tp = "repository"
	known_repositories[name] = result
	table.insert(known_repositories_all, result)
end

-- Either return the repo, if it is one already, or look it up. Nil if it doesn't exist.
function repository_get(repo)
	if type(repo) == "table" and (repo.tp == "repository" or repo.tp == "parsed-repository") then
		return repo
	else
		return known_repositories[repo]
	end
end

local allowed_install_extras = utils.arr2set({
	"priority",
	"version",
	"repository",
	"reinstall",
	"critical",
	"ignore"
})

content_requests = {}

local function content_request(context, cmd, allowed, ...)
	local batch = {}
	local function submit(extras)
		for _, pkg in ipairs(batch) do
			pkg = package_wrap(context, pkg)
			DBG("Request " .. cmd .. " of " .. (pkg.name or pkg))
			local request = {
				package = pkg,
				tp = cmd
			}
			for name, opt in pairs(extras) do
				if not allowed[name] then
					WARN("There's no extra option " .. name .. " for " .. cmd .. " request")
				else
					request[name] = opt
				end
			end
			request.priority = request.priority or 50
			table.insert(content_requests, request)
		end
		batch = {}
	end
	for _, val in ipairs({...}) do
		if type(val) == "table" and val.tp ~= "package" then
			submit(val)
		else
			table.insert(batch, val)
		end
	end
	submit({})
end

function install(_, context, ...)
	return content_request(context, "install", allowed_install_extras, ...)
end

local allowed_uninstall_extras = utils.arr2set({
	"priority"
})

function uninstall(_, context, ...)
	return content_request(context, "uninstall", allowed_uninstall_extras, ...)
end

local allowed_script_extras = utils.arr2set({
	"security",
	"restrict",
	"verification",
	"sig",
	"pubkey",
	"ca",
	"crl",
	"ignore"
})

local function uri_validate(name, value, context)
	if type(value) == 'string' then
		value = {value}
	end
	if type(value) ~= 'table' then
		error('bad value', name .. " must be string or table")
	end
	for _, u in ipairs(value) do
		uri.parse(context, u)
	end
end

--[[
We want to insert these options into the new context, if they exist.
The value may be a function, then it is used to validate the value
from the extra options.
]]
local script_insert_options = {
	restrict = true,
	pubkey = uri_validate,
	ca = uri_validate,
	crl = uri_validate
}

function script(result, context, name, script_uri, extra)
	DBG("Running script " .. name)
	extra = extra or {}
	for name in pairs(extra) do
		if allowed_script_extras[name] == nil then
			WARN("There's no extra option " .. name .. " for the Script command")
		end
	end
	local u = uri(context, script_uri, extra)
	local ok, content = u:get()
	if not ok then
		if utils.arr2set(extra.ignore or {})["missing"] then
			WARN("Script " .. name .. " not found, but ignoring its absence as requested")
			result.tp = "script"
			result.name = name
			result.ignored = true
			return
		end
		-- If couldn't get the script, propagate the error
		error(content)
	end
	-- Resolve circular dependency between this module and sandbox
	local sandbox = require "sandbox"
	if extra.security and not context:level_check(extra.security) then
		error(utils.exception("access violation", "Attempt to raise security level from " .. tostring(context.sec_level) .. " to " .. extra.security))
	end
	--[[
	If it was hard to write, it should be hard to read, but I'll add a small hint
	about what it does anyway, to spoil the challenge.

	So, take the provided restrict option. If it is not there, fall back to guessing
	from the current URI of the script. However, take only the protocol and host
	part and convert it into a pattern (without anchors, they are added during
	the match).
	]]
	local restrict = extra.restrict or script_uri:match('^[^:]+:/*[^/]+'):gsub('[%^%$%(%)%%%.%[%]%*%+%-%?]', '%%%0') .. "/.*"
	--[[
	Now check that the new restrict is at least as restrictive as the old one, if there was one to begin with.
	Which means that both the new context and the parent context are "Restricted". However, if the parent is
	Restricted, the new one has no other option than to be so as well.

	We do so by making sure the old pattern is substring of the new one (with the exception of terminating .*).
	We explicitly take the prefix and compare instead of using find, because find uses a pattern. We want
	to compare tu patterns, not match one against another.
	]]
	local parent_restrict_trunc = (context.restrict or ''):gsub('%.%*$', '')
	if context.sec_level == sandbox.level("Restricted") and restrict:gsub('%.%*$', ''):sub(1, parent_restrict_trunc:len()) ~= parent_restrict_trunc then
		error(utils.exception("access violation", "Attempt to lower URL restriction"))
	end
	-- Insert the data related to validation, so scripts inside can reuse the info
	local merge = {}
	for name, check in pairs(script_insert_options) do
		if extra[name] ~= nil then
			if type(check) == 'function' then
				check(name, extra[name], context)
			end
			merge[name] = utils.clone(extra[name])
		end
	end
	merge.restrict = restrict
	local err = sandbox.run_sandboxed(content, name, extra.security, context, merge)
	if err and err.tp == 'error' then
		if not err.origin then
			err.oririn = script_uri
		end
		error(err)
	end
	-- Return a dummy handle, just as a formality
	result.tp = "script"
	result.name = name
	result.uri = script_uri
end

function store_flags(_, context, ...)
	DBG("Storing flags ", ...)
	backend.flags_mark(context.full_name, ...)
	backend.flags_write(false)
end

return _M
