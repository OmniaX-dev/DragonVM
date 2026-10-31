#include "DebuggerWindow.hpp"
#include "../runtime/DragonRuntime.hpp"

namespace dragon
{
	using namespace ogfx::gui;

	static void place(Widget& widget, f32 anchorLeft, f32 anchorTop, f32 anchorRight, f32 anchorBottom, f32 left, f32 top, f32 right, f32 bottom)
	{
		widget.setAnchors(anchorLeft, anchorTop, anchorRight, anchorBottom);
		widget.setOffsets(left, top, right, bottom);
	}




	//CommandInput
	void CommandInput::onKeyPressed(const Event& event)
	{
		const i32 key = event.keyboard->keyCode;
		if (key == ogfx::KeyCode::Return || key == ogfx::KeyCode::KpEnter)
		{
			const String command = getText();
			if (command.new_trim() != "" && (m_history.empty() || m_history.back() != command))
				m_history.push_back(command);
			m_historyIndex = -1;
			setText("");
			if (m_onCommand)
				m_onCommand(command);
			event.handle();
			return;
		}
		if ((key == ogfx::KeyCode::Up || key == ogfx::KeyCode::Down) && !m_history.empty())
		{
			if (key == ogfx::KeyCode::Up)
				m_historyIndex = (m_historyIndex < 0 ? cast<i32>(m_history.size()) - 1 : std::max(0, m_historyIndex - 1));
			else if (m_historyIndex >= 0)
				m_historyIndex = (m_historyIndex + 1 < cast<i32>(m_history.size()) ? m_historyIndex + 1 : -1);
			setText(m_historyIndex >= 0 ? m_history[m_historyIndex] : String(""));
			event.handle();
			return;
		}
		TextEdit::onKeyPressed(event);
	}




	//DebuggerWindow
	void DebuggerWindow::onInitialize(void)
	{
		m_theme.loadFromFile("res/debugger_theme.oss", true, getDefaultStylesheetVariableList());
		m_theme.loadFromFile("res/debugger_theme_detail.oss", false, getDefaultStylesheetVariableList());
		// m_theme.debugPrint();
		setTheme(m_theme);
		setClearColor({ 22, 24, 22 });
		build_toolbar();
		build_layout();
		build_tabs();
		build_subwindows();
		apply_layout();

		m_consoleOut.attach(m_consoleView);
		m_debugger.setConsole(m_consoleOut);
		m_debugger.setStateChangedCallback([this](void) { m_viewsDirty = true; });
		m_debugger.setViewCallback([this](Debugger::eView view, i64 argument) { show_view(view, argument); });
		m_debugger.setQuitCallback([this](void) { close(); });

		m_rootWidget.reloadTheme(true);
		// After the theme pass.
		m_followCheck.setChecked(true);
		m_memory.enableFollowHighlight(true);
		m_stackMemory.enableFollowHighlight(true);
	}

	ogfx::gui::Label& DebuggerWindow::add_title(Label& label, const String& text)
	{
		addWidget(label);
		label.setText(text);
		label.setFontSize(15);
		return label;
	}

	void DebuggerWindow::build_layout(void)
	{
		// ===== Disassembly =====
		addWidget(m_programLabel);
		m_programLabel.setFontSize(15);
		// Centered in its bounds (auto size would add the theme's padding and overflow them).
		m_programLabel.setSizeMode(eSizeMode::Manual);
		m_programLabel.addThemeID("disassembly_title");
		// addWidget(m_followCheck);
		m_followCheck.setText("Follow IP");
		m_followCheck.setChecked(true);
		m_followCheck.setStateChangedCallback([this](CheckBox&, bool checked) {
			m_disassembly.enableFollow(checked);
			m_viewsDirty = true;
		});
		addWidget(m_disassembly);
		m_disassembly.setFontSize(15);
		m_disassembly.setShowInMemoryCallback([this](u16 address) { show_in_memory(address); });
		m_disassembly.setBreakpointsChangedCallback([this](void) { m_viewsDirty = true; });

		// ===== Console =====
		// add_title(m_consoleTitle, "Console  (help: list of commands, Enter: step)");
		m_consoleDetachButton.setText("Detach");
		m_consoleDetachButton.addThemeID("toolbar_btn");
		m_consoleDetachButton.addThemeID("console_detach_btn");
		m_consoleDetachButton.setFontSize(14);
		m_consoleDetachButton.setTooltipText("Move the console to its own window");
		m_consoleDetachButton.enableTooltip();
		m_consoleDetachButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { set_console_detached(true); });
		addWidget(m_console);
		m_console.addWidget(m_consoleView);
		place(m_consoleView, 0, 0, 1, 1, 0, 0, 0, -40);
		m_consoleView.setFontSize(14);
		m_consoleView.enableFollowTail();
		m_consoleView.getBuffer().setMaxLines(5000);
		m_console.addWidget(m_commandInput);
		// The detach button is beside the command line (it goes with the console).
		m_console.addWidget(m_consoleDetachButton);
		m_commandInput.setCommandCallback([this](const String& command) { run_command(command); });

		// ===== Tabs =====
		addWidget(m_tabs);

		// ===== Screen =====
		// add_title(m_displayTitle, "Screen  (click it to type into the machine)");
		m_screenDetachButton.setText("Detach");
		m_screenDetachButton.addThemeID("toolbar_btn");
		m_screenDetachButton.addThemeID("screen_detach_btn");
		m_screenDetachButton.setFontSize(14);
		m_screenDetachButton.setTooltipText("Move the screen to its own window");
		m_screenDetachButton.enableTooltip();
		m_screenDetachButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { set_screen_detached(true); });
		addWidget(m_display);
		m_display.enableFocus();
		m_display.enableFocusBorder();
		m_display.setFocusBorderColor({ 0, 117, 199 });
		// Over the screen's top right corner: added after it.
		addWidget(m_screenDetachButton);
	}

	void DebuggerWindow::apply_layout(void)
	{
		const f32 split = Split;
		place(m_programLabel, 0, 0, split, 0, 8, 0, -6, 26);
		// place(m_followCheck, split, 0, split, 0, -112, 6, -6, 32);

		// Left column: the disassembly takes the console's place when it's detached.
		m_consoleTitle.setVisible(!m_consoleDetached);
		m_consoleDetachButton.setVisible(!m_consoleDetached);
		if (!m_consoleDetached)
		{
			place(m_disassembly, 0, 0, split, ConsoleTop, 6, 28, -3, -3);
			place(m_console, 0, ConsoleTop, split, 1, 6, 3, -3, -6);
			place(m_commandInput, 0, 1, 1, 1, 0, -ButtonSize - 4, -ButtonSize - 7, 0);
			place(m_consoleDetachButton, 1, 1, 1, 1, -ButtonSize - 2, -ButtonSize - 2, -2, -2);
		}
		else
		{
			place(m_disassembly, 0, 0, split, 1, 6, 28, -3, -6);
			place(m_commandInput, 0, 1, 1, 1, 0, -ButtonSize - 4, 0, 0);
		}

		// Right column: the tabs take the screen's place when it's detached.
		m_displayTitle.setVisible(!m_screenDetached);
		m_screenDetachButton.setVisible(!m_screenDetached);
		if (!m_screenDetached)
		{
			place(m_tabs, split, 0, 1, ScreenTop, 3, 3, -6, -3);
			place(m_display, split, ScreenTop, 1, 1, 3, 3, -6, -6);
			place(m_screenDetachButton, 1, ScreenTop, 1, ScreenTop, -ButtonSize - 10, 7, -10, ButtonSize + 7);
		}
		else
			place(m_tabs, split, 0, 1, 1, 3, 3, -6, -6);
	}

	void DebuggerWindow::build_subwindows(void)
	{
		auto l_setup = [this](SubWindow& window, const Rectangle& bounds, const String& tooltip, std::function<void(void)> attach) {
			window.setBounds(bounds);
			window.setButtons(SubWindow::Buttons::Close | SubWindow::Buttons::Minimize | SubWindow::Buttons::Maximize | SubWindow::Buttons::AlwaysOnTop);
			window.addTitlebarButton("Attach", [attach](SubWindow&) { attach(); }, tooltip);
			// Closing it puts the view back too.
			window.setCloseCallback([attach](SubWindow&) { attach(); return true; });
		};
		l_setup(m_screenWindow, { 260, 140, 940, 640 }, "Put the screen back in the debugger window", [this](void) { set_screen_detached(false); });
		l_setup(m_consoleWindow, { 160, 220, 1000, 560 }, "Put the console back in the debugger window", [this](void) { set_console_detached(false); });
		// Empty for now.
		m_diskManagerWindow.setBounds({ 220, 160, 900, 600 });
		m_diskManagerWindow.setButtons(SubWindow::Buttons::Close | SubWindow::Buttons::Minimize | SubWindow::Buttons::Maximize | SubWindow::Buttons::AlwaysOnTop);
	}

	void DebuggerWindow::set_screen_detached(bool detached)
	{
		if (detached == m_screenDetached)
			return;
		m_screenDetached = detached;
		if (detached)
		{
			removeWidget(m_display);
			m_screenWindow.addWidget(m_display);
			place(m_display, 0, 0, 1, 1, 4, 4, -4, -4);
			m_screenWindow.open();
		}
		else
		{
			m_screenWindow.removeWidget(m_display);
			addWidget(m_display);
			// Back over it.
			removeWidget(m_screenDetachButton);
			addWidget(m_screenDetachButton);
			// Not close(): its callback is what may have called this.
			m_screenWindow.setVisible(false);
		}
		apply_layout();
	}

	void DebuggerWindow::set_console_detached(bool detached)
	{
		if (detached == m_consoleDetached)
			return;
		m_consoleDetached = detached;
		if (detached)
		{
			removeWidget(m_console);
			m_consoleWindow.addWidget(m_console);
			place(m_console, 0, 0, 1, 1, 4, 4, -4, -4);
			m_consoleWindow.open();
		}
		else
		{
			m_consoleWindow.removeWidget(m_console);
			addWidget(m_console);
			m_consoleWindow.setVisible(false);
		}
		apply_layout();
		m_consoleView.scrollToBottom();
	}

	void DebuggerWindow::build_tabs(void)
	{
		auto l_tab = [this](const String& title, Widget& content) -> Panel& {
			Panel& tab = m_tabs.addTab(title);
			tab.addWidget(content);
			content.setAnchorPreset(eAnchorPreset::FullRect, ePresetResizeMode::KeepSize, 2);
			return tab;
		};
		m_registersTab = &l_tab("Registers", m_registers);
		m_breakpointsTab = &l_tab("Breakpoints", m_breakpoints);
		m_memoryTab = &l_tab("Memory", m_memory);
		m_stackTab = &l_tab("Stack", m_stack);
		m_callsTab = &l_tab("Call trace", m_callTrace);
		m_trackerTab = &l_tab("Tracker", m_tracker);
		m_historyTab = &l_tab("History", m_historyView);

		m_registers.setShowInMemoryCallback([this](u16 address) { show_in_memory(address); });
		m_registers.setShowInDisassemblyCallback([this](u16 address) { show_in_disassembly(address); });
		m_breakpoints.setShowInMemoryCallback([this](u16 address) { show_in_memory(address); });
		m_breakpoints.setShowInDisassemblyCallback([this](u16 address) { show_in_disassembly(address); });
		m_callTrace.setShowInDisassemblyCallback([this](u16 address) { show_in_disassembly(address); });
		m_tracker.setShowInMemoryCallback([this](u16 address) { show_in_memory(address); });

		// Stack: the frames (from the saved frame pointers) over the stack's memory.
		m_stack.addWidget(m_stackFramesView);
		place(m_stackFramesView, 0, 0, 1, 0.36f, 0, 0, 0, -3);
		m_stackFramesView.setFontSize(13);
		m_stackFramesView.enableHoverHighlight(false);
		m_stack.addWidget(m_stackMemory);
		place(m_stackMemory, 0, 0.36f, 1, 1, 0, 3, 0, 0);
		m_historyView.setFontSize(14);

		m_tabs.setCurrentTab(*m_memoryTab);
		m_tabs.setTabChangedCallback([this](TabPanel&, Panel&) { refresh_tab(); });
	}

	void DebuggerWindow::build_toolbar(void)
	{
		auto& tb = getToolBar();
		tb.setHeight(36);
		// Icon buttons placed one after the other (their text is the tooltip).
		f32 x = 6;
		auto l_place = [&](Button& button, const String& text, const String& theme_id) {
			button.setText(text);
			button.setFontSize(15);
			button.addThemeID("toolbar_btn");
			button.addThemeID(theme_id);
			button.setTooltipText(text);
			button.enableTooltip();
			place(button, 0, 0.5f, 0, 0.5f, x, -ButtonSize / 2, x + ButtonSize, ButtonSize / 2);
			x += ButtonSize + ButtonSpacing;
		};
		auto l_button = [&](const String& text, const String& theme_id, std::function<void(void)> action) {
			Button& button = m_toolButtons.emplace_back(*this);
			tb.addWidget(button);
			l_place(button, text, theme_id);
			button.setCallback(Widget::eCallback::ActionPerformed, [action](const Event&) { action(); });
		};
		auto l_separator = [&](void) {
			ToolbarSeparator& separator = m_toolSeparators.emplace_back(*this);
			tb.addWidget(separator);
			place(separator, 0, 0.5f, 0, 0.5f, x + 2, -ButtonSize / 2 + 4, x + 2 + SeparatorWidth, ButtonSize / 2 - 4);
			x += SeparatorWidth + 4 + ButtonSpacing;
		};
		tb.addWidget(m_programButton);
		l_place(m_programButton, "Programs (the loaded disassemblies)", "code_program_btn");
		m_programButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { show_programs_menu(); });
		l_separator();
		l_button("Run (F5)", "toolbar_run", [this](void) { m_debugger.resume(); });
		l_button("Pause (F6)", "toolbar_pause", [this](void) { m_debugger.pause(); });
		l_button("Step (F10)", "toolbar_step", [this](void) { m_debugger.stepInto(1); });
		l_button("Over (F11)", "toolbar_over", [this](void) { m_debugger.stepOver(); });
		l_button("Out (Sh+F11)", "toolbar_out", [this](void) { m_debugger.stepOut(); });
		l_button("Back (F9)", "toolbar_back", [this](void) { m_debugger.stepBack(1); });
		l_button("Reverse run (Sh+F5)", "toolbar_reverse", [this](void) { m_debugger.reverseContinue(); });
		l_button("Rewind", "toolbar_rewind", [this](void) { m_debugger.seek(m_debugger.history().getFirstPosition()); });
		l_separator();
		l_button("Disk Manager", "toolbar_disks", [this](void) { m_diskManagerWindow.open(); });
		tb.addWidget(m_recordCheck);
		m_recordCheck.setText("Record history");
		m_recordCheck.setAnchorPreset(eAnchorPreset::CenterRight, ePresetResizeMode::KeepSize, 160);
		m_recordCheck.setStateChangedCallback([this](CheckBox&, bool checked) { m_debugger.enableHistory(checked); });
		tb.addWidget(m_maxSpeedCheck);
		m_maxSpeedCheck.setText("Max speed");
		m_maxSpeedCheck.setAnchorPreset(eAnchorPreset::CenterRight, ePresetResizeMode::KeepSize, 30);
		m_maxSpeedCheck.setStateChangedCallback([this](CheckBox&, bool checked) { m_debugger.setMaxSpeed(checked); m_viewsDirty = true; });
		tb.addWidget(m_stateLabel);
		m_stateLabel.setFontSize(16);
		m_stateLabel.setAnchorPreset(eAnchorPreset::CenterRight, ePresetResizeMode::KeepSize, 330);
		showToolBar(true);

		auto& sb = getStatusBar();
		sb.setHeight(30);
		sb.addWidget(m_statusLabel);
		m_statusLabel.setFontSize(15);
		m_statusLabel.setAnchorPreset(eAnchorPreset::CenterLeft, ePresetResizeMode::KeepSize, 8);
		// sb.addWidget(m_timeline);
		// place(m_timeline, 0.6f, 0.5f, 1.0f, 0.5f, 0, -9, -12, 9);
		// // A fine step (a position per history entry, more or less): no ticks.
		// m_timeline.enableDrawTicks(false);
		// m_timeline.setTooltipText("History timeline: drag to move through the recorded cycles");
		// m_timeline.enableTooltip();
		// m_timeline.setValueChangedCallback([this](f32, f32 value) {
		//     if (m_updatingTimeline || !m_debugger.isHistoryEnabled())
		//         return;
		//     const History& history = m_debugger.history();
		//     const u64 span = history.getLastPosition() - history.getFirstPosition();
		//     m_debugger.seek(history.getFirstPosition() + cast<u64>(std::llround(cast<f64>(value) * cast<f64>(span))));
		// });
		showStatusBar(true);
	}

	void DebuggerWindow::build_memory(void)
	{
		// Memory: the whole address space, the regions of the memory map highlighted.
		const auto& image = m_debugger.getBusImage();
		const std::span<const i8> bus(reinterpret_cast<const i8*>(image.data()), image.size());
		const u16 ip = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::IP));
		const u16 sp = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::SP));
		auto l_write = [this](ogfx::gui::StreamIndex index, u64 value, u8 size) {
			return m_debugger.writeMemory(cast<u16>(index), value, size);
		};
		m_memory.setData(bus, ip & 0xFFF0, 16, 0, ip, 1, "Bus");
		m_memory.setBigEndian(true);
		m_memory.setUnitSize(2);
		m_memory.enableFollowHighlight(true);
		const Color regionColors[] = { { 40, 80, 160, 40 }, { 160, 120, 20, 40 }, { 150, 60, 170, 40 }, { 60, 160, 60, 40 }, { 160, 60, 60, 40 } };
		i32 colorIndex = 0;
		for (const auto& region : DragonRuntime::memMap.getRegions())
		{
			if (region.name == "RAM")
				continue;
			m_memory.addHighlight(region.startAddress, cast<u32>(region.endAddress) - region.startAddress + 1, regionColors[colorIndex++ % 5], region.name);
		}
		m_stackHighlight = m_memory.addHighlight(sp, std::max<u32>(1, 0x10000 - sp), Color(40, 160, 100, 50), "stack");
		m_memory.setWriteCallback(l_write);

		// The instructions' opcodes as aliases (with their operand bytes), colored by kind.
		auto l_color = [](const String& opName) -> Color {
			const String name = opName.new_toLower();
			if (name == "noop")
				return { 95, 95, 105 };
			if (name.startsWith("debug") || name.startsWith("bios") || name == "halt")
				return { 220, 130, 230 };
			if (name.startsWith("mov"))
				return { 100, 160, 255 };
			if (name.startsWith("push") || name.startsWith("pop") || name.startsWith("arg"))
				return { 90, 210, 220 };
			if (name.startsWith("call") || name.startsWith("ret") || name == "int")
				return { 240, 100, 100 };
			if (name.startsWith("jmp"))
				return { 240, 170, 60 };
			return { 130, 220, 120 };
		};
		auto l_pattern = [](u8 first, i32 second, u8 size) {
			String pattern = String::getHexStr(first, false, 1);
			u8 bytes = 1;
			if (second >= 0)
			{
				pattern.add(" ").add(String::getHexStr(cast<u8>(second), false, 1));
				bytes++;
			}
			for (; bytes < size; bytes++)
				pattern.add(" ??");
			return pattern;
		};
		for (i32 op = 0; op < 256; op++)
		{
			if (op >= data::OpCodes::Ext01 && op <= data::OpCodes::Ext16)
			{
				data::CPUExtension* ext = DragonRuntime::cpu.getExtension(cast<u8>(op));
				if (ext == nullptr)
					continue;
				for (i32 inst = 0; inst < 256; inst++)
				{
					const u8 size = ext->getInstructionSIze(cast<u8>(inst));
					const String name = ext->getOpCodeString(cast<u8>(inst));
					if (size == 0 || name == "" || name.contains("UNKNOWN"))
						continue;
					m_memory.addAlias(l_pattern(cast<u8>(op), inst, cast<u8>(size + 1)), name, l_color(name));
				}
				continue;
			}
			const String name = data::OpCodes::getBaseOpCodeString(cast<u8>(op));
			const u8 size = data::OpCodes::getBaseInstructionSize(cast<u8>(op));
			if (size == 0 || name == "UNKNOWN_INST")
				continue;
			m_memory.addAlias(l_pattern(cast<u8>(op), -1, size), name, l_color(name));
		}

		// Stack: the same bus, the frames highlighted by refresh_stack().
		m_stackMemory.setData(bus, std::min<u16>(sp & 0xFFF0, 0xFF80), 16, 0, sp, 2, "Stack");
		m_stackMemory.setBigEndian(true);
		m_stackMemory.setUnitSize(2);
		m_stackMemory.enableFollowHighlight(true);
		m_stackMemory.setWriteCallback(l_write);
	}

	void DebuggerWindow::start(void)
	{
		auto& config = DragonRuntime::machine_config;
		m_display.initialize("font.bmp");
		m_display.enableCRTEffect(config.crt_effect);
		m_display.enableBloomEffect(config.bloom_effect);
		m_disassembly.build();
		m_recordCheck.setChecked(m_debugger.isHistoryEnabled());
		m_maxSpeedCheck.setChecked(m_debugger.isMaxSpeed());
		build_memory();

		m_consoleOut.fg(ostd::ConsoleColors::Green).p("DragonVM Debugger ").p(VERSION_STR).reset().nl();
		m_consoleOut.fg(ostd::ConsoleColors::BrightGray).p("Machine: ").p(m_debugger.data().args.machine_config_path).p("  -  ");
		m_consoleOut.p(cast<i32>(m_debugger.data().code.size())).p(" instructions, ").p(cast<i32>(m_debugger.data().labels.size())).p(" labels, ");
		m_consoleOut.p(cast<i32>(m_debugger.data().data.size())).p(" data symbols in ").p(cast<i32>(DisassemblyLoader::getTables().size())).p(" programs.").reset().nl();
		i32 debugBreaks = 0;
		for (const auto& [address, bp] : m_debugger.data().breakpoints)
			debugBreaks += (bp.debugBreak ? 1 : 0);
		if (debugBreaks > 0)
			m_consoleOut.fg(ostd::ConsoleColors::BrightGray).p(debugBreaks).p(" debug_break instructions are breakpoints (click them in the margin, or in the Breakpoints tab, to disable them).").reset().nl();
		if (DisassemblyLoader::getTables().empty())
			m_consoleOut.fg(ostd::ConsoleColors::Yellow).p("No disassembly found in '").p(m_debugger.data().disassemblyDirectory).p("/': assemble with -D to get it.").reset().nl();
		m_consoleOut.fg(ostd::ConsoleColors::BrightGray).p("Type 'help' for the commands. F5 run, F6 pause, F10 step, F11 step over, F9 step back.").reset().nl();
		for (auto& command : String(m_debugger.data().args.exec_commands).tokenize(";").getRawData())
			m_pendingCommands.push_back(command);
		m_started = true;
		m_viewsDirty = true;
		if (m_debugger.data().args.auto_start_debug)
			m_debugger.resume();
	}

	void DebuggerWindow::__main_loop(void)
	{
		while (isRunning())
		{
			const bool running = m_debugger.isRunning();
			// Paused: nothing to do between the events (the frames still come, at the refresh rate).
			enableBlockingEvents(!running);
			handle_events();
			m_debugger.update();
			m_fixedUpdateTimer.update();
			m_mainLoopTimer.update();
			if (running && !m_debugger.isMaxSpeed() && m_debugger.getMode() == Debugger::eMode::Running)
				SDL_Delay(1);
		}
	}

	void DebuggerWindow::onUpdate(f64 delta)
	{
		if (!m_started)
			return;
		// The machine gets the keys only while it runs and its screen has the focus.
		DragonRuntime::vKeyboard.enableInput(m_debugger.isRunning() && m_display.isFocused());
		m_display.setVisible(!m_debugger.data().args.hide_virtual_display);
		m_refreshTimer += delta;
		if (!m_pendingCommands.empty() && !m_debugger.isRunning())
		{
			const String command = m_pendingCommands.front();
			m_pendingCommands.pop_front();
			run_command(command);
		}
		// The console follows its output.
		if (m_consoleView.getBuffer().getVersion() != m_seenConsoleVersion)
		{
			m_seenConsoleVersion = m_consoleView.getBuffer().getVersion();
			m_consoleView.scrollToBottom();
		}
		if (m_viewsDirty)
		{
			m_viewsDirty = false;
			m_refreshTimer = 0;
			refresh_views(true);
		}
		else if (m_debugger.isRunning() && m_refreshTimer >= 0.2)
		{
			m_refreshTimer = 0;
			refresh_views(false);
		}
		// The stack view is placed once it's laid out (and when it's resized).
		if (m_tabs.getCurrentTab() == m_stackTab && std::max(1, m_stackMemory.getVisibleRows()) != m_lastStackRows)
			refresh_stack();
		refresh_status();
	}

	void DebuggerWindow::onSDLEvent(SDL_Event& event)
	{
		if (event.type != SDL_EVENT_KEY_DOWN)
			return;
		const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
		switch (event.key.key)
		{
			case SDLK_F5:
				if (shift)
					m_debugger.reverseContinue();
				else
					m_debugger.resume();
				break;
			case SDLK_F6: m_debugger.pause(); break;
			case SDLK_F9: m_debugger.stepBack(1); break;
			case SDLK_F10: m_debugger.stepInto(1); break;
			case SDLK_F11:
				if (shift)
					m_debugger.stepOut();
				else
					m_debugger.stepOver();
				break;
			default: break;
		}
	}

	void DebuggerWindow::sync_watches(void)
	{
		// The tracked data are the memory view's watches.
		if (m_seenTrackedVersion == m_debugger.data().trackedVersion)
			return;
		m_seenTrackedVersion = m_debugger.data().trackedVersion;
		static const Color colors[] = { { 0, 220, 180 }, { 90, 170, 255 }, { 240, 200, 60 }, { 120, 230, 120 }, { 230, 130, 230 }, { 255, 150, 80 } };
		m_memory.clearWatches();
		i32 index = 0;
		for (const auto& item : m_debugger.data().tracked)
		{
			const Color color = item.breakOnChange ? Color(240, 80, 80) : colors[index++ % 6];
			m_memory.addWatch(item.address, cast<u8>(std::min<u16>(item.size, 255)), item.breakOnChange ? String("(w) ").add(item.label) : item.label, color);
		}
	}

	void DebuggerWindow::refresh_views(bool full)
	{
		const bool running = m_debugger.isRunning();
		const u16 ip = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::IP));
		m_disassembly.setCurrentAddress(ip, true);
		m_programLabel.setText(m_disassembly.getProgramName(m_disassembly.getCurrentProgram()));

		// Memory
		if (running)
			m_debugger.refreshBusImage();
		const auto& image = m_debugger.getBusImage();
		m_memory.updateData(std::span<const i8>(reinterpret_cast<const i8*>(image.data()), image.size()));
		u8 size = 1;
		Debugger::Utils::decodeInstruction(ip, &size);
		m_memory.setHighlight(ip, size);
		const u16 sp = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::SP));
		m_memory.updateHighlight(m_stackHighlight, sp, std::max<u32>(1, 0x10000 - sp));
		sync_watches();

		if (!full)
		{
			// The slow views only when shown.
			if (m_tabs.getCurrentTab() == m_stackTab)
				refresh_stack();
			return;
		}
		m_breakpoints.refreshRows();
		refresh_tab();
	}

	void DebuggerWindow::refresh_stack(void)
	{
		RichTextOutputHandler out(m_stackFramesView);
		out.clear();
		Debugger::Display::printStackFrames(out, m_debugger, std::max(60, m_stackFramesView.getVisibleColumns() - 1));
		const auto& image = m_debugger.getBusImage();
		m_stackMemory.updateData(std::span<const i8>(reinterpret_cast<const i8*>(image.data()), image.size()));
		const u16 sp = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::SP));
		m_stackMemory.setHighlight(sp, 2);
		const i32 rows = std::max(1, m_stackMemory.getVisibleRows());
		if (sp != m_lastStackSP || rows != m_lastStackRows)
		{
			// The top of the stack (the end of memory) at the bottom of the view, as long as SP fits.
			m_lastStackSP = sp;
			m_lastStackRows = rows;
			m_stackMemory.setStart(cast<u64>(std::max<i64>(0, std::min<i64>(sp & 0xFFF0, 0x10000 - cast<i64>(rows) * 16))));
		}

		// Each frame's saved registers and arguments, named (hover them).
		for (i32 id : m_stackFrameHighlights)
			m_stackMemory.removeHighlight(id);
		m_stackFrameHighlights.clear();
		i32 depth = 0;
		for (const auto& frame : Debugger::Utils::getStackFrames())
		{
			String returnTo = Debugger::Utils::findNearestSymbol(m_debugger.data().labels, frame.savedIP);
			if (returnTo == "")
				returnTo = String::getHexStr(frame.savedIP, true, 2);
			const Color saved = (depth % 2 == 0 ? Color(60, 120, 220, 55) : Color(220, 150, 60, 55));
			m_stackFrameHighlights.push_back(m_stackMemory.addHighlight(cast<u16>(frame.fp + 2), 32,
				saved, String("#").add(depth).add(" saved frame (returns to ").add(returnTo).add(")")));
			if (frame.argCount > 0 && frame.argCount < 64)
				m_stackFrameHighlights.push_back(m_stackMemory.addHighlight(cast<u16>(frame.fp + 34), cast<u32>(frame.argCount) * 2,
					Color(170, 70, 190, 60), String("#").add(depth).add(" arguments (").add(frame.argCount).add(")")));
			depth++;
		}
	}

	void DebuggerWindow::refresh_tab(void)
	{
		if (!m_started)
			return;
		const Panel* tab = m_tabs.getCurrentTab();
		if (tab == m_stackTab)
			refresh_stack();
		else if (tab == m_callsTab)
			m_callTrace.refreshRows();
		else if (tab == m_breakpointsTab)
			m_breakpoints.refreshRows();
		else if (tab == m_historyTab)
		{
			RichTextOutputHandler out(m_historyView);
			out.clear();
			Debugger::Display::printHistory(out, m_debugger, std::max(10, m_historyView.getVisibleRows() - 6));
		}
	}

	void DebuggerWindow::refresh_status(void)
	{
		const auto& history = m_debugger.history();
		String state = "";
		switch (m_debugger.getMode())
		{
			case Debugger::eMode::Paused: state = (DragonRuntime::cpu.isHalted() ? "HALTED" : "PAUSED"); break;
			case Debugger::eMode::Running: state = "RUNNING"; break;
			case Debugger::eMode::StepOver: state = "STEPPING OVER"; break;
			case Debugger::eMode::StepOut: state = "STEPPING OUT"; break;
			case Debugger::eMode::RunTo: state = "RUNNING TO"; break;
			case Debugger::eMode::ReverseRunning: state = "RUNNING BACKWARDS"; break;
		}
		if (m_debugger.isHistoryEnabled() && !history.isAtEnd() && m_debugger.getMode() == Debugger::eMode::Paused)
			state.add("  (in the past)");
		m_stateLabel.setText(state);
		m_stateLabel.setTextColor(m_debugger.isRunning() ? Color(120, 230, 120) : Color(255, 210, 90));

		String status = "Cycle ";
		status.add(m_debugger.getCycle());
		status.add("   IP ").add(String::getHexStr(cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::IP)), true, 2));
		status.add("   depth ").add(m_debugger.getCallDepth());
		status.add("   speed ");
		if (m_debugger.isRunning())
			status.add(cast<u64>(m_debugger.getMeasuredSpeed())).add(" Hz");
		else
			status.add(m_debugger.isMaxSpeed() ? String("max") : String("").add(cast<u64>(m_debugger.getClockRate())).add(" Hz"));
		if (m_debugger.isHistoryEnabled())
		{
			status.add("   history ").add(history.getPosition() - history.getFirstPosition()).add(" / ").add(history.getLastPosition() - history.getFirstPosition());
			status.add(" (").add(cast<f64>(history.getMemoryUsage()) / (1024.0 * 1024.0), 1).add(" MiB)");
		}
		else
			status.add("   history off");
		if (m_debugger.getStopReason() != "" && !m_debugger.isRunning())
			status.add("   -  ").add(m_debugger.getStopReason());
		m_statusLabel.setText(status);

		const u64 span = history.getLastPosition() - history.getFirstPosition();
		m_updatingTimeline = true;
		m_timeline.setValueQuiet(span == 0 ? 1.0f : cast<f32>(cast<f64>(history.getPosition() - history.getFirstPosition()) / cast<f64>(span)));
		m_updatingTimeline = false;
	}

	void DebuggerWindow::show_in_memory(u16 address)
	{
		m_tabs.setCurrentTab(*m_memoryTab);
		m_memory.jumpTo(address);
	}

	void DebuggerWindow::show_in_disassembly(u16 address)
	{
		m_followCheck.setChecked(false);
		m_disassembly.showAddress(address);
		m_programLabel.setText(m_disassembly.getProgramName(m_disassembly.getCurrentProgram()));
	}

	void DebuggerWindow::show_view(Debugger::eView view, i64 argument)
	{
		switch (view)
		{
			case Debugger::eView::Memory:
				m_tabs.setCurrentTab(*m_memoryTab);
				if (argument >= 0)
					m_memory.jumpTo(cast<u64>(argument));
				break;
			case Debugger::eView::Stack: m_tabs.setCurrentTab(*m_stackTab); break;
			case Debugger::eView::CallTrace: m_tabs.setCurrentTab(*m_callsTab); break;
			case Debugger::eView::Tracker: m_tabs.setCurrentTab(*m_trackerTab); break;
			case Debugger::eView::History: m_tabs.setCurrentTab(*m_historyTab); break;
			case Debugger::eView::Breakpoints: m_tabs.setCurrentTab(*m_breakpointsTab); break;
			case Debugger::eView::Registers: m_tabs.setCurrentTab(*m_registersTab); break;
			default: break;
		}
		m_viewsDirty = true;
	}

	void DebuggerWindow::run_command(const String& command)
	{
		const auto tokens = command.new_trim().toLower().tokenize().getRawData();
		if (tokens.size() == 2 && (tokens[0] == "detach" || tokens[0] == "attach") && (tokens[1] == "screen" || tokens[1] == "console"))
		{
			m_consoleOut.fg(ostd::ConsoleColors::Magenta).p(" (ddb) #/> ").fg(ostd::ConsoleColors::White).p(command.new_trim()).reset().nl();
			if (tokens[1] == "screen")
				set_screen_detached(tokens[0] == "detach");
			else
				set_console_detached(tokens[0] == "detach");
			return;
		}
		m_debugger.command(command);
	}

	void DebuggerWindow::show_programs_menu(void)
	{
		ContextMenu::Instance menu;
		// First: back to following the IP (picking a program stops it).
		menu.entries.push_back({ m_disassembly.isFollowEnabled() ? String("Following the IP") : String("Follow the IP"), -1 });
		for (i32 i = 0; i < m_disassembly.getProgramCount(); i++)
		{
			String name = m_disassembly.getProgramName(i);
			if (i == m_disassembly.getCurrentProgram())
				name = String("> ").add(name);
			menu.entries.push_back({ name, i });
		}
		menu.onActivate = [this](const ContextMenu::Entry& entry) {
			if (entry.id < 0)
			{
				m_followCheck.setChecked(true);
				m_disassembly.enableFollow(true);
				m_disassembly.setCurrentAddress(cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::IP)), true);
			}
			else
			{
				m_disassembly.showProgram(entry.id);
				m_followCheck.setChecked(false);
			}
			m_programLabel.setText(m_disassembly.getProgramName(m_disassembly.getCurrentProgram()));
		};
		const Rectangle bounds = m_programButton.getGlobalBounds();
		showContextMenu(menu, { bounds.x, bounds.y + bounds.h });
	}
}
