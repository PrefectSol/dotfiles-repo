local M = {}

local function center(url, area)
	local info = ya.image_info(url)
	local cw, ch = rt.term.cell_size()
	if not info or not cw or info.w == 0 or info.h == 0 then
		return area
	end
	local s = math.min(1, area.w * cw / info.w, area.h * ch / info.h, rt.preview.max_width / info.w, rt.preview.max_height / info.h)
	local w = math.min(area.w, math.ceil(info.w * s / cw))
	local h = math.min(area.h, math.ceil(info.h * s / ch))
	return ui.Rect {
		x = area.x + math.floor((area.w - w) / 2),
		y = area.y + math.floor((area.h - h) / 2),
		w = w,
		h = h,
	}
end

function M:peek(job)
	local start, url = os.clock(), ya.file_cache(job)
	if not url or not fs.cha(url) then
		url = Url(job.file.path)
	end

	ya.sleep(math.max(0, rt.preview.image_delay / 1000 + start - os.clock()))

	local area = job.area
	local _, err = ya.image_show(url, center(url, ui.Rect { x = area.x + 1, y = area.y, w = math.max(1, area.w - 2), h = area.h }))
	ya.preview_widget(job, err)
end

function M:seek() end

return M
