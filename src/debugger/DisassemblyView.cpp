#include "DisassemblyView.hpp"
#include "Debugger.hpp"
#include "../runtime/DragonRuntime.hpp"
#include <ogfx/gui/Window.hpp>

namespace dragon
{
	using ostd::Color;
	using ostd::Vec2;
	using ostd::Rectangle;

	DisassemblyView::DisassemblyView(ogfx::gui::Window& window, Debugger& debugger) : RichTextView(window), m_debugger(debugger)
	{
		setTypeName("dragon::DisassemblyView");
		enableSelection(false);
		setLineCallback([this](ogfx::gui::RichTextView&, i32 line, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick) {
			on_line_clicked(line, column, button, doubleClick);
		});
		setLineDecorator([this](ogfx::gui::RichTextView&, ogfx::BasicRenderer2D& gfx, i32 line, const Rectangle& bounds) {
			decorate(gfx, line, bounds);
		});
	}

	void DisassemblyView::build(void)
	{
		m_documents.clear();
		ogfx::gui::RichTextOutputHandler out;
		for (const auto& table : DisassemblyLoader::getTables())
		{
			if (table.getCodeTable().empty())
				continue;
			Document& doc = m_documents.emplace_back();
			doc.table = &table;
			String first = Debugger::Utils::findSymbol(m_debugger.data().labels, cast<u16>(table.getCodeStart()));
			if (first == "" && !table.getLabelTable().empty())
				first = table.getLabelTable().front().code;
			doc.name = String("[").add(String::getHexStr(cast<u16>(table.getCodeStart()), true, 2)).add(" - ")
							.add(String::getHexStr(cast<u16>(table.getCodeEnd()), true, 2)).add("]  ").add(first.new_trim());
			out.attach(doc.buffer);
			for (const auto& line : table.getCodeTable())
			{
				doc.buffer.setLineUserData(doc.buffer.getCursorLine(), line.addr);
				Debugger::Display::printDisassemblyLine(out, m_debugger, line);
				out.nl();
			}
		}
		Document& live = m_documents.emplace_back();
		live.name = "Live (decoded from memory)";
		select_document(cast<i32>(m_documents.size()) - 1);
	}

	String DisassemblyView::getProgramName(i32 index) const
	{
		if (index < 0 || index >= getProgramCount())
			return "";
		return m_documents[index].name;
	}

	void DisassemblyView::showProgram(i32 index)
	{
		if (index < 0 || index >= getProgramCount())
			return;
		m_follow = false;
		if (index == getProgramCount() - 1)
			build_live(m_currentAddress);
		select_document(index);
		if (!scrollToAddress(m_currentAddress))
			scrollToTop();
	}

	bool DisassemblyView::scrollToAddress(u16 address)
	{
		const i32 line = getBuffer().findLine(address);
		if (line < 0)
			return false;
		scrollToLine(line, true);
		return true;
	}

	void DisassemblyView::showAddress(u16 address)
	{
		if (m_documents.empty())
			return;
		m_follow = false;
		i32 index = -1;
		if (m_currentDocument >= 0 && getBuffer().findLine(address) >= 0)
			index = m_currentDocument;
		for (i32 i = 0; i < getProgramCount() && index < 0; i++)
		{
			const Document& doc = m_documents[i];
			if (doc.table != nullptr && address >= doc.table->getCodeStart() && address <= doc.table->getCodeEnd() && doc.buffer.findLine(address) >= 0)
				index = i;
		}
		if (index < 0)
		{
			index = getProgramCount() - 1;
			build_live(address);
		}
		if (index != m_currentDocument)
			select_document(index);
		m_currentLine = getBuffer().findLine(m_currentAddress);
		const i32 line = getBuffer().findLine(address);
		if (line >= 0)
		{
			scrollToLine(line, true);
			selectLines(line, line);
		}
	}

	void DisassemblyView::setCurrentAddress(u16 ip, bool scroll)
	{
		m_currentAddress = ip;
		if (m_follow && !m_documents.empty())
		{
			// The line shown by showAddress().
			clearSelection();
			i32 index = pick_document(ip);
			if (index < 0)
			{
				index = getProgramCount() - 1;
				if (m_currentDocument != index || ip < m_liveStart || ip > m_liveEnd)
					build_live(ip);
			}
			if (index != m_currentDocument)
				select_document(index);
		}
		m_currentLine = getBuffer().findLine(ip, std::max(0, m_currentLine));
		if (scroll && m_currentLine >= 0)
		{
			// Recentered only when the line gets close to the edges.
			const i32 first = getFirstVisibleLine();
			const i32 last = getLastVisibleLine();
			const i32 margin = std::max(1, (last - first) / 6);
			if (m_currentLine < first + margin || m_currentLine > last - margin)
				scrollToLine(m_currentLine, true);
		}
	}

	i32 DisassemblyView::pick_document(u16 ip) const
	{
		i32 best = -1;
		i32 bestScore = -1;
		for (i32 i = 0; i < getProgramCount(); i++)
		{
			const Document& doc = m_documents[i];
			if (doc.table == nullptr || ip < doc.table->getCodeStart() || ip > doc.table->getCodeEnd())
				continue;
			const i32 score = match_score(*doc.table, ip);
			if (score > bestScore)
			{
				best = i;
				bestScore = score;
			}
		}
		return best;
	}

	i32 DisassemblyView::match_score(const DisassemblyTable& table, u16 ip) const
	{
		// The program must have an instruction exactly at the IP; then the instructions around
		// it are compared with memory (the decoded size must be the size in the table).
		const auto& code = table.getCodeTable();
		auto it = std::lower_bound(code.begin(), code.end(), ip, [](const code::Assembler::tDisassemblyLine& line, u16 addr) { return line.addr < addr; });
		if (it == code.end() || it->addr != ip)
			return -1;
		const i64 index = it - code.begin();
		i32 score = 0;
		for (i64 i = std::max<i64>(0, index - 8); i < std::min<i64>(cast<i64>(code.size()), index + 8); i++)
		{
			const auto& line = code[i];
			if (line.code.startsWith("["))
				continue;
			u8 size = 0;
			Debugger::Utils::decodeInstruction(cast<u16>(line.addr), &size);
			if (size == line.size)
				score++;
		}
		return score;
	}

	void DisassemblyView::build_live(u16 ip)
	{
		Document& live = m_documents.back();
		live.buffer.clear();
		ogfx::gui::RichTextOutputHandler out(live.buffer);
		out.fg(ostd::ConsoleColors::BrightGray).p("  ; Not part of a loaded program: decoded from memory (no operands).").reset().nl();
		u32 addr = ip;
		m_liveStart = ip;
		for (i32 i = 0; i < LiveInstructions && addr <= 0xFFFF; i++)
		{
			u8 size = 1;
			const String name = Debugger::Utils::decodeInstruction(cast<u16>(addr), &size);
			live.buffer.setLineUserData(live.buffer.getCursorLine(), addr);
			String label = Debugger::Utils::findNearestSymbol(m_debugger.data().labels, cast<u16>(addr));
			label.fixedLength(m_debugger.data().labelLineLength);
			out.p("  ").fg(ostd::ConsoleColors::Gray).p(label).p("  ");
			out.fg(ostd::ConsoleColors::BrightGray).p(String::getHexStr(cast<u16>(addr), true, 2)).p("  ");
			String nameCol = name;
			nameCol.addRightPadding(24);
			out.fg(ostd::ConsoleColors::Blue).p(nameCol);
			for (u8 b = 0; b < size; b++)
				out.fg(ostd::ConsoleColors::BrightYellow).p(String::getHexStr(DragonRuntime::memMap.peek8(cast<u16>(addr + b)), false, 1)).p(" ");
			out.reset().nl();
			m_liveEnd = cast<u16>(addr);
			addr += size;
		}
	}

	void DisassemblyView::select_document(i32 index)
	{
		m_currentDocument = index;
		m_currentLine = -1;
		setBuffer(&m_documents[index].buffer);
	}

	void DisassemblyView::on_line_clicked(i32 line, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick)
	{
		const i64 address = getBuffer().getLineUserData(line);
		if (address < 0)
			return;
		const u16 addr = cast<u16>(address);
		if (button == ogfx::MouseEventData::eButton::Left)
		{
			if (column >= 0 && column < GutterColumns)
				m_debugger.toggleBreakPoint(addr);
			else if (doubleClick)
				m_debugger.runTo(addr);
			return;
		}
		if (button != ogfx::MouseEventData::eButton::Right)
			return;
		ogfx::gui::ContextMenu::Instance menu;
		const Debugger::tBreakPoint* bp = m_debugger.getBreakPoint(addr);
		String toggleText = "Add breakpoint";
		if (bp != nullptr && bp->debugBreak)
			toggleText = bp->enabled ? "Disable this debug_break" : "Enable this debug_break";
		else if (bp != nullptr)
			toggleText = "Remove breakpoint";
		menu.entries.push_back({ toggleText, 0 });
		menu.entries.push_back({ "Run to here", 1 });
		menu.entries.push_back({ "Set IP here (jump)", 2 });
		menu.entries.push_back({ "Show in memory", 3 });
		menu.onActivate = [this, addr](const ogfx::gui::ContextMenu::Entry& entry) {
			switch (entry.id)
			{
				case 0: m_debugger.toggleBreakPoint(addr); break;
				case 1: m_debugger.runTo(addr); break;
				case 2: m_debugger.setRegister("ip", addr); break;
				case 3: if (m_onShowInMemory) m_onShowInMemory(addr); break;
				default: break;
			}
		};
		getWindow().showContextMenu(menu, getWindow().getMousePosition());
	}

	void DisassemblyView::decorate(ogfx::BasicRenderer2D& gfx, i32 line, const Rectangle& bounds)
	{
		const i64 address = getBuffer().getLineUserData(line);
		if (address < 0)
			return;
		const f32 cw = getCharSize().x;
		const f32 gutterX = bounds.x + getScrollOffset().x;
		const f32 cy = bounds.y + bounds.h * 0.5f;
		if (line == m_currentLine)
		{
			gfx.fillRect(bounds, Color(230, 200, 75, m_debugger.isRunning() ? 25 : 55));
			gfx.fillRect({ bounds.x, bounds.y, 3, bounds.h }, Color(255, 215, 80));
			const f32 x = gutterX + cw;
			gfx.fillTriangle(Vec2 { x - cw * 0.3f, cy - bounds.h * 0.3f }, Vec2 { x + cw * 0.7f, cy }, Vec2 { x - cw * 0.3f, cy + bounds.h * 0.3f }, Color(255, 215, 80));
		}
		// Breakpoints: red, debug_break ones orange; disabled ones hollow.
		const Debugger::tBreakPoint* bp = m_debugger.getBreakPoint(cast<u16>(address));
		if (bp != nullptr)
		{
			const Color color = bp->debugBreak ? Color(240, 160, 50) : Color(235, 70, 70);
			const Vec2 center { gutterX + cw * 0.6f, cy };
			const f32 radius = std::min(cw * 0.55f, bounds.h * 0.32f);
			if (bp->enabled)
			{
				gfx.fillRect(bounds, Color(color.r, color.g, color.b, 28));
				gfx.fillCircle(center, radius, color);
			}
			else
				gfx.drawCircle(center, radius, color, 2);
		}
	}
}
