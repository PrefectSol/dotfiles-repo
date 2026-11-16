function Entity:padding() return ui.Span(" "):style(self:style()) end

function Linemode:padding()
	return self._file.is_hovered and ui.Span(" "):style(Entity:new(self._file):style()) or " "
end
