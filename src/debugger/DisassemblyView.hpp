#pragma once

#include <ogfx/gui/widgets/RichTextView.hpp>
#include <deque>
#include <unordered_map>
#include "DisassemblyLoader.hpp"

namespace dragon
{
	class Debugger;

	// ============================================================================
	// DisassemblyView — the code around the instruction pointer.
	//
	// One document per assembled program (the tables in the disassembly
	// directory), printed once with the old TUI's coloring (Debugger::Display);
	// the dynamic parts (current instruction, breakpoints) are drawn over it by
	// the line decorator, so nothing is rebuilt while the machine runs.
	// Programs can share addresses (the bootloader, the kernel, ...): the one
	// shown is the one whose code matches the memory around the instruction
	// pointer. Code outside every program is decoded from memory (the "live"
	// document).
	//
	// Programs with source lines (dsc, or dasm since disassembly version 2) show each source
	// line before its instructions; a source line's margin / menu acts on its first instruction.
	//
	// Mouse: left margin = toggle breakpoint, double click = run to the line,
	// right click = context menu (breakpoint, run to, set IP, show in memory).
	// ============================================================================
	class DisassemblyView : public ogfx::gui::RichTextView
	{
		public: using AddressCallback = std::function<void(u32 address)>;
		public:
			DisassemblyView(ogfx::gui::Window& window, Debugger& debugger);
			// Prints the programs (once the debugger loaded the disassembly).
			void build(void);
			// Shows the instruction at `ip` (picking the program, if following).
			void setCurrentAddress(u32 ip, bool scroll = true);
			inline u32 getCurrentAddress(void) const { return m_currentAddress; }

			// Programs: the loaded tables, plus the live document (the last one).
			inline i32 getProgramCount(void) const { return cast<i32>(m_documents.size()); }
			String getProgramName(i32 index) const;
			inline i32 getCurrentProgram(void) const { return m_currentDocument; }
			// Shows a program (turns following off).
			void showProgram(i32 index);
			inline void enableFollow(bool follow = true) { m_follow = follow; }
			inline bool isFollowEnabled(void) const { return m_follow; }
			// Scrolls to an address of the current program (if it has it).
			bool scrollToAddress(u32 address);
			// Shows an address in whichever program has it (decoded from memory if none),
			// turning the IP following off.
			void showAddress(u32 address);

			// "Show in memory" from the context menu.
			inline void setShowInMemoryCallback(AddressCallback callback) { m_onShowInMemory = std::move(callback); }
			inline void setBreakpointsChangedCallback(std::function<void(void)> callback) { m_onBreakpointsChanged = std::move(callback); }

		private:
			enum class eLineKind : u8 { Plain = 0, Source, Header, Assembly };
			// A source file with colors: spans per line; `assembly` lines (dasm sources, low
			// blocks) are colored like the disassembly.
			struct tSpan { String text; ostd::Color color; };
			struct tHighlightedLine { std::vector<tSpan> spans; bool assembly { false }; };
			struct tHighlightedFile { std::vector<tHighlightedLine> lines; };
			struct Document
			{
				ogfx::gui::RichTextBuffer buffer;
				const DisassemblyTable* table { nullptr };
				String name { "" };
				// Source lines (printed before their instructions): buffer line -> the address of
				// the first instruction after it.
				std::unordered_map<i32, u32> sourceLines;
				std::vector<eLineKind> kinds;
				std::unordered_map<i32, String> headers;	// header line -> file name
			};
			static tHighlightedFile highlight(const tSourceFile& file);
			static ostd::Color desaturate(const ostd::Color& color);
			// Under the black layer (AsmDimming).
			static ostd::Color dim(const ostd::Color& color);
			static i32 leading_spaces(const String& text);
			static void set_kind(Document& doc, eLineKind kind);
			eLineKind line_kind(i32 line) const;
			void build_plain(Document& doc, ogfx::gui::RichTextOutputHandler& out);
			void build_with_sources(Document& doc, ogfx::gui::RichTextOutputHandler& out, ogfx::gui::RichTextOutputHandler& asmOut);
			// The source line of the current instruction (-1 if none).
			i32 find_source_line(i32 instructionLine) const;
			i32 pick_document(u32 ip) const;
			i32 match_score(const DisassemblyTable& table, u32 ip) const;
			void build_live(u32 ip);
			void select_document(i32 index);
			void on_line_clicked(i32 line, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick);
			void decorate(ogfx::BasicRenderer2D& gfx, i32 line, const Rectangle& bounds);

		private:
			Debugger& m_debugger;
			std::deque<Document> m_documents;
			i32 m_currentDocument { -1 };
			u32 m_currentAddress { 0 };
			i32 m_currentLine { -1 };
			i32 m_currentSourceLine { -1 };
			bool m_follow { true };
			// Where the live document starts (it's rebuilt when the IP leaves it).
			u32 m_liveStart { 0 };
			u32 m_liveEnd { 0 };
			AddressCallback m_onShowInMemory { nullptr };
			std::function<void(void)> m_onBreakpointsChanged { nullptr };

		public:
			inline static constexpr i32 GutterColumns { 2 };
			inline static constexpr i32 LiveInstructions { 256 };
			// Programs with source lines.
			inline static const ostd::Color LineNumberColor { 120, 150, 190 };
			inline static const ostd::Color InlineNumberColor { 120, 190, 140 };
			inline static const ostd::Color SourceSeparatorColor { 110, 120, 140 };
			inline static const ostd::Color AsmSeparatorColor { 70, 90, 120 };
			inline static const ostd::Color AsmTextColor { 150, 150, 155 };
			inline static const ostd::Color AddressColor { 100, 103, 110 };
			// Source lines (stronger on the current one, with a bar on the left).
			inline static const ostd::Color SourceBackground { 0x0F, 0x14, 0x18 };
			inline static const ostd::Color CurrentSourceColor { 120, 200, 230 };
			// The instruction lines are these colors under a black layer of this opacity.
			inline static constexpr f32 AsmDimming { 0.46f };
			// The column of the separator line (after the gutter and the line number).
			inline static constexpr i32 SeparatorColumn { 8 };
			// At most this many lines without code are shown between two source lines.
			inline static constexpr i32 MaxContextLines { 12 };
			inline static const ostd::Color AsmBackground { 0, 10, 30, 70 };
			inline static const ostd::Color HeaderBackground { 0x24, 0x41, 0x1E };
			inline static const ostd::Color HeaderBorder { 0x22, 0x4D, 0x18 };
			inline static const ostd::Color HeaderTextColor { 230, 235, 245 };
	};
}
