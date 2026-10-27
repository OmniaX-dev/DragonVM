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
		setTheme(m_theme);
		setClearColor({ 22, 24, 22 });
		build_toolbar();
		build_layout();
		build_tabs();

		m_consoleOut.attach(m_consoleView);
		m_debugger.setConsole(m_consoleOut);
		m_debugger.setStateChangedCallback([this](void) { m_viewsDirty = true; });
		m_debugger.setViewCallback([this](Debugger::eView view, i64 argument) { show_view(view, argument); });
		m_debugger.setQuitCallback([this](void) { close(); });
	}

	ogfx::gui::Label& DebuggerWindow::add_title(Label& label, const String& text, const Rectangle& anchors, const Offsets& offsets)
	{
		addWidget(label);
		label.setText(text);
		label.setFontSize(15);
		place(label, anchors.x, anchors.y, anchors.w, anchors.h, offsets.left, offsets.top, offsets.right, offsets.bottom);
		return label;
	}

	void DebuggerWindow::build_layout(void)
	{
		// Columns and rows, as fractions of the window.
		const f32 split = 0.47f;
		const f32 right = 0.78f;
		const f32 top = 0.30f;
		const f32 bottom = 0.66f;

		// ===== Disassembly =====
		addWidget(m_programButton);
		m_programButton.setText("Programs");
		place(m_programButton, 0, 0, 0, 0, 6, 4, 112, 34);
		m_programButton.setCallback(Widget::eCallback::ActionPerformed, [this](const Event&) { show_programs_menu(); });
		addWidget(m_programLabel);
		m_programLabel.setFontSize(15);
		place(m_programLabel, 0, 0, split, 0, 120, 4, -120, 34);
		addWidget(m_followCheck);
		m_followCheck.setText("Follow IP");
		m_followCheck.setChecked(true);
		place(m_followCheck, split, 0, split, 0, -112, 6, -6, 32);
		m_followCheck.setStateChangedCallback([this](CheckBox&, bool checked) {
			m_disassembly.enableFollow(checked);
			m_viewsDirty = true;
		});
		addWidget(m_disassembly);
		place(m_disassembly, 0, 0, split, bottom, 6, 38, -3, -3);
		m_disassembly.setFontSize(15);
		m_disassembly.setShowInMemoryCallback([this](u16 address) { show_view(Debugger::eView::Memory, address); });

		// ===== Console =====
		add_title(m_consoleTitle, "Console  (help: list of commands, Enter: step)", { 0, bottom, split, bottom }, { 8, 0, -3, 30 });
		addWidget(m_consoleView);
		place(m_consoleView, 0, bottom, split, 1, 6, 32, -3, -42);
		m_consoleView.setFontSize(14);
		m_consoleView.enableFollowTail();
		m_consoleView.getBuffer().setMaxLines(5000);
		addWidget(m_commandInput);
		place(m_commandInput, 0, 1, split, 1, 6, -38, -3, -6);
		m_commandInput.setCommandCallback([this](const String& command) { m_debugger.command(command); });

		// ===== Screen =====
		add_title(m_displayTitle, "Screen  (click it to type into the machine)", { split, 0, right, 0 }, { 6, 2, -3, 32 });
		addWidget(m_display);
		place(m_display, split, 0, right, top, 3, 34, -3, -3);
		m_display.enableFocus();
		m_display.enableFocusBorder();
		m_display.setFocusBorderColor({ 0, 117, 199 });

		// ===== Breakpoints =====
		add_title(m_breakpointsTitle, "Breakpoints & watches", { right, 0, 1, 0 }, { 6, 2, -6, 32 });
		addWidget(m_breakpointsView);
		place(m_breakpointsView, right, 0, 1, top, 3, 34, -6, -3);
		m_breakpointsView.setFontSize(14);
		m_breakpointsView.setLineCallback([this](RichTextView& view, i32 line, i32, ogfx::MouseEventData::eButton button, bool doubleClick) {
			const i64 address = view.getBuffer().getLineUserData(line);
			if (address < 0)
				return;
			if (button == ogfx::MouseEventData::eButton::Right)
				m_debugger.removeBreakPoint(cast<u16>(address));
			else if (doubleClick)
				m_disassembly.scrollToAddress(cast<u16>(address));
			m_viewsDirty = true;
		});

		// ===== Registers =====
		add_title(m_registersTitle, "Registers  (the last cycle, red: changed)", { split, top, 1, top }, { 6, 0, -6, 30 });
		addWidget(m_registersView);
		place(m_registersView, split, top, 1, bottom, 3, 32, -6, -3);
		m_registersView.setFontSize(13);
		m_registersView.enableHoverHighlight(false);
		m_registersView.setLinePadding(0);

		// ===== Tabs =====
		addWidget(m_tabs);
		place(m_tabs, split, bottom, 1, 1, 3, 3, -6, -6);
	}

	void DebuggerWindow::build_tabs(void)
	{
		auto l_tab = [this](const String& title, Widget& content) -> Panel& {
			Panel& tab = m_tabs.addTab(title);
			tab.addWidget(content);
			content.setAnchorPreset(eAnchorPreset::FullRect, ePresetResizeMode::KeepSize, 2);
			return tab;
		};
		m_memoryTab = &l_tab("Memory", m_memory);
		m_stackTab = &l_tab("Stack", m_stackView);
		m_callsTab = &l_tab("Call trace", m_callsView);
		m_trackerTab = &l_tab("Tracker", m_trackerView);
		m_historyTab = &l_tab("History", m_historyView);
		for (RichTextView* view : { &m_stackView, &m_callsView, &m_trackerView, &m_historyView })
			view->setFontSize(14);
		m_tabs.setCurrentTab(*m_memoryTab);
	}

	void DebuggerWindow::build_toolbar(void)
	{
		auto& tb = getToolBar();
		tb.setHeight(36);
		// Buttons placed one after the other (the bar's own button box squeezes long texts).
		f32 x = 6;
		auto l_button = [&](const String& text, std::function<void(void)> action) {
			Button& button = m_toolButtons.emplace_back(*this);
			tb.addWidget(button);
			button.setText(text);
			button.setFontSize(15);
			const f32 w = cast<f32>(text.len()) * 8.0f + 22.0f;
			place(button, 0, 0.5f, 0, 0.5f, x, -14, x + w, 14);
			button.setCallback(Widget::eCallback::ActionPerformed, [action](const Event&) { action(); });
			x += w + 5;
		};
		l_button("Run (F5)", [this](void) { m_debugger.resume(); });
		l_button("Pause (F6)", [this](void) { m_debugger.pause(); });
		l_button("Step (F10)", [this](void) { m_debugger.stepInto(1); });
		l_button("Over (F11)", [this](void) { m_debugger.stepOver(); });
		l_button("Out (Sh+F11)", [this](void) { m_debugger.stepOut(); });
		x += 14;
		l_button("Back (F9)", [this](void) { m_debugger.stepBack(1); });
		l_button("Reverse run (Sh+F5)", [this](void) { m_debugger.reverseContinue(); });
		l_button("Rewind", [this](void) { m_debugger.seek(m_debugger.history().getFirstPosition()); });
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
		sb.addWidget(m_timeline);
		place(m_timeline, 0.6f, 0.5f, 1.0f, 0.5f, 0, -9, -12, 9);
		// A fine step (a position per history entry, more or less): no ticks.
		m_timeline.enableDrawTicks(false);
		m_timeline.setTooltipText("History timeline: drag to move through the recorded cycles");
		m_timeline.enableTooltip();
		m_timeline.setValueChangedCallback([this](f32, f32 value) {
			if (m_updatingTimeline || !m_debugger.isHistoryEnabled())
				return;
			const History& history = m_debugger.history();
			const u64 span = history.getLastPosition() - history.getFirstPosition();
			m_debugger.seek(history.getFirstPosition() + cast<u64>(std::llround(cast<f64>(value) * cast<f64>(span))));
		});
		showStatusBar(true);
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

		// Memory: the whole address space, the regions of the memory map highlighted.
		const auto& image = m_debugger.getBusImage();
		const u16 ip = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::IP));
		m_memory.setData(std::span<const i8>(reinterpret_cast<const i8*>(image.data()), image.size()), ip & 0xFFF0, 16, 0, ip, 1, "Bus");
		m_memory.setBigEndian(true);
		m_memory.setUnitSize(2);
		const Color regionColors[] = { { 40, 80, 160, 40 }, { 160, 120, 20, 40 }, { 150, 60, 170, 40 }, { 60, 160, 60, 40 }, { 160, 60, 60, 40 } };
		i32 colorIndex = 0;
		for (const auto& region : DragonRuntime::memMap.getRegions())
		{
			if (region.name == "RAM")
				continue;
			m_memory.addHighlight(region.startAddress, cast<u32>(region.endAddress) - region.startAddress + 1, regionColors[colorIndex++ % 5], region.name);
		}
		const u16 sp = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::SP));
		m_stackHighlight = m_memory.addHighlight(sp, std::max<u32>(1, 0x10000 - sp), Color(40, 160, 100, 50), "stack");
		m_memory.setWriteCallback([this](ogfx::gui::StreamIndex index, u64 value, u8 size) {
			return m_debugger.writeMemory(cast<u16>(index), value, size);
		});

		m_consoleOut.fg(ostd::ConsoleColors::Green).p("DragonVM Debugger ").p(VERSION_STR).reset().nl();
		m_consoleOut.fg(ostd::ConsoleColors::BrightGray).p("Machine: ").p(m_debugger.data().args.machine_config_path).p("  -  ");
		m_consoleOut.p(cast<i32>(m_debugger.data().code.size())).p(" instructions, ").p(cast<i32>(m_debugger.data().labels.size())).p(" labels, ");
		m_consoleOut.p(cast<i32>(m_debugger.data().data.size())).p(" data symbols in ").p(cast<i32>(DisassemblyLoader::getTables().size())).p(" programs.").reset().nl();
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
			m_debugger.command(command);
		}
		Panel* shown = nullptr;
		for (Panel* tab : { m_memoryTab, m_stackTab, m_callsTab, m_trackerTab, m_historyTab })
		{
			if (tab != nullptr && tab->getParent() != nullptr)
				shown = tab;
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
		if (shown != m_shownTab)
		{
			m_shownTab = shown;
			refresh_tab();
		}
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

		if (!full)
			return;
		RichTextOutputHandler out(m_registersView);
		out.clear();
		Debugger::Display::printDiff(out, m_debugger);

		out.attach(m_breakpointsView);
		out.clear();
		Debugger::Display::printBreakpoints(out, m_debugger);
		// Lines of the breakpoints: their address, for the clicks.
		auto& buffer = m_breakpointsView.getBuffer();
		i32 line = 1;
		for (u16 addr : m_debugger.data().breakpoints)
			buffer.setLineUserData(line++, addr);
		refresh_tab();
	}

	void DebuggerWindow::refresh_tab(void)
	{
		RichTextOutputHandler out;
		auto l_width = [](RichTextView& view) { return std::max(80, view.getVisibleColumns() - 1); };
		if (m_shownTab == m_stackTab)
		{
			out.attach(m_stackView);
			out.clear();
			Debugger::Display::printStack(out, m_debugger, m_stackRows, l_width(m_stackView));
		}
		else if (m_shownTab == m_callsTab)
		{
			out.attach(m_callsView);
			out.clear();
			Debugger::Display::printCallStack(out, m_debugger, l_width(m_callsView));
			m_callsView.scrollToBottom();
		}
		else if (m_shownTab == m_trackerTab)
		{
			out.attach(m_trackerView);
			out.clear();
			Debugger::Display::printTrackedAddresses(out, m_debugger, l_width(m_trackerView));
		}
		else if (m_shownTab == m_historyTab)
		{
			out.attach(m_historyView);
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

	void DebuggerWindow::show_view(Debugger::eView view, i64 argument)
	{
		switch (view)
		{
			case Debugger::eView::Memory:
				m_tabs.setCurrentTab(*m_memoryTab);
				if (argument >= 0)
					m_memory.jumpTo(cast<u64>(argument));
				break;
			case Debugger::eView::Stack:
				if (argument > 0)
					m_stackRows = cast<u16>(argument);
				m_tabs.setCurrentTab(*m_stackTab);
				break;
			case Debugger::eView::CallTrace: m_tabs.setCurrentTab(*m_callsTab); break;
			case Debugger::eView::Tracker: m_tabs.setCurrentTab(*m_trackerTab); break;
			case Debugger::eView::History: m_tabs.setCurrentTab(*m_historyTab); break;
			case Debugger::eView::Breakpoints:
			case Debugger::eView::Registers:
			default:
				break;
		}
		m_viewsDirty = true;
	}

	void DebuggerWindow::show_programs_menu(void)
	{
		ContextMenu::Instance menu;
		for (i32 i = 0; i < m_disassembly.getProgramCount(); i++)
			menu.entries.push_back({ m_disassembly.getProgramName(i), i });
		menu.onActivate = [this](const ContextMenu::Entry& entry) {
			m_disassembly.showProgram(entry.id);
			m_followCheck.setChecked(false);
			m_programLabel.setText(m_disassembly.getProgramName(entry.id));
		};
		showContextMenu(menu, getMousePosition());
	}
}
