#pragma once

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
				void handleSignal(ostd::Signal& signal) override;

			public:
				inline static const i32 Signal_HardwareInterruptOccurred = ostd::SignalHandler::newCustomSignal(8129);
		};
		public: struct tCommandLineArgs
		{
			String machine_config_path = "";
			bool verbose_load = false;
			bool force_load = false;
			String force_load_file = "";
			u16 force_load_mem_offset = 0x00;
		};
		public: struct tRuntimeInitInfo
		{
			String configFilePath;
			bool verboseLoad { false };
			bool hideVirtualDisplay { false };
			bool debugModeEnabled { false };
		};
		public:
			static void processErrors(void);
			static std::vector<data::ErrorHandler::tError> getErrorList(void);
			static i32 loadArguments(int argc, char** argv, tCommandLineArgs& args);
			static i32 initMachine(const tRuntimeInitInfo& info);
			static void shutdownMachine(void);
			static void runMachine(void);
			static void runMachine2(void);
			static bool runStep(void);
			static void forceLoad(const String& filePath, u16 loadAddress);

			inline static bool hasError(void) { return data::ErrorHandler::hasError(); }
			inline static ostd::ConsoleOutputHandler& output(void) { return out; }
			inline static u64 getAvgClockSpeed(void) { return (u64)std::round(1000000.0 / s_avgInstTime);  }

		private:
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

			inline static std::unordered_map<i32, hw::VirtualHardDrive> vDisks;

			inline static hw::VirtualDisplay vDisplay;

			inline static tMachineConfig machine_config;

			inline static u64 s_avgInstTime { 0 };
			inline static f64 s_stepAcc2 { 0 };
			inline static bool s_enableScreenRedrawDelay { true };

		private:
			inline static SignalListener s_signalListener;

		public:
			inline static const i32 RETURN_VAL_CLOSE_DEBUGGER = 128;
			inline static const i32 RETURN_VAL_CLOSE_RUNTIME = 256;
			inline static const i32 RETURN_VAL_INVALID_MACHINE_CONFIG = 1;
			inline static const i32 RETURN_VAL_NO_DISK = 2;
			inline static const i32 RETURN_VAL_TOO_FEW_ARGUMENTS = 3;
			inline static const i32 RETURN_VAL_MISSING_PARAM = 4;
			inline static const i32 RETURN_VAL_PARAMETER_NOT_NUMERIC = 5;
			inline static const i32 RETURN_VAL_EXIT_SUCCESS = 0;

		friend class SignalListener;
	};
}
