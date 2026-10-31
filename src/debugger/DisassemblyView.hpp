#pragma once

#include <ogfx/gui/widgets/RichTextView.hpp>
#include <deque>
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
	// Mouse: left margin = toggle breakpoint, double click = run to the line,
	// right click = context menu (breakpoint, run to, set IP, show in memory).
	// ============================================================================
	class DisassemblyView : public ogfx::gui::RichTextView
	{
		public: using AddressCallback = std::function<void(u16 address)>;
		public:
			DisassemblyView(ogfx::gui::Window& window, Debugger& debugger);
			// Prints the programs (once the debugger loaded the disassembly).
			void build(void);
			// Shows the instruction at `ip` (picking the program, if following).
			void setCurrentAddress(u16 ip, bool scroll = true);
			inline u16 getCurrentAddress(void) const { return m_currentAddress; }

			// Programs: the loaded tables, plus the live document (the last one).
			inline i32 getProgramCount(void) const { return cast<i32>(m_documents.size()); }
			String getProgramName(i32 index) const;
			inline i32 getCurrentProgram(void) const { return m_currentDocument; }
			// Shows a program (turns following off).
			void showProgram(i32 index);
			inline void enableFollow(bool follow = true) { m_follow = follow; }
			inline bool isFollowEnabled(void) const { return m_follow; }
			// Scrolls to an address of the current program (if it has it).
			bool scrollToAddress(u16 address);
			// Shows an address in whichever program has it (decoded from memory if none),
			// turning the IP following off.
			void showAddress(u16 address);

			// "Show in memory" from the context menu.
			inline void setShowInMemoryCallback(AddressCallback callback) { m_onShowInMemory = std::move(callback); }
			inline void setBreakpointsChangedCallback(std::function<void(void)> callback) { m_onBreakpointsChanged = std::move(callback); }

		private:
			struct Document
			{
				ogfx::gui::RichTextBuffer buffer;
				const DisassemblyTable* table { nullptr };
				String name { "" };
			};
			i32 pick_document(u16 ip) const;
			i32 match_score(const DisassemblyTable& table, u16 ip) const;
			void build_live(u16 ip);
			void select_document(i32 index);
			void on_line_clicked(i32 line, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick);
			void decorate(ogfx::BasicRenderer2D& gfx, i32 line, const Rectangle& bounds);

		private:
			Debugger& m_debugger;
			std::deque<Document> m_documents;
			i32 m_currentDocument { -1 };
			u16 m_currentAddress { 0 };
			i32 m_currentLine { -1 };
			bool m_follow { true };
			// Where the live document starts (it's rebuilt when the IP leaves it).
			u16 m_liveStart { 0 };
			u16 m_liveEnd { 0 };
			AddressCallback m_onShowInMemory { nullptr };
			std::function<void(void)> m_onBreakpointsChanged { nullptr };

		public:
			inline static constexpr i32 GutterColumns { 2 };
			inline static constexpr i32 LiveInstructions { 256 };
	};
}
