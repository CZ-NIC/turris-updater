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

--[[
This module prepares and manipulates contexts and environments for
the configuration scripts to be run in.
]]

local pairs = pairs
local ipairs = ipairs
local type = type
local string = string
local error = error
local require = require
local tostring = tostring
local assert = assert
local table = table
local utils = require "utils"
local uri = require "uri"
local backend = require "backend"
local DBG = DBG
local WARN = WARN

module "requests"

-- luacheck: globals known_packages package_wrap known_repositories known_repositories_all repo_serial repository repository_get content_requests install uninstall script store_flags known_content_packages

-- Verifications fields are same for script, repository and package. Lets define them here once and then just append.
local allowed_extras_verification = {
	["verification"] = utils.arr2set({"string"}),
	["sig"] = utils.arr2set({"string"}),
	["pubkey"] = utils.arr2set({"string", "table"}),
	["ca"] = utils.arr2set({"string", "table"}),
	["crl"] = utils.arr2set({"string", "table"}),
	["ocsp"] = utils.arr2set({"boolean"})
}

-- Just die with common message about invalid type in extra field
local function extra_field_invalid_type(value, field, what)
	local t
	if type(value) == "table" and value.tp then
		assert(type(value.tp) == "string")
		t = value.tp
	else
		t = type(value)
	end
	error(utils.exception("bad value", "Invalid type " .. t .. " of extra option " .. field .. " for a " .. what))
end

--[[
Checks if extra fields are allowed and if they have correct types. More checking
have to be done in command it self.

First argument (allowed_extras) is table where key is allowed extra and value is
set of allowed types. Argument "what" is string used in messages. Last argument
(extra) contains all passed extras.
It returns argument extra with field nilled for invalid types.
]]
local function allowed_extras_check_type(allowed_extras, what, extra)
	if type(extra) ~= "table" then
		error(utils.exception("bad value", "Invalid type " .. type(extra) .. " (table expected) of extras for a " .. what))
	end
	for name, value in pairs(extra) do
		if allowed_extras[name] then
			if not allowed_extras[name][type(value)] then
				extra_field_invalid_type(value, name, what)
			end
		else
			WARN("There's no extra option " .. name .. " for a " .. what)
			extra[name] = nil -- this is not required, but it might be possible to break updater with some clever field name that we might use later on in code.
		end
	end
	return extra
end

-- Common check for package type. We die if given field isn't package type or string.
local function extra_check_package_type(pkg, field)
	if (type(pkg) ~= "string" and type(pkg) ~= "table") or (type(pkg) == "table" and pkg.tp ~= "package") then
		extra_field_invalid_type(pkg, field, "package")
	end
end

-- Common check for accepted values in table
local function extra_check_table(field, what, table, accepted)
	local acc = utils.arr2set(accepted)
	for _, v in pairs(table) do
		if type(v) ~= "string" then
			extra_field_invalid_type(v, field, what)
		end
		if not acc[v] then
			WARN("Unknown value " .. v .. " in table of extra option " .. field .. " for a " .. what)
		end
	end
end

-- Common check for verification field
local function extra_check_verification(what, extra)
	if extra.verification == nil then return end -- we don't care if there is no setting
	if not utils.arr2set({"none", "cert", "sig", "both"})[extra.verification] then
		error(utils.exception("bad value", "Invalid value " .. extra.verification .. " in extra option verification for a " .. what))
	end
	for _, name in pairs({"pubkey", "ca", "crl"}) do
		if type(extra[name]) == "table" then
			for _, v in pairs(extra[name]) do
				if type(v) ~= "string" then
					extra_field_invalid_type(v, name, what)
				end
			end
		end
	end
end

local allowed_package_extras_hooks = utils.arr2set({"table", "function"})
local allowed_package_extras = {
	["virtual"] = utils.arr2set({"boolean"}),
	["deps"] = utils.arr2set({"string", "table"}),
	["order_after"] = utils.arr2set({"string", "table"}),
	["order_before"] = utils.arr2set({"string", "table"}),
	["pre_inst"] = allowed_package_extras_hooks,
	["post_inst"] = allowed_package_extras_hooks,
	["pre_rm"] = allowed_package_extras_hooks,
	["post_rm"] = allowed_package_extras_hooks,
	["reboot"] = utils.arr2set({"string"}),
	["replan"] = utils.arr2set({"boolean", "string"}),
	["abi_change"] = utils.arr2set({"table", "boolean"}),
	["content"] = utils.arr2set({"string"}),
	["priority"] = utils.arr2set({"number"}),
	["ignore"] = utils.arr2set({"table"})
}
utils.table_merge(allowed_package_extras, allowed_extras_verification)

local function extra_check_deps(what, field, deps)
	local function invalid(v)
		error(utils.exception("bad value", "Invalid type " .. tostring(v) .. " (expecting dependency description) of extra option " .. field .. " for a " .. what))
	end
	if type(deps) == "table" then
		if table.tp then
			local tp = table.tp
			if tp ~= "dep-and" and tp ~= "dep-or" and tp ~= "dep-not" and tp ~= "package" then
				invalid(tp)
			end
		else
			for _, v in pairs(deps) do -- iterate trough dependency
				extra_check_deps(what, field, v)
			end
		end
	elseif type(deps) == "string" then
		if not string.match(deps, "[^%s]+") and not string.match(deps, "[^%s]+ ([=<>]+.*)") then
			error(utils.exception("bad value", "Invalid dependency description " .. deps .. " in extra option " .. field .. " for a " .. what))
		end
	else
		invalid(deps)
	end
end

--[[
We simply store all package promises, so they can be taken
into account when generating the real packages. Note that
there might be multiple package promises for a single package.
We just store them in an array for future processing.
]]
known_packages = {}

--[[
We store here packages with content extra field. These must be
downloaded and parsed before they are aggregated. But they are
also added to known_packages, this is just list of packages needing
special treatment.
]]
known_content_packages = {}

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
function package(result, content, pkg, extra)
	-- Minimal typo verification. Further verification is done when actually using the package.
	extra = allowed_extras_check_type(allowed_package_extras, "package", extra or {})
	extra_check_verification("package", extra)
	for name, value in pairs(extra) do
		if name == "deps" then
			extra_check_deps("package", name, value)
		elseif name == "reboot" then
			if not utils.arr2set({"delayed", "finished", "immediate"})[value] then
				error(utils.exception("bad value", "Invalid value " .. value .. " in extra option " .. name .. " for a package"))
			end
		elseif (name == "order_after" or name == "order_before") then
			if type(value) == "table" then
				for _, v in pairs(value) do
					extra_check_package_type(v, name)
				end
			else
				extra_check_package_type(value, name)
			end
		elseif (name == "pre_inst" or name == "post_inst" or name == "pre_rm" or name == "post_rm") and type(value) == "table" then
			for _, v in pairs(value) do
				if type(v) ~= "function" then
					extra_field_invalid_type(v, name)
				end
			end
		elseif name == "abi_change" and type(value) == "table" then
			for _, v in pairs(value) do
				if type(v) ~= "boolean" then
					extra_check_package_type(v, name)
				end
			end
		elseif name == "ignore" then
			extra_check_table("package", name, value, {"deps", "validation", "installation"})
		end
	end
	utils.table_merge(result, extra)
	result.name = pkg
	result.tp = "package"
	table.insert(known_packages, result)
	if extra.content then -- if content is specified, it requires special treatment before aggregation
		WARN("Content field of Package command is obsoleted! You can use local repository instead.")
		table.insert(known_content_packages, result)
		-- We start downloading right away
		utils.private(result).content_uri = uri(content, extra.content, extra)
	end
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
local allowed_repository_extras = {
	["subdirs"] = utils.arr2set({"table"}),
	["index"] = utils.arr2set({"string"}),
	["ignore"] = utils.arr2set({"table"}),
	["priority"] = utils.arr2set({"number"}),
}
utils.table_merge(allowed_repository_extras, allowed_extras_verification)

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
	-- Catch possible typos
	extra = allowed_extras_check_type(allowed_repository_extras, 'repository', extra or {})
	extra_check_verification("repository", extra)
	for name, value in pairs(extra) do
		if name == "subdirs" or name == "ignore" then
			for _, v in pairs(value) do
				if type(v) ~= "string" then
					extra_field_invalid_type(v, name, "repository")
				end
			end
		elseif name == "ignore" then
			extra_check_table("repository", name, value, {"missing", "integrity", "syntax"})
		end
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

local allowed_install_extras = {
	["priority"] = utils.arr2set({"number"}),
	["version"] = utils.arr2set({"string"}),
	["repository"] = utils.arr2set({"string", "table"}),
	["reinstall"] = utils.arr2set({"boolean"}),
	["critical"] = utils.arr2set({"boolean"}),
	["ignore"] = utils.arr2set({"table"})
}

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
			extras = allowed_extras_check_type(allowed, cmd, extras)
			for name, value in pairs(extras) do
				if name == "repository" and type(value) == "table" then
					for _, v in pairs(value) do
						if type(v) ~= "string" then
							extra_field_invalid_type(v, name, cmd)
						end
					end
				elseif name == "ignore" then -- note: we don't check what cmd we have as allowed_extras_check_type filters out ignore parameters for uninstall
					extra_check_table("cmd", name, value, {"missing"})
				end
			end
			utils.table_merge(request, extras)
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

local allowed_uninstall_extras = {
	["priority"] = utils.arr2set({"number"})
}

function uninstall(_, context, ...)
	return content_request(context, "uninstall", allowed_uninstall_extras, ...)
end

local allowed_script_extras = {
	["security"] = utils.arr2set({"string"}),
	["restrict"] = utils.arr2set({"string"}),
	["ignore"] = utils.arr2set({"table"})
}
utils.table_merge(allowed_script_extras, allowed_extras_verification)

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
	crl = uri_validate,
	ocsp = true
}

-- Remember here all executed scripts (by name)
local script_executed = {}

function script(result, context, name, script_uri, extra)
	if script_executed[context.full_name .. '/' .. name] then
		error(utils.exception("inconsistent", "Script with name " .. name .. " was already executed."))
	end
	script_executed[context.full_name .. '/' .. name] = true
	DBG("Running script " .. name)
	extra = allowed_extras_check_type(allowed_script_extras, 'script', extra or {})
	extra_check_verification("script", extra)
	for name, value in pairs(extra) do
		if name == "ignore" then
			extra_check_table("script", name, value, {"missing", "integrity"})
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
