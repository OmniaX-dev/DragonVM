#pragma once

#include <ostd/io/IOHandlers.hpp>

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
		public: class SignalListener : public ostd::Object
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
			bool debugModeEnabled { false };
		};
		public:
			static void processErrors(void);
			static std::vector<data::ErrorHandler::tError> getErrorList(void);
			static i32 loadArguments(int argc, char** argv, tCommandLineArgs& args);
			static i32 initMachine(const tRuntimeInitInfo& info);
			static void shutdownMachine(void);
			static void forceLoad(const String& filePath, u16 loadAddress);

			inline static bool hasError(void) { return data::ErrorHandler::hasError(); }
			// Where the runtime prints (loading info, errors): the terminal by default; the
			// debugger sends it to its console.
			inline static ostd::OutputHandlerBase& output(void) { return *s_output; }
			inline static void setOutputHandler(ostd::OutputHandlerBase& handler) { s_output = &handler; }
			inline static void resetOutputHandler(void) { s_output = &out; }

		private:
			static void __print_application_help(void);

		public:
			inline static ostd::ConsoleOutputHandler out;
			inline static ostd::OutputHandlerBase* s_output { &out };

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

			inline static tMachineConfig machine_config;

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
