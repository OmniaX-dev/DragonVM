#pragma once

#include <ogfx/gui/Window.hpp>
#include <ogfx/gui/Widgets.hpp>
#include "../hardware/VirtualDisplay.hpp"
#include "DisassemblyView.hpp"
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

	// ============================================================================
	// DebuggerWindow — the graphical debugger.
	//
	//   +------------------------------+---------------------------------------+
	//   | program  [v]  [x] follow     | VirtualDisplay        | Breakpoints   |
	//   | Disassembly                  |                       | & watches     |
	//   |                              +---------------------------------------+
	//   |                              | Registers (diff with the last cycle)  |
	//   +------------------------------+---------------------------------------+
	//   | Console (log + command line) | Memory | Stack | Calls | Tracker | ...|
	//   +------------------------------+---------------------------------------+
	//   status: state, cycle, speed, history                 [timeline slider]
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
			// full = also the views that are slow to print (the visible tab, the breakpoints).
			void refresh_views(bool full);
			void refresh_tab(void);
			void refresh_status(void);
			void show_view(Debugger::eView view, i64 argument);
			void show_programs_menu(void);
			ogfx::gui::Label& add_title(ogfx::gui::Label& label, const String& text, const Rectangle& fractions, const ogfx::gui::Offsets& insets);

		private:
			Debugger& m_debugger;
			ostd::Stylesheet m_theme;

			// Left column
			ogfx::gui::Label m_programLabel { *this };
			ogfx::gui::Button m_programButton { *this };
			ogfx::gui::CheckBox m_followCheck { *this };
			DisassemblyView m_disassembly { *this, m_debugger };
			ogfx::gui::Label m_consoleTitle { *this };
			ogfx::gui::RichTextView m_consoleView { *this };
			ogfx::gui::RichTextOutputHandler m_consoleOut;
			CommandInput m_commandInput { *this };

			// Right column
			ogfx::gui::Label m_displayTitle { *this };
			hw::VirtualDisplay m_display { *this };
			ogfx::gui::Label m_breakpointsTitle { *this };
			ogfx::gui::RichTextView m_breakpointsView { *this };
			ogfx::gui::Label m_registersTitle { *this };
			ogfx::gui::RichTextView m_registersView { *this };
			ogfx::gui::TabPanel m_tabs { *this };
			ogfx::gui::HexEditor m_memory { *this };
			ogfx::gui::RichTextView m_stackView { *this };
			ogfx::gui::RichTextView m_callsView { *this };
			ogfx::gui::RichTextView m_trackerView { *this };
			ogfx::gui::RichTextView m_historyView { *this };
			ogfx::gui::Panel* m_memoryTab { nullptr };
			ogfx::gui::Panel* m_stackTab { nullptr };
			ogfx::gui::Panel* m_callsTab { nullptr };
			ogfx::gui::Panel* m_trackerTab { nullptr };
			ogfx::gui::Panel* m_historyTab { nullptr };
			ogfx::gui::Panel* m_shownTab { nullptr };
			i32 m_stackHighlight { -1 };
			u16 m_stackRows { 16 };

			// Bars
			std::deque<ogfx::gui::Button> m_toolButtons;
			ogfx::gui::CheckBox m_maxSpeedCheck { *this };
			ogfx::gui::CheckBox m_recordCheck { *this };
			ogfx::gui::Label m_stateLabel { *this };
			ogfx::gui::Label m_statusLabel { *this };
			ogfx::gui::Slider m_timeline { *this, false, 0.0f, 1.0f, 0.000001f };
			bool m_updatingTimeline { false };

			std::deque<String> m_pendingCommands;
			bool m_started { false };
			bool m_viewsDirty { true };
			f64 m_refreshTimer { 0.0 };
	};
}
