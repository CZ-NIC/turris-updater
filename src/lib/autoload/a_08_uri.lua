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

local error = error
local ipairs = ipairs
local pairs = pairs
local unpack = unpack
local setmetatable = setmetatable
local table = table
local utils = require "utils"

module "uri"

local handlers = {

}

function wait(...)
	local events = {}
	local offset = 0
	for _, u in pairs({...}) do
		for i, e in pairs(u.events) do
			events[i + offset] = e
		end
		offset = offset + #u.events
	end
	events_wait(unpack(events))
end

function new(context, uri, verification)
	local schema = uri:match('^(%a+):')
	if not schema then
		error(utils.exception("bad value", "Malformed URI " .. uri))
	end
	local handler = handlers[schema]
	if not handler then
		error(utils.exception("bad value", "Unknown URI schema " .. schema))
	end
	-- Prepare the result and callbacks into the handler
	local result = {
		tp = "uri",
		done = false,
		callbacks = {},
		events = {}
	}
	function result:ok()
		if self.done then
			return self.err == nil
		else
			return nil
		end
	end
	function result:get()
		wait(self)
		return self:ok(), self.content or content.err
	end
	local function dispatch()
		if result.done then
			for _, cback in ipairs(result.callbacks) do
				cback(result:get())
			end
			result.callbacks = {}
		end
	end
	function result:cback(cback)
		table.insert(self.callbacks, cback)
		dispatch()
	end
	local function err_cback(err)
		result.done = true
		result.err = err
		result.events = {}
		dispatch()
	end
	local function done_cback(content)
		result.done = true
		result.content = content
		result.events = {}
		dispatch()
	end
	--[[
	It can actually raise an error if that uri is not allowed in the given content.
	Things like non-existing file is reported through the err_cback
	]]
	handler(context, uri, verification, err_cback, done_cback)
	return result
end

--[[
Magic allowing to just call the module and get the corresponding object.
Instead of calling uri.new("file:///stuff"), uri("file://stuff") can be
used (the first version works too).
]]
local meta = {
	__call = function (module, context, uri, verification)
		return new(context, uri, verification)
	end
}

return setmetatable(_M, meta)
