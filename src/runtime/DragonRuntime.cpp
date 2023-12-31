#include "DragonRuntime.hpp"
#include <ostd/Defines.hpp>
#include "../gui/RawTextRenderer.hpp"

namespace dragon
{
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

	int32_t DragonRuntime::initMachine(const ostd::String& configFilePath,
										bool verbose,
										bool trackMachineInfoDiff,
										bool hideVirtualDisplay,
										bool trackCallStack,
										bool debugModeEnabled)
	{
		if (verbose)
			out.p("Loading machine config: ").p(configFilePath.cpp_str()).nl();
		machine_config = dragon::MachineConfigLoader::loadConfig(configFilePath);
		if (!machine_config.isValid()) return 1; //TODO: Error

		if (verbose)
			out.p("  Initializing virtual display:").nl();
		vDisplay.initialize(800, 600, "DragonVM", "font.bmp");
		int32_t w = RawTextRenderer::CONSOLE_CHARS_H * RawTextRenderer::FONT_CHAR_W; //60 * 16;
		int32_t h = RawTextRenderer::CONSOLE_CHARS_V * RawTextRenderer::FONT_CHAR_H; //60 * 9;
		vDisplay.setSize(w, h);
		if (hideVirtualDisplay)
			vDisplay.hide();
		if (verbose)
		{
			out.p("    Done. (").p(w).p("x").p(h).p(")");
			if (hideVirtualDisplay)
				out.p(" - HIDDEN").nl();
			else
				out.nl();
		}

		if (machine_config.vdisk_paths.size() == 0) return 2; //TODO: Error
		if (verbose)
			out.p("  Initializing virtual disks:").nl();
		for (auto const& disk_path : machine_config.vdisk_paths)
		{
			vDisks[disk_path.first] = dragon::hw::VirtualHardDrive(disk_path.second);
			vDiskInterface.connectDisk(vDisks[disk_path.first]);
			if (verbose)
				out.p("    Disk").p(disk_path.first).p(" connected: ").p(disk_path.second.cpp_str()).nl();
		}

		if (verbose)
			out.p("  Loading vBIOS file: ").p(machine_config.bios_path.cpp_str()).nl();
		vBIOS.init(machine_config.bios_path);

		if (verbose)
			out.p("  Loading vCMOS file: ").p(machine_config.cmos_path.cpp_str()).nl();
		vCMOS.init(machine_config.cmos_path);

		if (verbose)
		{
			out.p("  Initializing Memory Mapper:").nl();
			out.p("    vBIOS: ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::BIOS_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::BIOS_End, true, 2).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(vBIOS, dragon::data::MemoryMapAddresses::BIOS_Start, dragon::data::MemoryMapAddresses::BIOS_End, false, "vBIOS");
		if (verbose)
		{
			out.p("    vCMOS: ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::CMOS_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::CMOS_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vCMOS, dragon::data::MemoryMapAddresses::CMOS_Start, dragon::data::MemoryMapAddresses::CMOS_End, true, "vCMOS");
		if (verbose)
		{
			out.p("    intVec: ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::IntVector_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::IntVector_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(intVec, dragon::data::MemoryMapAddresses::IntVector_Start, dragon::data::MemoryMapAddresses::IntVector_End, true, "intVec");
		if (verbose)
		{
			out.p("    vKeyboard: ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::Keyboard_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::Keyboard_End, true, 2).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(vKeyboard, dragon::data::MemoryMapAddresses::Keyboard_Start, dragon::data::MemoryMapAddresses::Keyboard_End, false, "vKeyboard");
		if (verbose)
		{
			out.p("    vMouse: ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::Mouse_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::Mouse_End, true, 2).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(vMouse, dragon::data::MemoryMapAddresses::Mouse_Start, dragon::data::MemoryMapAddresses::Mouse_End, false, "vMouse");
		if (verbose)
		{
			out.p("    vMBR: ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::MBR_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::MBR_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vMBR, dragon::data::MemoryMapAddresses::MBR_Start, dragon::data::MemoryMapAddresses::MBR_End, true, "vMBR");
		if (verbose)
		{
			out.p("    vDiskInterface: ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::DiskInterface_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::DiskInterface_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vDiskInterface, dragon::data::MemoryMapAddresses::DiskInterface_Start, dragon::data::MemoryMapAddresses::DiskInterface_End, true, "vDiskInterface");
		if (verbose)
		{
			out.p("    vGraphicsInterface: ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::VideoCardInterface_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::VideoCardInterface_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vGraphicsInterface, dragon::data::MemoryMapAddresses::VideoCardInterface_Start, dragon::data::MemoryMapAddresses::VideoCardInterface_End, true, "vGraphicsInterface");
		if (verbose)
		{
			out.p("    vSerialInterface: ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::SerialInterface_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::SerialInterface_End, true, 2).cpp_str());
			out.p(" (remap=true)").nl();
		}
		memMap.mapDevice(vSerialInterface, dragon::data::MemoryMapAddresses::SerialInterface_Start, dragon::data::MemoryMapAddresses::SerialInterface_End, true, "vSerialInterface");
		if (verbose)
		{
			out.p("    RAM: ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::Memory_Start, true, 2).cpp_str());
			out.p(" to ");
			out.p(ostd::Utils::getHexStr(dragon::data::MemoryMapAddresses::Memory_End, true, 2).cpp_str());
			out.p(" (remap=false)").nl();
		}
		memMap.mapDevice(ram, dragon::data::MemoryMapAddresses::Memory_Start, dragon::data::MemoryMapAddresses::Memory_End, false, "RAM");

		//Default VideoBios Colors
		uint8_t default_bios_video_color = 0x4;
		if (verbose)
		{
			out.p("  Initializing vCPU reset sequence:").nl();
			// out.p("    BIOSVideo default colors: ").p(ostd::Utils::getHexStr(default_bios_video_color).cpp_str()).nl();
		}
		// memMap.write8(dragon::data::MemoryMapAddresses::BIOSVideo_Start, default_bios_video_color);

		uint16_t reset_ip_addr = 0x0000;
		if (verbose)
			out.p("    Reset IP register: ").p(ostd::Utils::getHexStr(reset_ip_addr, true, 2).cpp_str()).nl();
		cpu.writeRegister(dragon::data::Registers::IP, reset_ip_addr);

		if (verbose)
			out.p("    Debug mode enabled: ").p(STR_BOOL(debugModeEnabled)).nl();
		cpu.m_debugModeEnabled = debugModeEnabled;

		out.nl().nl();
		s_trackMachineInfo = trackMachineInfoDiff;
		s_trackCallStack = trackCallStack;
		return 0;
	}

	void DragonRuntime::runMachine(int32_t cycleLimit, bool basic_debug, bool step_exec)
	{
		int32_t cycleCounter = 0;
		bool running = true;
		uint8_t currentInst = 0x00;
		uint32_t instSize = 0;
		ostd::ByteStream& ramData = *ram.getByteStream();
		// while ((running || vDiskInterface.isBusy()) && cycleCounter++ < 2048)
		while (running || vDiskInterface.isBusy())
		{
			if (basic_debug)
				out.clear();
			uint16_t addr = cpu.readRegister(dragon::data::Registers::IP);
			uint16_t spAddr = cpu.readRegister(dragon::data::Registers::SP);
			running = cpu.execute() && vDisplay.isRunning();
			vDisplay.update();
			currentInst = cpu.getCurrentInstruction();
			vDiskInterface.cycleStep();
			ostd::ConsoleOutputHandler newOut; //TODO: This workaround is not good, this whole file needs to be updated to not use the legacy ConsoleOutputHandler
			if ((cpu.isInDebugBreakPoint() || step_exec) && basic_debug)
			{
				instSize = dragon::data::OpCodes::getInstructionSIze(currentInst);
				ostd::Utils::printByteStream(ramData, dragon::data::MemoryMapAddresses::Memory_Start, 16, 10, newOut, addr, instSize, " MEMORY");
				printRegisters(cpu);
				out.nl().fg("yellow").p("Current Instruction: ").fg("white").p(ostd::Utils::getHexStr(currentInst, true, 1).cpp_str());
				out.nl();
				out.fg(ostd::ConsoleColors::Magenta).p("######### Reached Break Point at ");
				out.p(ostd::Utils::getHexStr(addr, true, 2).cpp_str());
				out.nl().reset();
				std::cin.get();
				continue;
			}
			else if (dragon::data::ErrorHandler::hasError())
			{
				if (!basic_debug)
				{
					processErrors();
					continue;
				}
				instSize = dragon::data::OpCodes::getInstructionSIze(currentInst);
				ostd::Utils::printByteStream(ramData, dragon::data::MemoryMapAddresses::Memory_Start, 16, 10, newOut, addr, instSize, " MEMORY");
				printRegisters(cpu);
				out.nl().fg("yellow").p("Current Instruction: ").fg("white").p(ostd::Utils::getHexStr(currentInst, true, 1).cpp_str());
				out.nl();
				out.fg(ostd::ConsoleColors::Red).p("######### Error occurred at ");
				out.p(ostd::Utils::getHexStr(addr, true, 2).cpp_str());
				out.nl().reset();
				processErrors();
				std::cin.get();
				continue;
			}
		}
	}

	bool DragonRuntime::runStep(std::vector<uint16_t> trackedAddresses)
	{
		__get_machine_footprint(&s_machineInfo, trackedAddresses, true);
		__track_call_stack(&s_machineInfo);
		bool running = cpu.execute() && vDisplay.isRunning();
		vDisplay.update();
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
		uint8_t instSize = data::OpCodes::getInstructionSIze(memMap.read8(instAddr));
		ostd::String opCode = data::OpCodes::getOpCodeString(memMap.read8(instAddr));
		uint16_t stackFrameSize = cpu.m_stackFrameSize;
		int32_t subRoutineCounter = cpu.m_subroutineCounter;


		bool debugBreak = cpu.m_isDebugBreakPoint;
		bool intHandler = cpu.m_isInInterruptHandler;
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
			minfo.previousInstructionInterruptHandler = intHandler;
			minfo.previousInstructionBiosMode = biosMode;
			minfo.previousIsInSubRoutine = isInSubRoutine;
		}
		else
		{
			minfo.currentInstructionAddress = instAddr;
			minfo.currentInstructionFootprintSize = instSize;
			minfo.currentInstructionStackFrameSize = stackFrameSize;
			minfo.currentInstructionOpCode = opCode;
			minfo.currentSubRoutineCounter = subRoutineCounter;

			for (int8_t i = 0; i < instSize; i++)
				minfo.currentInstructionFootprint[i] = memMap.read8(instAddr + i);

			for (int8_t i = 0; i < 20; i++)
				minfo.currentInstructionRegisters[i] = cpu.readRegister(i);

			for (auto& addr : minfo.trackedAddresses)
				minfo.currentInstructionTrackedValues.push_back(memMap.read8(addr));

			minfo.currentInstructionDebugBreak = debugBreak;
			minfo.currentInstructionInterruptHandler = intHandler;
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
			minfo.callStack.push_back({ "CALL IMM", call_addr });
		}
		else if (inst == data::OpCodes::CallReg)
		{
			uint8_t reg_addr = memMap.read8(instAddr + 1);
			uint16_t call_addr = cpu.readRegister(reg_addr);
			minfo.callStack.push_back({ "CALL REG", call_addr });
		}
		else if (interrupts_enabled && inst == data::OpCodes::Int)
		{
			uint8_t int_num = memMap.read8(instAddr + 1);
			minfo.callStack.push_back({ "INT", int_num });
		}
		else if (inst == data::OpCodes::Ret)
		{
			minfo.callStack.push_back({ "RET", 0x0000 });
		}
		else if (interrupts_enabled && inst == data::OpCodes::RetInt)
		{
			minfo.callStack.push_back({ "RET INT", 0x0000 });
		}
	 }
}