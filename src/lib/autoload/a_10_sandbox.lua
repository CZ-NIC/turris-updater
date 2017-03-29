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

local G = _G
local pairs = pairs
local type = type
local loadstring = loadstring
local setfenv = setfenv
local pcall = pcall
local setmetatable = setmetatable
local tostring = tostring
local error = error
local unpack = unpack
local assert = assert
local next = next
local DBG = DBG
local WARN = WARN
local run_command = run_command
local events_wait = events_wait
local get_updater_version = get_updater_version
local utils = require "utils"
local backend = require "backend"
local requests = require "requests"
local uci_ok, uci = pcall(require, "uci")
if not uci_ok then
	ERROR("The uci library is not available. Continuing without it and expecting this is a test run on development PC.")
	uci = nil
end

module "sandbox"

-- luacheck: globals morpher state_vars level new run_sandboxed load_state_vars

-- This can be changed often (always when we add some new feature). So it is defined here at top and not buried in code. 
local updater_features = utils.arr2set({
	'priorities',
	'provides',
	'abi_change'
})

-- WARNING: BEGIN_MAGIC (read the design morphers documentation)

--[[
In short, morphers allow creating functions that can be called like:

function "Param" "Param" { x = 1, y = 2 }

They do so by accumulating the parameters through several separate function
calls and then doing the real call before use or when another morpher is called.
The function shall return the data in a table passed to it as a first parameter.
That table is empty and it can be filled with whatever object, possibly with
a meta table.

That table is actually the same one as the morpher object that accumulated the
parameters, but it has been gutted now, so the same address is shared
by the real result.
]]

-- The currently active morpher, so we can morph it as soon as we know we're done with it.
local active_morpher = nil

--[[
Return a morpher object calling the given function when done. The function
is called with any additional parameters passed here, concatenated with
the parameters accumulated later on.

Tip for use: The result is single use only. So, plug something like
function (...)
	return morpher(func, context, ...)
end

into the environment, instead of
morpher(func, context)
]]
function morpher(func, ...)
	if active_morpher then
		active_morpher:morph()
	end
	local params = {...}
	local index_pos = #params
	--[[
	We provide an empty table with meta table. This way,
	the __index and __newindex events happen every time,
	no matter what field is requested.

	The meta table contains references to closures,
	so the data is actually passed here in local variables.
	]]
	local result = {}
	local name = tostring(result)
	-- Accumulate some more parameters into the parameter list
	local function call(table, ...)
		local new_params = {...}
		--[[
		Just append the new parameters, by shifting
		the index by the appropriate number. We don't
		need (slightly slower) ipairs, since we don't
		need to copy them in order, they'll end up at
		the correct place.
		]]
		for i, v in pairs(new_params) do
			params[i + index_pos] = v
		end
		index_pos = index_pos + #new_params
		DBG("Added ", #new_params, " parameters to ", name)
		-- Pass the morpher further, to allow more calls
		return table
	end
	local function morph(result)
		DBG("Morphing ", name)
		-- The morpher is no longer active
		active_morpher = nil
		-- Get rid of the old meta table
		setmetatable(result, nil)
		-- The table should actually be empty
		assert(not next(result))
		-- We don't support multiple results yet. We may do so in future somehow, if needed.
		func(result, unpack(params))
		-- return the table we morphed into, just for good order.
		return result
	end
	local meta = {
		-- We accumulate the parameters by repeatedly invoking this behind the scenes
		__call = call,
		--[[
		Selection of operations. They first morph into the result
		and propagate the operation further.

		More operations are possible, we just don't think they'd be needed. If they
		are, they may be simply added.
		]]
		__index = function (table, key)
			if key == "morph" then
				-- Allow direct morphing by a request (shouldn't be much needed externally)
				return morph
			end
			morph(table)
			return table[key]
		end,
		__newindex = function (table, key, value)
			morph(table)
			table[key] = value
		end,
		__tostring = function (table)
			morph(table)
			return tostring(table)
		end
	}
	DBG("Creating morpher ", name, " with ", #params, " parameters")
	active_morpher = result
	return setmetatable(result, meta)
end

-- END_MAGIC

-- Functions and "constants" available in the restricted level
local rest_available_funcs = {
	"table",
	"string",
	"math",
	"assert",
	"error",
	"ipairs",
	"next",
	"pairs",
	"pcall",
	"select",
	"tonumber",
	"tostring",
	"type",
	"unpack",
	"xpcall",
	"ERROR",
	"WARN",
	"INFO",
	"DBG",
	"system_cas",
	"no_crl"
}

state_vars = nil

function load_state_vars()
	local status_ok, run_state = pcall(backend.run_state)
	local status
	if status_ok then
		status = run_state.status
	else
		WARN("Couldn't read the status file: " .. tostring(run_state))
		status = {}
	end
	--[[
	Some state variables provided for each sandbox. They are copied
	into each, so the fact a sandbox can modified its own copy doesn't
	bother us, it can't destroy it for others.

	Let the table be module-global, so tests can actually manipulate it.

	We ignore errors (eg. the files not existing), because some platforms
	might not have them legally and we mark that by providing nil.
	]]
	state_vars = {
		model = utils.strip(utils.slurp('/tmp/sysinfo/model')),
		board_name = utils.strip(utils.slurp('/tmp/sysinfo/board_name')),
		turris_version = utils.strip(utils.slurp('/etc/turris-version')),
		self_version = get_updater_version(),
		language_version = 1,
		features = updater_features,
		--[[
		In case we fail to read that file (it is not there), we match against
		an empty string, which produces nil ‒ the element won't be in there.
		We don't have a better fallback for platforms we don't know for now.
		]]
		architectures = {'all', (utils.slurp('/etc/openwrt_release') or ""):match("DISTRIB_TARGET='([^'/]*)")},
		installed = utils.map(status, function (name, pkg)
			if utils.multi_index(pkg, "Status", 3) == "installed" then
				return name, {
					version = pkg.Version,
					files = utils.set2arr(pkg.files or {}),
					configs = utils.set2arr(pkg.Conffiles or {}),
					-- TODO: We currently don't store the repository anywhere. So we can't provide it.
					install_time = pkg["Installed-Time"]
				}
			else
				-- The package is not installed - don't list it
				return "", nil
			end
		end)
	}
	events_wait(run_command(function (ecode, _, stdout, _)
		if ecode == 0 then
			state_vars.serial = utils.strip(stdout)
		end
	end, nil, nil, -1, -1, '/usr/bin/atsha204cmd', 'serial-number'))
end


-- Functions to be injected into an environment in the given security level
local funcs = {
	Full = {

	},
	Local = {

	},
	Remote = {

	},
	Restricted = {
		Package = {
			mode = "morpher",
			value = requests.package
		},
		Repository = {
			mode = "morpher",
			value = requests.repository
		},
		Install = {
			mode = "morpher",
			value = requests.install
		},
		Uninstall = {
			mode = "morpher",
			value = requests.uninstall
		},
		Script = {
			mode = "morpher",
			value = requests.script
		},
		StoreFlags = {
			mode = "morpher",
			value = requests.store_flags
		}
	}
}

-- The operators for dependencies. They just wrap their arguments, nothing more.
for _, name in pairs({'And', 'Or', 'Not'}) do
	local objname = "dep-" .. name:lower()
	funcs.Restricted[name] = {
		mode = "inject",
		value = function (...)
			return {
				tp = objname,
				sub = {...}
			}
		end
	}
end
-- Provide the global lua functions into places where they are needed
for _, name in pairs(rest_available_funcs) do
	funcs.Restricted[name] = {
		mode = "inject",
		value = G[name]
	}
end
if uci then
	funcs.Local.uci = {
		mode = "inject",
		value = uci
	}
end
--[[
List the variable names here. This way we ensure they are actually set in case
they are nil. This helps in testing and also ensures some other global variable
isn't mistaken for the actual value that isn't available.
]]
for _, name in pairs({'model', 'board_name', 'turris_version', 'serial', 'architectures', 'installed', 'self_version', 'language_version', 'features'}) do
	funcs.Restricted[name] = {
		mode = "state",
		value = name
	}
end
funcs.Restricted["flags"] = {
	mode = "flags"
}
for name, val in pairs(G) do
	funcs.Full[name] = {
		mode = "inject",
		value = val
	}
end
-- Add the functions to all richer contexts as well
utils.table_merge(funcs.Remote, funcs.Restricted)
utils.table_merge(funcs.Local, funcs.Remote)
utils.table_merge(funcs.Full, funcs.Local)

local level_meta = {
	__tostring = function (level)
		return level.name
	end,
	__eq = function (l1, l2)
		return l1._cmp == l2._cmp
	end,
	__lt = function (l1, l2)
		return l1._cmp < l2._cmp
	end,
	__le = function (l1, l2)
		return l1._cmp <= l2._cmp
	end
}
local level_values = {}
for i, l in pairs({"Restricted", "Remote", "Local", "Full"}) do
	level_values[l] = setmetatable({
		tp = "level",
		name = l,
		_cmp = i,
		f = funcs[l]
	}, level_meta)
end

function level(l)
	if l == nil then
		return nil
	elseif type(l) == "table" and l.tp == "level" then
		return l
	else
		return level_values[l] or error(utils.exception("bad value", "No such level " .. l))
	end
end

local function flags_new(context)
	return setmetatable({}, {
		__index = function (_, path)
			-- Make sure the hierarchy is there ‒ it was created by run_sandboxed, not standalone
			assert(context.root_parent)
			local requested
			local full_path
			if path == "" then
				-- It's us
				requested = context
			elseif path.match('^/') then
				-- It's an absolute path
				full_path = path
				requested = context.root_parent.hierarchy[path]
			else
				-- It's relative path ‒ construct the absolute one first
				full_path = context.full_name + '/' + path
				requested = context.root_parent.hierarchy[full_path]
			end
			if requested == context then
				-- It's us. Provide full access to the table (eg. let it be written there as well)
				return context.flags
			else
				return backend.flags_get_ro(full_path)
			end
		end
	})
end

--[[
Create a new context. The context inherits everything
from its parent (if the parent is not nil). The security
level is set to the one given (if nil is given, it is also
inherited).

A new environment, corresponding to the security level,
is constructed and stored in the result as „env“.
]]
function new(sec_level, parent, name)
	sec_level = level(sec_level)
	local result = {}
	--[[
	Inherit the properties of the parent context.
	We use a shallow copy, so not a clone.
	]]
	for n, v in pairs(parent or {}) do
		result[n] = v
	end
	result.parent = parent
	parent = parent or {}
	sec_level = sec_level or parent.sec_level
	result.sec_level = sec_level
	if name then
		--[[
		Construct the name, full path and a hierarchy table with all the existing
		contexts. The hierarchy table is in the top-level script.

		However, there are also name-less sandboxes. These are abused by the
		uri module and don't actually run any real code and they don't have flags.
		So we completely skip this flag manipulation for them. The flags variable
		will be broken in such sandboxes, but as nobody would access it, it
		doesn't matter.
		]]
		result.name = name
		if parent and parent.full_name then
			result.full_name = parent.full_name .. "/" .. name
			result.root_parent = parent.root_parent
		else
			result.full_name = name
			result.root_parent = result
			result.hierarchy = {}
		end
		result.root_parent.hierarchy[result.full_name or ""] = result
		result.flags = backend.flags_get(result.full_name)
	end
	-- Construct a new environment
	result.env = {}
	local inject = utils.clone
	if sec_level >= level("Full") then
		inject = function (...) return ... end
	end
	for n, v in pairs(sec_level.f) do
		if v.mode == "inject" then
			result.env[n] = inject(v.value)
		elseif v.mode == "state" then
			if state_vars == nil then
				load_state_vars()
			end
			result.env[n] = utils.clone(state_vars[v.value])
		elseif v.mode == "flags" then
			result.env[n] = flags_new(result)
		elseif v.mode == "wrap" then
			result.env[n] = function(...)
				return v.value(result, ...)
			end
		elseif v.mode == "morpher" then
			result.env[n] = function(...)
				return morpher(v.value, result, ...)
			end
		else
			DIE("Unknown environment func mode " .. v.mode)
		end
	end
	-- Pretend it is an environment
	result.env._G = result.env
	result.tp = "context"
	function result:level_check(sec_level)
		return level(sec_level) <= self.sec_level
	end
	return result
end

--[[
Run a given chunk in a sandbox.

The chunk, if it is a string, is compiled (and given the name). If it is a function,
it is run directly.

A new context is created, using the new() function. Everything that is in the
context_merge table is added to the context (not the environment contained therein).
The context_mod is run with the context as a parameter. Both context_merge and
context_mod may be nil, in which case nothing is modified.

The result describes success of the run. If nil is returned, everything went well.
Otherwise, an error description structure is returned. It looks something like this:
{
	tp = "error",
	reason = <reason>,
	msg = <human readable description>
}

The <reason> is one of:
 • "compilation": The compilation (eg. loading) of the chunk failed.
 • "runtime": An error has been caught at run time.
 • Something else ‒ anything is allowed to throw these structured exceptions and they
   are simply passed.
]]
function run_sandboxed(chunk, name, sec_level, parent, context_merge, context_mod)
	assert(active_morpher == nil)
	if type(chunk) == "string" then
		local err
		chunk, err = loadstring(chunk, name)
		if not chunk then
			return utils.exception("compilation", err)
		end
	end
	local context = new(sec_level, parent, name)
	utils.table_merge(context, context_merge or {})
	context_mod = context_mod or function () end
	context_mod(context)
	local func = setfenv(chunk, context.env)
	local ok, err = pcall(func)
	if ok and active_morpher then
		ok, err = pcall(function () active_morpher:morph() end)
	else
		active_morpher = nil
	end
	if ok then
		return context
	else
		if type(err) == "table" and err.tp == "error" then
			return err
		else
			return utils.exception("runtime", err)
		end
	end
end

return _M
