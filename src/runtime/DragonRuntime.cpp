#include "DragonRuntime.hpp"
#include <ogfx/render/PixelRenderer.hpp>
#include <ostd/io/Memory.hpp>
#include <ostd/utils/Time.hpp>

namespace dragon
{
	void DragonRuntime::SignalListener::init(void)
	{
		setTypeName("dragon::DragonRuntime::SignalListener");
		validate();
		enableSignals();
		connectSignal(Signal_HardwareInterruptOccurred);
	}

	void DragonRuntime::SignalListener::handleSignal(ostd::Signal& signal)
	{
		if (signal.ID == Signal_HardwareInterruptOccurred)
		{
		}
	}




	void DragonRuntime::processErrors(void)
	{
		while (dragon::data::ErrorHandler::hasError())
		{
			auto err = dragon::data::ErrorHandler::popError();
			out.nl().fg(ostd::ConsoleColors::Red).p("Error ").p(String::getHexStr(err.code, true, 8).cpp_str()).p(": ").p(err.text.cpp_str()).nl();
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

	i32 DragonRuntime::loadArguments(int argc, char** argv, tCommandLineArgs& args)
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
			for (i32 i = 2; i < argc; i++)
			{
				String edit(argv[i]);
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
					args.force_load_mem_offset = (u16)edit.toInt();
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

	i32 DragonRuntime::initMachine(const tRuntimeInitInfo& info)
	{
		s_signalListener.init();
		vKeyboard.init();
		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::Magenta).p("Loading machine config: ").fg(ostd::ConsoleColors::BrightYellow).p(info.configFilePath.cpp_str()).nl();
		machine_config = dragon::MachineConfigLoader::loadConfig(info.configFilePath);
		if (!machine_config.isValid()) return RETURN_VAL_INVALID_MACHINE_CONFIG; //TODO: Error

		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::Magenta).p("  Initializing virtual display:").nl();
		i32 w = ogfx::PixelRenderer::TextRenderer::CONSOLE_CHARS_H * ogfx::PixelRenderer::TextRenderer::FONT_CHAR_W; //60 * 16;
		i32 h = ogfx::PixelRenderer::TextRenderer::CONSOLE_CHARS_V * ogfx::PixelRenderer::TextRenderer::FONT_CHAR_H; //60 * 9;
		vDisplay.initialize(w, h, "DragonVM");
		vDisplay.setFont("font.bmp");
		if (info.hideVirtualDisplay)
			vDisplay.hide();
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p("    Done. (").p(w).p("x").p(h).p(")");
			if (info.hideVirtualDisplay)
				out.p(" - HIDDEN").nl();
			else
				out.nl();
		}

		if (machine_config.vdisk_paths.size() == 0) return RETURN_VAL_NO_DISK; //TODO: Error
		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::Magenta).p("  Initializing virtual disks:").nl();
		for (auto const& disk_path : machine_config.vdisk_paths)
		{
			vDisks[disk_path.first] = dragon::hw::VirtualHardDrive(disk_path.second);
			vDiskInterface.connectDisk(vDisks[disk_path.first], disk_path.first);
			if (info.verboseLoad)
				out.fg(ostd::ConsoleColors::BrightYellow).p("    Disk").p(disk_path.first).p(" connected: ").p(disk_path.second.cpp_str()).nl();
		}

		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::Magenta).p("  Loading vBIOS file: ").fg(ostd::ConsoleColors::BrightYellow).p(machine_config.bios_path.cpp_str()).nl();
		vBIOS.init(machine_config.bios_path);

		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::Magenta).p("  Loading vCMOS file: ").fg(ostd::ConsoleColors::BrightYellow).p(machine_config.cmos_path.cpp_str()).nl();
		vCMOS.init(machine_config.cmos_path);

		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("  Initializing Memory Mapper:").nl();
			out.fg(ostd::ConsoleColors::Magenta).p("    vBIOS: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::BIOS_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::BIOS_End, true, 2).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(vBIOS, dragon::data::MemoryMapAddresses::BIOS_Start, dragon::data::MemoryMapAddresses::BIOS_End, false, "BIOS");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vCMOS: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::CMOS_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::CMOS_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vCMOS, dragon::data::MemoryMapAddresses::CMOS_Start, dragon::data::MemoryMapAddresses::CMOS_End, true, "CMOS");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    intVec: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::IntVector_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::IntVector_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(intVec, dragon::data::MemoryMapAddresses::IntVector_Start, dragon::data::MemoryMapAddresses::IntVector_End, true, "intVec");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vKeyboard: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::Keyboard_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::Keyboard_End, true, 2).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(vKeyboard, dragon::data::MemoryMapAddresses::Keyboard_Start, dragon::data::MemoryMapAddresses::Keyboard_End, true, "Keyb.");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vMouse: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::Mouse_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::Mouse_End, true, 2).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(vMouse, dragon::data::MemoryMapAddresses::Mouse_Start, dragon::data::MemoryMapAddresses::Mouse_End, false, "Mouse");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vMBR: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::MBR_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::MBR_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vMBR, dragon::data::MemoryMapAddresses::MBR_Start, dragon::data::MemoryMapAddresses::MBR_End, true, "MBR");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vDiskInterface: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::DiskInterface_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::DiskInterface_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vDiskInterface, dragon::data::MemoryMapAddresses::DiskInterface_Start, dragon::data::MemoryMapAddresses::DiskInterface_End, true, "Disk");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vGraphicsInterface: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::VideoCardInterface_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::VideoCardInterface_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vGraphicsInterface, dragon::data::MemoryMapAddresses::VideoCardInterface_Start, dragon::data::MemoryMapAddresses::VideoCardInterface_End, true, "VGA");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vSerialInterface: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::SerialInterface_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::SerialInterface_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vSerialInterface, dragon::data::MemoryMapAddresses::SerialInterface_Start, dragon::data::MemoryMapAddresses::SerialInterface_End, true, "serial");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    RAM: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::Memory_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::Memory_End, true, 2).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(ram, dragon::data::MemoryMapAddresses::Memory_Start, dragon::data::MemoryMapAddresses::Memory_End, false, "RAM");

		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::Magenta).p("  Initializing vCPU reset sequence:").nl();

		u16 reset_ip_addr = 0x0000;
		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::BrightYellow).p("    Reset IP register: ").p(String::getHexStr(reset_ip_addr, true, 2).cpp_str()).nl();
		cpu.writeRegister16(dragon::data::Registers::IP, reset_ip_addr);

		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::BrightYellow).p("    Debug mode enabled: ").p(STR_BOOL(info.debugModeEnabled)).nl();
		cpu.m_debugModeEnabled = info.debugModeEnabled;

		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::BrightYellow).p("    BIOS mode enabled").nl();
		cpu.m_biosMode = true;

		if (machine_config.cpuext_list.size() > 0)
		{
			if (info.verboseLoad)
				out.fg(ostd::ConsoleColors::BrightYellow).p("    Loading CPU Extensions").nl();
			for (auto& ext :  machine_config.cpuext_list)
			{
				cpu.m_extensions[ext.first] = ext.second;
				if (info.verboseLoad)
					out.fg(ostd::ConsoleColors::BrightYellow).p("        ").p(ext.first + 1).p(": ").p(ext.second->m_name).nl();
			}
		}

		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::BrightYellow).p("    Fixed clock enabled: ").p(STR_BOOL(machine_config.fixed_clock)).nl();
			if (machine_config.fixed_clock)
				out.fg(ostd::ConsoleColors::BrightYellow).p("    Clock speed: ").p(machine_config.clock_rate_sec).p(" Hz").nl();
			out.fg(ostd::ConsoleColors::BrightYellow).p("    Screen redraw rate: ").p((i32)machine_config.screen_redraw_rate_per_second).p(" Hz").nl();
		}

		vCMOS.write16(data::CMOSRegisters::MemoryStart, data::MemoryMapAddresses::Memory_Start);
		vCMOS.write16(data::CMOSRegisters::MemorySize, data::MemoryMapAddresses::Memory_End);
		vCMOS.write16(data::CMOSRegisters::ClockSpeed, machine_config.clock_rate_sec);
		vCMOS.write8(data::CMOSRegisters::ScreenRedrawRate, machine_config.screen_redraw_rate_per_second);
		vCMOS.write16(data::CMOSRegisters::ScreenWidth, static_cast<i16>(ogfx::PixelRenderer::TextRenderer::CONSOLE_CHARS_H));
		vCMOS.write16(data::CMOSRegisters::ScreenHeight, static_cast<i16>(ogfx::PixelRenderer::TextRenderer::CONSOLE_CHARS_V));
		vCMOS.write16(data::CMOSRegisters::StackSize, 0x1000);
		ostd::BitField_16 disk_list_bitfield;
		disk_list_bitfield.value = 0;
		for (i32 i = 0; i < 16; i++)
		{
			if (vDisks.count(i) > 0)
				ostd::Bits::set(disk_list_bitfield, i);
		}
		vCMOS.write16(data::CMOSRegisters::DiskList, disk_list_bitfield.value);
		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::BrightYellow).p("    Loading CMOS Machine info").nl();

		out.nl().nl();
		return RETURN_VAL_EXIT_SUCCESS;
	}

	void DragonRuntime::shutdownMachine(void)
	{
		machine_config.destroy();
	}

	void DragonRuntime::runMachine2(void)
	{
		f64 clock_speed_us = 1000000.0 / machine_config.clock_rate_sec;
		f64 acc = 0;
		f64 acc2 = 0;
		u64 avg_count = 0;
		u64 _time = 0;
		f64 avg_tot = 0;
		ostd::Counter clock_timer;
		bool running = true;
		bool fixed_clock = machine_config.fixed_clock;
		ostd::Counter _timer;
		while (running || vDiskInterface.isBusy())
		{
			clock_timer.startCount(ostd::eTimeUnits::Microseconds);
			u8 screenRedrawRate = vCMOS.read8(data::CMOSRegisters::ScreenRedrawRate);
			running = cpu.execute() && vDisplay.isRunning();
			vDisplay.mainLoop();
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
				s_avgInstTime = (u64)std::round(avg_tot / avg_count);
				acc = 0;
			}
			if (acc2 == (1000.0 / screenRedrawRate))
			{
				vDisplay.redrawScreen();
				acc2 = 0;
			}
			_time = clock_timer.endCount();
			acc++;
			acc2++;
			if (_time < clock_speed_us && fixed_clock)
				ostd::Time::sleep(clock_speed_us - _time, ostd::eTimeUnits::Microseconds);
		}
	}

	void DragonRuntime::runMachine(void)
	{
		bool running = true;
		ostd::StepTimer cycleTimer(machine_config.clock_rate_sec, [&](f64 dt) {
			running = runStep();
		});
		while (running)
		{
			cycleTimer.update();
		}
	}

	bool DragonRuntime::runStep(void)
	{
		bool running = cpu.execute() && vDisplay.isRunning();
		u8 screenRedrawRate = vCMOS.read8(data::CMOSRegisters::ScreenRedrawRate);
		vDisplay.mainLoop();
		if (s_enableScreenRedrawDelay && s_stepAcc2 == (1000.0 / screenRedrawRate))
		{
			vDisplay.redrawScreen();
			s_stepAcc2 = 0;
		}
		else if (!s_enableScreenRedrawDelay)
		{
			vDisplay.redrawScreen();
		}
		s_stepAcc2++;
		vDiskInterface.cycleStep();
		return running || vDiskInterface.isBusy();
	}

	void DragonRuntime::forceLoad(const String& filePath, u16 loadAddress)
	{
		ostd::ByteStream code;
		ostd::Memory::loadByteStreamFromFile(filePath, code);

		i16 index = 0;
		for (auto& b : code)
		{
			ram.write8(dragon::data::MemoryMapAddresses::Memory_Start + loadAddress + index, b);
			index++;
		}
	}

	void DragonRuntime::__print_application_help(void)
	{
		i32 commandLength = 46;

		out.nl().fg(ostd::ConsoleColors::Yellow).p("List of available parameters:").reset().nl();
		String tmpCommand = "--verbose-load";
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
