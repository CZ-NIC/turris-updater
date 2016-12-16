require 'lunit'
require 'utils'

function launch(test)
	local stats = lunit.main({test})
	return stats.errors, stats.failed
end

function assert_table_equal(t1, t2, tables, checked)
	if t1 == t2 then
		-- The exact same instance
		return
	end
	-- Some pretty print of errors
	if not tables then
		local ok
		ok, tables = pcall(DataDumper, {t1, t2})
		if not ok then
			tables = "Can't dump :-("
		end
	end
	-- Avoid infinite recursion
	if checked then
		if checked[t1] then
			return
		end
		checked[t1] = true
	else
		checked = {}
	end
	lunit.assert_table(t1)
	lunit.assert_table(t2)
	local function cmp(t1, t2, name)
		for k, v in pairs(t1) do
			local v2 = t2[k]
			if type(v) ~= "table" or type(v2) ~= "table" then
				-- In case of two tables, we have special comparison below
				lunit.assert_equal(v, v2, "Values for key '" .. tostring(k) .. "' differ, " .. name .. " tables: " .. tables)
			end
		end
	end
	cmp(t1, t2, " pass 1")
	cmp(t2, t1, " pass 2")
	-- Recurse into sub-tables
	for k, v in pairs(t1) do
		local v2 = t2[k]
		if type(v) == "table" and type(v2) == "table" then
			assert_table_equal(v, v2, tables, checked)
		end
	end
end

function assert_exception(func, reason, msg, extra)
	local ok, err = pcall(func)
	lunit.assert_false(ok, "It hasn't failed")
	local dumped, result = pcall(DataDumper, err)
	if not dumped then
		result = "Can't dump :-("
	end
	lunit.assert_equal("error", err.tp, "Not an exception: " .. result)
	lunit.assert_equal(reason, err.reason, "Failed with a wrong reason " .. (err.reason or err))
	if msg then
		lunit.assert_equal(msg, err.msg, "Failed with a wrong message " .. (err.msg or msg))
	end
	if extra then
		local eerr = utils.shallow_copy(extra)
		eerr.tp = 'error'
		eerr.reason = err.reason
		eerr.msg = err.msg
		assert_table_equal(eerr, err)
	end
end

mocks_called = {}
local mocks_origs = {}

-- Insert fun as name and return the original
local function mod_insert(name, fun)
	local mod = _G
	for mname in name:gmatch("([^%.]*)%.") do
		mod = mod[mname]
	end
	local fname = name:match("[^%.]*$")
	local result = mod[fname]
	mod[fname] = fun
	return result
end

--[[
Generate a mock function that records it has been called and with what parameters into
the mocks_called array. It then calls the fun() with given parameters and returns the
result.

The generated function is entered into the given global name. module.name is allowed
syntax.

The original is stored and all can be returned with mocks_reset()
]]
function mock_gen(name, fun, skip_log)
	-- Make sure there's something to call
	fun = fun or function() end
	local f = function (...)
		if not skip_log then
			table.insert(mocks_called, {
				f = name,
				-- Make a copy. Things may change later, we want to preserve the state whet it was called.
				p = utils.clone({...})
			})
		end
		return fun(...)
	end
	local orig = mod_insert(name, f)
	if not mocks_origs[name] then
		mocks_origs[name] = orig
	end
end

--[[
Return originals before mocks and reset the mocks_called array.
]]
function mocks_reset()
	for n, f in pairs(mocks_origs) do
		mod_insert(n, f)
	end
	mocks_called = {}
	mocks_origs = {}
end
