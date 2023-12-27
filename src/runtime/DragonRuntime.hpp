#pragma once

#include "../gui/Window.hpp"
#include "../gui/widgets/VirtualConsoleWidget.hpp"

#include "../hardware/VirtualCPU.hpp"
#include "../hardware/MemoryMapper.hpp"
#include "../hardware/VirtualRAM.hpp"
#include "../hardware/VirtualIODevices.hpp"
#include "../hardware/VirtualHardDrive.hpp"

#include "../tools/GlobalData.hpp"

#include "ConfigLoader.hpp"

namespace dragon
{
	class DragonRuntime
	{
		public: struct tMachineDebugInfo {
			inline tMachineDebugInfo(void) {  }

			uint16_t previousInstructionAddress { 0x0000 };
			uint16_t currentInstructionAddress { 0x0000 };
			int8_t previousInstructionFootprintSize { 0x00 };
			int8_t currentInstructionFootprintSize { 0x00 };
			uint16_t previousInstructionStackFrameSize { 0x00 };
			uint16_t currentInstructionStackFrameSize { 0x00 };

			ostd::String previousInstructionOpCode { "" };
			ostd::String currentInstructionOpCode { "" };

			int8_t previousInstructionFootprint[5] { 0x00, 0x00, 0x00, 0x00, 0x00 };
			int8_t currentInstructionFootprint[5] { 0x00, 0x00, 0x00, 0x00, 0x00 };
			int16_t previousInstructionRegisters[20] { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
			int16_t currentInstructionRegisters[20] { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		
			std::vector<uint16_t> trackedAddresses;
			std::vector<int8_t> previousInstructionTrackedValues;
			std::vector<int8_t> currentInstructionTrackedValues;

			bool previousInstructionDebugBreak { false };
			bool currentInstructionDebugBreak { false };
			bool previousInstructionInterruptHandler { false };
			bool currentInstructionInterruptHandler { false };
			bool previousInstructionBiosMode { false };
			bool currentInstructionBiosMode { false };

			bool vCPUHalt { false };
		};
		public:
			static void printRegisters(dragon::hw::VirtualCPU& cpu);
			static void processErrors(void);
			static std::vector<data::ErrorHandler::tError> getErrorList(void);
			static int32_t initMachine(const ostd::String& configFilePath, bool verbose = false, bool trackMachineInfoDiff = false, bool hideVirtualDisplay = false);
			static void runMachine(int32_t cycleLimit, bool basic_debug, bool step_exec);
			static bool runStep(std::vector<uint16_t> trackedAddresses = {  });
			static void forceLoad(const ostd::String& filePath, uint16_t loadAddress);

			inline static const tMachineDebugInfo& getMachineInfoDiff(void) { return s_machineInfo; }
			inline static bool hasError(void) { return data::ErrorHandler::hasError(); }

		private:
			static void __get_machine_footprint(tMachineDebugInfo* machineInfo, std::vector<uint16_t> trackedAddresses, bool previous);

		public:
			inline static ostd::legacy::ConsoleOutputHandler out;

			inline static dragon::hw::MemoryMapper memMap;
			inline static dragon::hw::VirtualCPU cpu { memMap };
			inline static dragon::hw::VirtualRAM ram;
			inline static dragon::hw::InterruptVector intVec;
			inline static dragon::hw::VirtualBIOS vBIOS;
			inline static dragon::hw::interface::CMOS vCMOS;
			inline static dragon::hw::VirtualBIOSVideo vBIOSVideo { ram };
			inline static dragon::hw::VirtualBootloader vMBR;
			inline static dragon::hw::VirtualKeyboard vKeyboard;
			inline static dragon::hw::VirtualMouse vMouse;
			inline static dragon::hw::interface::Disk vDiskInterface { memMap, cpu };
			inline static dragon::hw::interface::Graphics vGraphicsInterface;
			inline static dragon::hw::interface::SerialPort vSerialInterface;

			inline static std::unordered_map<int32_t, dragon::hw::VirtualHardDrive> vDisks;

			inline static dragon::Window vDisplay;
			inline static dragon::VirtualConsoleWidtget vConsWidg { vBIOSVideo, cpu };

			inline static dragon::tMachineConfig machine_config;

		private:
			inline static tMachineDebugInfo s_machineInfo;
			inline static bool s_trackMachineInfo { false };
	};
}