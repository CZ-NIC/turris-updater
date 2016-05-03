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
local tostring = tostring
local error = error
local pcall = pcall
local next = next
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

function get_repos()
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
		for subrepo, index_uri in pairs(repo.index_uri) do
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
			local function decompressed(ecode, killed, stdout, stderr)
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
		repo.index_uri = nil
	end
	-- Make sure everything is downloaded
	uri.wait(unpack(uris))
	uris = nil
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

local function aggregate(into, what)
	for name, value in pairs(what) do
		if not into[name] then
			into[name] = {candidates = {}, modifiers = {}}
		end
		table.insert(into[name].candidates, value)
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
				aggregate(available_packages, cont.list)
			end
		end
	end
	for _, pkg in pairs(requests.known_packages) do
		if not available_packages[pkg.name] then
			available_packages[pkg.name] = {candidates = {}, modifiers = {}}
		end
		local pkg_group = available_packages[pkg.name]
		pkg.group = pkg_group
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
		if pkg_group.virtual and #pkg_group.candidate > 1 then
			error(utils.exception("inconsistent", "More than one candidate with a virtual package " .. name))
		end
		-- Merge the modifiers together to form single one.
		local modifier = {
			tp = 'package',
			name = name,
			deps = {},
			["order-after"] = {},
			["order-before"] = {},
			["pre-install"] = {},
			["pre-remove"] = {},
			["post-install"] = {},
			["post-remove"] = {},
			reboot = false,
			abi_change = {}
		}
		for _, m in pairs(pkg_group.modifiers) do
			m.final = modifier
			-- Merge the values in
			modifier.reboot = modifier.reboot or m.reboot
			-- Take a single value or a list from the source and merge it into a set in the destination
			local function set_merge(name)
				local src = m[name]
				if type(src) == "table" then
					for _, v in pairs(src) do
						modifier[name][v] = true
					end
				else
					modifier[name][src] = true
				end
			end
			-- TODO: We need to make the deps canonical somehow for this to work properly.
			set_merge("deps")
			set_merge("order-after")
			set_merge("order-before")
			set_merge("pre-install")
			set_merge("pre-remove")
			set_merge("post-install")
			set_merge("post-remove")
			set_merge("abi_change")
			local reboot_vals = {
				[false] = 0,
				delayed = 1,
				finished = 2,
				immediate = 3
			}
			-- Pick the highest value for the reboot
			if (reboot_vals[m.reboot] or 0) > reboot_vals[modifier.reboot] then
				modifier.reboot = m.reboot
			end
		end
		pkg_group.modifier = modifier
		-- We merged them together, they are no longer needed separately
		pkg_group.modifiers = nil
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
