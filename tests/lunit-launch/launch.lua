require 'lunit'

function launch(test)
	local stats = lunit.main({test})
	return stats.errors, stats.failed
end

function assert_table_equal(t1, t2)
	lunit.assert_table(t1)
	lunit.assert_table(t2)
	local function cmp(t1, t2, name)
		for k, v in pairs(t1) do
			local v2 = t2[k]
			if v2 == nil then v2 = "nil" end
			lunit.assert_equal(v, t2[k], "Values for key '" .. k .. "' differ: '" .. tostring(v) .. "' vs '" .. tostring(v2) .. "'" .. name)
		end
	end
	cmp(t1, t2, " pass 1")
	cmp(t2, t1, " pass 2")
end
