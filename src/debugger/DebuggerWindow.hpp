#pragma once

#include <ogfx/gui/Window.hpp>
#include <ogfx/gui/Widgets.hpp>
#include "../hardware/VirtualDisplay.hpp"
#include "DisassemblyView.hpp"
#include "DebuggerWidgets.hpp"
#include "Debugger.hpp"

namespace dragon
{
	using ostd::Color;
	using ostd::Vec2;
	using ostd::Rectangle;

	// The console's command line: a TextEdit with a command history (Up / Down).
	class CommandInput : public ogfx::gui::TextEdit
	{
		public: using CommandCallback = std::function<void(const String& command)>;
		public:
			inline CommandInput(ogfx::gui::Window& window) : TextEdit(window) { setTypeName("dragon::CommandInput"); }
			inline void setCommandCallback(CommandCallback callback) { m_onCommand = std::move(callback); }
			void onKeyPressed(const ogfx::gui::Event& event) override;

		private:
			CommandCallback m_onCommand { nullptr };
			stdvec<String> m_history;
			i32 m_historyIndex { -1 };
	};

	// A plain widget holding others (the console: its log and its command line).
	class WidgetGroup : public ogfx::gui::Widget
	{
		public:
			inline WidgetGroup(ogfx::gui::Window& window) : Widget({ 0, 0, 0, 0 }, window) { setTypeName("dragon::WidgetGroup"); setStylesheetCategoryName("dragonWidgetGroup"); disableFocus(); validate(); }
	};

	// A thin vertical line between groups of tool bar buttons.
	class ToolbarSeparator : public ogfx::gui::Widget
	{
		public:
			inline ToolbarSeparator(ogfx::gui::Window& window) : Widget({ 0, 0, 0, 0 }, window) { setTypeName("dragon::ToolbarSeparator"); setStylesheetCategoryName("dragonToolbarSeparator"); disableFocus(); disableChildren(); validate(); }
			inline void onDraw(ogfx::BasicRenderer2D& gfx) override
			{
				const Rectangle bounds = getGlobalBounds();
				gfx.fillRect({ bounds.x + std::floor(bounds.w * 0.5f), bounds.y, 1, bounds.h }, Color(90, 110, 90));
			}
	};

	// ============================================================================
	// DebuggerWindow — the graphical debugger.
	//
	//   +------------------------------+---------------------------------------+
	//   | program  [v]  [x] follow     | Registers | Breakpoints | Memory |    |
	//   | Disassembly                  | Stack | Call trace | Tracker | History|
	//   |                              |                                       |
	//   |                              |                                       |
	//   +------------------------------+---------------------------------------+
	//   | Console (log + command line) | Screen (VirtualDisplay)               |
	//   +------------------------------+---------------------------------------+
	//   status: state, cycle, speed, history                 [timeline slider]
	//
	// The screen and the console can be detached into SubWindows (their "Detach"
	// buttons) and attached back ("Attach" in the SubWindow's title bar, or closing
	// it); the views next to them take their space meanwhile.
	//
	// The toolbar (and F5 / F6 / F9 / F10 / F11) drives the Debugger; the views are
	// refreshed when it stops, and a few times per second while it runs. The
	// window is driven by its own loop (__main_loop()): it runs the machine in
	// slices between the frames, and waits for events while it's paused.
	// ============================================================================
	class DebuggerWindow : public ogfx::gui::Window
	{
		public:
			inline DebuggerWindow(Debugger& debugger) : m_debugger(debugger) {  }
			inline hw::VirtualDisplay& getDisplay(void) { return m_display; }
			// Once the debugger loaded the machine: sets the views up.
			void start(void);

			void onInitialize(void) override;
			void onUpdate(f64 delta) override;
			void onSDLEvent(SDL_Event& event) override;

		protected:
			void __main_loop(void) override;

		private:
			void build_layout(void);
			void build_toolbar(void);
			void build_tabs(void);
			void build_memory(void);
			void build_subwindows(void);
			// Places the widgets for the current detach state.
			void apply_layout(void);
			void set_screen_detached(bool detached);
			void set_console_detached(bool detached);
			// full = also the views that are slow to print (the visible tab).
			void refresh_views(bool full);
			void refresh_tab(void);
			void refresh_status(void);
			void refresh_stack(void);
			void sync_watches(void);
			void show_view(Debugger::eView view, i64 argument);
			void show_in_memory(u16 address);
			void show_in_disassembly(u16 address);
			void show_programs_menu(void);
			// The window's own commands (detach / attach), the others go to the debugger.
			void run_command(const String& command);
			ogfx::gui::Label& add_title(ogfx::gui::Label& label, const String& text);

		private:
			Debugger& m_debugger;
			ostd::Stylesheet m_theme;

			// Left column
			ogfx::gui::Label m_programLabel { *this };
			ogfx::gui::Button m_programButton { *this };
			ogfx::gui::CheckBox m_followCheck { *this };
			DisassemblyView m_disassembly { *this, m_debugger };
			ogfx::gui::Label m_consoleTitle { *this };
			ogfx::gui::Button m_consoleDetachButton { *this };
			WidgetGroup m_console { *this };
			ogfx::gui::RichTextView m_consoleView { *this };
			ogfx::gui::RichTextOutputHandler m_consoleOut;
			CommandInput m_commandInput { *this };

			// Right column
			ogfx::gui::TabPanel m_tabs { *this };
			RegisterView m_registers { *this, m_debugger };
			BreakpointView m_breakpoints { *this, m_debugger };
			ogfx::gui::HexEditor m_memory { *this };
			WidgetGroup m_stack { *this };
			ogfx::gui::RichTextView m_stackFramesView { *this };
			ogfx::gui::HexEditor m_stackMemory { *this };
			CallTraceView m_callTrace { *this, m_debugger };
			TrackerView m_tracker { *this, m_debugger };
			ogfx::gui::RichTextView m_historyView { *this };
			ogfx::gui::Panel* m_registersTab { nullptr };
			ogfx::gui::Panel* m_breakpointsTab { nullptr };
			ogfx::gui::Panel* m_memoryTab { nullptr };
			ogfx::gui::Panel* m_stackTab { nullptr };
			ogfx::gui::Panel* m_callsTab { nullptr };
			ogfx::gui::Panel* m_trackerTab { nullptr };
			ogfx::gui::Panel* m_historyTab { nullptr };
			ogfx::gui::Label m_displayTitle { *this };
			ogfx::gui::Button m_screenDetachButton { *this };
			hw::VirtualDisplay m_display { *this };
			i32 m_stackHighlight { -1 };
			stdvec<i32> m_stackFrameHighlights;
			i32 m_lastStackSP { -1 };
			i32 m_lastStackRows { -1 };
			u64 m_seenTrackedVersion { cast<u64>(-1) };

			// Detached views
			ogfx::gui::SubWindow m_screenWindow { *this, "Screen" };
			ogfx::gui::SubWindow m_consoleWindow { *this, "Console" };
			ogfx::gui::SubWindow m_diskManagerWindow { *this, "Disk Manager" };
			bool m_screenDetached { false };
			bool m_consoleDetached { false };

			// Bars
			std::deque<ogfx::gui::Button> m_toolButtons;
			std::deque<ToolbarSeparator> m_toolSeparators;
			ogfx::gui::CheckBox m_maxSpeedCheck { *this };
			ogfx::gui::CheckBox m_recordCheck { *this };
			ogfx::gui::Label m_stateLabel { *this };
			ogfx::gui::Label m_statusLabel { *this };
			ogfx::gui::Slider m_timeline { *this, false, 0.0f, 1.0f, 0.000001f };
			bool m_updatingTimeline { false };

			std::deque<String> m_pendingCommands;
			bool m_started { false };
			bool m_viewsDirty { true };
			u64 m_seenConsoleVersion { 0 };
			f64 m_refreshTimer { 0.0 };

		public:
			// Column / rows, as fractions of the window.
			inline static constexpr f32 Split { 0.47f };
			inline static constexpr f32 ConsoleTop { 0.66f };
			inline static constexpr f32 ScreenTop { 0.62f };
			// The icon buttons (tool bar, detach).
			inline static constexpr f32 ButtonSize { 28.0f };
			inline static constexpr f32 ButtonSpacing { 3.0f };
			inline static constexpr f32 SeparatorWidth { 5.0f };
	};
}
