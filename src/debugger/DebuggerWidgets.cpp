#include "DebuggerWidgets.hpp"
#include "../runtime/DragonRuntime.hpp"

namespace dragon
{
	using namespace ogfx::gui;
	using eButton = ogfx::MouseEventData::eButton;

	static u64 now_ms(void)
	{
		return SDL_GetTicks();
	}

	static void show_menu(Window& window, ContextMenu::Instance& menu)
	{
		window.showContextMenu(menu, window.getMousePosition());
	}




	//DebugListView
	DebugListView::DebugListView(Window& window) : Widget({ 0, 0, 0, 0 }, window)
	{
		setTypeName("dragon::DebugListView");
		// Drawn with its own colors (a category the theme doesn't have).
		setStylesheetCategoryName("dragonDebugList");
		enableFocus();
		disableChildren();
		setFontSize(14);
		validate();
	}

	void DebugListView::setColumns(const stdvec<Column>& columns)
	{
		m_columns = columns;
	}

	void DebugListView::setSelectedRow(i32 row)
	{
		const i32 count = getRowCount();
		m_selectedRow = (count == 0 ? -1 : std::clamp(row, 0, count - 1));
		if (m_selectedRow >= 0)
			ensureRowVisible(m_selectedRow);
	}

	i32 DebugListView::getVisibleRowCount(void) const
	{
		return std::max(1, cast<i32>(std::floor(rowsArea().h / rowHeight())));
	}

	void DebugListView::ensureRowVisible(i32 row, bool centered)
	{
		const i32 visible = getVisibleRowCount();
		if (centered)
			m_scroll = cast<f32>(row - visible / 2);
		else if (row < cast<i32>(m_scroll))
			m_scroll = cast<f32>(row);
		else if (row >= cast<i32>(m_scroll) + visible)
			m_scroll = cast<f32>(row - visible + 1);
		clampScroll();
	}

	void DebugListView::scrollToBottom(void)
	{
		m_scroll = maxScroll();
	}

	Rectangle DebugListView::rowsArea(void) const
	{
		const Vec2 pos = getGlobalPosition();
		const f32 header = rowHeight() + 2.0f;
		return { pos.x, pos.y + header, std::max(0.0f, getw() - ScrollbarWidth), std::max(0.0f, geth() - header) };
	}

	i32 DebugListView::rowAt(const Vec2& position) const
	{
		const Rectangle area = rowsArea();
		if (!area.contains(position, true))
			return -1;
		const i32 row = cast<i32>(std::floor((position.y - area.y) / rowHeight() + m_scroll));
		return (row >= 0 && row < getRowCount()) ? row : -1;
	}

	Rectangle DebugListView::columnBounds(i32 column) const
	{
		const Rectangle area = rowsArea();
		f32 x = area.x + 4.0f;
		for (i32 i = 0; i < cast<i32>(m_columns.size()); i++)
		{
			const bool last = (i + 1 == cast<i32>(m_columns.size()));
			const f32 w = last ? std::max(0.0f, area.x + area.w - x) : m_columns[i].chars * m_charSize.x;
			if (i == column)
				return { x, area.y, w, area.h };
			x += w;
		}
		return { 0, 0, 0, 0 };
	}

	i32 DebugListView::columnAt(f32 x) const
	{
		for (i32 i = 0; i < cast<i32>(m_columns.size()); i++)
		{
			const Rectangle b = columnBounds(i);
			if (x >= b.x && x < b.x + b.w)
				return i;
		}
		return -1;
	}

	f32 DebugListView::maxScroll(void) const
	{
		return std::max(0.0f, cast<f32>(getRowCount() - getVisibleRowCount()));
	}

	void DebugListView::clampScroll(void)
	{
		m_scroll = std::clamp(m_scroll, 0.0f, maxScroll());
	}

	Rectangle DebugListView::scrollbarTrack(void) const
	{
		const Rectangle area = rowsArea();
		return { area.x + area.w, area.y, ScrollbarWidth, area.h };
	}

	Rectangle DebugListView::scrollbarThumb(void) const
	{
		const Rectangle track = scrollbarTrack();
		const i32 count = getRowCount();
		const i32 visible = getVisibleRowCount();
		if (count <= visible || track.h <= 0)
			return { 0, 0, 0, 0 };
		const f32 h = std::max(24.0f, track.h * cast<f32>(visible) / cast<f32>(count));
		const f32 y = track.y + (track.h - h) * (m_scroll / std::max(1.0f, maxScroll()));
		return { track.x + 2, y, track.w - 4, h };
	}

	void DebugListView::text(ogfx::BasicRenderer2D& gfx, const String& str, f32 x, f32 y, const Color& color)
	{
		gfx.drawString(str, { x, y }, color, getFontSize());
	}

	void DebugListView::onUpdate(void)
	{
		const i32 count = getRowCount();
		if (count != m_lastRowCount)
		{
			const bool atBottom = (m_scroll >= cast<f32>(m_lastRowCount - getVisibleRowCount()) - 0.5f);
			m_lastRowCount = count;
			if (m_followTail && atBottom)
				scrollToBottom();
			if (m_selectedRow >= count)
				m_selectedRow = count - 1;
		}
		clampScroll();
	}

	void DebugListView::onDraw(ogfx::BasicRenderer2D& gfx)
	{
		TTF_Font* font = gfx.getDefaultMonoFont(getFontSize());
		if (font == nullptr)
			return;
		if (m_metricsFontSize != getFontSize())
		{
			const Vec2 size = gfx.getStringDimensions("MMMMMMMMMM", getFontSize(), font);
			if (size.x > 0)
			{
				m_charSize = { size.x / 10.0f, size.y };
				m_metricsFontSize = getFontSize();
			}
		}
		const Rectangle bounds { getGlobalPosition(), { getw(), geth() } };
		gfx.fillRect(bounds, BackgroundColor);
		gfx.pushClippingRect(bounds, true);
		gfx.pushFont(font);

		// Header
		const f32 rh = rowHeight();
		gfx.fillRect({ bounds.x, bounds.y, bounds.w, rh + 2 }, HeaderColor);
		for (i32 c = 0; c < cast<i32>(m_columns.size()); c++)
		{
			const Rectangle col = columnBounds(c);
			text(gfx, m_columns[c].title, col.x + 2, bounds.y + 3, HeaderTextColor);
			if (c > 0)
				gfx.fillRect({ col.x - 3, bounds.y + 3, 1, rh - 4 }, GridColor);
		}

		// Rows
		const Rectangle area = rowsArea();
		const i32 count = getRowCount();
		gfx.pushClippingRect(area, true);
		if (count == 0)
			text(gfx, getEmptyText(), area.x + 8, area.y + 6, DimTextColor);
		const i32 first = std::max(0, cast<i32>(m_scroll));
		const f32 offset = (m_scroll - cast<f32>(first)) * rh;
		for (i32 row = first; row < count; row++)
		{
			const f32 y = area.y + cast<f32>(row - first) * rh - offset;
			if (y > area.y + area.h)
				break;
			const Rectangle rowRect { area.x, y, area.w, rh };
			const Color tint = getRowColor(row);
			if (tint.a > 0)
				gfx.fillRect(rowRect, tint);
			if (row == m_selectedRow)
				gfx.fillRect(rowRect, SelectionColor);
			else if (row == m_hoveredRow)
				gfx.fillRect(rowRect, HoverColor);
			for (i32 c = 0; c < cast<i32>(m_columns.size()); c++)
			{
				const Rectangle col = columnBounds(c);
				const Rectangle cell { col.x, y, std::max(0.0f, col.w - 6.0f), rh };
				gfx.pushClippingRect(cell, true);
				drawCell(gfx, row, c, { cell.x + 2, y + 2, cell.w - 2, rh - 4 });
				gfx.popClippingRect();
			}
		}
		gfx.popClippingRect();

		// Scrollbar
		const Rectangle track = scrollbarTrack();
		gfx.fillRect(track, Color(12, 14, 12));
		const Rectangle thumb = scrollbarThumb();
		if (thumb.w > 0)
			gfx.fillRoundRect(thumb, m_draggingScrollbar ? Color(110, 130, 110) : Color(70, 84, 70), 4);
		gfx.popFont();
		gfx.popClippingRect();
		if (isFocused())
			gfx.drawRect(bounds, Color(0, 117, 199), 1);
		else
			gfx.drawRect(bounds, Color(50, 56, 50), 1);
	}

	void DebugListView::onMousePressed(const Event& event)
	{
		const Vec2 pos { event.mouse->position_x, event.mouse->position_y };
		const Rectangle track = scrollbarTrack();
		if (track.contains(pos, true))
		{
			const Rectangle thumb = scrollbarThumb();
			if (thumb.w <= 0)
				return;
			m_draggingScrollbar = true;
			if (thumb.contains(pos, true))
				m_dragGrab = pos.y - thumb.y;
			else
			{
				m_dragGrab = thumb.h * 0.5f;
				onMouseDragged(event);
			}
			return;
		}
		const i32 row = rowAt(pos);
		if (row < 0)
			return;
		const u64 now = now_ms();
		const bool doubleClick = (event.mouse->button == eButton::Left && row == m_lastClickRow && now - m_lastClickMs <= DoubleClickMs);
		m_lastClickMs = doubleClick ? 0 : now;
		m_lastClickRow = row;
		m_selectedRow = row;
		onRowClicked(row, columnAt(pos.x), event.mouse->button, doubleClick);
	}

	void DebugListView::onMouseReleased(const Event& event)
	{
		m_draggingScrollbar = false;
	}

	void DebugListView::onMouseMoved(const Event& event)
	{
		m_hoveredRow = rowAt({ event.mouse->position_x, event.mouse->position_y });
	}

	void DebugListView::onMouseDragged(const Event& event)
	{
		if (!m_draggingScrollbar)
			return;
		const Rectangle track = scrollbarTrack();
		const Rectangle thumb = scrollbarThumb();
		const f32 range = track.h - thumb.h;
		if (range <= 0)
			return;
		const f32 t = std::clamp((event.mouse->position_y - m_dragGrab - track.y) / range, 0.0f, 1.0f);
		m_scroll = t * maxScroll();
	}

	void DebugListView::onMouseExited(const Event& event)
	{
		m_hoveredRow = -1;
	}

	void DebugListView::onMouseScrolled(const Event& event)
	{
		m_scroll -= event.mouse->scrollAmount.y * 3.0f;
		clampScroll();
		m_hoveredRow = rowAt(getWindow().getMousePosition());
		event.handle();
	}

	void DebugListView::onKeyPressed(const Event& event)
	{
		const u32 key = event.keyboard->keyCode;
		const i32 count = getRowCount();
		if (count == 0)
			return;
		const i32 page = std::max(1, getVisibleRowCount() - 1);
		const i32 row = std::max(0, m_selectedRow);
		if (key == ogfx::KeyCode::Up)
			setSelectedRow(m_selectedRow < 0 ? 0 : row - 1);
		else if (key == ogfx::KeyCode::Down)
			setSelectedRow(m_selectedRow < 0 ? 0 : row + 1);
		else if (key == ogfx::KeyCode::Pageup)
			setSelectedRow(row - page);
		else if (key == ogfx::KeyCode::Pagedown)
			setSelectedRow(row + page);
		else if (key == ogfx::KeyCode::Home)
			setSelectedRow(0);
		else if (key == ogfx::KeyCode::End)
			setSelectedRow(count - 1);
		else if ((key == ogfx::KeyCode::Return || key == ogfx::KeyCode::KpEnter) && m_selectedRow >= 0)
			onRowClicked(m_selectedRow, -1, eButton::Left, true);
		else
			return;
		event.handle();
	}




	//RegisterView
	static const struct { const char* name; u8 reg; u8 group; } s_registers[RegisterView::RegisterCount] = {
		{ "IP", data::Registers::IP, 0 }, { "SP", data::Registers::SP, 0 }, { "FP", data::Registers::FP, 0 },
		{ "RV", data::Registers::RV, 0 }, { "PP", data::Registers::PP, 0 }, { "FL", data::Registers::FL, 0 },
		{ "ACC", data::Registers::ACC, 0 },
		{ "R1", data::Registers::R1, 1 }, { "R2", data::Registers::R2, 1 }, { "R3", data::Registers::R3, 1 },
		{ "R4", data::Registers::R4, 1 }, { "R5", data::Registers::R5, 1 }, { "R6", data::Registers::R6, 1 },
		{ "R7", data::Registers::R7, 1 }, { "R8", data::Registers::R8, 1 }, { "R9", data::Registers::R9, 1 },
		{ "R10", data::Registers::R10, 1 },
		{ "S1", data::Registers::S1, 2 }, { "S2", data::Registers::S2, 2 }, { "OF", data::Registers::OFFSET, 2 }
	};
	static const Color s_groupColors[3] = { { 90, 200, 230 }, { 120, 230, 120 }, { 240, 110, 110 } };

	void RegisterView::ValueInput::onKeyPressed(const Event& event)
	{
		const u32 key = event.keyboard->keyCode;
		if (key == ogfx::KeyCode::Return || key == ogfx::KeyCode::KpEnter)
		{
			m_view.end_edit(true);
			event.handle();
			return;
		}
		if (key == ogfx::KeyCode::Escape)
		{
			m_view.end_edit(false);
			event.handle();
			return;
		}
		TextEdit::onKeyPressed(event);
	}

	void RegisterView::ValueInput::onFocusLost(const Event& event)
	{
		TextEdit::onFocusLost(event);
		m_view.end_edit(false);
	}

	RegisterView::RegisterView(Window& window, Debugger& debugger) : Widget({ 0, 0, 0, 0 }, window), m_debugger(debugger)
	{
		setTypeName("dragon::RegisterView");
		setStylesheetCategoryName("dragonRegisterView");
		disableFocus();
		addWidget(m_input);
		m_input.setVisible(false);
		m_input.setFontSize(m_fontSize);
		validate();
	}

	void RegisterView::update_metrics(ogfx::BasicRenderer2D& gfx)
	{
		if (m_metricsFontSize == m_fontSize)
			return;
		TTF_Font* font = gfx.getDefaultMonoFont(m_fontSize);
		if (font == nullptr)
			return;
		const Vec2 size = gfx.getStringDimensions("MMMMMMMMMM", m_fontSize, font);
		if (size.x <= 0)
			return;
		m_charSize = { size.x / 10.0f, size.y };
		m_metricsFontSize = m_fontSize;
	}

	void RegisterView::text(ogfx::BasicRenderer2D& gfx, const String& str, f32 x, f32 y, const Color& color, i32 fontSize)
	{
		gfx.drawString(str, { x, y }, color, fontSize == 0 ? m_fontSize : fontSize);
	}

	f32 RegisterView::registers_top(void) const
	{
		return getGlobalPosition().y - m_scroll + m_infoHeight;
	}

	Rectangle RegisterView::register_rect(i32 index) const
	{
		const f32 pad = 8.0f;
		const f32 w = CardChars * m_charSize.x + pad * 2;
		const f32 h = CardLines * m_charSize.y + 6;
		const i32 cols = std::max(1, m_cardColumns);
		const Vec2 pos = getGlobalPosition();
		const f32 top = registers_top();
		// Groups start on a new row: system, general, special.
		i32 row = 0, col = 0;
		u8 group = 0;
		for (i32 i = 0; i <= index; i++)
		{
			if (i > 0)
			{
				col++;
				if (s_registers[i].group != group || col >= cols)
				{
					row++;
					col = 0;
				}
			}
			group = s_registers[i].group;
		}
		// A gap between the groups.
		const f32 groupGap = cast<f32>(s_registers[index].group) * 8.0f;
		return { pos.x + 8 + cast<f32>(col) * (w + 6), top + groupGap + cast<f32>(row) * (h + 6), w, h };
	}

	Rectangle RegisterView::flag_rect(i32 bit) const
	{
		const Rectangle last = register_rect(RegisterCount - 1);
		const f32 w = m_charSize.x * 3.2f;
		const f32 x = getGlobalPosition().x + 8 + m_charSize.x * 4 + cast<f32>(15 - bit) * (w + 3);
		return { x, last.y + last.h + 18 + m_charSize.y, w, m_charSize.y + 6 };
	}

	i32 RegisterView::register_at(const Vec2& position) const
	{
		for (i32 i = 0; i < RegisterCount; i++)
		{
			if (register_rect(i).contains(position, true))
				return i;
		}
		return -1;
	}

	i32 RegisterView::flag_at(const Vec2& position) const
	{
		for (i32 bit = 0; bit < 16; bit++)
		{
			if (flag_rect(bit).contains(position, true))
				return bit;
		}
		return -1;
	}

	u16 RegisterView::value_of(i32 index) const
	{
		// Live while the machine runs; what the debugger captured when it stopped otherwise.
		if (m_debugger.isRunning())
			return cast<u16>(DragonRuntime::cpu.readRegister(s_registers[index].reg));
		return cast<u16>(m_debugger.getCurrentInfo().registers[s_registers[index].reg]);
	}

	void RegisterView::onDraw(ogfx::BasicRenderer2D& gfx)
	{
		update_metrics(gfx);
		TTF_Font* font = gfx.getDefaultMonoFont(m_fontSize);
		if (font == nullptr)
			return;
		const Rectangle bounds { getGlobalPosition(), { getw(), geth() } };
		m_scroll = std::clamp(m_scroll, 0.0f, std::max(0.0f, m_contentHeight - bounds.h));
		gfx.fillRect(bounds, DebugListView::BackgroundColor);
		gfx.pushClippingRect(bounds, true);
		gfx.pushFont(font);
		const f32 cw = m_charSize.x;
		const f32 lh = m_charSize.y + 4;
		const bool running = m_debugger.isRunning();
		const auto& prev = m_debugger.getPreviousInfo();
		const auto& curr = m_debugger.getCurrentInfo();
		const Color dim = DebugListView::DimTextColor;
		const Color label = DebugListView::HeaderTextColor;
		const Color changed = DebugListView::ChangedColor;

		// ===== Machine info =====
		f32 x = bounds.x + 10;
		f32 y = bounds.y + 8 - m_scroll;
		auto l_instruction = [&](const char* title, const Debugger::tMachineInfo& info) {
			text(gfx, title, x, y, label);
			f32 ix = x + cw * 10;
			text(gfx, String::getHexStr(info.instructionAddress, true, 2), ix, y, Color(180, 180, 190));
			ix += cw * 8;
			String code = Debugger::Utils::findSymbol(m_debugger.data().code, info.instructionAddress).new_trim();
			if (code == "")
				code = info.opCode;
			code.fixedLength(34);
			text(gfx, code, ix, y, Color(110, 160, 255));
			ix += cw * 35;
			String bytes = "";
			for (u8 i = 0; i < info.footprintSize; i++)
				bytes.add(String::getHexStr(info.footprint[i], false, 1)).add(" ");
			text(gfx, bytes, ix, y, Color(235, 215, 90));
			y += lh;
		};
		if (running)
		{
			text(gfx, "Running... (the values are live; the changes are shown when the machine stops)", x, y, dim);
			y += lh;
			const u16 ip = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::IP));
			text(gfx, "IP", x, y, label);
			text(gfx, String::getHexStr(ip, true, 2), x + cw * 10, y, Color(180, 180, 190));
			y += lh * 2;
		}
		else
		{
			text(gfx, "Cycle", x, y, label);
			text(gfx, String("").add(prev.cycle).add("  ->  ").add(curr.cycle), x + cw * 10, y, Color(235, 215, 90));
			y += lh;
			l_instruction("Executed", prev);
			l_instruction("Next", curr);
		}

		// Status chips
		f32 cx = x;
		auto l_chip = [&](const String& str, bool on, bool wasOn, const Color& onColor) {
			const f32 w = cw * cast<f32>(str.len()) + 14;
			if (cx > x && cx + w > bounds.x + bounds.w - 10)
			{
				cx = x;
				y += lh + 8;
			}
			const Rectangle r { cx, y, w, lh + 2 };
			gfx.fillRoundRect(r, on ? Color(onColor.r, onColor.g, onColor.b, 60) : Color(40, 44, 40), 5);
			if (!running && on != wasOn)
				gfx.drawRoundRect(r, changed, 5, 1);
			text(gfx, str, cx + 7, y + 3, on ? onColor : dim);
			cx += w + 6;
		};
		const bool ie = DragonRuntime::cpu.readFlag(data::Flags::InterruptsEnabled);
		const u16 prevFL = cast<u16>(prev.registers[data::Registers::FL]);
		l_chip("Interrupts", ie, (prevFL & 1) != 0, Color(120, 230, 120));
		l_chip("Offset mode", DragonRuntime::cpu.readFlag(data::Flags::OffsetModeEnabled), (prevFL & 2) != 0, Color(120, 200, 240));
		l_chip("BIOS mode", curr.biosMode, prev.biosMode, Color(240, 180, 90));
		l_chip("Debug break", curr.debugBreak, prev.debugBreak, Color(240, 220, 90));
		l_chip("Halted", curr.halted, prev.halted, Color(240, 90, 90));
		l_chip(String("Subroutines ").add(curr.subroutineCounter), curr.inSubroutine, prev.inSubroutine, Color(200, 140, 240));
		l_chip(String("INT handlers ").add(curr.interruptHandlerCount), curr.interruptHandlerCount > 0, prev.interruptHandlerCount > 0, Color(140, 160, 255));
		l_chip(String("Frame ").add(String::getHexStr(curr.stackFrameSize, true, 2)), curr.stackFrameSize > 0, prev.stackFrameSize > 0, Color(150, 220, 200));
		y += lh + 14;
		m_infoHeight = y - (bounds.y - m_scroll);

		// ===== Registers =====
		const f32 cardW = CardChars * cw + 16;
		m_cardColumns = std::max(1, cast<i32>((bounds.w - 16) / (cardW + 6)));
		for (i32 i = 0; i < RegisterCount; i++)
		{
			const Rectangle r = register_rect(i);
			const u16 value = value_of(i);
			const u16 before = cast<u16>(prev.registers[s_registers[i].reg]);
			const bool isChanged = !running && value != before;
			gfx.fillRoundRect(r, isChanged ? Color(120, 60, 20, 90) : Color(34, 38, 34), 6);
			if (i == m_hovered)
				gfx.drawRoundRect(r, Color(0, 117, 199), 6, 1);
			else
				gfx.drawRoundRect(r, isChanged ? changed : Color(52, 58, 52), 6, 1);
			const f32 tx = r.x + 8;
			const f32 ty = r.y + 4;
			text(gfx, s_registers[i].name, tx, ty, s_groupColors[s_registers[i].group]);
			const String hex = String::getHexStr(value, true, 2);
			text(gfx, hex, r.x + r.w - 8 - cw * cast<f32>(hex.len()), ty, isChanged ? Color(255, 235, 200) : Color(235, 235, 235));
			String second = "";
			if (isChanged)
				second.add("was ").add(String::getHexStr(before, true, 2));
			else
				second.add(value).add(" / ").add(cast<i16>(value));
			text(gfx, second, tx, ty + lh, isChanged ? changed : dim, m_fontSize - 2);
		}

		// ===== Flags =====
		const Rectangle flags0 = flag_rect(15);
		text(gfx, "Flags (FL), click to toggle", bounds.x + 10, flags0.y - lh - 2, label);
		text(gfx, "FL", bounds.x + 10, flags0.y + 3, s_groupColors[0]);
		const u16 fl = value_of(5);
		for (i32 bit = 15; bit >= 0; bit--)
		{
			const Rectangle r = flag_rect(bit);
			const bool on = (fl >> bit) & 1;
			const bool was = (prevFL >> bit) & 1;
			gfx.fillRoundRect(r, on ? Color(47, 140, 220, 140) : Color(34, 38, 34), 4);
			gfx.drawRoundRect(r, (!running && on != was) ? changed : (bit == m_hoveredFlag ? Color(0, 117, 199) : Color(52, 58, 52)), 4, 1);
			String name = (bit == data::Flags::InterruptsEnabled ? "IE" : (bit == data::Flags::OffsetModeEnabled ? "OF" : String("").add(bit)));
			text(gfx, name, r.x + (r.w - cw * cast<f32>(name.len())) * 0.5f, r.y + 3, on ? Color(240, 240, 240) : dim, m_fontSize - 1);
		}

		// ===== Hovered value =====
		const f32 statusY = flags0.y + flags0.h + 14;
		if (m_hovered >= 0)
		{
			const u16 v = value_of(m_hovered);
			String info = s_registers[m_hovered].name;
			info.add(" = ").add(String::getHexStr(v, true, 2)).add(" = ").add(v).add(" (unsigned) = ").add(cast<i16>(v)).add(" (signed) = ").add(String::getBinStr(v, true, 2));
			const char hi = cast<char>(v >> 8), lo = cast<char>(v & 0xFF);
			info.add(" = '").addChar(std::isprint(cast<u8>(hi)) ? hi : '.').addChar(std::isprint(cast<u8>(lo)) ? lo : '.').add("'");
			String symbol = Debugger::Utils::findNearestSymbol(m_debugger.data().labels, v);
			if (symbol != "")
				info.add("   ").add(symbol);
			text(gfx, info, bounds.x + 10, statusY, Color(200, 210, 200));
		}
		else if (m_hoveredFlag >= 0)
			text(gfx, String("Flag bit ").add(m_hoveredFlag).add(m_hoveredFlag == 0 ? ": interrupts enabled" : (m_hoveredFlag == 1 ? ": offset addressing mode" : ": unused")), bounds.x + 10, statusY, Color(200, 210, 200));
		else
			text(gfx, "Double-click a register to edit it, right-click for more.", bounds.x + 10, statusY, dim);
		m_contentHeight = statusY + lh + 8 - (bounds.y - m_scroll);
		if (m_contentHeight > bounds.h)
		{
			// The scroll position, on the right.
			const f32 h = std::max(20.0f, bounds.h * bounds.h / m_contentHeight);
			const f32 t = m_scroll / (m_contentHeight - bounds.h);
			gfx.fillRoundRect({ bounds.x + bounds.w - 8, bounds.y + (bounds.h - h) * t, 5, h }, Color(90, 104, 90), 2);
		}
		gfx.popFont();
		gfx.popClippingRect();
	}

	void RegisterView::onMousePressed(const Event& event)
	{
		const Vec2 pos { event.mouse->position_x, event.mouse->position_y };
		const i32 index = register_at(pos);
		const i32 bit = (index < 0 ? flag_at(pos) : -1);
		if (bit >= 0 && event.mouse->button == eButton::Left)
		{
			if (m_debugger.isRunning())
				return;
			m_debugger.setRegister("fl", value_of(5) ^ (1 << bit));
			return;
		}
		if (index < 0)
			return;
		if (event.mouse->button == eButton::Right)
		{
			show_menu(index);
			return;
		}
		const u64 now = now_ms();
		const bool doubleClick = (index == m_lastClickIndex && now - m_lastClickMs <= DebugListView::DoubleClickMs);
		m_lastClickMs = doubleClick ? 0 : now;
		m_lastClickIndex = index;
		if (doubleClick)
			begin_edit(index);
	}

	void RegisterView::onMouseMoved(const Event& event)
	{
		const Vec2 pos { event.mouse->position_x, event.mouse->position_y };
		m_hovered = register_at(pos);
		m_hoveredFlag = (m_hovered < 0 ? flag_at(pos) : -1);
	}

	void RegisterView::onMouseExited(const Event& event)
	{
		m_hovered = -1;
		m_hoveredFlag = -1;
	}

	void RegisterView::onMouseScrolled(const Event& event)
	{
		end_edit(false);
		m_scroll = std::clamp(m_scroll - event.mouse->scrollAmount.y * 40.0f, 0.0f, std::max(0.0f, m_contentHeight - geth()));
		const Vec2 pos = getWindow().getMousePosition();
		m_hovered = register_at(pos);
		m_hoveredFlag = (m_hovered < 0 ? flag_at(pos) : -1);
		event.handle();
	}

	void RegisterView::begin_edit(i32 index)
	{
		if (m_debugger.isRunning())
			return;
		m_editing = index;
		const Rectangle r = register_rect(index);
		const Vec2 origin = getGlobalContentPosition();
		m_input.setBounds(r.x - origin.x, r.y - origin.y + r.h * 0.5f - 2, r.w, r.h * 0.5f + 4);
		m_input.setText(String::getHexStr(value_of(index), true, 2));
		m_input.setVisible(true);
		getWindow().getFocusManager().requestFocus(m_input);
	}

	void RegisterView::end_edit(bool apply)
	{
		if (m_editing < 0)
			return;
		const i32 index = m_editing;
		m_editing = -1;
		m_input.setVisible(false);
		if (!apply)
			return;
		i64 value = 0;
		const String input = m_input.getText().new_trim();
		if (input.len() == 3 && input.startsWith("'") && input.endsWith("'"))
			value = cast<u8>(input[1]);
		else if (!Debugger::Utils::parseValue(input, value))
		{
			m_debugger.console().fg(ostd::ConsoleColors::Red).p("Invalid value: ").p(input).reset().nl();
			return;
		}
		m_debugger.setRegister(String(s_registers[index].name).toLower(), value);
	}

	void RegisterView::show_menu(i32 index)
	{
		const u16 value = value_of(index);
		const String name = String(s_registers[index].name).toLower();
		ContextMenu::Instance menu;
		menu.entries.push_back({ String("Edit ").add(s_registers[index].name).add("..."), 0 });
		menu.entries.push_back({ String("Show ").add(String::getHexStr(value, true, 2)).add(" in memory"), 1 });
		menu.entries.push_back({ String("Show ").add(String::getHexStr(value, true, 2)).add(" in the disassembly"), 2 });
		menu.entries.push_back({ "Copy value", 3 });
		menu.entries.push_back(ContextMenu::Entry::separator());
		menu.entries.push_back({ "Set to 0", 4 });
		menu.entries.push_back({ "Increment", 5 });
		menu.entries.push_back({ "Decrement", 6 });
		menu.onActivate = [this, index, value, name](const ContextMenu::Entry& entry) {
			switch (entry.id)
			{
				case 0: begin_edit(index); break;
				case 1: if (m_onShowInMemory) m_onShowInMemory(value); break;
				case 2: if (m_onShowInDisassembly) m_onShowInDisassembly(value); break;
				case 3: SDL_SetClipboardText(String::getHexStr(value, true, 2).c_str()); break;
				case 4: if (!m_debugger.isRunning()) m_debugger.setRegister(name, 0); break;
				case 5: if (!m_debugger.isRunning()) m_debugger.setRegister(name, cast<u16>(value + 1)); break;
				case 6: if (!m_debugger.isRunning()) m_debugger.setRegister(name, cast<u16>(value - 1)); break;
				default: break;
			}
		};
		dragon::show_menu(getWindow(), menu);
	}




	//TrackerView
	TrackerView::TrackerView(Window& window, Debugger& debugger) : DebugListView(window), m_debugger(debugger)
	{
		setTypeName("dragon::TrackerView");
		setColumns({ { "W", 3 }, { "Symbol", 26 }, { "Size", 6 }, { "Address", 9 }, { "Region", 9 }, { "Previous", 26 }, { "Current", 26 }, { "Text", 10 } });
	}

	i32 TrackerView::getRowCount(void) const
	{
		return cast<i32>(m_debugger.data().tracked.size());
	}

	String TrackerView::getEmptyText(void) const
	{
		return "Nothing is tracked (use the 'track' or 'watch' command).";
	}

	void TrackerView::onUpdate(void)
	{
		if (m_seenVersion != m_debugger.data().trackedVersion || m_valueOffsets.size() != m_debugger.data().tracked.size())
		{
			m_seenVersion = m_debugger.data().trackedVersion;
			m_valueOffsets.clear();
			size_t offset = 0;
			for (const auto& item : m_debugger.data().tracked)
			{
				m_valueOffsets.push_back(offset);
				offset += item.size;
			}
		}
		DebugListView::onUpdate();
	}

	bool TrackerView::changed(i32 row) const
	{
		if (m_debugger.isRunning() || row >= cast<i32>(m_valueOffsets.size()))
			return false;
		const auto& item = m_debugger.data().tracked[row];
		const auto& prev = m_debugger.getPreviousInfo().trackedValues;
		const size_t offset = m_valueOffsets[row];
		for (u16 j = 0; j < item.size; j++)
		{
			if (offset + j < prev.size() && prev[offset + j] != DragonRuntime::memMap.peek8(cast<u16>(item.address + j)))
				return true;
		}
		return false;
	}

	Color TrackerView::getRowColor(i32 row) const
	{
		return changed(row) ? Color(255, 150, 60, 30) : Color(0, 0, 0, 0);
	}

	void TrackerView::drawCell(ogfx::BasicRenderer2D& gfx, i32 row, i32 column, const Rectangle& bounds)
	{
		if (row >= cast<i32>(m_valueOffsets.size()))
			return;
		const auto& item = m_debugger.data().tracked[row];
		const size_t offset = m_valueOffsets[row];
		const auto& prev = m_debugger.getPreviousInfo().trackedValues;
		const f32 cw = charWidth();
		auto l_bytes = [&](bool previous) {
			f32 x = bounds.x;
			const i32 maxBytes = std::max(1, cast<i32>((bounds.w + cw) / (cw * 3)));
			for (u16 j = 0; j < item.size; j++)
			{
				if (j >= maxBytes - 1 && j + 1 < item.size)
				{
					text(gfx, "...", x, bounds.y, DimTextColor);
					break;
				}
				const u8 now = DragonRuntime::memMap.peek8(cast<u16>(item.address + j));
				const bool known = offset + j < prev.size();
				const u8 before = known ? prev[offset + j] : now;
				const bool diff = (known && before != now && !m_debugger.isRunning());
				if (previous)
					text(gfx, known ? String::getHexStr(before, false, 1) : String("??"), x, bounds.y, diff ? Color(240, 110, 110) : Color(190, 120, 120));
				else
					text(gfx, String::getHexStr(now, false, 1), x, bounds.y, diff ? ChangedColor : Color(120, 230, 120));
				x += cw * 3;
			}
		};
		switch (column)
		{
			case 0:
			{
				const Rectangle box { bounds.x + 2, bounds.y + bounds.h * 0.5f - 6, 12, 12 };
				gfx.drawRect(box, item.breakOnChange ? Color(240, 110, 110) : DimTextColor, 1);
				if (item.breakOnChange)
					gfx.fillRect({ box.x + 3, box.y + 3, 6, 6 }, Color(240, 110, 110));
				break;
			}
			case 1: text(gfx, item.label, bounds.x, bounds.y, Color(220, 130, 230)); break;
			case 2: text(gfx, String("").add(item.size), bounds.x, bounds.y, Color(110, 220, 230)); break;
			case 3: text(gfx, String::getHexStr(item.address, true, 2), bounds.x, bounds.y, Color(180, 180, 190)); break;
			case 4:
			{
				const u16 sp = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::SP));
				text(gfx, item.address >= sp ? String("STACK") : DragonRuntime::memMap.getMemoryRegionName(item.address), bounds.x, bounds.y, Color(235, 215, 90));
				break;
			}
			case 5: l_bytes(true); break;
			case 6: l_bytes(false); break;
			case 7:
			{
				String str = "";
				for (u16 j = 0; j < item.size && j < 64; j++)
				{
					const u8 c = DragonRuntime::memMap.peek8(cast<u16>(item.address + j));
					str.addChar(std::isprint(c) ? cast<char>(c) : '.');
				}
				text(gfx, str, bounds.x, bounds.y, Color(200, 200, 160));
				break;
			}
			default: break;
		}
	}

	void TrackerView::onRowClicked(i32 row, i32 column, eButton button, bool doubleClick)
	{
		if (row >= getRowCount())
			return;
		const Debugger::tTrackedData item = m_debugger.data().tracked[row];
		if (button == eButton::Left)
		{
			if (column == 0)
				m_debugger.setWatch(item.address, !item.breakOnChange);
			else if (doubleClick && m_onShowInMemory)
				m_onShowInMemory(item.address);
			return;
		}
		if (button != eButton::Right)
			return;
		ContextMenu::Instance menu;
		menu.entries.push_back({ "Show in memory", 0 });
		menu.entries.push_back({ item.breakOnChange ? "Stop watching (keep tracking)" : "Watch (stop when it changes)", 1 });
		menu.entries.push_back({ "Copy address", 2 });
		menu.entries.push_back(ContextMenu::Entry::separator());
		menu.entries.push_back({ "Untrack", 3 });
		menu.entries.push_back({ "Untrack all", 4 });
		menu.onActivate = [this, item](const ContextMenu::Entry& entry) {
			switch (entry.id)
			{
				case 0: if (m_onShowInMemory) m_onShowInMemory(item.address); break;
				case 1: m_debugger.setWatch(item.address, !item.breakOnChange); break;
				case 2: SDL_SetClipboardText(String::getHexStr(item.address, true, 2).c_str()); break;
				case 3: m_debugger.untrack(item.address); break;
				case 4: m_debugger.untrackAll(); break;
				default: break;
			}
		};
		dragon::show_menu(getWindow(), menu);
	}




	//CallTraceView
	CallTraceView::CallTraceView(Window& window, Debugger& debugger) : DebugListView(window), m_debugger(debugger)
	{
		setTypeName("dragon::CallTraceView");
		setColumns({ { "Cycle", 11 }, { "From", 8 }, { "Call / return", 10 } });
		enableFollowTail();
	}

	bool CallTraceView::is_call(const Debugger::tCallInfo& call)
	{
		return call.info != "RET" && call.info != "RET INT";
	}

	u64 CallTraceView::key_of(const Debugger::tCallInfo& call)
	{
		const u64 kind = (call.info == "HW INT" ? 2 : (call.info == "INT" ? 1 : 0));
		return (call.position << 2) | kind;
	}

	i32 CallTraceView::getRowCount(void) const
	{
		return cast<i32>(m_rows.size());
	}

	String CallTraceView::getEmptyText(void) const
	{
		return "No calls yet.";
	}

	void CallTraceView::refreshRows(void)
	{
		const auto& trace = m_debugger.getCallTrace();
		const size_t visible = m_debugger.getVisibleCallTraceSize();
		const u64 front = trace.empty() ? 0 : trace.front().position;
		if (!m_dirty && trace.size() == m_seenSize && visible == m_seenVisible && front == m_seenFront)
			return;
		m_dirty = false;
		m_seenSize = trace.size();
		m_seenVisible = visible;
		m_seenFront = front;
		rebuild();
	}

	void CallTraceView::rebuild(void)
	{
		const auto& trace = m_debugger.getCallTrace();
		const size_t count = m_debugger.getVisibleCallTraceSize();
		m_rows.clear();
		stdvec<size_t> open;	// rows of the calls that haven't returned yet
		for (size_t i = 0; i < count; i++)
		{
			const auto& call = trace[i];
			Row row;
			row.index = cast<u32>(i);
			if (!is_call(call))
			{
				if (!open.empty())
					open.pop_back();
				m_rows.push_back(row);
				continue;
			}
			row.open = true;
			if (m_collapsed.count(key_of(call)) > 0)
			{
				// Hidden up to (and with) the matching return: the first return below the call's depth.
				size_t j = i + 1;
				while (j < count && (is_call(trace[j]) || trace[j].depth >= call.depth))
					j++;
				row.hidden = cast<u32>((j < count ? j + 1 : count) - i - 1);
				row.active = (j >= count);
				m_rows.push_back(row);
				i = (j < count ? j : count);
				continue;
			}
			open.push_back(m_rows.size());
			m_rows.push_back(row);
		}
		for (size_t r : open)
			m_rows[r].active = true;
	}

	void CallTraceView::onUpdate(void)
	{
		refreshRows();
		DebugListView::onUpdate();
	}

	Color CallTraceView::getRowColor(i32 row) const
	{
		return m_rows[row].active ? Color(230, 200, 75, 22) : Color(0, 0, 0, 0);
	}

	void CallTraceView::drawCell(ogfx::BasicRenderer2D& gfx, i32 row, i32 column, const Rectangle& bounds)
	{
		const auto& trace = m_debugger.getCallTrace();
		const Row& r = m_rows[row];
		if (r.index >= trace.size())
			return;
		const auto& call = trace[r.index];
		const f32 cw = charWidth();
		if (column == 0)
		{
			u64 cycle = call.position;
			const History& history = m_debugger.history();
			if (m_debugger.isHistoryEnabled() && call.position >= history.getFirstPosition() && call.position <= history.getLastPosition())
				cycle = history.getCycleAt(call.position);
			text(gfx, String("").add(cycle), bounds.x, bounds.y, DimTextColor);
			return;
		}
		if (column == 1)
		{
			text(gfx, call.info == "HW INT" ? String("  --  ") : String::getHexStr(call.inst_addr, true, 2), bounds.x, bounds.y,
				 call.interrupts_disabled ? DimTextColor : Color(220, 130, 230));
			return;
		}
		const bool isCall = is_call(call);
		const i32 level = std::max(0, isCall ? call.depth - 1 : call.depth);
		f32 x = bounds.x;
		const f32 cy = bounds.y + bounds.h * 0.5f;
		for (i32 l = 0; l < level; l++)
		{
			gfx.fillRect({ x + cw * 0.5f, bounds.y - 4, 1, bounds.h + 8 }, Color(70, 76, 70));
			x += cw * 2;
		}
		if (isCall)
		{
			// The expander: right (collapsed) or down.
			const f32 s = cw * 0.4f;
			const f32 ex = x + cw * 0.5f;
			if (r.hidden > 0)
				gfx.fillTriangle(Vec2 { ex - s * 0.6f, cy - s }, Vec2 { ex + s, cy }, Vec2 { ex - s * 0.6f, cy + s }, Color(200, 200, 200));
			else
				gfx.fillTriangle(Vec2 { ex - s, cy - s * 0.6f }, Vec2 { ex + s, cy - s * 0.6f }, Vec2 { ex, cy + s }, Color(140, 140, 140));
		}
		else
			gfx.fillRect({ x + cw * 0.5f, bounds.y - 4, 1, bounds.h * 0.5f + 4 }, Color(70, 76, 70));
		x += cw * 1.5f;
		auto l_part = [&](const String& str, const Color& color) {
			text(gfx, str, x, bounds.y, color);
			x += cw * cast<f32>(str.len());
		};
		if (call.info == "CALL IMM" || call.info == "CALL REG")
		{
			l_part("call ", Color(240, 110, 110));
			const String symbol = Debugger::Utils::findSymbol(m_debugger.data().labels, call.addr).new_trim();
			if (symbol != "")
				l_part(symbol + " ", Color(120, 230, 120));
			l_part(String(call.info == "CALL REG" ? "*" : "").add(String::getHexStr(call.addr, true, 2)), Color(235, 215, 90));
		}
		else if (call.info == "INT")
		{
			l_part("int ", Color(110, 160, 255));
			l_part(String::getHexStr(cast<u8>(call.addr), true, 1), Color(235, 215, 90));
		}
		else if (call.info == "HW INT")
		{
			l_part("hwi ", Color(220, 130, 230));
			l_part(String::getHexStr(cast<u8>(call.addr), true, 1), Color(235, 215, 90));
			l_part(String(" (").add(data::InterruptCodes::getInterruptName(cast<u8>(call.addr))).add(")"), DimTextColor);
		}
		else if (call.info == "RET")
			l_part("ret", Color(240, 110, 110));
		else
			l_part("rti", Color(110, 160, 255));
		if (r.hidden > 0)
			l_part(String("   ... ").add(r.hidden).add(r.hidden == 1 ? " entry" : " entries").add(r.active ? " (not returned yet)" : ""), DimTextColor);
		else if (r.active && isCall)
			l_part("   (not returned yet)", Color(230, 200, 75));
	}

	void CallTraceView::go_to(const Debugger::tCallInfo& call)
	{
		const History& history = m_debugger.history();
		if (!m_debugger.isHistoryEnabled() || call.position == 0 || call.position - 1 < history.getFirstPosition() || call.position - 1 > history.getLastPosition())
		{
			m_debugger.console().fg(ostd::ConsoleColors::Yellow).p("That point isn't in the recorded history anymore.").reset().nl();
			return;
		}
		m_debugger.seek(call.position - 1);
	}

	void CallTraceView::onRowClicked(i32 row, i32 column, eButton button, bool doubleClick)
	{
		if (row >= getRowCount())
			return;
		const auto& trace = m_debugger.getCallTrace();
		const Row r = m_rows[row];
		const Debugger::tCallInfo call = trace[r.index];
		const bool isCall = is_call(call);
		if (button == eButton::Left)
		{
			if (isCall && column == 2)
			{
				// Clicked the expander?
				const i32 level = std::max(0, call.depth - 1);
				const f32 ex = columnBounds(2).x + 2 + charWidth() * cast<f32>(level * 2);
				const f32 mx = getWindow().getMousePosition().x;
				if (mx >= ex - 2 && mx <= ex + charWidth() * 1.5f)
				{
					const u64 key = key_of(call);
					if (m_collapsed.count(key) > 0)
						m_collapsed.erase(key);
					else
						m_collapsed.insert(key);
					m_dirty = true;
					refreshRows();
					return;
				}
			}
			if (doubleClick)
				go_to(call);
			return;
		}
		if (button != eButton::Right)
			return;
		ContextMenu::Instance menu;
		menu.entries.push_back({ "Go back in time to here", 0 });
		if (call.info != "HW INT")
			menu.entries.push_back({ String("Show the caller (").add(String::getHexStr(call.inst_addr, true, 2)).add(") in the disassembly"), 1 });
		if (call.info == "CALL IMM" || call.info == "CALL REG")
			menu.entries.push_back({ String("Show the target (").add(String::getHexStr(call.addr, true, 2)).add(") in the disassembly"), 2 });
		menu.entries.push_back(ContextMenu::Entry::separator());
		if (isCall)
			menu.entries.push_back({ r.hidden > 0 ? "Expand" : "Collapse", 3 });
		menu.entries.push_back({ "Collapse all finished calls", 4 });
		menu.entries.push_back({ "Expand all", 5 });
		menu.entries.push_back({ "Scroll to the end", 6 });
		menu.onActivate = [this, call, isCall](const ContextMenu::Entry& entry) {
			switch (entry.id)
			{
				case 0: go_to(call); break;
				case 1: if (m_onShowInDisassembly) m_onShowInDisassembly(call.inst_addr); break;
				case 2: if (m_onShowInDisassembly) m_onShowInDisassembly(call.addr); break;
				case 3:
				{
					const u64 key = key_of(call);
					if (m_collapsed.count(key) > 0)
						m_collapsed.erase(key);
					else
						m_collapsed.insert(key);
					break;
				}
				case 4:
				{
					// The outermost finished calls (their return is in the trace).
					const auto& trace = m_debugger.getCallTrace();
					const size_t count = m_debugger.getVisibleCallTraceSize();
					stdvec<size_t> open;
					for (size_t i = 0; i < count; i++)
					{
						if (is_call(trace[i]))
							open.push_back(i);
						else if (!open.empty())
						{
							if (open.size() == 1)
								m_collapsed.insert(key_of(trace[open.back()]));
							open.pop_back();
						}
					}
					break;
				}
				case 5: m_collapsed.clear(); break;
				case 6: scrollToBottom(); break;
				default: break;
			}
			m_dirty = true;
			refreshRows();
		};
		dragon::show_menu(getWindow(), menu);
	}




	//BreakpointView
	BreakpointView::BreakpointView(Window& window, Debugger& debugger) : DebugListView(window), m_debugger(debugger)
	{
		setTypeName("dragon::BreakpointView");
		setColumns({ { "On", 4 }, { "Type", 13 }, { "Address", 9 }, { "Symbol", 30 }, { "Code / value", 10 } });
	}

	void BreakpointView::refreshRows(void)
	{
		m_rows.clear();
		for (const auto& [address, bp] : m_debugger.data().breakpoints)
		{
			Row row;
			row.address = address;
			row.label = Debugger::Utils::findNearestSymbol(m_debugger.data().labels, address);
			m_rows.push_back(row);
		}
		for (const auto& item : m_debugger.data().tracked)
		{
			if (!item.breakOnChange)
				continue;
			Row row;
			row.address = item.address;
			row.watch = true;
			row.size = item.size;
			row.label = item.label;
			m_rows.push_back(row);
		}
		if (m_selectedRow >= cast<i32>(m_rows.size()))
			m_selectedRow = cast<i32>(m_rows.size()) - 1;
	}

	i32 BreakpointView::getRowCount(void) const
	{
		return cast<i32>(m_rows.size());
	}

	String BreakpointView::getEmptyText(void) const
	{
		return "No breakpoints: click the left margin of the disassembly, or use 'break <addr|$label>'.";
	}

	Color BreakpointView::getRowColor(i32 row) const
	{
		const Row& r = m_rows[row];
		const u16 ip = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::IP));
		if (!r.watch && r.address == ip && !m_debugger.isRunning())
			return { 230, 200, 75, 30 };
		return { 0, 0, 0, 0 };
	}

	void BreakpointView::drawCell(ogfx::BasicRenderer2D& gfx, i32 row, i32 column, const Rectangle& bounds)
	{
		const Row& r = m_rows[row];
		const Debugger::tBreakPoint* bp = (r.watch ? nullptr : m_debugger.getBreakPoint(r.address));
		if (!r.watch && bp == nullptr)
			return;
		const bool enabled = r.watch || bp->enabled;
		const Color textColor = enabled ? TextColor : DimTextColor;
		switch (column)
		{
			case 0:
			{
				const Rectangle box { bounds.x + 4, bounds.y + bounds.h * 0.5f - 6, 12, 12 };
				gfx.drawRect(box, enabled ? Color(220, 220, 220) : DimTextColor, 1);
				if (enabled)
					gfx.fillRect({ box.x + 3, box.y + 3, 6, 6 }, r.watch ? Color(220, 130, 230) : (bp->debugBreak ? Color(240, 170, 60) : Color(235, 70, 70)));
				break;
			}
			case 1:
				if (r.watch)
					text(gfx, "watch", bounds.x, bounds.y, enabled ? Color(220, 130, 230) : DimTextColor);
				else
					text(gfx, bp->debugBreak ? "debug_break" : "breakpoint", bounds.x, bounds.y, enabled ? (bp->debugBreak ? Color(240, 170, 60) : Color(235, 90, 90)) : DimTextColor);
				break;
			case 2: text(gfx, String::getHexStr(r.address, true, 2), bounds.x, bounds.y, enabled ? Color(235, 215, 90) : DimTextColor); break;
			case 3: text(gfx, r.label, bounds.x, bounds.y, enabled ? Color(110, 220, 230) : DimTextColor); break;
			case 4:
				if (r.watch)
				{
					String value = String("").add(r.size).add(r.size == 1 ? " byte: " : " bytes: ");
					for (u16 j = 0; j < r.size && j < 16; j++)
						value.add(String::getHexStr(DragonRuntime::memMap.peek8(cast<u16>(r.address + j)), false, 1)).add(" ");
					text(gfx, value, bounds.x, bounds.y, textColor);
				}
				else
					text(gfx, Debugger::Utils::findSymbol(m_debugger.data().code, r.address).new_trim(), bounds.x, bounds.y, textColor);
				break;
			default: break;
		}
	}

	void BreakpointView::onRowClicked(i32 row, i32 column, eButton button, bool doubleClick)
	{
		if (row >= getRowCount())
			return;
		const Row r = m_rows[row];
		const Debugger::tBreakPoint* bp = (r.watch ? nullptr : m_debugger.getBreakPoint(r.address));
		if (!r.watch && bp == nullptr)
			return;
		if (button == eButton::Left)
		{
			if (column == 0)
			{
				if (r.watch)
					m_debugger.setWatch(r.address, false);
				else
					m_debugger.setBreakPointEnabled(r.address, !bp->enabled);
				refreshRows();
			}
			else if (doubleClick)
			{
				if (r.watch && m_onShowInMemory)
					m_onShowInMemory(r.address);
				else if (!r.watch && m_onShowInDisassembly)
					m_onShowInDisassembly(r.address);
			}
			return;
		}
		if (button != eButton::Right)
			return;
		ContextMenu::Instance menu;
		if (r.watch)
		{
			menu.entries.push_back({ "Show in memory", 0 });
			menu.entries.push_back({ "Remove the watch (keep tracking)", 1 });
		}
		else
		{
			menu.entries.push_back({ "Show in the disassembly", 2 });
			menu.entries.push_back({ "Show in memory", 0 });
			menu.entries.push_back({ bp->enabled ? "Disable" : "Enable", 3 });
			if (!bp->debugBreak)
				menu.entries.push_back({ "Remove", 4 });
		}
		menu.entries.push_back(ContextMenu::Entry::separator());
		menu.entries.push_back({ "Enable all breakpoints", 5 });
		menu.entries.push_back({ "Disable all breakpoints", 6 });
		menu.entries.push_back({ "Remove all (debug_break ones disabled)", 7 });
		const bool enabled = (bp != nullptr && bp->enabled);
		menu.onActivate = [this, r, enabled](const ContextMenu::Entry& entry) {
			switch (entry.id)
			{
				case 0: if (m_onShowInMemory) m_onShowInMemory(r.address); break;
				case 1: m_debugger.setWatch(r.address, false); break;
				case 2: if (m_onShowInDisassembly) m_onShowInDisassembly(r.address); break;
				case 3: m_debugger.setBreakPointEnabled(r.address, !enabled); break;
				case 4: m_debugger.removeBreakPoint(r.address); break;
				case 5:
				case 6:
				{
					stdvec<u16> addresses;
					for (const auto& [address, bp] : m_debugger.data().breakpoints)
						addresses.push_back(address);
					for (u16 address : addresses)
						m_debugger.setBreakPointEnabled(address, entry.id == 5);
					break;
				}
				case 7: m_debugger.clearBreakPoints(); break;
				default: break;
			}
			refreshRows();
		};
		dragon::show_menu(getWindow(), menu);
	}
}
