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
local require = require
local unpack = unpack
local setmetatable = setmetatable
local table = table
local os = os
local io = io
local file = file
local string = string
local events_wait = events_wait
local download = download
local run_command = run_command
local utils = require "utils"
local DBG = DBG

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
local function handler_http(uri, err_cback, done_cback, ca, crl)
	return download(function (status, answer)
		if status == 200 then
			done_cback(answer)
		else
			err_cback(utils.exception("unreachable", uri .. ": " .. tostring(answer)))
		end
	end, uri, ca, crl)
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

local function tempfile()
	local fname = os.tmpname()
	local f, err = io.open(fname, "w")
	if not f then DIE(err) end
	return fname, f
end

local function tmpstore(content)
	local fname, f = tempfile()
	f:write(content)
	f:close()
	return fname
end

function signature_check(content, key, signature)
	local ok
	local fcontent = tmpstore(content)
	local fkey = tmpstore(key)
	local fsig = tmpstore(signature)
	events_wait(run_command(function (ecode)
		ok = (ecode == 0)
	end, nil, nil, -1, -1, '/usr/bin/usign', '-V', '-p', fkey, '-x', fsig, '-m', fcontent))
	os.remove(fcontent)
	os.remove(fkey)
	os.remove(fsig)
	return ok
end

local allowed_verifications = utils.arr2set({'none', 'cert', 'sig', 'both'})

-- Parse the uri and throw an error or return the handler for it
function parse(context, uri)
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
	return handler
end

function new(context, uri, verification)
	DBG("Creating new URI: ", uri)
	local handler = parse(context, uri)
	-- TODO: Check restricted URIs
	-- Prepare verification
	verification = verification or {}
	local context_nocheck_cache
	local function context_nocheck(want_context)
		if want_context and not context_nocheck_cache then
			local sandbox = require "sandbox"
			context_nocheck_cache = sandbox.new('Full', context)
		end
		return context_nocheck_cache
	end
	-- Try to find out verification parameter, in the verification argument, in the context or use a default. Provide a context used to check the required URIs inside the result (eg. if it is inherited, don't check them).
	local function ver_lookup(field, default, want_context)
		if verification[field] ~= nil then
			return verification[field], context
		elseif context[field] ~= nil then
			return context[field], context_nocheck(want_context)
		else
			return default, context
		end
	end
	local result = {
		tp = "uri",
		done = false,
		done_primary = false,
		uri = uri,
		callbacks = {},
		events = {}
	}
	local tmp_files = {}
	-- Called when everything is done, to remove some temporary files (will not be needed once we move to libcurl)
	local function cleanup()
		for _, fname in ipairs(tmp_files) do
			os.remove(fname)
		end
		tmp_files = {}
	end
	-- Soft failure during the preparation
	local function give_up(err)
		result.done = true
		result.err = err
		result.events = {}
		cleanup()
		return result
	end
	local vermode = ver_lookup('verification', handler.def_verif)
	if not allowed_verifications[vermode] then
		error(utils.exception('bad value', "Unknown verification mode " .. vermode))
	end
	local do_cert = handler.can_check_cert and (vermode == 'both' or vermode == 'cert')
	local use_ca, use_crl
	if do_cert then
		local ca, ca_context = ver_lookup('ca', nil, true)
		local function pem_get(uris, context)
			local fname, f = tempfile()
			table.insert(tmp_files, fname)
			if type(uris) == 'string' then
				uris = {uris}
			end
			if type(uris) == 'table' then
				for _, curi in ipairs(uris) do
					local u = new(context, curi, {verification = 'none'})
					local ok, content = u:get()
					if not ok then
						give_up(content)
						return nil
					end
					f:write(content)
				end
			else
				error(utils.exception('bad value', "The ca and crl must be either string or table, not " .. type(uris)))
			end
			f:close()
			return fname
		end
		use_ca = pem_get(ca, ca_context)
		if not use_ca then
			return result
		end
		local crl, crl_context = ver_lookup('crl', nil, true)
		if crl then
			use_crl = pem_get(crl, crl_context)
			if not use_crl then
				return result
			end
		end
	end
	local do_sig = vermode == 'both' or vermode == 'sig'
	local sig_data
	local sig_pubkeys = {}
	local sub_uris = {}
	if do_sig then
		-- As we check the signature after we download everything, just schedule it now
		local sig_uri = verification.sig or uri .. ".sig"
		local sig_veri = utils.table_overlay(verification)
		sig_veri.verification = 'none'
		if do_cert then sig_veri.verification = 'cert' end -- If the main resource checks cert, the .sig should too
		sig_data = new(context, sig_uri, sig_veri)
		table.insert(sub_uris, sig_data)
		local pubkeys, pk_context = ver_lookup('pubkey', nil, true)
		if type(pubkeys) == 'string' then
			pubkeys = {pubkeys}
		end
		if type(pubkeys) == 'table' then
			for _, uri in ipairs(pubkeys) do
				local u = new(pk_context, uri, {verification = 'none'})
				table.insert(sub_uris, u)
				table.insert(sig_pubkeys, u)
			end
		else
			error(utils.exception('bad value', "The pubkey must be either string or table, not " .. type(uris)))
		end
	end
	-- Prepare the result and callbacks into the handler
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
	local wait_sub_uris = #sub_uris
	local function dispatch()
		if result.done_primary and wait_sub_uris == 0 and not result.done then
			result.done = true
			if do_sig and result.err == nil then
				local ok, signature = sig_data:get()
				if ok then
					local found = false
					local key_broken = false
					local function sigval(content)
						for _, key in ipairs(sig_pubkeys) do
							local ok, k = key:get()
							if ok then
								if signature_check(content, k, signature) then
									found = true
									break
								end
							else
								key_broken = true
							end
						end
					end
					sigval(result.content)
					if not found and result.content:sub(1, 2) == string.char(0x1F, 0x8B) then
						-- Try once more with gzip decompressed
						function gzip_done(ecode, killed, stdout)
							if ecode == 0 then
								sigval(stdout)
							end
						end
						events_wait(run_command(gzip_done, nil, result.content, -1, -1, '/bin/gzip', '-c', '-d'))
					end
					if not found then
						local msg = "Signature validation failed"
						if key_broken then
							msg = msg .. " (some keys are missing)"
						end
						result.err = utils.exception("corruption", msg)
						result.content = nil
					end
				else
					result.err = signature
					result.content = nil
				end
			end
		end
		if result.done then
			cleanup()
			result.events = {}
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
		result.done_primary = true
		result.err = err
		dispatch()
	end
	local function done_cback(content)
		result.done_primary = true
		result.content = content
		dispatch()
	end
	result.events = {handler.handler(uri, err_cback, done_cback, use_ca, use_crl)}
	-- Wait for the sub uris and include them in our events
	local function sub_cback()
		wait_sub_uris = wait_sub_uris - 1
		dispatch()
	end
	for _, subu in ipairs(sub_uris) do
		subu:cback(sub_cback)
		for _, e in ipairs(subu.events) do
			table.insert(result.events, e)
		end
	end
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
