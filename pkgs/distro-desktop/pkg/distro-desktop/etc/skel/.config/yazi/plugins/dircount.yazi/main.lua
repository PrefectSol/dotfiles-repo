local M = { counts = {} }

local save = ya.sync(function(st, counts)
	for url, n in pairs(counts) do
		st.counts[url] = n
	end
	ui.render()
end)

function M:fetch(job)
	return ya.co(function()
		local counts = {}
		for _, file in ipairs(job.files) do
			local files = fs.read_dir(file.url, {})
			if files then
				counts[tostring(file.url)] = #files
			end
			coroutine.yield(file, {})
		end
		save(counts)
	end)
end

return M
