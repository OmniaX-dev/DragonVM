#pragma once

#include <ostd/io/IOHandlers.hpp>
#include "GlobalData.hpp"
#include <unordered_map>

namespace dragon
{
	class Tools
	{
		public:
			static inline ostd::ConsoleOutputHandler& output(void) { return out; }
			static bool createVirtualHardDrive(u32 sizeInBytes, const String& dataFilePath);
			static i32 execute(int argc, char** argv);

		private:
			static i32 tool_new_virtual_disk(int argc, char** argv);
			static i32 tool_load_binary(int argc, char** argv);
			static i32 tool_read_dpt(int argc, char** argv);
			static i32 tool_new_dpt(int argc, char** argv);
			static i32 tool_print_disassembly(int argc, char** argv);
			static void print_application_help(void);
			static i32 get_tool(int argc, char** argv, String& outTool);

		private:
			inline static ostd::ConsoleOutputHandler out;

			inline static std::unordered_map<u8, String> m_dpt_flags_str {
				{ data::DPTStructure::tFlags::Boot, "boot" }
			};

		public:
			inline static constexpr i32 ErrorNoError = 0;
			inline static constexpr i32 ErrorTopLevelTooFewArgs = 1;
			inline static constexpr i32 ErrorTopLevelUnknownTool = 2;
			inline static constexpr i32 ErrorNewVDiskTooFewArgs = 3;
			inline static constexpr i32 ErrorNewVDiskNonIntSize = 4;
			inline static constexpr i32 ErrorNewVDiskUnable = 5;
			inline static constexpr i32 ErrorLoadProgTooFewArgs = 6;
			inline static constexpr i32 ErrorLoadProgNonIntAddr = 7;
			inline static constexpr i32 ErrorLoadProgUnableToLoadVDisk = 8;
			inline static constexpr i32 ErrorLoadProgUnableToLoadDataFile = 9;
			inline static constexpr i32 ErrorReadDPTTooFewArgs = 10;
			inline static constexpr i32 ErrorReadDPTUnableToLoadVDisk = 11;
			inline static constexpr i32 ErrorReadDPTSmallDisk = 12;
			inline static constexpr i32 ErrorReadDPTUnableToRead = 13;
			inline static constexpr i32 ErrorReadDPTNoPartitionTable = 14;
			inline static constexpr i32 ErrorNewDPTNoPartitionSize = 15;
			inline static constexpr i32 ErrorNewDPTInvalidPartitionSize = 16;
			inline static constexpr i32 ErrorNewDPTNoPartitionLabel = 17;
			inline static constexpr i32 ErrorNewDPTNoPartitionFlag = 18;
			inline static constexpr i32 ErrorNewDPTDiskOverflow = 19;
			inline static constexpr i32 ErrorNewDPTUnknownFlag = 20;
			inline static constexpr i32 ErrorNewDPTTooManyPartitions = 21;
			inline static constexpr i32 ErrorPrintDisassemblyTooFewArgs = 22;
			inline static constexpr i32 ErrorPrintDisassemblyInvalidFile = 23;
			inline static constexpr i32 ErrorPrintDisassemblyInvalidArg = 24;

	};
}
