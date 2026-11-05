#include "DisassemblyView.hpp"
#include "Debugger.hpp"
#include "../runtime/DragonRuntime.hpp"
#include <ogfx/gui/Window.hpp>
#include <filesystem>
#include <set>
#include <cctype>

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
		// The instructions under source lines: the same colors, less saturated.
		ogfx::gui::RichTextOutputHandler asmOut;
		static const char* colorNames[] = { "red", "brightred", "green", "brightgreen", "blue", "brightblue", "magenta", "brightmagenta",
			"cyan", "brightcyan", "yellow", "brightyellow", "darkgray", "brightgray", "white", nullptr };
		for (i32 i = 0; colorNames[i] != nullptr; i++)
			asmOut.setConsoleColor(colorNames[i], dim(desaturate(asmOut.getConsoleColor(colorNames[i]))));
		asmOut.setDefaultForeground(dim(AsmTextColor));
		asmOut.setConsoleColor("brightgray", dim(AddressColor));	// the addresses
		for (const auto& table : DisassemblyLoader::getTables())
		{
			if (table.getCodeTable().empty())
				continue;
			Document& doc = m_documents.emplace_back();
			doc.table = &table;
			// Files saved by an older assembler have no name: the file's own name then.
			doc.name = table.getName();
			if (doc.name == "")
				doc.name = String(std::filesystem::path(table.getFilePath().cpp_str()).filename().string());
			doc.name.add("  [").add(String::getHexStr(cast<u32>(table.getCodeStart()), true, 4)).add(" - ")
					.add(String::getHexStr(cast<u32>(table.getCodeEnd()), true, 4)).add("]");
			out.attach(doc.buffer);
			asmOut.attach(doc.buffer);
			if (table.getSources().empty())
				build_plain(doc, out);
			else
				build_with_sources(doc, out, asmOut);
		}
		Document& live = m_documents.emplace_back();
		live.name = "Live (decoded from memory)";
		select_document(cast<i32>(m_documents.size()) - 1);
	}

	namespace
	{
		// ===== DScript highlighting (the source lines) =====
		const Color SrcKeyword { 198, 120, 221 };
		const Color SrcType { 229, 192, 123 };
		const Color SrcNumber { 209, 154, 102 };
		const Color SrcString { 152, 195, 121 };
		const Color SrcComment { 110, 118, 130 };
		const Color SrcDirective { 86, 182, 194 };
		const Color SrcFunction { 97, 175, 239 };
		const Color SrcText { 200, 204, 212 };
		const Color SrcPunct { 150, 158, 172 };

		bool is_dscript_keyword(const std::string& w)
		{
			static const std::set<std::string> words = {
				"var", "const", "sub", "struct", "enum", "alias", "return", "if", "else", "while", "do", "for",
				"break", "continue", "null", "true", "false", "cast", "sizeof", "low", "interrupt", "inline", "static", "priv",
				"assert", "debug_print", "and", "or", "not" };
			return words.count(w) > 0;
		}

		bool is_dscript_type(const std::string& w)
		{
			static const std::set<std::string> words = { "i8", "u8", "i16", "u16", "i32", "u32", "int", "uint", "bool", "string", "void" };
			return words.count(w) > 0;
		}
	}

	DisassemblyView::tHighlightedFile DisassemblyView::highlight(const tSourceFile& file)
	{
		// Line by line, with the state that crosses lines (block comments, low blocks).
		tHighlightedFile result;
		const bool assembly = file.name.new_toLower().endsWith(".dss") || file.name.new_toLower().endsWith(".dasm");
		bool inComment = false;
		i32 lowDepth = 0;		// > 0: inside a low block (its braces)
		bool lowPending = false;	// 'low' seen, '{' not yet
		for (const auto& rawLine : file.lines)
		{
			const std::string text = String(rawLine).replaceAll("\t", "    ").cpp_str();
			tHighlightedLine out;
			auto l_span = [&](const std::string& t, const Color& c) { if (!t.empty()) out.spans.push_back({ String(t), c }); };
			if (assembly || (lowDepth > 0 && text.find('}') == std::string::npos))
			{
				// dasm (a .dss file, or a whole line of a low block): the disassembly's colors.
				out.assembly = true;
				out.spans.push_back({ String(text), SrcText });
				result.lines.push_back(out);
				continue;
			}
			size_t i = 0;
			while (i < text.size())
			{
				const char c = text[i];
				if (inComment)
				{
					const size_t end = text.find("*/", i);
					const size_t stop = (end == std::string::npos ? text.size() : end + 2);
					l_span(text.substr(i, stop - i), SrcComment);
					inComment = (end == std::string::npos);
					i = stop;
				}
				else if (c == '/' && i + 1 < text.size() && text[i + 1] == '/')
				{
					l_span(text.substr(i), SrcComment);
					i = text.size();
				}
				else if (c == '/' && i + 1 < text.size() && text[i + 1] == '*')
				{
					inComment = true;
					l_span("/*", SrcComment);
					i += 2;
				}
				else if (c == '"' || c == '\'')
				{
					size_t j = i + 1;
					while (j < text.size() && text[j] != c)
						j += (text[j] == '\\' ? 2 : 1);
					j = std::min(j + 1, text.size());
					l_span(text.substr(i, j - i), SrcString);
					i = j;
				}
				else if (c == '#')
				{
					size_t j = i + 1;
					while (j < text.size() && (std::isalnum((unsigned char)text[j]) || text[j] == '_'))
						j++;
					l_span(text.substr(i, j - i), SrcDirective);
					i = j;
				}
				else if (std::isdigit((unsigned char)c))
				{
					size_t j = i;
					while (j < text.size() && (std::isalnum((unsigned char)text[j]) || text[j] == '_'))
						j++;
					l_span(text.substr(i, j - i), SrcNumber);
					i = j;
				}
				else if (std::isalpha((unsigned char)c) || c == '_')
				{
					size_t j = i;
					while (j < text.size() && (std::isalnum((unsigned char)text[j]) || text[j] == '_'))
						j++;
					const std::string word = text.substr(i, j - i);
					size_t k = j;
					while (k < text.size() && text[k] == ' ')
						k++;
					if (word == "low")
						lowPending = true;
					if (is_dscript_keyword(word))
						l_span(word, SrcKeyword);
					else if (is_dscript_type(word))
						l_span(word, SrcType);
					else if (k < text.size() && text[k] == '(')
						l_span(word, SrcFunction);
					else
						l_span(word, SrcText);
					i = j;
				}
				else
				{
					if (c == '{' && (lowPending || lowDepth > 0))
					{
						lowDepth++;
						lowPending = false;
					}
					else if (c == '}' && lowDepth > 0)
						lowDepth--;
					else if (c != ' ' && c != '{')
						lowPending = (lowPending && c == ' ');
					l_span(std::string(1, c), c == ' ' ? SrcText : SrcPunct);
					i++;
				}
			}
			result.lines.push_back(out);
		}
		return result;
	}

	i32 DisassemblyView::leading_spaces(const String& text)
	{
		const std::string t = text.cpp_str();
		const size_t first = t.find_first_not_of(' ');
		return cast<i32>(first == std::string::npos ? 0 : first);
	}

	Color DisassemblyView::dim(const Color& color)
	{
		auto l_dim = [](u8 c) { return cast<u8>(cast<f32>(c) * (1.0f - AsmDimming) + 0.5f); };
		return Color(l_dim(color.r), l_dim(color.g), l_dim(color.b), color.a);
	}

	Color DisassemblyView::desaturate(const Color& color)
	{
		const f32 gray = (cast<f32>(color.r) + cast<f32>(color.g) + cast<f32>(color.b)) / 3.0f;
		auto l_mix = [gray](u8 c) { return cast<u8>(std::clamp((cast<f32>(c) * 0.5f + gray * 0.5f) * 0.82f, 0.0f, 255.0f)); };
		return Color(l_mix(color.r), l_mix(color.g), l_mix(color.b), color.a);
	}

	void DisassemblyView::set_kind(Document& doc, eLineKind kind)
	{
		const i32 line = doc.buffer.getCursorLine();
		if (cast<i32>(doc.kinds.size()) <= line)
			doc.kinds.resize(line + 1, eLineKind::Plain);
		doc.kinds[line] = kind;
	}

	void DisassemblyView::build_plain(Document& doc, ogfx::gui::RichTextOutputHandler& out)
	{
		// Labels on their own lines (no address: they're not instructions), before the
		// instruction they point to; the label table is sorted, like the code table.
		const auto& labels = doc.table->getLabelTable();
		size_t nextLabel = 0;
		for (const auto& line : doc.table->getCodeTable())
		{
			for (; nextLabel < labels.size() && labels[nextLabel].addr <= line.addr; nextLabel++)
			{
				Debugger::Display::printDisassemblyLabel(out, labels[nextLabel].code.new_trim());
				out.nl();
			}
			doc.buffer.setLineUserData(doc.buffer.getCursorLine(), line.addr);
			Debugger::Display::printDisassemblyLine(out, m_debugger, line, -1, &labels);
			out.nl();
		}
	}

	void DisassemblyView::build_with_sources(Document& doc, ogfx::gui::RichTextOutputHandler& out, ogfx::gui::RichTextOutputHandler& asmOut)
	{
		// Columns: the gutter (2), the line number (5), " | ", then the source text; the
		// instructions are 4 columns further right than their source line's indentation, the
		// labels at the source text's column.
		const auto& table = *doc.table;
		const auto& labels = table.getLabelTable();
		const auto& sources = table.getSources();
		std::vector<tHighlightedFile> highlighted;
		for (const auto& file : sources)
			highlighted.push_back(highlight(file));
		auto l_prefix = [&](ogfx::gui::RichTextOutputHandler& o, const String& number, const Color& numberColor, const String& separator, const Color& separatorColor) {
			String n = number;
			n.addLeftPadding(5);
			o.p("  ").fg(numberColor).p(n).p("   ").reset();	// the separator (a line) is drawn by decorate()
		};
		// The last source line printed, and every one printed (each is shown once as context).
		i32 shownLineFile = -1, shownLine = 0, shownLineDepth = 0;
		std::set<std::pair<i32, i32>> shownLines;
		auto l_source_line = [&](const tSourceFile& file, i32 fileIndex, i32 lineNr, u8 depth, u32 address) {
			set_kind(doc, eLineKind::Source);
			doc.sourceLines[doc.buffer.getCursorLine()] = address;
			const bool inlined = depth > 0;
			l_prefix(out, String("").add(lineNr), inlined ? InlineNumberColor : LineNumberColor, "", SourceSeparatorColor);
			const String text = String(file.line(lineNr)).replaceAll("\t", "    ");
			const auto& hl = highlighted[fileIndex];
			if (lineNr >= 1 && lineNr <= cast<i32>(hl.lines.size()))
			{
				const auto& hline = hl.lines[lineNr - 1];
				if (hline.assembly)
				{
					// Low block lines / dasm sources: the instruction coloring, after the indentation.
					const String trimmed = text.new_trim();
					out.p(String::duplicateChar(' ', leading_spaces(text)));
					if (trimmed.endsWith(":") && !trimmed.contains(" "))
						out.fg(ostd::ConsoleColors::Magenta).p(trimmed);
					else if (trimmed.startsWith("##") || trimmed.startsWith("//"))
						out.fg(SrcComment).p(trimmed);
					else if (trimmed != "")
						Debugger::Display::colorCodeInstructions(out, trimmed, labels);
				}
				else
				{
					for (const auto& span : hline.spans)
						out.fg(span.color).p(span.text);
				}
			}
			out.reset().nl();
			shownLines.insert({ fileIndex, lineNr });
		};
		size_t nextLabel = 0;
		i32 lastFile = -1, lastLine = 0, lastDepth = 0, shownFile = -1;
		i32 indent = 0;
		for (const auto& line : table.getCodeTable())
		{
			for (; nextLabel < labels.size() && labels[nextLabel].addr <= line.addr; nextLabel++)
			{
				set_kind(doc, eLineKind::Assembly);
				l_prefix(asmOut, "", LineNumberColor, "", AsmSeparatorColor);
				asmOut.fg(ostd::ConsoleColors::Magenta).p(labels[nextLabel].code.new_trim()).p(":").reset();
				asmOut.nl();
			}
			// The source line, when the instructions move to another one.
			const bool hasSource = (line.srcFile >= 0 && line.srcFile < cast<i32>(sources.size()));
			if (hasSource && (line.srcFile != lastFile || line.srcLine != lastLine || line.inlineDepth != lastDepth))
			{
				const tSourceFile& file = sources[line.srcFile];
				if (line.srcFile != shownFile)
				{
					set_kind(doc, eLineKind::Header);
					doc.headers[doc.buffer.getCursorLine()] = file.name;
					out.nl();
					shownFile = line.srcFile;
				}
				// The lines without code before this one (declarations without a value, "do {",
				// comments, ...): shown once, when the code goes forwards a little in the file.
				const bool forwards = (line.srcFile == shownLineFile && line.inlineDepth == shownLineDepth && line.srcLine > shownLine
					&& line.srcLine - shownLine - 1 <= MaxContextLines);
				if (forwards)
				{
					for (i32 n = shownLine + 1; n < line.srcLine; n++)
					{
						if (shownLines.count({ line.srcFile, n }) == 0)
							l_source_line(file, line.srcFile, n, line.inlineDepth, cast<u32>(line.addr));
					}
				}
				l_source_line(file, line.srcFile, line.srcLine, line.inlineDepth, cast<u32>(line.addr));
				shownLineFile = line.srcFile;
				shownLine = line.srcLine;
				shownLineDepth = line.inlineDepth;
				const String text = String(file.line(line.srcLine)).replaceAll("\t", "    ");
				indent = leading_spaces(text);
			}
			else if (!hasSource && lastFile >= 0)
				indent = 0;		// generated code (no source line)
			lastFile = (hasSource ? line.srcFile : -1);
			lastLine = line.srcLine;
			lastDepth = line.inlineDepth;
			set_kind(doc, eLineKind::Assembly);
			doc.buffer.setLineUserData(doc.buffer.getCursorLine(), line.addr);
			l_prefix(asmOut, "", LineNumberColor, "", AsmSeparatorColor);
			asmOut.p(String::duplicateChar(' ', indent + 4));
			Debugger::Display::printDisassemblyLine(asmOut, m_debugger, line, 0, &labels);
			asmOut.nl();
		}
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

	bool DisassemblyView::scrollToAddress(u32 address)
	{
		const i32 line = getBuffer().findLine(address);
		if (line < 0)
			return false;
		scrollToLine(line, true);
		return true;
	}

	void DisassemblyView::showAddress(u32 address)
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

	void DisassemblyView::setCurrentAddress(u32 ip, bool scroll)
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
		m_currentSourceLine = find_source_line(m_currentLine);
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

	i32 DisassemblyView::pick_document(u32 ip) const
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

	i32 DisassemblyView::match_score(const DisassemblyTable& table, u32 ip) const
	{
		// The program must have an instruction exactly at the IP; then the instructions around
		// it are compared with memory (the decoded size must be the size in the table).
		const auto& code = table.getCodeTable();
		auto it = std::lower_bound(code.begin(), code.end(), ip, [](const code::Assembler::tDisassemblyLine& line, u32 addr) { return line.addr < addr; });
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
			Debugger::Utils::decodeInstruction(cast<u32>(line.addr), &size);
			if (size == line.size)
				score++;
		}
		return score;
	}

	void DisassemblyView::build_live(u32 ip)
	{
		Document& live = m_documents.back();
		live.buffer.clear();
		ogfx::gui::RichTextOutputHandler out(live.buffer);
		out.fg(ostd::ConsoleColors::BrightGray).p("  ; Not part of a loaded program: decoded from memory (no operands).").reset().nl();
		u32 addr = ip;
		m_liveStart = ip;
		for (i32 i = 0; i < LiveInstructions && addr <= data::AddressMask; i++)
		{
			u8 size = 1;
			const String name = Debugger::Utils::decodeInstruction(cast<u32>(addr), &size);
			const String label = Debugger::Utils::findSymbol(m_debugger.data().labels, cast<u32>(addr));
			if (label != "")
			{
				Debugger::Display::printDisassemblyLabel(out, label.new_trim());
				out.nl();
			}
			live.buffer.setLineUserData(live.buffer.getCursorLine(), addr);
			out.p("  ").p(String::duplicateChar(' ', Debugger::Display::LabelIndent));
			out.fg(ostd::ConsoleColors::BrightGray).p(String::getHexStr(cast<u32>(addr), true, 4)).p("  ");
			String nameCol = name;
			nameCol.addRightPadding(24);
			out.fg(ostd::ConsoleColors::Blue).p(nameCol);
			for (u8 b = 0; b < size; b++)
				out.fg(ostd::ConsoleColors::BrightYellow).p(String::getHexStr(DragonRuntime::memMap.peek8(cast<u32>(addr + b)), false, 1)).p(" ");
			out.reset().nl();
			m_liveEnd = cast<u32>(addr);
			addr += size;
		}
	}

	DisassemblyView::eLineKind DisassemblyView::line_kind(i32 line) const
	{
		if (m_currentDocument < 0)
			return eLineKind::Plain;
		const auto& kinds = m_documents[m_currentDocument].kinds;
		return (line >= 0 && line < cast<i32>(kinds.size())) ? kinds[line] : eLineKind::Plain;
	}

	i32 DisassemblyView::find_source_line(i32 instructionLine) const
	{
		// Back from the instruction, over the instructions and labels of its line.
		if (instructionLine < 0 || m_currentDocument < 0)
			return -1;
		const Document& doc = m_documents[m_currentDocument];
		if (doc.sourceLines.empty())
			return -1;
		for (i32 l = instructionLine, n = 0; l >= 0 && n < 4096; l--, n++)
		{
			if (doc.sourceLines.count(l))
				return l;
			// Instructions without a source line (generated code) end the search.
			const i64 address = getBuffer().getLineUserData(l);
			if (address >= 0 && !m_debugger.getSourceLocation(cast<u32>(address)).isValid())
				return -1;
		}
		return -1;
	}

	void DisassemblyView::select_document(i32 index)
	{
		m_currentDocument = index;
		m_currentLine = -1;
		m_currentSourceLine = -1;
		setBuffer(&m_documents[index].buffer);
	}

	void DisassemblyView::on_line_clicked(i32 line, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick)
	{
		i64 address = getBuffer().getLineUserData(line);
		if (address < 0 && m_currentDocument >= 0)
		{
			// A source line: its first instruction.
			const auto& sourceLines = m_documents[m_currentDocument].sourceLines;
			auto it = sourceLines.find(line);
			if (it != sourceLines.end())
				address = it->second;
		}
		if (address < 0)
			return;
		const u32 addr = cast<u32>(address);
		if (button == ogfx::MouseEventData::eButton::Left)
		{
			if (column >= 0 && column < GutterColumns)
				m_debugger.toggleBreakPoint(addr);
			else if (doubleClick && m_onShowInMemory)
				m_onShowInMemory(addr);
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
		const eLineKind kind = line_kind(line);
		if (kind == eLineKind::Assembly || kind == eLineKind::Source)
		{
			// The background, and the separator after the line numbers: a continuous line.
			if (kind == eLineKind::Assembly)
			{
				// The background, then the black layer (the text is dimmed the same way).
				gfx.fillRect(bounds, AsmBackground);
				gfx.fillRect(bounds, Color(0, 0, 0, cast<u8>(AsmDimming * 255.0f + 0.5f)));
			}
			else
			{
				gfx.fillRect(bounds, SourceBackground);
				if (line == m_currentSourceLine)
				{
					gfx.fillRect(bounds, Color(CurrentSourceColor.r, CurrentSourceColor.g, CurrentSourceColor.b, m_debugger.isRunning() ? 14 : 30));
					gfx.fillRect({ bounds.x, bounds.y, 3, bounds.h }, CurrentSourceColor);
				}
			}
			const f32 x = bounds.x + getScrollOffset().x + getCharSize().x * (SeparatorColumn + 0.5f);
			gfx.fillRect({ x, bounds.y, 2, bounds.h }, kind == eLineKind::Source ? SourceSeparatorColor : dim(AsmSeparatorColor));
		}
		else if (kind == eLineKind::Header)
		{
			// The file name, centered in the visible width.
			gfx.fillRect(bounds, HeaderBackground);
			gfx.fillRect({ bounds.x, bounds.y + bounds.h - 1, bounds.w, 1 }, HeaderBorder);
			const auto& headers = m_documents[m_currentDocument].headers;
			auto it = headers.find(line);
			if (it != headers.end())
			{
				TTF_Font* font = gfx.getDefaultMonoFont(getFontSize());
				if (font != nullptr)
				{
					gfx.pushFont(font);
					gfx.drawCenteredString(it->second, bounds, HeaderTextColor, getFontSize());
					gfx.popFont();
				}
			}
			return;
		}
		if (address < 0)
		{
			return;
		}
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
		const Debugger::tBreakPoint* bp = m_debugger.getBreakPoint(cast<u32>(address));
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
