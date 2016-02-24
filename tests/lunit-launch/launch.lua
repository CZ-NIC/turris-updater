require 'lunit'

function launch(test)
	local stats = lunit.main({test})
	return stats.errors, stats.failed
end

function assert_table_equal(t1, t2)
	if t1 == t2 then
		-- The exact same instance
		return
	end
	lunit.assert_table(t1)
	lunit.assert_table(t2)
	local function cmp(t1, t2, name)
		for k, v in pairs(t1) do
			local v2 = t2[k]
			if type(v) ~= "table" or type(v2) ~= "table" then
				-- In case of two tables, we have special comparison below
				lunit.assert_equal(v, v2, "Values for key '" .. k .. "' differ, " .. name)
			end
		end
	end
	cmp(t1, t2, " pass 1")
	cmp(t2, t1, " pass 2")
	-- Recurse into sub-tables
	for k, v in pairs(t1) do
		local v2 = t2[k]
		if type(v) == "table" and type(v2) == "table" then
			assert_table_equal(v, v2)
		end
	end
end
