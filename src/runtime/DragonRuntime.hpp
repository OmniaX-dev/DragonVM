#pragma once

#include "../gui/Window.hpp"

#include "../hardware/VirtualCPU.hpp"
#include "../hardware/MemoryMapper.hpp"
#include "../hardware/VirtualRAM.hpp"
#include "../hardware/VirtualIODevices.hpp"
#include "../hardware/VirtualHardDrive.hpp"
#include "../hardware/VirtualDisplay.hpp"

#include "../tools/GlobalData.hpp"

#include "ConfigLoader.hpp"

namespace dragon
{
	class DragonRuntime
	{
		public: class SignalListener : public ostd::BaseObject
		{
			public:
				inline SignalListener(void) {  }
				void init(void);
				void handleSignal(ostd::tSignal& signal) override;

			public:
				inline static const int32_t Signal_HardwareInterruptOccurred = ostd::SignalHandler::newCustomSignal(8129);
		};
		public: struct tCallInfo : public ostd::BaseObject
		{
			inline tCallInfo(void) {  }
			inline tCallInfo(const ostd::String& _info, uint16_t _addr, uint16_t _inst_addr, bool ints_disabled) : info(_info), addr(_addr), inst_addr(_inst_addr), interrupts_disabled(ints_disabled) {  }
			ostd::String info;
			uint16_t addr;
			uint16_t inst_addr;
			bool interrupts_disabled;
		};
		public: struct tCommandLineArgs
		{
			ostd::String machine_config_path = "";
			bool verbose_load = false;
			bool force_load = false;
			ostd::String force_load_file = "";
			uint16_t force_load_mem_offset = 0x00;
		};
		public: struct tMachineDebugInfo
		{
			inline tMachineDebugInfo(void) {  }

			uint16_t previousInstructionAddress { 0x0000 };
			uint16_t currentInstructionAddress { 0x0000 };
			int8_t previousInstructionFootprintSize { 0x00 };
			int8_t currentInstructionFootprintSize { 0x00 };
			uint16_t previousInstructionStackFrameSize { 0x00 };
			uint16_t currentInstructionStackFrameSize { 0x00 };
			int32_t previousSubRoutineCounter { 0x00000000 };
			int32_t currentSubRoutineCounter { 0x00000000 };

			ostd::String previousInstructionOpCode { "" };
			ostd::String currentInstructionOpCode { "" };

			int8_t previousInstructionFootprint[5] { 0x00, 0x00, 0x00, 0x00, 0x00 };
			int8_t currentInstructionFootprint[5] { 0x00, 0x00, 0x00, 0x00, 0x00 };
			int16_t previousInstructionRegisters[20] { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
			int16_t currentInstructionRegisters[20] { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
			int32_t previousInstructionInterruptHandlerCount { 0 };
			int32_t currentInstructionInterruptHandlerCount { 0 };

			std::vector<uint16_t> trackedAddresses;
			std::vector<int8_t> previousInstructionTrackedValues;
			std::vector<int8_t> currentInstructionTrackedValues;

			bool previousInstructionDebugBreak { false };
			bool currentInstructionDebugBreak { false };
			bool previousInstructionBiosMode { false };
			bool currentInstructionBiosMode { false };
			bool previousIsInSubRoutine { false };
			bool currentIsInSubRoutine { false };

			bool vCPUHalt { false };


			std::vector<tCallInfo> callStack;
		};
		public:
			static void printRegisters(hw::VirtualCPU& cpu);
			static void processErrors(void);
			static std::vector<data::ErrorHandler::tError> getErrorList(void);
			static int32_t loadArguments(int argc, char** argv, tCommandLineArgs& args);
			static int32_t initMachine(const ostd::String& configFilePath,
										bool verbose = false,
										bool trackMachineInfoDiff = false, 
										bool hideVirtualDisplay = false,
										bool rackCallStack = false,
										bool debugModeEnabled = false);
			static void shutdownMachine(void);
			static void runMachine(void);
			static bool runStep(std::vector<uint16_t> trackedAddresses = {  });
			static void forceLoad(const ostd::String& filePath, uint16_t loadAddress);

			inline static const tMachineDebugInfo& getMachineInfoDiff(void) { return s_machineInfo; }
			inline static bool hasError(void) { return data::ErrorHandler::hasError(); }
			inline static ostd::ConsoleOutputHandler& output(void) { return out; }
			inline static uint64_t getAvgClockSpeed(void) { return (uint64_t)std::round(1000000.0 / s_avgInstTime);  }

		private:
			static void __get_machine_footprint(tMachineDebugInfo* machineInfo, std::vector<uint16_t> trackedAddresses, bool previous);
			static void __track_call_stack(tMachineDebugInfo* machineInfo);
			static void __print_application_help(void);

		public:
			inline static ostd::ConsoleOutputHandler out;

			inline static hw::MemoryMapper memMap;
			inline static hw::VirtualCPU cpu { memMap };
			inline static hw::VirtualRAM ram;
			inline static hw::InterruptVector intVec;
			inline static hw::VirtualBIOS vBIOS;
			inline static hw::interface::CMOS vCMOS;
			inline static hw::VirtualBootloader vMBR;
			inline static hw::VirtualKeyboard vKeyboard;
			inline static hw::VirtualMouse vMouse;
			inline static hw::interface::Disk vDiskInterface { memMap, cpu };
			inline static hw::interface::Graphics vGraphicsInterface;
			inline static hw::interface::SerialPort vSerialInterface;

			inline static std::unordered_map<int32_t, hw::VirtualHardDrive> vDisks;

			inline static hw::VirtualDisplay vDisplay;

			inline static tMachineConfig machine_config;

			inline static uint64_t s_avgInstTime { 0 };
			inline static double s_stepAcc2 { 0 };
			inline static bool s_enableScreenRedrawDelay { true };

		private:
			inline static tMachineDebugInfo s_machineInfo;
			inline static bool s_trackMachineInfo { false };
			inline static bool s_trackCallStack { false };
			inline static SignalListener s_signalListener;


		public:
			inline static const int32_t RETURN_VAL_CLOSE_DEBUGGER = 128;
			inline static const int32_t RETURN_VAL_CLOSE_RUNTIME = 256;
			inline static const int32_t RETURN_VAL_INVALID_MACHINE_CONFIG = 1;
			inline static const int32_t RETURN_VAL_NO_DISK = 2;
			inline static const int32_t RETURN_VAL_TOO_FEW_ARGUMENTS = 3;
			inline static const int32_t RETURN_VAL_MISSING_PARAM = 4;
			inline static const int32_t RETURN_VAL_PARAMETER_NOT_NUMERIC = 5;
			inline static const int32_t RETURN_VAL_EXIT_SUCCESS = 0;

		friend class SignalListener;
	};
}