#pragma once

#include <ogfx/gui/Window.hpp>
#include <ogfx/gui/Widgets.hpp>
#include "Debugger.hpp"

namespace dragon
{
	using ostd::Color;
	using ostd::Vec2;
	using ostd::Rectangle;

	// ============================================================================
	// DebugListView — the base of the debugger's lists (tracker, call trace,
	// breakpoints): a header with columns and virtualized rows (only the visible
	// ones are drawn, the rows come from the subclass), in the monospace font.
	//
	// Columns have a width in characters; the last one takes the remaining space.
	// Mouse: wheel scrolls, the scrollbar on the right can be dragged, clicks go to
	// onRowClicked() (with the column; double-clicks flagged). Keyboard (focused):
	// Up / Down / PageUp / PageDown / Home / End move the selection, Return
	// activates it (as a double-click).
	// ============================================================================
	class DebugListView : public ogfx::gui::Widget
	{
		public: struct Column
		{
			String title { "" };
			f32 chars { 10 };
		};
		public: using AddressCallback = std::function<void(u16 address)>;

		public:
			DebugListView(ogfx::gui::Window& window);
			void setColumns(const stdvec<Column>& columns);

			virtual i32 getRowCount(void) const = 0;
			// Draws a cell's content (clipped to the cell).
			virtual void drawCell(ogfx::BasicRenderer2D& gfx, i32 row, i32 column, const Rectangle& bounds) = 0;
			// Tint of a row's background (alpha 0 = none).
			inline virtual Color getRowColor(i32 row) const { return { 0, 0, 0, 0 }; }
			inline virtual void onRowClicked(i32 row, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick) {  }
			// Shown when there are no rows.
			inline virtual String getEmptyText(void) const { return ""; }

			inline i32 getSelectedRow(void) const { return m_selectedRow; }
			void setSelectedRow(i32 row);
			void ensureRowVisible(i32 row, bool centered = false);
			void scrollToBottom(void);
			// Sticks to the bottom when rows are added (while the view is at the bottom).
			inline void enableFollowTail(bool follow = true) { m_followTail = follow; }
			i32 getVisibleRowCount(void) const;

			void onDraw(ogfx::BasicRenderer2D& gfx) override;
			void onUpdate(void) override;
			void onMousePressed(const ogfx::gui::Event& event) override;
			void onMouseReleased(const ogfx::gui::Event& event) override;
			void onMouseMoved(const ogfx::gui::Event& event) override;
			void onMouseDragged(const ogfx::gui::Event& event) override;
			void onMouseExited(const ogfx::gui::Event& event) override;
			void onMouseScrolled(const ogfx::gui::Event& event) override;
			void onKeyPressed(const ogfx::gui::Event& event) override;

		protected:
			// Draws text in the monospace font.
			void text(ogfx::BasicRenderer2D& gfx, const String& str, f32 x, f32 y, const Color& color);
			inline f32 charWidth(void) const { return m_charSize.x; }
			inline f32 rowHeight(void) const { return m_charSize.y + 4.0f; }
			Rectangle rowsArea(void) const;
			i32 rowAt(const Vec2& position) const;
			i32 columnAt(f32 x) const;
			// The x range of a column (global).
			Rectangle columnBounds(i32 column) const;
			f32 maxScroll(void) const;
			void clampScroll(void);
			Rectangle scrollbarTrack(void) const;
			Rectangle scrollbarThumb(void) const;

		protected:
			stdvec<Column> m_columns;
			i32 m_selectedRow { -1 };
			i32 m_hoveredRow { -1 };
			f32 m_scroll { 0 };		// rows
			bool m_followTail { false };
			i32 m_lastRowCount { 0 };
			bool m_draggingScrollbar { false };
			f32 m_dragGrab { 0 };
			u64 m_lastClickMs { 0 };
			i32 m_lastClickRow { -1 };
			Vec2 m_charSize { 9, 18 };
			i32 m_metricsFontSize { 0 };

		public:
			inline static const Color BackgroundColor { 18, 20, 18 };
			inline static const Color HeaderColor { 32, 38, 32 };
			inline static const Color HeaderTextColor { 230, 200, 90 };
			inline static const Color TextColor { 210, 215, 210 };
			inline static const Color DimTextColor { 120, 128, 120 };
			inline static const Color GridColor { 48, 54, 48 };
			inline static const Color SelectionColor { 47, 140, 220, 90 };
			inline static const Color HoverColor { 255, 255, 255, 14 };
			inline static const Color ChangedColor { 255, 150, 60 };
			inline static constexpr f32 ScrollbarWidth { 12 };
			inline static constexpr u64 DoubleClickMs { 400 };
	};




	// ============================================================================
	// RegisterView — the CPU state: the registers in cards (value in hex, signed /
	// unsigned decimal, the value before the last step when it changed), the flags
	// as toggles, and the machine info (executed / next instruction, cycle, stack
	// frame, interrupt handlers, BIOS mode, subroutine depth, halt).
	//
	// Double-click a register to edit it (hex, decimal, binary or 'c'), click a
	// flag to toggle it; right-click a register for more (show its value in
	// memory / in the disassembly, copy, zero it). The bottom line shows the hovered
	// register's value in every format.
	// ============================================================================
	class RegisterView : public ogfx::gui::Widget
	{
		public: using AddressCallback = std::function<void(u16 address)>;

		public:
			RegisterView(ogfx::gui::Window& window, Debugger& debugger);
			inline void setShowInMemoryCallback(AddressCallback callback) { m_onShowInMemory = std::move(callback); }
			inline void setShowInDisassemblyCallback(AddressCallback callback) { m_onShowInDisassembly = std::move(callback); }

			void onDraw(ogfx::BasicRenderer2D& gfx) override;
			void onMousePressed(const ogfx::gui::Event& event) override;
			void onMouseMoved(const ogfx::gui::Event& event) override;
			void onMouseExited(const ogfx::gui::Event& event) override;
			void onMouseScrolled(const ogfx::gui::Event& event) override;

		private: class ValueInput : public ogfx::gui::TextEdit
		{
			public:
				inline ValueInput(ogfx::gui::Window& window, RegisterView& view) : TextEdit(window), m_view(view) { setTypeName("dragon::RegisterView::ValueInput"); }
				void onKeyPressed(const ogfx::gui::Event& event) override;
				void onFocusLost(const ogfx::gui::Event& event) override;
			private:
				RegisterView& m_view;
		};

		private:
			void update_metrics(ogfx::BasicRenderer2D& gfx);
			void text(ogfx::BasicRenderer2D& gfx, const String& str, f32 x, f32 y, const Color& color, i32 fontSize = 0);
			// Global bounds of a register card / a flag box.
			Rectangle register_rect(i32 index) const;
			Rectangle flag_rect(i32 bit) const;
			f32 registers_top(void) const;
			i32 register_at(const Vec2& position) const;
			i32 flag_at(const Vec2& position) const;
			void begin_edit(i32 index);
			void end_edit(bool apply);
			void show_menu(i32 index);
			u16 value_of(i32 index) const;

		private:
			Debugger& m_debugger;
			ValueInput m_input { getWindow(), *this };
			AddressCallback m_onShowInMemory { nullptr };
			AddressCallback m_onShowInDisassembly { nullptr };
			i32 m_hovered { -1 };
			i32 m_hoveredFlag { -1 };
			i32 m_editing { -1 };
			u64 m_lastClickMs { 0 };
			i32 m_lastClickIndex { -1 };
			Vec2 m_charSize { 9, 18 };
			i32 m_metricsFontSize { 0 };
			i32 m_fontSize { 15 };
			f32 m_infoHeight { 0 };
			f32 m_contentHeight { 0 };
			f32 m_scroll { 0 };	// pixels
			i32 m_cardColumns { 1 };

		public:
			inline static constexpr i32 RegisterCount { 20 };
			inline static constexpr f32 CardChars { 15 };
			inline static constexpr f32 CardLines { 2.4f };
	};




	// ============================================================================
	// TrackerView — the tracked data: symbol, size, address, memory region, the
	// value before the last step and now (the changed bytes highlighted) and as
	// text. The first column is the watch toggle (the machine stops when a watched
	// value changes). Double-click shows the data in the memory view; right-click
	// for more.
	// ============================================================================
	class TrackerView : public DebugListView
	{
		public:
			TrackerView(ogfx::gui::Window& window, Debugger& debugger);
			inline void setShowInMemoryCallback(AddressCallback callback) { m_onShowInMemory = std::move(callback); }

			i32 getRowCount(void) const override;
			void drawCell(ogfx::BasicRenderer2D& gfx, i32 row, i32 column, const Rectangle& bounds) override;
			Color getRowColor(i32 row) const override;
			void onRowClicked(i32 row, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick) override;
			String getEmptyText(void) const override;
			void onUpdate(void) override;

		private:
			bool changed(i32 row) const;

		private:
			Debugger& m_debugger;
			AddressCallback m_onShowInMemory { nullptr };
			// Offset of each item's bytes in the captured values.
			stdvec<size_t> m_valueOffsets;
			u64 m_seenVersion { cast<u64>(-1) };
	};




	// ============================================================================
	// CallTraceView — the calls, returns and interrupts, as a tree: each call can
	// be collapsed (click its arrow) to hide everything up to its return. The calls
	// that haven't returned yet (the current call stack) are highlighted.
	//
	// Double-click a row to go back in time to it (the history position before the
	// call / return executed); right-click for more (show the caller or the target
	// in the disassembly, collapse / expand all).
	// ============================================================================
	class CallTraceView : public DebugListView
	{
		public:
			CallTraceView(ogfx::gui::Window& window, Debugger& debugger);
			inline void setShowInDisassemblyCallback(AddressCallback callback) { m_onShowInDisassembly = std::move(callback); }
			// Rebuilds the rows (if the trace changed).
			void refreshRows(void);

			i32 getRowCount(void) const override;
			void drawCell(ogfx::BasicRenderer2D& gfx, i32 row, i32 column, const Rectangle& bounds) override;
			Color getRowColor(i32 row) const override;
			void onRowClicked(i32 row, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick) override;
			String getEmptyText(void) const override;
			void onUpdate(void) override;

		private: struct Row
		{
			u32 index { 0 };		// in the trace
			u32 hidden { 0 };		// entries hidden under a collapsed call
			bool active { false };	// a call that hasn't returned yet
			bool open { false };	// a call with a return (or still running): it can be collapsed
		};

		private:
			static bool is_call(const Debugger::tCallInfo& call);
			static u64 key_of(const Debugger::tCallInfo& call);
			void rebuild(void);
			void go_to(const Debugger::tCallInfo& call);

		private:
			Debugger& m_debugger;
			AddressCallback m_onShowInDisassembly { nullptr };
			stdvec<Row> m_rows;
			std::set<u64> m_collapsed;
			bool m_dirty { true };
			size_t m_seenSize { 0 };
			size_t m_seenVisible { 0 };
			u64 m_seenFront { 0 };
	};




	// ============================================================================
	// BreakpointView — the breakpoints (yours and the program's debug_break
	// instructions) and the watches. The first column enables / disables a
	// breakpoint (removes a watch); double-click shows it in the disassembly (in
	// memory, for a watch); right-click for more.
	// ============================================================================
	class BreakpointView : public DebugListView
	{
		public:
			BreakpointView(ogfx::gui::Window& window, Debugger& debugger);
			inline void setShowInMemoryCallback(AddressCallback callback) { m_onShowInMemory = std::move(callback); }
			inline void setShowInDisassemblyCallback(AddressCallback callback) { m_onShowInDisassembly = std::move(callback); }
			void refreshRows(void);

			i32 getRowCount(void) const override;
			void drawCell(ogfx::BasicRenderer2D& gfx, i32 row, i32 column, const Rectangle& bounds) override;
			Color getRowColor(i32 row) const override;
			void onRowClicked(i32 row, i32 column, ogfx::MouseEventData::eButton button, bool doubleClick) override;
			String getEmptyText(void) const override;

		private: struct Row
		{
			u16 address { 0 };
			bool watch { false };
			u16 size { 1 };
			String label { "" };
		};

		private:
			Debugger& m_debugger;
			AddressCallback m_onShowInMemory { nullptr };
			AddressCallback m_onShowInDisassembly { nullptr };
			stdvec<Row> m_rows;
	};
}
