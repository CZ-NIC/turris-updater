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

local pairs = pairs
local ipairs = ipairs
local rawget = rawget
local setmetatable = setmetatable
local unpack = unpack
local table = table
local picosat = picosat

module "sat"

-- luacheck: globals new

--[[
Creates new batch object for given picosat. Batch stores clauses without pushing
them to picosat. You can call "clause" and "var" same as with picosat (Note that
"var" is immediate call to picosat). When you want to add batch to picosat, you
call "commit".

It also allows creation of sub-batches using "new_batch" method of original batch.
Such new batch is just new batch, except that it's committed when original batch
is committed. Although it can be committed on its own too.
]]--
local function new_batch(sat)
	local batch = {
		tp = "sat.batch",
		clauses = {},
		batches = {},
		sat = sat
	}
	-- Add given clause to batch
	function batch:clause(...)
		table.insert(self.clauses, {...})
	end
	-- Just a wrapper for calling picosat var method
	function batch:var(num)
		return self.sat:var(num)
	end
	-- Function to add clauses batch to picosat and all batches created as part of this batch
	function batch:commit()
		if not self.clauses then
			return
		end
		for _, clause in ipairs(batch.clauses) do
			batch.sat:clause(unpack(clause))
		end
		batch.clauses = nil
		for b, _ in pairs(batch.batches) do
			b:commit()
		end
		batch.batches = nil
	end
	-- Creates new sub-batch
	function batch:new_batch()
		local nbatch = self.sat:new_batch()
		self:reg_batch(nbatch)
		return nbatch
	end
	-- Registers existing batch as sub-batch
	function batch:reg_batch(b)
		self.batches[b] = true
	end
	-- Unregister given batch
	function batch:unreg_batch(b)
		self.batches[b] = nil
	end
	return batch
end

local function __index(sat, key)
	local v = rawget(sat, key)
	if v then return v end
	return sat._picosat[key]
end

--[[
Creates new sat object. It is extension for picosat, so you can call all methods
from picosat in addition to "new_batch".
]]--
function new()
	local picosat = picosat.new()
	local sat = {
		tp = "sat",
		_picosat = picosat,
		new_batch = new_batch
	}
	for _, c in pairs({ 'var', 'clause', 'assume', 'satisfiable', 'max_satisfiable' }) do
		sat[c] = function(sat, ...)
			return sat._picosat[c](sat._picosat, unpack({...}))
		end
	end
	setmetatable(sat, { __index = __index })
	return sat
end
