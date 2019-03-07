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
local print = print

--[[
This module prepares and manipulates contexts and environments for
the configuration scripts to be run in.
]]

local pairs = pairs
local ipairs = ipairs
local type = type
local pcall = pcall
local string = string
local error = error
local require = require
local tostring = tostring
local assert = assert
local table = table
local unpack = unpack
local utils = require "utils"
local uri = require "uri"
local DBG = DBG
local WARN = WARN
local ERROR = ERROR

module "requests"

-- luacheck: globals known_packages known_repositories repositories_uri_master repo_serial repository content_requests install uninstall script package

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
	["abi_change_deep"] = utils.arr2set({"table", "boolean"}),
	["priority"] = utils.arr2set({"number"}),
	["ignore"] = utils.arr2set({"table"})
}
utils.table_merge(allowed_package_extras, allowed_extras_verification)

local function extra_check_deps(what, field, deps)
	local function invalid(v)
		error(utils.exception("bad value", "Invalid type " .. tostring(v) .. " (expecting dependency description) of extra option " .. field .. " for a " .. what))
	end
	if type(deps) == "table" then
		if deps.tp then
			local tp = deps.tp
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
	end end

--[[
We simply store all package promises, so they can be taken
into account when generating the real packages. Note that
there might be multiple package promises for a single package.
We just store them in an array for future processing.
]]
known_packages = {}

local function new_package(pkg_name, extra)
	local pkg = {
		tp = "package",
		name = pkg_name,
	}
	utils.table_merge(pkg, extra)
	table.insert(known_packages, pkg)
	return pkg
end

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
function package(_, pkg_name, extra)
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
	new_package(pkg_name, extra)
end

-- List of allowed extra options for a Repository command
local allowed_repository_extras = {
	["subdirs"] = utils.arr2set({"table"}),
	["index"] = utils.arr2set({"string"}),
	["ignore"] = utils.arr2set({"table"}),
	["priority"] = utils.arr2set({"number"}),
}
utils.table_merge(allowed_repository_extras, allowed_extras_verification)

-- All added known repositories
known_repositories = {}

-- Order of the repositories as they are introduced
-- We need this to decide in corner case of same repository priority
repo_serial = 1

repositories_uri_master = uri.new()

--[[
Promise of a future repository. The repository shall be downloaded after
all the configuration scripts are run, parsed and used as a source of
packages. Then it shall mutate into a parsed repository object, but
until then, it is just a stupid data structure without any methods.
]]
function repository(context, name, repo_uri, extra)
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
	-- Canonize some extra fields
	extra.ignore = utils.arr2set(extra.ignore or {})
	--[[
	We do some mangling with the sig URI, since they are not at Package.gz.sig, but at
	Package.sig only.
	]]
	local function register_repo(u, repo_name)
		if known_repositories[repo_name] then
			ERROR("Repository of name '" .. repo_name "' was already added. Repetition is ignored.")
			return
		end
		local iuri = repositories_uri_master:to_buffer(u, context.paret_script_uri)
		utils.uri_config(iuri, {unpack(extra), ["sig"] = extra.sig or u:gsub('%.gz$', '') .. '.sig'})

		local repo = {
			tp = "repository",
			index_uri = iuri,
			repo_uri = repo_uri,
			name = repo_name,
			serial = repo_serial,
		}
		utils.table_merge(repo, extra)
		repo.priority = extra.priority or 50
		known_repositories[repo_name] = repo
		repo_serial = repo_serial + 1
	end

	if extra.subdirs then
		for _, sub in pairs(extra.subdirs) do
			register_repo(repo_uri .. '/' .. sub .. '/' .. (extra.index or 'Packages.gz'), name .. '-' .. sub)
		end
	else
		register_repo(repo_uri .. '/' .. (extra.index or 'Packages.gz'), name)
	end
end

-- This is list of all requests to be fulfilled
content_requests = {}

local function content_request(context, cmd, allowed, ...)
	local batch = {}
	local function submit(extras)
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
		for _, pkg_name in ipairs(batch) do
			DBG("Request " .. cmd .. " of " .. pkg_name)
			local request = {
				package = new_package(pkg_name, {}),
				tp = cmd
			}
			utils.table_merge(request, extras)
			request.priority = request.priority or 50
			table.insert(content_requests, request)
		end
		batch = {}
	end
	for _, val in ipairs({...}) do
		if type(val) == "table" then
			submit(val)
		else
			table.insert(batch, val)
		end
	end
	submit({})
end

local allowed_install_extras = {
	["priority"] = utils.arr2set({"number"}),
	["version"] = utils.arr2set({"string"}),
	["repository"] = utils.arr2set({"string", "table"}),
	["reinstall"] = utils.arr2set({"boolean"}),
	["critical"] = utils.arr2set({"boolean"}),
	["ignore"] = utils.arr2set({"table"})
}

function install(context, ...)
	return content_request(context, "install", allowed_install_extras, ...)
end

local allowed_uninstall_extras = {
	["priority"] = utils.arr2set({"number"})
}

function uninstall(context, ...)
	return content_request(context, "uninstall", allowed_uninstall_extras, ...)
end

local allowed_script_extras = {
	["security"] = utils.arr2set({"string"}),
	["restrict"] = utils.arr2set({"string"}), -- This is now obsoleted (not used)
	["ignore"] = utils.arr2set({"table"})
}
utils.table_merge(allowed_script_extras, allowed_extras_verification)

--[[
Note that we have filler field just for backward compatibility so when we have
just one argument or two arguments where second one is table we move all arguments
to their appropriate variables.

Originally filler contained name of script.
]]
function script(context, filler, script_uri, extra)
	if (not extra and not script_uri) or type(script_uri) == "table" then
		extra = script_uri
		script_uri = filler
	else
		WARN("Syntax \"Script('script-name', 'uri', { extra })\" is deprecated and will be removed.")
	end
	extra = allowed_extras_check_type(allowed_script_extras, 'script', extra or {})
	extra_check_verification("script", extra)
	for name, value in pairs(extra) do
		if name == "ignore" then
			extra_check_table("script", script_uri, value, {"missing", "integrity"})
		end
	end
	local ok, content, u = pcall(utils.uri_content, script_uri, context.paret_script_uri, extra)
	if not ok then
		if utils.arr2set(extra.ignore or {})["missing"] then
			WARN("Script " .. script_uri .. " not found, but ignoring its absence as requested")
			return
		end
		-- If couldn't get the script, propagate the error
		error(content)
	end
	DBG("Running script " .. script_uri)
	-- Resolve circular dependency between this module and sandbox
	local sandbox = require "sandbox"
	if extra.security and not context:level_check(extra.security) then
		error(utils.exception("access violation", "Attempt to raise security level from " .. tostring(context.sec_level) .. " to " .. extra.security))
	end
	-- Insert the data related to validation, so scripts inside can reuse the info
	local merge = {
		-- Note: this uri does not contain any data (it was finished) so we use it only as paret for meta data
		["parent_script_uri"] = u
	}
	local err = sandbox.run_sandboxed(content, script_uri, extra.security, context, merge)
	if err and err.tp == 'error' then
		if not err.origin then
			err.oririn = script_uri
		end
		error(err)
	end
end

return _M
