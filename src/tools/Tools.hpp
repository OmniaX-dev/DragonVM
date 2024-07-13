#pragma once

#include <ostd/IOHandlers.hpp>
#include "GlobalData.hpp"

namespace dragon
{
	class Tools
	{
		public:
			static inline ostd::ConsoleOutputHandler& output(void) { return out; }
			static bool createVirtualHardDrive(uint32_t sizeInBytes, const ostd::String& dataFilePath);
			static int32_t execute(int argc, char** argv);

		private:
			static int32_t tool_new_virtual_disk(int argc, char** argv);
			static int32_t tool_load_binary(int argc, char** argv);
			static int32_t tool_read_dpt(int argc, char** argv);
			static int32_t tool_new_dpt(int argc, char** argv);
			static void print_application_help(void);
			static int32_t get_tool(int argc, char** argv, ostd::String& outTool);
			
		private:
			inline static ostd::ConsoleOutputHandler out;

			inline static std::unordered_map<uint8_t, ostd::String> m_dpt_flags_str {
				{ data::DPTStructure::tFlags::Boot, "boot" }
			};

		public:
			inline static constexpr int32_t ErrorNoError = 0;
			inline static constexpr int32_t ErrorTopLevelTooFewArgs = 1;
			inline static constexpr int32_t ErrorTopLevelUnknownTool = 2;
			inline static constexpr int32_t ErrorNewVDiskTooFewArgs = 3;
			inline static constexpr int32_t ErrorNewVDiskNonIntSize = 4;
			inline static constexpr int32_t ErrorNewVDiskUnable = 5;
			inline static constexpr int32_t ErrorLoadProgTooFewArgs = 6;
			inline static constexpr int32_t ErrorLoadProgNonIntAddr = 7;
			inline static constexpr int32_t ErrorLoadProgUnableToLoadVDisk = 8;
			inline static constexpr int32_t ErrorLoadProgUnableToLoadDataFile = 9;
			inline static constexpr int32_t ErrorReadDPTTooFewArgs = 10;
			inline static constexpr int32_t ErrorReadDPTUnableToLoadVDisk = 11;
			inline static constexpr int32_t ErrorReadDPTSmallDisk = 12;
			inline static constexpr int32_t ErrorReadDPTUnableToRead = 13;
			inline static constexpr int32_t ErrorReadDPTNoPartitionTable = 14;
			inline static constexpr int32_t ErrorNewDPTNoPartitionSize = 15;
			inline static constexpr int32_t ErrorNewDPTInvalidPartitionSize = 16;
			inline static constexpr int32_t ErrorNewDPTNoPartitionLabel = 17;
			inline static constexpr int32_t ErrorNewDPTNoPartitionFlag = 18;
			inline static constexpr int32_t ErrorNewDPTDiskOverflow = 19;
			inline static constexpr int32_t ErrorNewDPTUnknownFlag = 20;
			inline static constexpr int32_t ErrorNewDPTTooManyPartitions = 21;

	};
}