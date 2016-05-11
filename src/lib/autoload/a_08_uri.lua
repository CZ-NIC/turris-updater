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
local tonumber = tonumber
local tostring = tostring
local pcall = pcall
local type = type
local unpack = unpack
local setmetatable = setmetatable
local table = table
local string = string
local events_wait = events_wait
local download = download
local utils = require "utils"

module "uri"

local function percent_decode(text)
	return text:gsub('%%(..)', function (encoded)
		local cnum = tonumber(encoded, 16)
		if not cnum then
			error(utils.exception("bad value", encoded .. " is not a hex number"))
		end
		return string.char(cnum)
	end)
end

--[[
The following function is borrowed from http://lua-users.org/wiki/BaseSixtyFour
-- Lua 5.1+ base64 v3.0 (c) 2009 by Alex Kloss <alexthkloss@web.de>
-- licensed under the terms of the LGPL2
]]
-- character table string
local b='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'

-- decoding
local function base64_decode(data)
    data = string.gsub(data, '[^'..b..'=]', '')
    return (data:gsub('.', function(x)
        if (x == '=') then return '' end
        local r,f='',(b:find(x)-1)
        for i=6,1,-1 do r=r..(f%2^i-f%2^(i-1)>0 and '1' or '0') end
        return r;
    end):gsub('%d%d%d?%d?%d?%d?%d?%d?', function(x)
        if (#x ~= 8) then return '' end
        local c=0
        for i=1,8 do c=c+(x:sub(i,i)=='1' and 2^(8-i) or 0) end
        return string.char(c)
    end))
end
-- End of borrowed function.

local function handler_data(uri, err_cback, done_cback)
	local params, data = uri:match('^data:([^,]*),(.*)')
	if not data then
		return err_cback(utils.exception("malformed URI", "It doesn't look like data URI"))
	end
	local ok, result = pcall(percent_decode, data)
	if ok then
		data = result
	else
		return err_cback(utils.exception("malformed URI", "Bad URL encoding"))
	end
	params = utils.lines2set(params, ';')
	if params['base64'] then
		local ok, result = pcall(base64_decode, data)
		if ok then
			data = result
		else
			return err_cback(utils.exception("malformed URI", "Bad base64 data"))
		end
	end
	-- Once decoded, this is complete ‒ nothing asynchronous about this URI
	done_cback(data)
end

local function handler_file(uri, err_cback, done_cback)
	local fname = uri:match('^file://(.*)')
	if not fname then
		return err_cback(utils.exception("malformed URI", "Not a file:// URI"))
	end
	local ok
	ok, fname = pcall(percent_decode, fname)
	if not ok then
		return err_cback(utils.exception("malformed URI", "Bad URL encoding"))
	end
	local ok, content, err = pcall(utils.slurp, fname)
	if (not ok) or (not content) then
		return err_cback(utils.exception("unreachable", tostring(content or err)))
	end
	done_cback(content)
end

-- Actually, both for http and https
local function handler_http(uri, err_cback, done_cback, ca)
	return download(function (status, answer)
		if status == 200 then
			done_cback(answer)
		else
			err_cback(utils.exception("unreachable", tostring(answer)))
		end
	end, uri, ca)
end

local handlers = {
	data = {
		handler = handler_data,
		immediate = true,
		def_verif = 'none',
		sec_level = 'Restricted'
	},
	file = {
		handler = handler_file,
		immediate = true,
		def_verif = 'none',
		sec_level = 'Local'
	},
	http = {
		handler = handler_http,
		def_verif = 'sig',
		sec_level = 'Restricted'
	},
	https = {
		handler = handler_http,
		can_check_cert = true,
		def_verif = 'both',
		sec_level = 'Restricted'
	}
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
	if not context:level_check(handler.sec_level) then
		error(utils.exception("access violation", "At least " .. handler.sec_level .. " level required for " .. schema .. " URI"))
	end
	-- TODO: Check restricted URIs
	-- Some auxiliary functions
	verification = verification or {}
	local function ver_lookup(field, default)
		if verification[field] ~= nil then
			return verification[field]
		elseif context[field] ~= nil then
			return context[field]
		else
			return default
		end
	end
	local vermode = ver_lookup('verification', handler.def_verif)
	local do_cert = handler.can_check_cert and (vermode == 'both' or vermode == 'cert')
	local use_ca
	if do_cert then
		local ca = ver_lookup('ca')
		if type(ca) == 'string' and ca:match('^file://') then
			-- FIXME: It might not be allowed to use this from the verification (but it would from the context)
			use_ca = ca:match('^file://(.*)')
		elseif type(ca) == 'string' then
			error(utils.exception('not implemented', "Currently only file:// URIs are supported for CA"))
		elseif type(ca) == 'table' then
			error(utils.exception('not implemented', "Multiple CA files are not supported yet"))
		else
			error(utils.exception('bad value', "The ca must be either string or table, not " .. type(ca)))
		end
	end
	-- TODO: CRL
	-- Prepare the result and callbacks into the handler
	local result = {
		tp = "uri",
		done = false,
		uri = uri,
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
		return self:ok(), self.content or self.err
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
	result.events = {handler.handler(uri, err_cback, done_cback, use_ca)}
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
