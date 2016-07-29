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

-- Generate appropriate logging functions
for _, name in ipairs({ 'ERROR', 'WARN', 'INFO', 'DBG' }) do
	_G[name] = function(...)
		log(name, ...)
	end
end

-- The DIE function (which should really terminate, not just throw)
function DIE(...)
	log('DIE', ...)
	os.exit(1)
end

function log_event(action, package)
	if state_log_enabled() then
		local f = io.open("/tmp/update-state/log2", "a")
		if f then
			f:write(action, " ", package, "\n")
			f:close()
		end
	end
end

-- Function used from C to generate message from error
function c_pcall_error_handler(err)
	function err2string(msg, err)
		if type(err) == "string" then
			msg = msg .. "\n" .. err
		elseif err.tp == "error" then
			msg = msg .. "\n" .. err.reason .. ": " .. err.msg
		else
			error(utils.exception("Unknown error", "Unknown error"))
		end
		return msg
	end

	local msg = ""
	if (err.errors) then -- multiple errors
		for _, merr in pairs(err.errors) do
			msg = err2string(msg, merr)
		end
	else
		msg = err2string(msg, err)
	end
	return {msg=msg, trace=stacktraceplus.stacktrace()}
end
