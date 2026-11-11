#include "DragonRuntime.hpp"
#include "../tools/DRX.hpp"
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
		auto& out = output();
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
		auto& out = output();
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
					args.force_load_mem_offset = cast<u32>(edit.toInt());
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
		auto& out = output();
		s_signalListener.init();
		vKeyboard.init();
		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::Magenta).p("Loading machine config: ").fg(ostd::ConsoleColors::BrightYellow).p(info.configFilePath.cpp_str()).nl();
		machine_config = dragon::MachineConfigLoader::loadConfig(info.configFilePath);
		if (!machine_config.isValid()) return RETURN_VAL_INVALID_MACHINE_CONFIG; //TODO: Error

		if (machine_config.vdisk_paths.size() == 0) return RETURN_VAL_NO_DISK; //TODO: Error
		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::Magenta).p("  Initializing virtual disks:").nl();
		vDiskInterface.setBlockSize(machine_config.disk_block_size);
		for (auto const& disk_path : machine_config.vdisk_paths)
		{
			vDisks[disk_path.first] = dragon::hw::VirtualHardDrive(disk_path.second);
			vDiskInterface.connectDisk(vDisks[disk_path.first], disk_path.first);
			if (info.verboseLoad)
				out.fg(ostd::ConsoleColors::BrightYellow).p("    Disk").p(disk_path.first).p(" connected: ").p(disk_path.second.cpp_str()).nl();
		}
		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::BrightYellow).p("    Disk block size: ").p(vDiskInterface.getBlockSize()).p(" bytes per cycle").nl();

		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::Magenta).p("  Loading vBIOS file: ").fg(ostd::ConsoleColors::BrightYellow).p(machine_config.bios_path.cpp_str()).nl();
		vBIOS.init(machine_config.bios_path);

		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::Magenta).p("  Loading vCMOS file: ").fg(ostd::ConsoleColors::BrightYellow).p(machine_config.cmos_path.cpp_str()).nl();
		vCMOS.init(machine_config.cmos_path);

		const u32 ram_size = machine_config.ram_size_kb * 1024;
		const u32 ram_end = data::MemoryMapAddresses::Memory_Start + ram_size - 1;
		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::Magenta).p("  Initializing RAM: ").fg(ostd::ConsoleColors::BrightYellow).p(machine_config.ram_size_kb).p(" KiB").nl();
		ram.init(ram_size);
		memMap.clear();

		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("  Initializing Memory Mapper:").nl();
			out.fg(ostd::ConsoleColors::Magenta).p("    vBIOS: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::BIOS_Start, true, 4).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::BIOS_End, true, 4).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(vBIOS, dragon::data::MemoryMapAddresses::BIOS_Start, dragon::data::MemoryMapAddresses::BIOS_End, false, "BIOS");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vCMOS: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::CMOS_Start, true, 4).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::CMOS_End, true, 4).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vCMOS, dragon::data::MemoryMapAddresses::CMOS_Start, dragon::data::MemoryMapAddresses::CMOS_End, true, "CMOS");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    intVec: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::IntVector_Start, true, 4).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::IntVector_End, true, 4).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(intVec, dragon::data::MemoryMapAddresses::IntVector_Start, dragon::data::MemoryMapAddresses::IntVector_End, true, "intVec");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vKeyboard: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::Keyboard_Start, true, 4).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::Keyboard_End, true, 4).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(vKeyboard, dragon::data::MemoryMapAddresses::Keyboard_Start, dragon::data::MemoryMapAddresses::Keyboard_End, true, "Keyb.");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vMouse: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::Mouse_Start, true, 4).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::Mouse_End, true, 4).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(vMouse, dragon::data::MemoryMapAddresses::Mouse_Start, dragon::data::MemoryMapAddresses::Mouse_End, false, "Mouse");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vTimer: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::Timer_Start, true, 4).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::Timer_End, true, 4).cpp_str());
			out.p(" (remap=true)").nl();
		}
		vTimer.init();
		vTimer.setTimeSource(hw::VirtualTimer::eTimeSource::WallClock, machine_config.clock_rate_sec);
		memMap.mapDevice(vTimer, dragon::data::MemoryMapAddresses::Timer_Start, dragon::data::MemoryMapAddresses::Timer_End, true, "Timer");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vDiskInterface: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::DiskInterface_Start, true, 4).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::DiskInterface_End, true, 4).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vDiskInterface, dragon::data::MemoryMapAddresses::DiskInterface_Start, dragon::data::MemoryMapAddresses::DiskInterface_End, true, "Disk");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vGraphicsInterface: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::VideoCardInterface_Start, true, 4).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::VideoCardInterface_End, true, 4).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vGraphicsInterface, dragon::data::MemoryMapAddresses::VideoCardInterface_Start, dragon::data::MemoryMapAddresses::VideoCardInterface_End, true, "VGA");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    vSerialInterface: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::SerialInterface_Start, true, 4).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::SerialInterface_End, true, 4).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vSerialInterface, dragon::data::MemoryMapAddresses::SerialInterface_Start, dragon::data::MemoryMapAddresses::SerialInterface_End, true, "serial");
		if (info.verboseLoad)
		{
			out.fg(ostd::ConsoleColors::Magenta).p("    RAM: ");
			out.fg(ostd::ConsoleColors::BrightYellow);
			out.p(String::getHexStr(dragon::data::MemoryMapAddresses::Memory_Start, true, 4).cpp_str());
			out.p(" to ");
			out.p(String::getHexStr(ram_end, true, 4).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(ram, dragon::data::MemoryMapAddresses::Memory_Start, ram_end, true, "RAM");

		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::Magenta).p("  Initializing vCPU reset sequence:").nl();

		u32 reset_ip_addr = 0x00000000;
		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::BrightYellow).p("    Reset IP register: ").p(String::getHexStr(reset_ip_addr, true, 4).cpp_str()).nl();
		cpu.writeRegister32(dragon::data::Registers::IP, cast<i32>(reset_ip_addr));

		// The stack grows down from the last word of the RAM.
		const u32 stack_top = ram_end + 1 - data::WordSize;
		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::BrightYellow).p("    Stack top: ").p(String::getHexStr(stack_top, true, 4).cpp_str()).nl();
		cpu.setStackTop(stack_top);

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

		vCMOS.write32(data::CMOSRegisters::MemoryStart, cast<i32>(data::MemoryMapAddresses::Memory_Start));
		vCMOS.write32(data::CMOSRegisters::MemorySize, cast<i32>(ram_size));
		vCMOS.write32(data::CMOSRegisters::ClockSpeed, machine_config.clock_rate_sec);
		vCMOS.write8(data::CMOSRegisters::ScreenRedrawRate, machine_config.screen_redraw_rate_per_second);
		vCMOS.write32(data::CMOSRegisters::ScreenWidth, static_cast<i32>(hw::VirtualDisplay::Columns));
		vCMOS.write32(data::CMOSRegisters::ScreenHeight, static_cast<i32>(hw::VirtualDisplay::Rows));
		vCMOS.write16(data::CMOSRegisters::SystemSignals, 0);	// (2 bytes: HeapStart follows)
		vCMOS.write32(data::CMOSRegisters::StackSize, cast<i32>(std::min<u32>(data::DefaultValues::DefaultStackSize, ram_size / 2)));
		vDiskInterface.writeBlockSize();
		ostd::BitField_32 disk_list_bitfield;
		disk_list_bitfield.value = 0;
		for (i32 i = 0; i < 32; i++)
		{
			if (vDisks.count(i) > 0)
				ostd::Bits::set(disk_list_bitfield, i);
		}
		vCMOS.write32(data::CMOSRegisters::DiskList, cast<i32>(disk_list_bitfield.value));
		if (info.verboseLoad)
			out.fg(ostd::ConsoleColors::BrightYellow).p("    Loading CMOS Machine info").nl();

		out.nl().nl();
		return RETURN_VAL_EXIT_SUCCESS;
	}

	void DragonRuntime::resetMachine(void)
	{
		cpu.reset();
		ram.init(ram.getSize());		// zeroed, every page marked as written
		intVec.reset();
		vGraphicsInterface.reset();
		vKeyboard.resetState();
		vTimer.reset();
		vDiskInterface.reset();
		vCMOS.write16(data::CMOSRegisters::SystemSignals, 0);
		s_resetCount++;
	}

	void DragonRuntime::shutdownMachine(void)
	{
		machine_config.destroy();
	}

	void DragonRuntime::forceLoad(const String& filePath, u32 loadAddress)
	{
		// loadAddress is an offset in the RAM. DRX programs (dsc's output) are relocated there.
		ostd::ByteStream code;
		ostd::Memory::loadByteStreamFromFile(filePath, code);
		std::vector<uint8_t> bytes(code.begin(), code.end());
		if (drx::isDRX(bytes))
		{
			drx::Program program;
			std::string message;
			if (!drx::read(bytes, program, message))
			{
				output().fg(ostd::ConsoleColors::Red).p("Invalid DRX file: ").p(filePath).p(" (").p(message).p(")").reset().nl();
				return;
			}
			const u32 base = data::MemoryMapAddresses::Memory_Start + loadAddress;
			bytes = drx::relocate(program, base);
			output().fg(ostd::ConsoleColors::BrightYellow).p("DRX program loaded at ").p(String::getHexStr(base, true, 4)).p(", entry ")
				.p(String::getHexStr(base + program.entry, true, 4)).p(" (").p((i32)program.relocations.size()).p(" relocations)").reset().nl();
		}
		u32 index = 0;
		for (auto& b : bytes)
		{
			ram.write8(loadAddress + index, cast<i8>(b));
			index++;
		}
	}

	void DragonRuntime::__print_application_help(void)
	{
		auto& out = output();
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
