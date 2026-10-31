#include "Debugger.hpp"
#include "DisassemblyLoader.hpp"
#include "../runtime/DragonRuntime.hpp"
#include <ostd/io/Memory.hpp>
#include <ostd/string/String.hpp>

namespace dragon
{
	//Debugger::Utils
	String Debugger::Utils::findSymbol(const DisassemblyList& list, u16 address, u16* outSize)
	{
		for (auto& line : list)
		{
			if (line.addr == address)
			{
				if (outSize != nullptr)
					*outSize = line.size;
				return line.code;
			}
		}
		return "";
	}

	u16 Debugger::Utils::findSymbol(const DisassemblyList& list, const String& symbol, u16* outSize)
	{
		for (auto& line : list)
		{
			if (line.code == symbol)
			{
				if (outSize != nullptr)
					*outSize = line.size;
				return line.addr;
			}
		}
		return 0x0000;
	}

	String Debugger::Utils::findNearestSymbol(const DisassemblyList& list, u16 address)
	{
		// The lists are sorted by address.
		const code::Assembler::tDisassemblyLine* best = nullptr;
		for (auto& line : list)
		{
			if (line.addr > address)
				break;
			best = &line;
		}
		if (best == nullptr)
			return "";
		String symbol = best->code.new_trim();
		if (best->addr != address)
			symbol.add("+").add(String::getHexStr(cast<u16>(address - best->addr), true, 1));
		return symbol;
	}

	bool Debugger::Utils::isValidLabelNameChar(char c)
	{
		return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == '_');
	}

	ostd::OutputHandlerBase& Debugger::Utils::printFullLine(ostd::OutputHandlerBase& out, char c, const ostd::ConsoleColors::tConsoleColor& foreground, i32 width)
	{
		String str = String::duplicateChar(c, std::max(width, 1));
		out.fg(foreground).p(str).reset().nl();
		return out;
	}

	ostd::OutputHandlerBase& Debugger::Utils::printFullLine(ostd::OutputHandlerBase& out, char c, const ostd::ConsoleColors::tConsoleColor& foreground, const ostd::ConsoleColors::tConsoleColor& background, i32 width)
	{
		String str = String::duplicateChar(c, std::max(width, 1));
		out.bg(background).fg(foreground).p(str).reset().nl();
		return out;
	}

	String Debugger::Utils::decodeInstruction(u16 address, u8* outSize)
	{
		auto& mem = DragonRuntime::memMap;
		const u8 opCode = mem.peek8(address);
		String name = "";
		u8 size = 1;
		if (opCode >= data::OpCodes::Ext01 && opCode <= data::OpCodes::Ext16)
		{
			data::CPUExtension* ext = DragonRuntime::cpu.getExtension(opCode);
			const u8 extInst = mem.peek8(cast<u16>(address + 1));
			if (ext != nullptr)
			{
				name = ext->getOpCodeString(extInst);
				size = 1 + ext->getInstructionSIze(extInst);
			}
			else
				name = data::OpCodes::getBaseOpCodeString(opCode);
		}
		else
		{
			name = data::OpCodes::getBaseOpCodeString(opCode);
			size = data::OpCodes::getBaseInstructionSize(opCode);
		}
		if (outSize != nullptr)
			*outSize = std::max<u8>(size, 1);
		return name;
	}

	bool Debugger::Utils::parseValue(const String& text, i64& outValue)
	{
		String edit = text.new_trim().toLower();
		bool negative = false;
		if (edit.startsWith("-"))
		{
			negative = true;
			edit.substr(1);
		}
		if (edit.len() == 0)
			return false;
		i64 value = 0;
		if (edit.startsWith("0x"))
		{
			edit.substr(2);
			if (edit.len() == 0)
				return false;
			for (char c : edit.cpp_str())
			{
				if (!std::isxdigit(cast<u8>(c)))
					return false;
				value = value * 16 + (std::isdigit(cast<u8>(c)) ? c - '0' : 10 + (c - 'a'));
			}
		}
		else if (edit.startsWith("0b"))
		{
			edit.substr(2);
			if (edit.len() == 0)
				return false;
			for (char c : edit.cpp_str())
			{
				if (c != '0' && c != '1')
					return false;
				value = value * 2 + (c - '0');
			}
		}
		else
		{
			for (char c : edit.cpp_str())
			{
				if (!std::isdigit(cast<u8>(c)))
					return false;
				value = value * 10 + (c - '0');
			}
		}
		outValue = negative ? -value : value;
		return true;
	}

	bool Debugger::Utils::parseAddress(const Debugger& dbg, const String& text, u16& outAddress, u16* outSize)
	{
		String edit = text.new_trim();
		if (edit.startsWith("$"))
		{
			u16 size = 1;
			u16 addr = findSymbol(dbg.data().data, edit, &size);
			if (addr == 0)
				addr = findSymbol(dbg.data().labels, edit, &size);
			if (addr == 0)
				return false;
			outAddress = addr;
			if (outSize != nullptr)
				*outSize = std::max<u16>(size, 1);
			return true;
		}
		i64 value = 0;
		if (!parseValue(edit, value) || value < 0 || value > 0xFFFF)
			return false;
		outAddress = cast<u16>(value);
		if (outSize != nullptr)
			*outSize = 1;
		return true;
	}




	//Debugger
	i32 Debugger::loadArguments(int argc, char** argv)
	{
		auto& out = console();
		if (argc < 2)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
			out.fg(ostd::ConsoleColors::Red).p("Use the --help option for more info.").reset().nl();
			return DragonRuntime::RETURN_VAL_TOO_FEW_ARGUMENTS;
		}
		m_data.args.machine_config_path = argv[1];
		if (m_data.args.machine_config_path == "--help")
		{
			Display::printApplicationHelp(out);
			return DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER;
		}
		for (i32 i = 2; i < argc; i++)
		{
			String edit(argv[i]);
			if (edit == "--verbose-load")
				m_data.args.verbose_load = true;
			else if (edit == "--step-exec")
				m_data.args.step_exec = true;
			else if (edit == "--track-step-diff-off" || edit == "--history-off")
				m_data.args.track_step_diff = false;
			else if (edit == "--auto-track-data-off")
				m_data.args.auto_track_all_data_symbols = false;
			else if (edit == "--hide-vdisplay")
				m_data.args.hide_virtual_display = true;
			else if (edit == "--auto-start")
				m_data.args.auto_start_debug = true;
			else if (edit == "--max-speed")
				m_data.args.max_speed = true;
			else if (edit == "--exec")
			{
				if (i + 1 >= argc)
					return DragonRuntime::RETURN_VAL_MISSING_PARAM;
				m_data.args.exec_commands = argv[++i];
			}
			else if (edit == "--history-limit")
			{
				if (i + 1 >= argc)
					return DragonRuntime::RETURN_VAL_MISSING_PARAM;
				edit = argv[++i];
				if (!edit.isNumeric())
					return DragonRuntime::RETURN_VAL_PARAMETER_NOT_NUMERIC;
				m_data.args.history_limit_mb = cast<u64>(std::max<i64>(16, edit.toInt()));
			}
			else if (edit == "--force-load")
			{
				if ((argc - 1) - i < 2)
					return DragonRuntime::RETURN_VAL_MISSING_PARAM;
				i++;
				m_data.args.force_load_file = argv[i];
				i++;
				edit = argv[i];
				if (!edit.isNumeric())
					return DragonRuntime::RETURN_VAL_PARAMETER_NOT_NUMERIC;
				m_data.args.force_load_mem_offset = (u16)edit.toInt();
				m_data.args.force_load = true;
			}
			else if (edit == "--help")
			{
				Display::printApplicationHelp(out);
				return DragonRuntime::RETURN_VAL_CLOSE_DEBUGGER;
			}
		}
		return DragonRuntime::RETURN_VAL_EXIT_SUCCESS;
	}

	i32 Debugger::initRuntime(hw::VirtualDisplay& display)
	{
		m_display = &display;
		// The runtime's messages (loading info, errors) go to the debugger's console.
		DragonRuntime::setOutputHandler(console());
		dragon::DragonRuntime::tRuntimeInitInfo initInfo;
		initInfo.configFilePath = m_data.args.machine_config_path;
		initInfo.verboseLoad = m_data.args.verbose_load;
		initInfo.debugModeEnabled = true;
		i32 init_state = dragon::DragonRuntime::initMachine(initInfo);
		if (init_state != 0) return init_state; //TODO: Error

		if (m_data.args.force_load)
			dragon::DragonRuntime::forceLoad(m_data.args.force_load_file, m_data.args.force_load_mem_offset);

		dragon::DisassemblyLoader::loadDirectory(m_data.disassemblyDirectory);
		m_data.code = dragon::DisassemblyLoader::getCodeTable();
		m_data.labels = dragon::DisassemblyLoader::getLabelTable();
		m_data.data = dragon::DisassemblyLoader::getDataTable();
		register_debug_breaks();

		if (m_data.args.auto_track_all_data_symbols)
		{
			for (auto& sym : m_data.data)
			{
				if (sym.size > 0)
					track(cast<u16>(sym.addr), sym.size, sym.code);
			}
		}

		// The machine's clock: the debugger counts cycles, so the screen is redrawn at the
		// same cycles whatever the speed the machine runs at.
		auto& config = DragonRuntime::machine_config;
		m_clockRate = std::max<f64>(1.0, cast<f64>(config.clock_rate_sec));
		m_maxSpeed = m_data.args.max_speed || !config.fixed_clock;
		m_cyclesPerRedraw = std::max<u64>(1, cast<u64>(config.clock_rate_sec) / std::max<u64>(1, config.screen_redraw_rate_per_second));
		display.enableAutoRedraw(false);

		DragonRuntime::cpu.setInterruptListener([this](u8 intValue, bool hardware, u16 handlerAddress) {
			if (!m_data.args.track_call_stack)
				return;
			tCallInfo call;
			call.info = (hardware ? "HW INT" : "INT");
			call.addr = intValue;
			call.inst_addr = (hardware ? 0x0000 : m_currentInfo.instructionAddress);
			call.interrupts_disabled = !DragonRuntime::cpu.readFlag(data::Flags::InterruptsEnabled);
			call.position = (m_historyEnabled ? m_history.getPosition() : m_cycle) + 1;
			call.depth = ++m_callDepth;
			m_callTrace.push_back(call);
			if (m_callTrace.size() > MaxCallTraceSize)
				m_callTrace.pop_front();
		});
		hw::VirtualHardDrive::setWriteListener([this](hw::VirtualHardDrive& drive, u32 addr, u8 oldValue, u8 newValue) {
			if (m_historyEnabled)
				m_history.recordFileWrite(drive, addr, oldValue, newValue);
		});

		m_historyEnabled = m_data.args.track_step_diff;
		m_history.setMemoryLimit(m_data.args.history_limit_mb * 1024 * 1024);
		setup_history();
		if (m_historyEnabled)
			m_history.begin(0);

		DragonRuntime::vKeyboard.enableInput(false);
		capture_info(m_currentInfo);
		m_previousInfo = m_currentInfo;
		refreshBusImage();
		m_lastSliceTime = Clock::now();
		m_speedTime = m_lastSliceTime;
		return DragonRuntime::RETURN_VAL_EXIT_SUCCESS;
	}

	void Debugger::setup_history(void)
	{
		auto add_device = [this](const String& name, hw::IMemoryDevice& device) {
			ostd::ByteStream* stream = device.getByteStream();
			if (stream == nullptr || stream->empty())
				return;
			m_history.addSegment(name, reinterpret_cast<u8*>(stream->data()), cast<u32>(stream->size()), nullptr, [&device](void) { device.syncByteStream(); });
			m_deviceSegments[&device] = name;
		};
		add_device("RAM", DragonRuntime::ram);
		add_device("VRAM", DragonRuntime::vGraphicsInterface);
		add_device("IntVector", DragonRuntime::intVec);
		add_device("Keyboard", DragonRuntime::vKeyboard);
		add_device("MBR", DragonRuntime::vMBR);
		add_device("DiskInterface", DragonRuntime::vDiskInterface);
		add_device("CMOS", DragonRuntime::vCMOS);
		m_history.addSegment("CPU", reinterpret_cast<u8*>(&m_cpuState), sizeof(m_cpuState),
			[this](void) { DragonRuntime::cpu.saveState(m_cpuState); },
			[this](void) { DragonRuntime::cpu.loadState(m_cpuState); });
		m_history.addSegment("Graphics", reinterpret_cast<u8*>(&m_graphicsState), sizeof(m_graphicsState),
			[this](void) { DragonRuntime::vGraphicsInterface.saveState(m_graphicsState); },
			[this](void) { DragonRuntime::vGraphicsInterface.loadState(m_graphicsState); });
		m_history.addSegment("DiskController", &m_diskBusy, 1,
			[this](void) { m_diskBusy = DragonRuntime::vDiskInterface.isBusy() ? 1 : 0; },
			[this](void) { DragonRuntime::vDiskInterface.setBusy(m_diskBusy != 0); });
		m_displayState.assign(hw::VirtualDisplay::StateSize, 0);
		m_history.addSegment("Display", m_displayState.data(), cast<u32>(m_displayState.size()),
			[this](void) { m_display->saveState(m_displayState.data()); },
			[this](void) { m_display->loadState(m_displayState.data()); });
	}

	void Debugger::enableHistory(bool enable)
	{
		if (enable == m_historyEnabled)
			return;
		m_historyEnabled = enable;
		if (enable)
		{
			m_history.begin(m_cycle);
			// The trace positions are history positions from now on.
			m_callTrace.clear();
			console().fg(ostd::ConsoleColors::Green).p("History recording enabled (starting at cycle ").p(m_cycle).p(").").reset().nl();
		}
		else
			console().fg(ostd::ConsoleColors::Yellow).p("History recording disabled.").reset().nl();
		notify_state_changed();
	}

	void Debugger::commitEdit(void)
	{
		if (m_historyEnabled && m_history.recordIfChanged(m_cycle, History::eEntryKind::Edit))
		{
			while (!m_callTrace.empty() && m_callTrace.back().position > m_history.getLastPosition())
				m_callTrace.pop_back();
		}
		capture_info(m_currentInfo);
		refreshBusImage();
		m_display->refresh();
		notify_state_changed();
	}

	void Debugger::update(void)
	{
		if (m_mode == eMode::Paused)
			return;
		const auto sliceStart = Clock::now();
		const auto sliceBudget = std::chrono::milliseconds(12);

		if (m_mode == eMode::ReverseRunning)
		{
			bool stop = false;
			String reason = "";
			while (Clock::now() - sliceStart < sliceBudget && !stop)
			{
				for (i32 i = 0; i < 256 && !stop; i++)
				{
					if (!m_history.stepBack())
					{
						stop = true;
						reason = "Reached the start of the recorded history.";
						break;
					}
					const u16 ip = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::IP));
					if (should_break_at(ip))
					{
						stop = true;
						reason = String(getBreakPoint(ip)->debugBreak ? "debug_break at " : "Breakpoint at ").add(String::getHexStr(ip, true, 2)).add(" (reverse).");
					}
					for (const auto& item : m_data.tracked)
					{
						if (!item.breakOnChange || stop)
							continue;
						// Going back over an entry that changes the value: that's where it changed.
						for (u16 j = 0; j < item.size && !stop; j++)
						{
							const i64 offset = bus_state_offset(cast<u16>(item.address + j));
							if (offset >= 0 && m_history.entryChanges(m_history.getPosition() + 1, cast<u32>(offset)))
							{
								stop = true;
								reason = String("Watched data changed: ").add(item.label).add(" (reverse).");
							}
						}
					}
				}
			}
			after_navigation();
			if (stop)
				stopped(reason);
			return;
		}

		// Forwards: replays the recorded future first, then executes.
		u64 cycles = std::numeric_limits<u64>::max();
		if (m_mode == eMode::Running && !m_maxSpeed && m_stepsLeft == 0)
		{
			const f64 elapsed = std::chrono::duration<f64>(sliceStart - m_lastSliceTime).count();
			m_cycleBudget = std::min(m_cycleBudget + elapsed * m_clockRate, m_clockRate * 0.25);
			cycles = cast<u64>(m_cycleBudget);
			m_cycleBudget -= cast<f64>(cycles);
		}
		m_lastSliceTime = sliceStart;
		bool replayed = false;
		u64 executed = 0;
		while (executed < cycles)
		{
			if ((executed & 0x3F) == 0 && Clock::now() - sliceStart >= sliceBudget)
				break;
			const u16 ip = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::IP));
			if (m_historyEnabled && !m_history.isAtEnd())
			{
				m_history.stepForward();
				m_cycle = m_history.getCycleAt(m_history.getPosition());
				replayed = true;
			}
			else if (!step_machine())
				break;
			executed++;
			m_speedCycles++;
			if (check_stop_conditions(ip))
				break;
		}
		if (replayed)
			after_navigation();
		const auto now = Clock::now();
		const f64 speedElapsed = std::chrono::duration<f64>(now - m_speedTime).count();
		if (speedElapsed >= 0.5)
		{
			m_measuredSpeed = cast<f64>(m_speedCycles) / speedElapsed;
			m_speedCycles = 0;
			m_speedTime = now;
		}
	}

	bool Debugger::step_machine(void)
	{
		auto& cpu = DragonRuntime::cpu;
		const u16 ip = cast<u16>(cpu.readRegister(data::Registers::IP));
		if (!m_historyEnabled)
			capture_info(m_previousInfo);
		m_currentInfo.instructionAddress = ip;
		if (m_data.args.track_call_stack)
			track_call(ip);
		cpu.execute();
		m_display->processSignal();
		DragonRuntime::vDiskInterface.cycleStep();
		m_cycle++;
		if (m_cycle % m_cyclesPerRedraw == 0)
			m_display->redrawScreen();
		m_display->update();
		if (m_historyEnabled)
			m_history.record(m_cycle);
		return true;
	}

	void Debugger::track_call(u16 ip)
	{
		auto& cpu = DragonRuntime::cpu;
		auto& mem = DragonRuntime::memMap;
		const u8 inst = mem.peek8(ip);
		const bool interrupts_enabled = cpu.readFlag(data::Flags::InterruptsEnabled);
		tCallInfo call;
		call.inst_addr = ip;
		call.interrupts_disabled = !interrupts_enabled;
		call.position = (m_historyEnabled ? m_history.getPosition() : m_cycle) + 1;
		if (inst == data::OpCodes::CallImm)
		{
			call.info = "CALL IMM";
			call.addr = mem.peek16(cast<u16>(ip + 1));
			call.depth = ++m_callDepth;
		}
		else if (inst == data::OpCodes::CallReg)
		{
			call.info = "CALL REG";
			call.addr = cast<u16>(cpu.readRegister(mem.peek8(cast<u16>(ip + 1))));
			call.depth = ++m_callDepth;
		}
		else if (inst == data::OpCodes::Ret)
		{
			call.info = "RET";
			m_callDepth = std::max(0, m_callDepth - 1);
			call.depth = m_callDepth;
		}
		else if (inst == data::OpCodes::RetInt)
		{
			call.info = "RET INT";
			m_callDepth = std::max(0, m_callDepth - 1);
			call.depth = m_callDepth;
		}
		else
			return;
		m_callTrace.push_back(call);
		if (m_callTrace.size() > MaxCallTraceSize)
			m_callTrace.pop_front();
	}

	bool Debugger::check_stop_conditions(u16 previousIP)
	{
		auto& cpu = DragonRuntime::cpu;
		const u16 ip = cast<u16>(cpu.readRegister(data::Registers::IP));
		if (DragonRuntime::hasError())
		{
			process_errors();
			stopped("Execution stopped by an error.");
			return true;
		}
		if (cpu.isHalted())
		{
			stopped("The CPU is halted.");
			return true;
		}
		if (cpu.isRamDumped())
			console().fg(ostd::ConsoleColors::Red).p("RAM Dumped to <ram_dump.bin>.").reset().nl();
		if (cpu.isInDebugBreakPoint() && getBreakPoint(previousIP) == nullptr)
		{
			// A debug_break the disassembly didn't know about: a breakpoint from now on (the
			// known ones already stopped before executing, or are disabled).
			m_data.breakpoints[previousIP] = { true, true };
			stopped(String("Reached debug_break at ").add(String::getHexStr(previousIP, true, 2)).add(" (now a breakpoint: click it in the margin to disable it)."));
			return true;
		}
		if (should_break_at(ip))
		{
			stopped(String(getBreakPoint(ip)->debugBreak ? "debug_break at " : "Breakpoint at ").add(String::getHexStr(ip, true, 2)).add("."));
			return true;
		}
		size_t trackedIndex = 0;
		for (const auto& item : m_data.tracked)
		{
			const size_t first = trackedIndex;
			trackedIndex += item.size;
			if (!item.breakOnChange)
				continue;
			// Watches are few: checking them every cycle is cheap. With the history, the last
			// entry says whether they changed; otherwise the values are compared with the ones
			// captured before the cycle.
			bool changed = false;
			for (u16 j = 0; j < item.size && !changed; j++)
			{
				const u16 addr = cast<u16>(item.address + j);
				if (m_historyEnabled)
				{
					const i64 offset = bus_state_offset(addr);
					changed = (offset >= 0 && m_history.entryChanges(m_history.getPosition(), cast<u32>(offset)));
				}
				else if (first + j < m_previousInfo.trackedValues.size())
					changed = (DragonRuntime::memMap.peek8(addr) != m_previousInfo.trackedValues[first + j]);
			}
			if (changed)
			{
				stopped(String("Watched data changed: ").add(item.label).add(" (").add(String::getHexStr(item.address, true, 2)).add(")."));
				return true;
			}
		}
		switch (m_mode)
		{
			case eMode::StepOver:
			case eMode::StepOut:
				if (m_callDepth <= m_targetDepth)
				{
					stopped("");
					return true;
				}
				break;
			case eMode::RunTo:
				if (ip == m_runToAddress)
				{
					stopped("");
					return true;
				}
				break;
			case eMode::Running:
				if (m_stepsLeft > 0 && --m_stepsLeft == 0)
				{
					stopped("");
					return true;
				}
				break;
			default:
				break;
		}
		return false;
	}

	void Debugger::stopped(const String& reason)
	{
		m_mode = eMode::Paused;
		m_stepsLeft = 0;
		m_stopReason = reason;
		DragonRuntime::vKeyboard.enableInput(false);
		capture_info(m_currentInfo);
		if (m_historyEnabled)
		{
			if (m_history.canStepBack())
			{
				// The state before the last cycle, for the diff view.
				m_history.stepBack();
				capture_info(m_previousInfo);
				m_previousInfo.cycle = m_history.getCycleAt(m_history.getPosition());
				m_history.stepForward();
			}
			else
				m_previousInfo = m_currentInfo;
		}
		refreshBusImage();
		m_display->refresh();
		if (reason != "")
			console().fg(ostd::ConsoleColors::Yellow).p(reason).reset().nl();
		notify_state_changed();
	}

	void Debugger::after_navigation(void)
	{
		m_cycle = m_history.getCycleAt(m_history.getPosition());
		// The call depth is the one after the last call / return up to here.
		const u64 position = m_history.getPosition();
		m_callDepth = 0;
		for (auto it = m_callTrace.rbegin(); it != m_callTrace.rend(); ++it)
		{
			if (it->position <= position)
			{
				m_callDepth = it->depth;
				break;
			}
		}
		m_display->refresh();
	}

	void Debugger::capture_info(tMachineInfo& info) const
	{
		auto& cpu = DragonRuntime::cpu;
		auto& mem = DragonRuntime::memMap;
		info.cycle = m_cycle;
		info.instructionAddress = cast<u16>(cpu.readRegister(data::Registers::IP));
		u8 size = 1;
		info.opCode = Utils::decodeInstruction(info.instructionAddress, &size);
		info.footprintSize = std::min<u8>(size, 8);
		for (u8 i = 0; i < info.footprintSize; i++)
			info.footprint[i] = mem.peek8(cast<u16>(info.instructionAddress + i));
		info.stackFrameSize = cpu.getStackFrameSize();
		info.debugBreak = cpu.isInDebugBreakPoint();
		info.interruptHandlerCount = cpu.getInterruptHandlerCount();
		info.biosMode = cpu.isInBIOSMOde();
		info.subroutineCounter = cpu.getSubRoutineCounter();
		info.inSubroutine = cpu.isInSubRoutine();
		info.halted = cpu.isHalted();
		for (u8 i = 0; i < data::Registers::Last; i++)
			info.registers[i] = cpu.readRegister(i);
		info.trackedValues.clear();
		for (const auto& item : m_data.tracked)
		{
			for (u16 j = 0; j < item.size; j++)
				info.trackedValues.push_back(mem.peek8(cast<u16>(item.address + j)));
		}
	}

	size_t Debugger::getVisibleCallTraceSize(void) const
	{
		const u64 position = (m_historyEnabled ? m_history.getPosition() : m_cycle);
		size_t count = m_callTrace.size();
		while (count > 0 && m_callTrace[count - 1].position > position)
			count--;
		return count;
	}

	void Debugger::refreshBusImage(void)
	{
		m_busImage.resize(0x10000);
		DragonRuntime::memMap.readBusImage(m_busImage.data());
	}

	void Debugger::resume(void)
	{
		if (DragonRuntime::cpu.isHalted() && m_history.isAtEnd())
		{
			console().fg(ostd::ConsoleColors::Red).p("The CPU is halted.").reset().nl();
			return;
		}
		if (m_historyEnabled && m_mode == eMode::Paused)
			m_history.recordIfChanged(m_cycle, History::eEntryKind::External);
		m_mode = eMode::Running;
		m_stepsLeft = 0;
		m_stopReason = "";
		m_lastSliceTime = Clock::now();
		m_cycleBudget = 0;
		notify_state_changed();
	}

	void Debugger::pause(const String& reason)
	{
		if (m_mode == eMode::Paused)
			return;
		stopped(reason == "" ? String("Paused at cycle ").add(m_cycle).add(".") : reason);
	}

	void Debugger::stepInto(u32 count)
	{
		if (m_mode != eMode::Paused || count == 0)
			return;
		if (DragonRuntime::cpu.isHalted() && m_history.isAtEnd())
		{
			console().fg(ostd::ConsoleColors::Red).p("The CPU is halted.").reset().nl();
			return;
		}
		if (m_historyEnabled)
			m_history.recordIfChanged(m_cycle, History::eEntryKind::External);
		m_mode = eMode::Running;
		m_stepsLeft = count;
		update();
	}

	void Debugger::stepOver(void)
	{
		if (m_mode != eMode::Paused)
			return;
		const u16 ip = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::IP));
		const u8 inst = DragonRuntime::memMap.peek8(ip);
		if (inst != data::OpCodes::CallImm && inst != data::OpCodes::CallReg && inst != data::OpCodes::Int)
		{
			stepInto(1);
			return;
		}
		if (m_historyEnabled)
			m_history.recordIfChanged(m_cycle, History::eEntryKind::External);
		m_targetDepth = m_callDepth;
		m_mode = eMode::StepOver;
		update();
	}

	void Debugger::stepOut(void)
	{
		if (m_mode != eMode::Paused)
			return;
		if (m_callDepth <= 0)
		{
			console().fg(ostd::ConsoleColors::Yellow).p("Not inside a subroutine: running until the next stop.").reset().nl();
			resume();
			return;
		}
		if (m_historyEnabled)
			m_history.recordIfChanged(m_cycle, History::eEntryKind::External);
		m_targetDepth = m_callDepth - 1;
		m_mode = eMode::StepOut;
		update();
	}

	void Debugger::runTo(u16 address)
	{
		if (m_mode != eMode::Paused)
			return;
		if (m_historyEnabled)
			m_history.recordIfChanged(m_cycle, History::eEntryKind::External);
		m_runToAddress = address;
		m_mode = eMode::RunTo;
		update();
	}

	void Debugger::stepBack(u32 count)
	{
		if (!m_historyEnabled)
		{
			console().fg(ostd::ConsoleColors::Red).p("History recording is disabled: can't go back.").reset().nl();
			return;
		}
		if (m_mode != eMode::Paused)
			pause();
		m_history.recordIfChanged(m_cycle, History::eEntryKind::External);
		if (!m_history.canStepBack())
		{
			console().fg(ostd::ConsoleColors::Yellow).p("Reached the start of the recorded history.").reset().nl();
			return;
		}
		for (u32 i = 0; i < count && m_history.stepBack(); i++)
			;
		after_navigation();
		stopped("");
	}

	void Debugger::reverseContinue(void)
	{
		if (!m_historyEnabled)
		{
			console().fg(ostd::ConsoleColors::Red).p("History recording is disabled: can't go back.").reset().nl();
			return;
		}
		if (m_mode != eMode::Paused)
			pause();
		m_history.recordIfChanged(m_cycle, History::eEntryKind::External);
		// The current position may be a breakpoint itself: leave it first.
		if (!m_history.stepBack())
		{
			console().fg(ostd::ConsoleColors::Yellow).p("Reached the start of the recorded history.").reset().nl();
			return;
		}
		const u16 ip = cast<u16>(DragonRuntime::cpu.readRegister(data::Registers::IP));
		if (should_break_at(ip))
		{
			after_navigation();
			stopped(String("Breakpoint at ").add(String::getHexStr(ip, true, 2)).add(" (reverse)."));
			return;
		}
		m_mode = eMode::ReverseRunning;
		notify_state_changed();
	}

	void Debugger::seek(u64 position)
	{
		if (!m_historyEnabled)
			return;
		if (m_mode != eMode::Paused)
			pause();
		m_history.recordIfChanged(m_cycle, History::eEntryKind::External);
		m_history.seek(position);
		after_navigation();
		stopped("");
	}

	bool Debugger::toggleBreakPoint(u16 address)
	{
		auto it = m_data.breakpoints.find(address);
		if (it == m_data.breakpoints.end())
		{
			addBreakPoint(address);
			return true;
		}
		if (it->second.debugBreak)
		{
			it->second.enabled = !it->second.enabled;
			return it->second.enabled;
		}
		m_data.breakpoints.erase(it);
		return false;
	}

	bool Debugger::isBreakPoint(u16 address) const
	{
		auto it = m_data.breakpoints.find(address);
		return it != m_data.breakpoints.end() && it->second.enabled;
	}

	const Debugger::tBreakPoint* Debugger::getBreakPoint(u16 address) const
	{
		auto it = m_data.breakpoints.find(address);
		return it != m_data.breakpoints.end() ? &it->second : nullptr;
	}

	void Debugger::addBreakPoint(u16 address)
	{
		m_data.breakpoints[address].enabled = true;
	}

	void Debugger::removeBreakPoint(u16 address)
	{
		auto it = m_data.breakpoints.find(address);
		if (it == m_data.breakpoints.end())
			return;
		if (it->second.debugBreak)
			it->second.enabled = false;
		else
			m_data.breakpoints.erase(it);
	}

	void Debugger::setBreakPointEnabled(u16 address, bool enabled)
	{
		auto it = m_data.breakpoints.find(address);
		if (it != m_data.breakpoints.end())
			it->second.enabled = enabled;
		else if (enabled)
			addBreakPoint(address);
	}

	void Debugger::clearBreakPoints(void)
	{
		for (auto it = m_data.breakpoints.begin(); it != m_data.breakpoints.end(); )
		{
			if (it->second.debugBreak)
			{
				it->second.enabled = false;
				++it;
			}
			else
				it = m_data.breakpoints.erase(it);
		}
	}

	bool Debugger::should_break_at(u16 ip) const
	{
		const tBreakPoint* bp = getBreakPoint(ip);
		if (bp == nullptr || !bp->enabled)
			return false;
		if (!bp->debugBreak)
			return true;
		const u8 opCode = DragonRuntime::memMap.peek8(ip);
		return opCode == data::OpCodes::DEBUG_Break || opCode == data::OpCodes::DEBUG_DumpRAM;
	}

	void Debugger::register_debug_breaks(void)
	{
		for (const auto& line : m_data.code)
		{
			String mnemonic = line.code.new_trim().toLower();
			if (mnemonic.contains(" "))
				mnemonic = mnemonic.new_substr(0, mnemonic.indexOf(" "));
			if (mnemonic == "debug_break" || mnemonic == "debug_dump_ram")
				m_data.breakpoints[cast<u16>(line.addr)] = { true, true };
		}
	}

	void Debugger::tracked_changed(void)
	{
		m_data.trackedVersion++;
		capture_info(m_currentInfo);
		capture_info(m_previousInfo);
		notify_state_changed();
	}

	void Debugger::track(u16 address, u16 size, const String& label, bool breakOnChange)
	{
		for (auto& item : m_data.tracked)
		{
			if (item.address == address)
			{
				item.size = std::max<u16>(size, 1);
				item.breakOnChange = item.breakOnChange || breakOnChange;
				if (label != "")
					item.label = label;
				m_data.trackedVersion++;
				return;
			}
		}
		m_data.tracked.push_back({ address, std::max<u16>(size, 1), (label != "" ? label : String::getHexStr(address, true, 2)), breakOnChange });
		std::sort(m_data.tracked.begin(), m_data.tracked.end(), [](const tTrackedData& a, const tTrackedData& b) { return a.address < b.address; });
		m_data.trackedVersion++;
	}

	void Debugger::untrackAll(void)
	{
		m_data.tracked.clear();
		tracked_changed();
	}

	bool Debugger::setWatch(u16 address, bool breakOnChange)
	{
		for (auto& item : m_data.tracked)
		{
			if (item.address != address)
				continue;
			item.breakOnChange = breakOnChange;
			tracked_changed();
			return true;
		}
		return false;
	}

	bool Debugger::untrack(u16 address)
	{
		for (auto it = m_data.tracked.begin(); it != m_data.tracked.end(); ++it)
		{
			if (it->address == address)
			{
				m_data.tracked.erase(it);
				tracked_changed();
				return true;
			}
		}
		return false;
	}

	bool Debugger::setRegister(const String& name, i64 value)
	{
		static const std::pair<const char*, u8> registers[] = {
			{ "ip", data::Registers::IP }, { "sp", data::Registers::SP }, { "fp", data::Registers::FP },
			{ "rv", data::Registers::RV }, { "pp", data::Registers::PP }, { "fl", data::Registers::FL },
			{ "acc", data::Registers::ACC }, { "s1", data::Registers::S1 }, { "s2", data::Registers::S2 },
			{ "of", data::Registers::OFFSET }, { "offset", data::Registers::OFFSET },
			{ "r1", data::Registers::R1 }, { "r2", data::Registers::R2 }, { "r3", data::Registers::R3 },
			{ "r4", data::Registers::R4 }, { "r5", data::Registers::R5 }, { "r6", data::Registers::R6 },
			{ "r7", data::Registers::R7 }, { "r8", data::Registers::R8 }, { "r9", data::Registers::R9 },
			{ "r10", data::Registers::R10 }
		};
		const String reg = name.new_trim().toLower();
		for (const auto& entry : registers)
		{
			if (reg == entry.first)
			{
				DragonRuntime::cpu.writeRegister16(entry.second, cast<i16>(value & 0xFFFF));
				commitEdit();
				return true;
			}
		}
		return false;
	}

	bool Debugger::writeMemory(u16 address, u64 value, u8 size)
	{
		// Big endian, like the machine.
		bool ok = true;
		for (u8 i = 0; i < size; i++)
			ok = DragonRuntime::memMap.poke8(cast<u16>(address + i), cast<u8>((value >> (8 * (size - 1 - i))) & 0xFF)) && ok;
		commitEdit();
		return ok;
	}

	void Debugger::process_errors(void)
	{
		if (!dragon::DragonRuntime::hasError()) return;
		while (dragon::data::ErrorHandler::hasError())
		{
			auto err = dragon::data::ErrorHandler::popError();
			console().fg(ostd::ConsoleColors::Red).p("Error ").p(String::getHexStr(err.code, true, 8).cpp_str()).p(": ").p(err.text.cpp_str()).reset().nl();
		}
	}

	i64 Debugger::bus_state_offset(u16 address) const
	{
		for (const auto& region : DragonRuntime::memMap.getRegions())
		{
			if (address < region.startAddress || address > region.endAddress)
				continue;
			auto it = m_deviceSegments.find(region.device);
			if (it == m_deviceSegments.end())
				return -1;
			const u32 finalAddr = (region.remap ? address - region.startAddress : address);
			return m_history.getStateOffset(it->second, finalAddr);
		}
		return -1;
	}

	void Debugger::notify_state_changed(void)
	{
		if (m_onStateChanged)
			m_onStateChanged();
	}

	void Debugger::show_view(eView view, i64 argument)
	{
		if (m_onShowView)
			m_onShowView(view, argument);
	}




	//Commands
	void Debugger::command(const String& commandLine)
	{
		auto& out = console();
		String cmd = commandLine.new_trim();
		String args = "";
		String name = cmd.new_toLower();
		if (cmd.contains(" "))
		{
			name = cmd.new_substr(0, cmd.indexOf(" ")).toLower();
			args = cmd.new_substr(cmd.indexOf(" ") + 1).trim();
		}
		out.fg(ostd::ConsoleColors::Magenta).p(" (ddb) #/> ").fg(ostd::ConsoleColors::White).p(cmd).reset().nl();
		auto l_count = [&](u32 fallback) -> u32 {
			i64 value = 0;
			if (args == "" || !Utils::parseValue(args, value) || value < 1)
				return fallback;
			return cast<u32>(value);
		};

		if (name == "")
			stepInto(1);
		else if (name == "q" || name == "quit" || name == InputCommandQuit)
		{
			if (m_onQuit)
				m_onQuit();
		}
		else if (name == "h" || name == "help")
			Display::printHelp(out);
		else if (name == "c" || name == "continue" || name == "r" || name == "run")
			resume();
		else if (name == "pause" || name == "stop")
			pause();
		else if (name == "step" || name == "si")
			stepInto(l_count(1));
		else if (name == "n" || name == "next")
			stepOver();
		else if (name == "finish" || name == "out")
			stepOut();
		else if (name == "back" || name == "sb")
			stepBack(l_count(1));
		else if (name == "rc" || name == "rcontinue")
			reverseContinue();
		else if (name == "until" || name == "runto")
		{
			u16 addr = 0;
			if (Utils::parseAddress(*this, args, addr))
				runTo(addr);
			else
				out.fg(ostd::ConsoleColors::Red).p("Invalid value for <until> command.").reset().nl();
		}
		else if (name == "goto" || name == "seek")
		{
			i64 cycle = 0;
			if (!m_historyEnabled)
				out.fg(ostd::ConsoleColors::Red).p("History recording is disabled.").reset().nl();
			else if (!Utils::parseValue(args, cycle) || cycle < 0)
				out.fg(ostd::ConsoleColors::Red).p("Invalid value for <goto> command.").reset().nl();
			else
			{
				// The first position at (or after) the cycle.
				u64 lo = m_history.getFirstPosition(), hi = m_history.getLastPosition();
				while (lo < hi)
				{
					const u64 mid = lo + (hi - lo) / 2;
					if (m_history.getCycleAt(mid) < cast<u64>(cycle))
						lo = mid + 1;
					else
						hi = mid;
				}
				seek(lo);
			}
		}
		else if (name == "p" || name == "print")
			exec_print_command(args);
		else if (name == "b" || name == "break")
			exec_break_command(args);
		else if (name == "bl" || name == "breakpoints")
		{
			Display::printBreakpoints(out, *this);
			show_view(eView::Breakpoints);
		}
		else if (name == "bc" || name == "clear")
		{
			clearBreakPoints();
			out.fg(ostd::ConsoleColors::Yellow).p("All breakpoints removed (debug_break ones disabled).").reset().nl();
			notify_state_changed();
		}
		else if (name == "t" || name == "track" || name == "tracker")
		{
			if (args == "")
				show_view(eView::Tracker);
			else
				exec_track_command(args, false);
		}
		else if (name == "w" || name == "watch")
			exec_track_command(args, true);
		else if (name == "untrack")
		{
			u16 addr = 0;
			if (args == "all")
			{
				untrackAll();
				out.fg(ostd::ConsoleColors::Yellow).p("Nothing is tracked anymore.").reset().nl();
			}
			else if (Utils::parseAddress(*this, args, addr) && untrack(addr))
				out.fg(ostd::ConsoleColors::Yellow).p("Not tracking ").p(String::getHexStr(addr, true, 2)).p(" anymore.").reset().nl();
			else
				out.fg(ostd::ConsoleColors::Red).p("Not tracked: ").p(args).reset().nl();
			capture_info(m_currentInfo);
			capture_info(m_previousInfo);
			notify_state_changed();
		}
		else if (name == "d" || name == "diff-view")
			show_view(eView::Registers);
		else if (name == "s" || name == "stack")
		{
			i64 rows = 8;
			if (args != "" && (!Utils::parseValue(args, rows) || rows < 1 || rows > 0xFFFF / 16))
				rows = 8;
			show_view(eView::Stack, rows);
		}
		else if (name == "ca" || name == "call-stack")
			show_view(eView::CallTrace);
		else if (name == "x" || name == "mem")
		{
			u16 addr = 0;
			if (Utils::parseAddress(*this, args, addr))
				show_view(eView::Memory, addr);
			else
				out.fg(ostd::ConsoleColors::Red).p("Invalid value for <x> command.").reset().nl();
		}
		else if (name == "set")
		{
			auto tokens = args.tokenize().getRawData();
			i64 value = 0;
			if (tokens.size() != 2 || !Utils::parseValue(tokens[1], value))
				out.fg(ostd::ConsoleColors::Red).p("Usage: set <register> <value>").reset().nl();
			else if (!setRegister(tokens[0], value))
				out.fg(ostd::ConsoleColors::Red).p("Unknown register: ").p(tokens[0]).reset().nl();
			else
				out.fg(ostd::ConsoleColors::Yellow).p(tokens[0].new_toUpper()).p(" = ").p(String::getHexStr(cast<u16>(value), true, 2)).reset().nl();
		}
		else if (name == "display")
		{
			m_data.args.hide_virtual_display = !m_data.args.hide_virtual_display;
			out.p("Virtual Display Hidden = ").p(STR_BOOL(m_data.args.hide_virtual_display)).nl();
			notify_state_changed();
		}
		else if (name == "history")
		{
			if (args == "on")
				enableHistory(true);
			else if (args == "off")
				enableHistory(false);
			else if (args == "clear")
			{
				if (m_historyEnabled)
				{
					m_history.begin(m_cycle);
					m_callTrace.clear();
					m_callDepth = 0;
					out.fg(ostd::ConsoleColors::Yellow).p("History cleared.").reset().nl();
					notify_state_changed();
				}
			}
			else
				show_view(eView::History);
		}
		else if (name == "speed")
		{
			i64 rate = 0;
			if (args == "max")
				setMaxSpeed(true);
			else if (args == "" || args == "default")
			{
				m_clockRate = std::max<f64>(1.0, cast<f64>(DragonRuntime::machine_config.clock_rate_sec));
				setMaxSpeed(false);
			}
			else if (Utils::parseValue(args, rate) && rate > 0)
			{
				m_clockRate = cast<f64>(rate);
				setMaxSpeed(false);
			}
			out.fg(ostd::ConsoleColors::Yellow).p("Speed: ");
			if (m_maxSpeed)
				out.p("as fast as possible").reset().nl();
			else
				out.p(cast<u64>(m_clockRate)).p(" Hz").reset().nl();
			notify_state_changed();
		}
		else
			out.fg(ostd::ConsoleColors::Red).p("Unknown command: ").p(name).p(" (try 'help').").reset().nl();
	}

	void Debugger::exec_print_command(const String& commandArgs)
	{
		auto& out = console();
		String command = commandArgs;
		const u8 TYPE_STRING = 0;
		const u8 TYPE_BYTE = 1;
		const u8 TYPE_WORD = 2;
		const u8 TYPE_DWORD = 4;
		const u8 TYPE_QWORD = 8;
		u8 type = TYPE_WORD;
		if (command.contains(" "))
		{
			String size_str = command.new_substr(0, command.indexOf(" ")).trim().toLower();
			command.substr(command.indexOf(" ") + 1).trim();
			if (size_str == "byte") type = TYPE_BYTE;
			else if (size_str == "word") type = TYPE_WORD;
			else if (size_str == "dword") type = TYPE_DWORD;
			else if (size_str == "qword") type = TYPE_QWORD;
			else if (size_str == "string") type = TYPE_STRING;
		}
		i64 numeric = 0;
		if (Utils::parseValue(command, numeric))
		{
			if (type == TYPE_STRING)
				type = TYPE_WORD;
			u16 addr = cast<u16>(numeric);
			u16 end_addr = addr;
			String tmp = "";
			tmp.add("*(").add(String::getHexStr(addr, true, 2));
			if (type != TYPE_BYTE)
			{
				end_addr = addr + type - 1;
				if (end_addr < addr)
					end_addr = addr;
				else
					tmp.add("-").add(String::getHexStr(end_addr, true, 2));
			}
			tmp.add(")");
			ostd::RegexRichString rgx(tmp);
			rgx.fg("\\(|\\)|-", "darkgray");
			rgx.fg("\\*", "red");
			rgx.fg("0x[0-9A-Fa-f]+|0b[0-1]+|(?<!\\w)[0-9]+(?!\\w)", "cyan"); //Number Constants
			out.pStyled(rgx);
			out.fg(ostd::ConsoleColors::White).p(" = ");
			out.fg(ostd::ConsoleColors::Gray).p("[");
			for (u32 a = addr; a <= end_addr; a++)
			{
				u8 value = DragonRuntime::memMap.peek8(cast<u16>(a));
				out.fg(ostd::ConsoleColors::BrightRed).p(String::getHexStr(value, true, 1));
				if (a < end_addr)
					out.p(" ");
			}
			out.fg(ostd::ConsoleColors::Gray).p("]");
			out.reset().nl();
		}
		else if (command.startsWith("$"))
		{
			u16 size = 0;
			u16 addr = Utils::findSymbol(m_data.data, command, &size);
			if (addr == 0)
				addr = Utils::findSymbol(m_data.labels, command, &size);
			if (addr == 0)
				out.fg(ostd::ConsoleColors::Red).p("Unknown symbol for <print> command.").reset().nl();
			else
			{
				if (size == 0)
					size = (type == TYPE_STRING ? 1 : type);
				String tmp = "";
				tmp.add("*(").add(String::getHexStr(addr, true, 2));
				if (size > 1)
					tmp.add("-").add(String::getHexStr((u16)(addr + size - 1), true, 2));
				tmp.add(")");
				ostd::RegexRichString rgx(tmp);
				rgx.fg("\\(|\\)|-", "darkgray");
				rgx.fg("\\*", "red");
				rgx.fg("0x[0-9A-Fa-f]+|0b[0-1]+|(?<!\\w)[0-9]+(?!\\w)", "cyan"); //Number Constants
				out.pStyled(rgx);
				out.fg(ostd::ConsoleColors::White).p(" = ");
				out.fg(ostd::ConsoleColors::Gray).p("[");
				if (type == TYPE_STRING)
					out.fg(ostd::ConsoleColors::BrightRed).p("\"");
				for (u32 a = addr; a < cast<u32>(addr) + size; a++)
				{
					u8 value = DragonRuntime::memMap.peek8(cast<u16>(a));
					if (type == TYPE_STRING)
					{
						out.fg(ostd::ConsoleColors::BrightRed).pChar(std::isprint(value) ? (char)value : '.');
						continue;
					}
					out.fg(ostd::ConsoleColors::BrightRed).p(String::getHexStr(value, true, 1));
					if (a < cast<u32>(addr) + size - 1)
						out.p(" ");
				}
				if (type == TYPE_STRING)
					out.fg(ostd::ConsoleColors::BrightRed).p("\"");
				out.fg(ostd::ConsoleColors::Gray).p("]");
				out.reset().nl();
			}
		}
		else
		{
			out.fg(ostd::ConsoleColors::Red).p("Invalid value for <print> command.").reset().nl();
		}
	}

	void Debugger::exec_break_command(const String& commandArgs)
	{
		auto& out = console();
		u16 addr = 0;
		bool valid = false;
		i64 numeric = 0;
		if (Utils::parseValue(commandArgs, numeric))
		{
			addr = (u16)numeric;
			valid = true;
		}
		else if (commandArgs.startsWith("$"))
		{
			addr = Utils::findSymbol(m_data.labels, commandArgs);
			if (addr == 0x0000 || addr == 0xFFFF)
				out.fg(ostd::ConsoleColors::Red).p("Invalid symbol: ").p(commandArgs).reset().nl();
			else
				valid = true;
		}
		else
		{
			out.fg(ostd::ConsoleColors::Red).p("Invalid value for <break> command.").reset().nl();
		}
		if (valid)
		{
			const bool enabled = toggleBreakPoint(addr);
			const tBreakPoint* bp = getBreakPoint(addr);
			if (bp != nullptr && bp->debugBreak)
				out.fg(ostd::ConsoleColors::Yellow).p(enabled ? "debug_break enabled at address: " : "debug_break disabled at address: ").p(String::getHexStr(addr, true, 2)).reset().nl();
			else if (enabled)
				out.fg(ostd::ConsoleColors::Yellow).p("Breakpoint set at address: ").p(String::getHexStr(addr, true, 2)).reset().nl();
			else
				out.fg(ostd::ConsoleColors::Yellow).p("Breakpoint removed at address: ").p(String::getHexStr(addr, true, 2)).reset().nl();
			notify_state_changed();
		}
	}

	void Debugger::exec_track_command(const String& commandArgs, bool breakOnChange)
	{
		auto& out = console();
		auto tokens = commandArgs.tokenize();
		i32 count = 0;
		for (auto& token : tokens)
		{
			String item = token;
			u16 nbytes = 1;
			u16 addr = 0;
			// address[bytes]
			if (item.contains("[") && item.endsWith("]"))
			{
				String str_nbytes = item.new_substr(item.indexOf("[") + 1).trim();
				str_nbytes.substr(0, str_nbytes.len() - 1);
				item.substr(0, item.indexOf("[")).trim();
				i64 value = 1;
				if (Utils::parseValue(str_nbytes, value))
					nbytes = cast<u16>(std::max<i64>(1, value));
				if (!Utils::parseAddress(*this, item, addr))
					continue;
			}
			else if (!Utils::parseAddress(*this, item, addr, &nbytes))
			{
				out.fg(ostd::ConsoleColors::Red).p("Invalid address or symbol: ").p(item).reset().nl();
				continue;
			}
			String label = (item.startsWith("$") ? item : Utils::findSymbol(m_data.data, addr));
			track(addr, nbytes, label, breakOnChange);
			count++;
		}
		out.p(breakOnChange ? "Watching " : "Tracking ").p(count).p(count == 1 ? " item" : " items").p(" (").p(cast<i32>(m_data.tracked.size())).p(" tracked).").nl();
		tracked_changed();
	}
}
