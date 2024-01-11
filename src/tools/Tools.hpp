#pragma once

#include <ostd/IOHandlers.hpp>

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
			static void print_application_help(void);
			static int32_t get_tool(int argc, char** argv, ostd::String& outTool);
			
		private:
			inline static ostd::ConsoleOutputHandler out;

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
	};
}