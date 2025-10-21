#include "DragonRuntime.hpp"
#include <ostd/Defines.hpp>
#include "../gui/RawTextRenderer.hpp"

namespace dragon
{
	void DragonRuntime::SignalListener::init(void)
	{
		setTypeName("dragon::DragonRuntime::SignalListener");
		validate();
		enableSignals();
		connectSignal(Signal_HardwareInterruptOccurred);
	}

	void DragonRuntime::SignalListener::handleSignal(ostd::tSignal& signal)
	{
		if (signal.ID == Signal_HardwareInterruptOccurred)
		{
			tCallInfo& interruptData = (tCallInfo&)signal.userData;
			DragonRuntime::s_machineInfo.callStack.push_back(interruptData);
		}
	}




	void DragonRuntime::printRegisters(dragon::hw::VirtualCPU& cpu)
	{
		out.fg("green").p("IP:  ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::IP), true, 2).cpp_str());
		out.p("      ");
		out.fg("yellow").p("R1:  ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::R1), true, 2).cpp_str());
		out.p("      ");
		out.fg("yellow").p("R2:  ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::R2), true, 2).cpp_str()).nl();

		out.fg("green").p("SP:  ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::SP), true, 2).cpp_str());
		out.p("      ");
		out.fg("yellow").p("R3:  ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::R3), true, 2).cpp_str());
		out.p("      ");
		out.fg("yellow").p("R4:  ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::R4), true, 2).cpp_str()).nl();

		out.fg("green").p("FP:  ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::FP), true, 2).cpp_str());
		out.p("      ");
		out.fg("yellow").p("R5:  ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::R5), true, 2).cpp_str());
		out.p("      ");
		out.fg("yellow").p("R6:  ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::R6), true, 2).cpp_str()).nl();

		out.fg("green").p("RV:  ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::RV), true, 2).cpp_str());
		out.p("      ");
		out.fg("yellow").p("R7:  ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::R7), true, 2).cpp_str());
		out.p("      ");
		out.fg("yellow").p("R8:  ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::R8), true, 2).cpp_str()).nl();

		out.fg("green").p("PP:  ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::PP), true, 2).cpp_str());
		out.p("      ");
		out.fg("yellow").p("R9:  ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::R9), true, 2).cpp_str());
		out.p("      ");
		out.fg("yellow").p("R10: ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::R10), true, 2).cpp_str()).nl();

		out.fg("green").p("ACC: ").fg("white").p(ostd::Utils::getHexStr(cpu.readRegister(dragon::data::Registers::ACC), true, 2).cpp_str());
		out.p("      ");
		out.fg("yellow").p("FL:  ").fg("white").p(ostd::Utils::getBinStr(cpu.readRegister(dragon::data::Registers::FL), true, 2).cpp_str());
	}

	void DragonRuntime::processErrors(void)
	{
		while (dragon::data::ErrorHandler::hasError())
		{
			auto err = dragon::data::ErrorHandler::popError();
			out.nl().fg(ostd::ConsoleColors::Red).p("Error ").p(ostd::Utils::getHexStr(err.code, true, 8).cpp_str()).p(": ").p(err.text.cpp_str()).nl();
		}
	}

	std::vector<data::ErrorHandler::tError> DragonRuntime::getErrorList(void)
	{
		std::vector<data::ErrorHandler::tError> list;
		if (!hasError())
			return list;
		while (dragon::data::ErrorHandler::hasError())
		{
			auto err = dragon::data::ErrorHandler::popError();
			list.push_back(err);
		}
		return list;
	}

	int32_t DragonRuntime::loadArguments(int argc, char** argv, tCommandLineArgs& args)
	{
		if (argc < 2)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
			out.fg(ostd::ConsoleColors::Red).p("Use the --help option for more info.").reset().nl();
			return dragon::DragonRuntime::RETURN_VAL_TOO_FEW_ARGUMENTS;
		}
		else
		{
			args.machine_config_path = argv[1];
			if (args.machine_config_path == "--help")
			{
				__print_application_help();
				return DragonRuntime::RETURN_VAL_CLOSE_RUNTIME;
			}
			for (int32_t i = 2; i < argc; i++)
			{
				ostd::String edit(argv[i]);
				if (edit == "--verbose-load")
					args.verbose_load = true;
				else if (edit == "--force-load")
				{
					if ((argc - 1) - i < 2)
						return RETURN_VAL_MISSING_PARAM;
					i++;
					args.force_load_file = argv[i];
					i++;
					edit = argv[i];
					if (!edit.isNumeric())
						return RETURN_VAL_PARAMETER_NOT_NUMERIC;
					args.force_load_mem_offset = (uint16_t)edit.toInt();
					args.force_load = true;
				}
				else if (edit == "--help")
				{
					__print_application_help();
					return RETURN_VAL_CLOSE_RUNTIME;
				}
			}
		}
		return RETURN_VAL_EXIT_SUCCESS;
	}

	int32_t DragonRuntime::initMachine(const ostd::String& configFilePath,
										bool verbose,
										bool trackMachineInfoDiff,
										bool hideVirtualDisplay,
										bool trackCallStack,
										bool debugModeEnabled)
	{
		ostd::SignalHandler::init();
		s_signalListener.init();
		vKeyboard.init();
		if (verbose)
			out.fg(ostd::ConsoleColors::Magenta).p("Loading machine config: ").fg(ostd::ConsoleColors::BrightYellow).p(configFilePath.cpp_str()).nl();
		machine_config = dragon::MachineConfigLoader::loadConfig(configFilePath);
		if (!machine_config.isValid()) return RETURN_VAL_INVALID_MACHINE_CONFIG; //TODO: Error

		if (verbose)
			out.fg(ostd::ConsoleColors::Magenta).p("  Initializing virtual display:").nl();
		int32_t w = RawTextRenderer::CONSOLE_CHARS_H * RawTextRenderer::FONT_CHAR_W; //60 * 16;
		int32_t h = RawTextRenderer::CONSOLE_CHARS_V * RawTextRenderer::FONT_CHAR_H; //60 * 9;
		vDisplay.initialize(w, h, "DragonVM", "font.bmp");
		if (hideVirtualDisplay)
			vDisplay.hide();
		if (verbose)
		{
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p("    Done. (").p(w).p("x").p(h).p(")");
			if (hideVirtualDisplay)
				out.p(" - HIDDEN").nl();
			else
				out.nl();
		}

		if (machine_config.vdisk_paths.size() == 0) return RETURN_VAL_NO_DISK; //TODO: Error
		if (verbose)
			out.fg(ostd::ConsoleColors::Magenta).p("  Initializing virtual disks:").nl();
		for (auto const& disk_path : machine_config.vdisk_paths)
		{
			vDisks[disk_path.first] = dragon::hw::VirtualHardDrive(disk_path.second);
			vDiskInterface.connectDisk(vDisks[disk_path.first], disk_path.first);
			if (verbose)
				out.fg(ostd::ConsoleColors::BrightYellow).p("    Disk").p(disk_path.first).p(" connected: ").p(disk_path.second.cpp_str()).nl();
		}

		if (verbose)
			out.fg(ostd::ConsoleColors::Magenta).p("  Loading vBIOS file: ").fg(ostd::ConsoleColors::BrightYellow).p(machine_config.bios_path.cpp_str()).nl();
		vBIOS.init(machine_config.bios_path);

		if (verbose)
			out.fg(ostd::ConsoleColors::Magenta).p("  Loading vCMOS file: ").fg(ostd::ConsoleColors::BrightYellow).p(machine_config.cmos_path.cpp_str()).nl();
		vCMOS.init(machine_config.cmos_path);

		if (verbose)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("  Initializing Memory Mapper:").nl();
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p("    vBIOS: ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::BIOS_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::BIOS_End, true, 2).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(vBIOS, dragon::data::MemoryMapAddresses::BIOS_Start, dragon::data::MemoryMapAddresses::BIOS_End, false, "BIOS");
		if (verbose)
		{
			out.fg(ostd::ConsoleColors::Magenta).fg(ostd::ConsoleColors::Magenta).p("    vCMOS: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::CMOS_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::CMOS_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vCMOS, dragon::data::MemoryMapAddresses::CMOS_Start, dragon::data::MemoryMapAddresses::CMOS_End, true, "CMOS");
		if (verbose)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    intVec: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::IntVector_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::IntVector_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(intVec, dragon::data::MemoryMapAddresses::IntVector_Start, dragon::data::MemoryMapAddresses::IntVector_End, true, "intVec");
		if (verbose)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vKeyboard: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::Keyboard_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::Keyboard_End, true, 2).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(vKeyboard, dragon::data::MemoryMapAddresses::Keyboard_Start, dragon::data::MemoryMapAddresses::Keyboard_End, true, "Keyb.");
		if (verbose)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vMouse: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::Mouse_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::Mouse_End, true, 2).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(vMouse, dragon::data::MemoryMapAddresses::Mouse_Start, dragon::data::MemoryMapAddresses::Mouse_End, false, "Mouse");
		if (verbose)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vMBR: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::MBR_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::MBR_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vMBR, dragon::data::MemoryMapAddresses::MBR_Start, dragon::data::MemoryMapAddresses::MBR_End, true, "MBR");
		if (verbose)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vDiskInterface: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::DiskInterface_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::DiskInterface_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vDiskInterface, dragon::data::MemoryMapAddresses::DiskInterface_Start, dragon::data::MemoryMapAddresses::DiskInterface_End, true, "Disk");
		if (verbose)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vGraphicsInterface: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::VideoCardInterface_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::VideoCardInterface_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vGraphicsInterface, dragon::data::MemoryMapAddresses::VideoCardInterface_Start, dragon::data::MemoryMapAddresses::VideoCardInterface_End, true, "VGA");
		if (verbose)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vSerialInterface: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::SerialInterface_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::SerialInterface_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vSerialInterface, dragon::data::MemoryMapAddresses::SerialInterface_Start, dragon::data::MemoryMapAddresses::SerialInterface_End, true, "serial");
		if (verbose)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    RAM: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::Memory_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::Memory_End, true, 2).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(ram, dragon::data::MemoryMapAddresses::Memory_Start, dragon::data::MemoryMapAddresses::Memory_End, false, "RAM");

		if (verbose)
			out.fg(ostd::ConsoleColors::Magenta).p("  Initializing vCPU reset sequence:").nl();

		uint16_t reset_ip_addr = 0x0000;
		if (verbose)
			out.fg(ostd::ConsoleColors::BrightYellow).p("    Reset IP register: ").p(ostd::Utils::getHexStr(reset_ip_addr, true, 2).cpp_str()).nl();
		cpu.writeRegister16(dragon::data::Registers::IP, reset_ip_addr);

		if (verbose)
			out.fg(ostd::ConsoleColors::BrightYellow).p("    Debug mode enabled: ").p(STR_BOOL(debugModeEnabled)).nl();
		cpu.m_debugModeEnabled = debugModeEnabled;

		if (verbose)
			out.fg(ostd::ConsoleColors::BrightYellow).p("    BIOS mode enabled").nl();
		cpu.m_biosMode = true;

		if (machine_config.cpuext_list.size() > 0)
		{
			if (verbose)
				out.fg(ostd::ConsoleColors::BrightYellow).p("    Loading CPU Extensions").nl();
			for (auto& ext :  machine_config.cpuext_list)
			{
				cpu.m_extensions[ext.first] = ext.second;
				if (verbose)
					out.fg(ostd::ConsoleColors::BrightYellow).p("        ").p(ext.first + 1).p(": ").p(ext.second->m_name).nl();
			}
		}

		if (verbose)
		{
			out.fg(ostd::ConsoleColors::BrightYellow).p("    Fixed clock enabled: ").p(STR_BOOL(machine_config.fixed_clock)).nl();
			if (machine_config.fixed_clock)
				out.fg(ostd::ConsoleColors::BrightYellow).p("    Clock speed: ").p(machine_config.clock_rate_sec).p(" Hz").nl();
			out.fg(ostd::ConsoleColors::BrightYellow).p("    Screen redraw rate: ").p((int32_t)machine_config.screen_redraw_rate_per_second).p(" Hz").nl();
		}

		vCMOS.write16(data::CMOSRegisters::MemoryStart, data::MemoryMapAddresses::Memory_Start);
		vCMOS.write16(data::CMOSRegisters::MemorySize, data::MemoryMapAddresses::Memory_End);
		vCMOS.write16(data::CMOSRegisters::ClockSpeed, machine_config.clock_rate_sec);
		vCMOS.write8(data::CMOSRegisters::ScreenRedrawRate, machine_config.screen_redraw_rate_per_second);
		vCMOS.write16(data::CMOSRegisters::ScreenWidth, static_cast<int16_t>(RawTextRenderer::CONSOLE_CHARS_H));
		vCMOS.write16(data::CMOSRegisters::ScreenHeight, static_cast<int16_t>(RawTextRenderer::CONSOLE_CHARS_V));
		vCMOS.write16(data::CMOSRegisters::StackSize, 0x1000);
		ostd::BitField_16 disk_list_bitfield;
		disk_list_bitfield.value = 0;
		for (int32_t i = 0; i < 16; i++)
		{
			if (vDisks.count(i) > 0)
				ostd::Bits::set(disk_list_bitfield, i);
		}
		vCMOS.write16(data::CMOSRegisters::DiskList, disk_list_bitfield.value);
		if (verbose)
			out.fg(ostd::ConsoleColors::BrightYellow).p("    Loading CMOS Machine info").nl();




		out.nl().nl();
		s_trackMachineInfo = trackMachineInfoDiff;
		s_trackCallStack = trackCallStack;
		return RETURN_VAL_EXIT_SUCCESS;
	}

	void DragonRuntime::shutdownMachine(void)
	{
		machine_config.destroy();
	}

	void DragonRuntime::runMachine(void)
	{
		double clock_speed_us = 1000000.0 / machine_config.clock_rate_sec;
		double acc = 0;
		double acc2 = 0;
		uint64_t avg_count = 0;
		uint64_t _time = 0;
		double avg_tot = 0;
		ostd::Timer clock_timer;
		bool running = true;
		bool fixed_clock = machine_config.fixed_clock;
		ostd::Timer _timer;
		while (running || vDiskInterface.isBusy())
		{
			clock_timer.startCount(ostd::eTimeUnits::Microseconds);
			ostd::SignalHandler::refresh();
			uint16_t addr = cpu.readRegister(dragon::data::Registers::IP);
			uint16_t spAddr = cpu.readRegister(dragon::data::Registers::SP);
			uint8_t screenRedrawRate = vCMOS.read8(data::CMOSRegisters::ScreenRedrawRate);
			// _timer.start(true, "Profiling", ostd::eTimeUnits::Microseconds, &out);		
			running = cpu.execute() && vDisplay.isRunning();
			// _timer.end(true);
			vDisplay.update();
			vDiskInterface.cycleStep();
			if (dragon::data::ErrorHandler::hasError())
			{
				processErrors();
				break;		
			}
			if (acc == 500)
			{
				avg_count++;
				avg_tot += _time;
				s_avgInstTime = (uint64_t)std::round(avg_tot / avg_count);
				// out.fg(ostd::ConsoleColors::Red).p(getAvgClockSpeed()).nl().reset();
				acc = 0;
			}
			if (acc2 == (1000 / screenRedrawRate))
			{
				vDisplay.redrawScreen();
				acc2 = 0;
			}
			_time = clock_timer.endCount();
			acc++;
			acc2++;
			if (_time < clock_speed_us && fixed_clock)
				ostd::Utils::sleep(clock_speed_us - _time, ostd::eTimeUnits::Microseconds);
		}
	}

	bool DragonRuntime::runStep(std::vector<uint16_t> trackedAddresses)
	{
		std::sort(trackedAddresses.begin(), trackedAddresses.end());
		__get_machine_footprint(&s_machineInfo, trackedAddresses, true);
		__track_call_stack(&s_machineInfo);
		bool running = cpu.execute() && vDisplay.isRunning();
		uint8_t screenRedrawRate = vCMOS.read8(data::CMOSRegisters::ScreenRedrawRate);
		vDisplay.update();
		if (s_enableScreenRedrawDelay && s_stepAcc2 == (1000 / screenRedrawRate))
		{
			vDisplay.redrawScreen();
			s_stepAcc2 = 0;
		}
		else if (!s_enableScreenRedrawDelay)
		{
			vDisplay.redrawScreen();
		}
		s_stepAcc2++;
		// vDisplay.redrawScreen(); // This is slow...maybe it should be on a 100ms rate, like in normal runtime mode
		vDiskInterface.cycleStep();
		__get_machine_footprint(&s_machineInfo, trackedAddresses, false);
		return running || vDiskInterface.isBusy();
	}

	void DragonRuntime::forceLoad(const ostd::String& filePath, uint16_t loadAddress)
	{
		ostd::ByteStream code;
		ostd::Utils::loadByteStreamFromFile(filePath, code);

		int16_t index = 0;
		for (auto& b : code)
		{
			ram.write8(dragon::data::MemoryMapAddresses::Memory_Start + loadAddress + index, b);
			index++;
		}
	}


	void DragonRuntime::__get_machine_footprint(DragonRuntime::tMachineDebugInfo* machineInfo, std::vector<uint16_t> trackedAddresses, bool previous)
	{
		if (!s_trackMachineInfo || machineInfo == nullptr) return;
		auto& minfo = *machineInfo;

		if (previous)
		{
			minfo.vCPUHalt = cpu.m_halt;
			minfo.trackedAddresses.clear();
			minfo.previousInstructionTrackedValues.clear();
			minfo.currentInstructionTrackedValues.clear();

			for (int32_t i = 0; i < 20; i++)
			{
				if (i < 5)
				{
					minfo.previousInstructionFootprint[i] = 0;
					minfo.currentInstructionFootprint[i] = 0;
				}
				minfo.previousInstructionRegisters[i] = 0;
				minfo.currentInstructionRegisters[i] = 0;
			}

			for (auto& addr : trackedAddresses)
				minfo.trackedAddresses.push_back(addr);
		}

		uint16_t instAddr = cpu.readRegister(data::Registers::IP);
		uint8_t int_op_code = memMap.read8(instAddr);
		uint8_t instSize = data::OpCodes::getInstructionSIze(int_op_code);
		ostd::String opCode = data::OpCodes::getOpCodeString(int_op_code);
		uint16_t stackFrameSize = cpu.m_stackFrameSize;
		int32_t subRoutineCounter = cpu.m_subroutineCounter;

		bool debugBreak = cpu.m_isDebugBreakPoint;
		int32_t intHandlerCount = cpu.m_interruptHandlerCount;
		bool biosMode = cpu.m_biosMode;
		bool isInSubRoutine = cpu.isInSubRoutine();

		if (previous)
		{
			minfo.previousInstructionAddress = instAddr;
			minfo.previousInstructionFootprintSize = instSize;
			minfo.previousInstructionStackFrameSize = stackFrameSize;
			minfo.previousInstructionOpCode = opCode;
			minfo.previousSubRoutineCounter = subRoutineCounter;

			for (int8_t i = 0; i < instSize; i++)
				minfo.previousInstructionFootprint[i] = memMap.read8(instAddr + i);

			for (int8_t i = 0; i < 20; i++)
				minfo.previousInstructionRegisters[i] = cpu.readRegister(i);

			for (auto& addr : minfo.trackedAddresses)
				minfo.previousInstructionTrackedValues.push_back(memMap.read8(addr));

			minfo.previousInstructionDebugBreak = debugBreak;
			minfo.previousInstructionInterruptHandlerCount = intHandlerCount;
			minfo.previousInstructionBiosMode = biosMode;
			minfo.previousIsInSubRoutine = isInSubRoutine;
		}
		else
		{
			//if (int_op_code >= data::OpCodes::Ext01 && int_op_code <= data::OpCodes::Ext16)
			//	minfo.currentInstructionAddress = minfo.previousInstructionAddress;
			//else
				minfo.currentInstructionAddress = instAddr;
			minfo.currentInstructionFootprintSize = instSize;
			minfo.currentInstructionStackFrameSize = stackFrameSize;
			minfo.currentInstructionOpCode = opCode;
			minfo.currentSubRoutineCounter = subRoutineCounter;

			for (int8_t i = 0; i < instSize; i++)
				minfo.currentInstructionFootprint[i] = memMap.read8(minfo.currentInstructionAddress + i);

			for (int8_t i = 0; i < 20; i++)
				minfo.currentInstructionRegisters[i] = cpu.readRegister(i);

			for (auto& addr : minfo.trackedAddresses)
				minfo.currentInstructionTrackedValues.push_back(memMap.read8(addr));

			minfo.currentInstructionDebugBreak = debugBreak;
			minfo.currentInstructionInterruptHandlerCount = intHandlerCount;
			minfo.currentInstructionBiosMode = biosMode;
			minfo.currentIsInSubRoutine = isInSubRoutine;
		}
	}

	 void DragonRuntime::__track_call_stack(tMachineDebugInfo* machineInfo)
	 {
		if (!s_trackCallStack || machineInfo == nullptr) return;
		auto& minfo = *machineInfo;

		bool interrupts_enabled = cpu.readFlag(data::Flags::InterruptsEnabled);

		uint16_t instAddr = cpu.readRegister(data::Registers::IP);
		uint8_t inst = memMap.read8(instAddr);

		if (inst == data::OpCodes::CallImm)
		{
			uint16_t call_addr = memMap.read16(instAddr + 1);
			minfo.callStack.push_back({ "CALL IMM", call_addr, instAddr, !interrupts_enabled });
		}
		else if (inst == data::OpCodes::CallReg)
		{
			uint8_t reg_addr = memMap.read8(instAddr + 1);
			uint16_t call_addr = cpu.readRegister(reg_addr);
			minfo.callStack.push_back({ "CALL REG", call_addr, instAddr, !interrupts_enabled });
		}
		else if (interrupts_enabled && inst == data::OpCodes::Int)
		{
			uint8_t int_num = memMap.read8(instAddr + 1);
			minfo.callStack.push_back({ "INT", int_num, instAddr, !interrupts_enabled });
		}
		else if (inst == data::OpCodes::Ret)
		{
			minfo.callStack.push_back({ "RET", 0x0000, instAddr, !interrupts_enabled });
		}
		else if (interrupts_enabled && inst == data::OpCodes::RetInt)
		{
			minfo.callStack.push_back({ "RET INT", 0x0000, instAddr, !interrupts_enabled });
		}
	 }

	 void DragonRuntime::__print_application_help(void)
	 {
		int32_t commandLength = 46;

		out.nl().fg(ostd::ConsoleColors::Yellow).p("List of available parameters:").reset().nl();
		ostd::String tmpCommand = "--verbose-load";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to show more information while loading the virtual machine.").reset().nl();
		tmpCommand = "--force-load <binary-file> <ram-offset>";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Injects the specified binary into RAM at the specified offset.").reset().nl();
		tmpCommand = "--help";
		tmpCommand.addRightPadding(commandLength);
		out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Displays this help message.").reset().nl();
		
		out.nl().fg(ostd::ConsoleColors::Magenta).p("Usage: ./dvm <machine-config-file> [...options...]").reset().nl();
		out.nl();
	 }
}
