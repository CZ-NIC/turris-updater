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

local function concat_all(...)
	local result = ''
	for _, val in ipairs({...}) do
		result = result .. val
	end
	return result
end

-- Generate appropriate logging functions
for _, name in ipairs({ 'ERROR', 'WARN', 'DBG' }) do
	_G[name] = function(...)
		log(name, concat_all(...))
	end
end

-- The DIE function (which should really terminate, not just throw)
function DIE(...)
	local msg = concat_all(...)
	log('DIE', msg)
	os.exit(1)
end
