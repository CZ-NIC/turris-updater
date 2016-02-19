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

local error = error
local type = type
local pairs = pairs
local pcall = pcall
local require = require
local unpack = unpack
local io = io
local table = table
local mkdtemp = mkdtemp
local chdir = chdir
local run_command = run_command
local events_wait = events_wait
local DBG = DBG
local WARN = WARN
local utils = require "utils"

module "backend"

--[[
Configuration of the module. It is supported (yet unlikely to be
needed) to modify these variables.
]]
-- The file with status of installed packages
status_file = "/usr/lib/opkg/status"
-- The directory where unpacked control files of the packages live
info_dir = "/usr/lib/opkg/info/"
-- Time after which we SIGTERM external commands. Something incredibly long, just prevent them from being stuck.
cmd_timeout = 600000
-- Time after which we SIGKILL external commands
cmd_kill_timeout = 900000

--[[
Parse a single block of mail-header-like records.
Return as a table.
]]--
function block_parse(block)
	local result = {}
	local name
	local value
	local function store()
		if name then
			result[name] = value
			name = nil
			value = nil
		end
	end
	for line in block:gmatch("[^\n]+") do
		local n, v = line:match('^(%S+):%s*(.*)')
		if n then
			-- The beginning of the field
			store()
			name = n
			value = v
		elseif line:match('^%s') then
			-- The continuation of a field
			if not name then
				error("Continuation at the beginning of block: " .. line)
			end
			value = value .. "\n" .. line
		else
			error("Malformed line: " .. line)
		end
	end
	store()
	return result
end

--[[
Split text into blocks separated by at least one empty line.
Returns an iterator.
]]
function block_split(string)
	local pos = 0 -- 0 is the last one we /don't/ want.
	-- Get the next block (an iterator)
	local function next_block()
		if not pos then return end
		pos = pos + 1 -- Move /after/ the last char of the previous separator
		local bstart, bend = string:find("\n\n+", pos)
		-- Omit the first character of the separator from the result
		if bstart then bstart = bstart - 1 end
		-- It's OK to call with nil ‒ we take the rest of the string
		local block = string:sub(pos, bstart)
		pos = bend
		return block
	end
	-- Filter out empty results
	local function filter_empty()
		local result = next_block()
		-- Just retry as long as the block are empty
		while result and result:len() == 0 do
			result = next_block()
		end
		return result
	end
	return filter_empty
end

--[[
Postprocess the table representing a status of a package. The original table
is modified and returned.

It does:
Splitting these fielts into subtables of items:
Conffiles
Depends
Status
]]
function package_postprocess(status)
	--[[
	If the field is present, it replaces it with a new table.
	The table is created from the field by splitting it by
	separator (list of „forbidden“ characters and extracting
	two fields from cleanup pattern. If only one is provided,
	the second is replaced by true. If the cleanup doesn't match,
	the part is thrown away.
	]]
	local function replace(name, separator, cleanup)
		if type(cleanup) == "string" then
			local c = cleanup
			cleanup = function (s) return s:match(c) end
		end
		local value = status[name]
		if value then
			local result = {}
			for item in value:gmatch("[^" .. separator .. "]+") do
				local n, v = cleanup(item)
				if n then
					if not v then v = true end
					result[n] = v
				end
			end
			status[name] = result
		end
	end
	-- Conffiles are lines with two „words“
	replace("Conffiles", "\n", "%s*(%S+)%s+(%S+)")
	-- Depends are separated by commas and may contain a version in parentheses
	local idx = 0
	replace("Depends", ",", function (s)
		idx = idx + 1
		return idx, s:gsub("%s", ""):gsub("%(", " (")
	end)
	replace("Status", " ", "(%S+)")
	return status
end

--[[
Read the whole content of given file. Return the content, or nil and error message.
In case of errors during the reading (instead of when opening), it calls error()
]]
local function slurp(filename)
	local f, err = io.open(filename)
	if not f then
		return nil, err
	end
	local content = f:read("*a")
	f:close()
	if not content then error("Could not read content of " .. filename) end
	return content
end

-- Get pkg_name's file's content with given suffix. Nil on error.
local function pkg_file(pkg_name, suffix, warn)
	local fname = info_dir .. pkg_name .. "." .. suffix
	local content, err = slurp(fname)
	if not content then
		WARN("Could not read ." .. suffix .. " file of " .. pkg_name .. ": " .. err)
	end
	return content, err
end

-- Read pkg_name's .control file and return it as a parsed block
local function pkg_control(pkg_name)
	local content = pkg_file(pkg_name, "control", true)
	if content then
		return block_parse(content)
	else
		return {}
	end
end

--
local function pkg_files(pkg_name)
	local content = pkg_file(pkg_name, "list", true)
	if content then
		local result = {}
		for l in content:gmatch("[^\n]+") do
			result[l] = true
		end
		return result
	else
		return {}
	end
end

-- Merge additions into target (both are tables)
local function merge(target, additions)
	for n, v in pairs(additions) do
		target[n] = v
	end
end

function status_parse()
	DBG("Parsing status file ", status_file)
	local result = {}
	local f, err = io.open(status_file)
	if f then
		local content = f:read("*a")
		f:close()
		if not content then error("Failed to read content of the status file") end
		for block in block_split(content) do
			local pkg = block_parse(block)
			merge(pkg, pkg_control(pkg.Package))
			pkg.files = pkg_files(pkg.Package)
			pkg = package_postprocess(pkg)
			result[pkg.Package] = pkg
		end
	else
		error("Couldn't read status file " .. status_file .. ": " .. err)
	end
	return result
end

--[[
Take the .ipk package (passed as the data, not as a path to a file) and unpack it
into a temporary location somewhere under tmp_dir. If you omit tmp_dir, /tmp is used.

It returns a path to a subdirectory of tmp_dir, where the package is unpacked.
There are two further subdirectories, control and data. Data are the files to be merged
into the system, control are the control files for the package manager.

TODO:
• Sanity checking of the package.
• Less calling of external commands.
]]
function pkg_unpack(package, tmp_dir)
	-- The first unpack goes into the /tmp
	-- We assume s1dir returs sane names of directories ‒ no spaces or strange chars in them
	local s1dir = mkdtemp()
	-- The results go into the provided dir, or to /tmp if none was provided
	-- FIXME: Sanity-check provided tmp_dir ‒ it must not contain strange chars
	local s2dir = mkdtemp(tmp_dir)
	-- If anything goes wrong, this is where we find the error message
	local err
	-- Unpack the ipk into s1dir, getting control.tar.gz and data.tar.gz
	local function stage1()
		events_wait(run_command(function (ecode, killed, stdout, stderr)
			if ecode ~= 0 then
				err = "Stage 1 unpack failed: " .. stderr
			end
		end, function () chdir(s1dir) end, package, cmd_timeout, cmd_kill_timeout, "/bin/sh", "-c", "/bin/gzip -dc | /bin/tar x"))
		-- TODO: Sanity check debian-binary
		return err == nil
	end
	-- Unpack the control.tar.gz and data.tar.gz under respective subdirs in s2dir
	local function unpack_archive(what)
		local archive = s1dir .. "/" .. what .. ".tar.gz"
		local dir = s2dir .. "/" .. what
		return run_command(function (ecode, killed, stdout, stderr)
			if ecode ~= 0 then
				err = "Stage 2 unpack of " .. what .. " failed: " .. stderr
			end
		end, nil, package, cmd_timeout, cmd_kill_timeout, "/bin/sh", "-c", "mkdir -p '" .. dir .. "' && cd '" .. dir .. "' && /bin/gzip -dc <'" .. archive .. "' | /bin/tar xp")
	end
	local function stage2()
		events_wait(unpack_archive("control"), unpack_archive("data"))
		return err == nil
	end
	-- Try-finally like construct, make sure cleanup is called no matter what
	local success, ok = pcall(function () return stage1() and stage2() end)
	-- Do the cleanups
	local events = {}
	local function remove(dir)
		-- TODO: Would it be better to remove from within our code, without calling rm?
		table.insert(events, run_command(function (ecode, killed, stdout, stderr)
			if ecode ~= 0 then
				WARN("Failed to clean up work directory ", dir, ": ", stderr)
			end
		end, nil, nil, cmd_timeout, cmd_kill_timeout, "/bin/rm", "-rf", dir))
	end
	-- Intermediate work space, not needed by the caller
	remove(s1dir)
	if err then
		-- Clean up the resulting directory in case of errors
		remove(s2dir)
	end
	-- Run all the cleanup removes in parallel
	events_wait(unpack(events))
	-- Cleanup done, call error() if anything failed
	if not success then error(ok) end
	if not ok then error(err) end
	-- Everything went well. So return path to the directory where the package is unpacked
	return s2dir
end

--[[
Look into the dir with unpacked package (the one containing control and data subdirs).
Return three tables:
• Set of files, symlinks, pipes, etc.
  (in short, the things that are owned exclusively by the package)
• Set of directories
  (which may be shared between packages)
• Map of config files with their md5 sums.

In all three cases, the file names are keys, not values.

In case of errors, it raises error()
]]
function pkg_examine(dir)
	local data_dir = dir .. "/data"
	-- Events to wait for
	local events = {}
	local err = nil
	-- Launch scans of the data directory
	local function launch(dir, postprocess, ...)
		local function cback(ecode, killed, stdout, stderr)
			if ecode == 0 then
				postprocess(stdout)
			else
				err = stderr
			end
		end
		local event = run_command(cback, function () chdir(dir) end, nil, cmd_timeout, cmd_kill_timeout, ...)
		table.insert(events, event)
	end
	local function find_result(text)
		--[[
		Split into „lines“ separated by 0-char. Then eat leading dots and, in case
		there was only a dot, replace it by /.
		]]
		return utils.map(utils.lines2set(text, "%z"), function (f) return f:gsub("^%.", ""):gsub("^$", "/"), true end)
	end
	local files, dirs
	-- One for non-directories
	launch(data_dir, function (text) files = find_result(text) end, "/usr/bin/find", "!", "-type", "d", "-print0")
	-- One for directories
	launch(data_dir, function (text) dirs = find_result(text) end, "/usr/bin/find", "-type", "d", "-print0")
	-- Wait for all asynchronous processes to finish
	events_wait(unpack(events))
	-- How well did it go?
	if err then
		error(err)
	end
	return files, dirs
end

return _M
