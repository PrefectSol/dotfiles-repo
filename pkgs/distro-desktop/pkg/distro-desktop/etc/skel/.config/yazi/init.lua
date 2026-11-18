local dircount = require("dircount")

local function count(file)
	local folder = cx.active:history(file.url)
	if folder then
		return #folder.files
	end
	return dircount.counts[tostring(file.url)]
end

local function size(file)
	if file.cha.is_dir then
		local n = count(file)
		return n and tostring(n) or ""
	end
	return ya.readable_size(file.cha.len)
end

function Entity:padding() return ui.Span(" "):style(self:style()) end

function Linemode:padding()
	return self._file.is_hovered and ui.Span(" "):style(Entity:new(self._file):style()) or " "
end

function Linemode:info() return size(self._file) end

function Status:info()
	local h = self._current.hovered
	if not h then
		return ""
	end

	local user = ya.user_name and ya.user_name(h.cha.uid) or tostring(h.cha.uid)
	local time = math.floor(h.cha.mtime or 0)
	return string.format(
		" %s  %s  %s  %s",
		h.cha:perm() or "",
		user,
		size(h),
		time > 0 and os.date("%Y-%m-%d %H:%M", time) or ""
	)
end

function Status:position()
	local length = #self._current.files
	return string.format("%d/%d ", math.min(self._current.cursor + 1, length), length)
end

for id = 1, 3 do
	Status:children_remove(id, Status.LEFT)
end
for id = 4, 5 do
	Status:children_remove(id, Status.RIGHT)
end
Status:children_add("info", 1000, Status.LEFT)
