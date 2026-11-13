#pragma once

#include <ostd/io/IOHandlers.hpp>
#include "GlobalData.hpp"
#include <unordered_map>

namespace dfs { struct Volume; struct Device; }

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
			static i32 tool_dfs(int argc, char** argv);
			static void print_application_help(void);
			static i32 get_tool(int argc, char** argv, String& outTool);

			// dtools dfs (ToolsDfs.cpp)
			static i32 dfs_fail(const String& what, i32 error = 0);
			static i32 dfs_mkfs(dfs::Volume* vol, const dfs::Device& dev, u64 partitionSize, const stdvec<String>& args);
			static i32 dfs_info(dfs::Volume* vol);
			static i32 dfs_ls(dfs::Volume* vol, const String& path, bool longFormat);
			static i32 dfs_tree(dfs::Volume* vol, const String& path);
			static i32 dfs_cat(dfs::Volume* vol, const String& path);
			static i32 dfs_put(dfs::Volume* vol, const String& hostPath, const String& path, bool recursive);
			static i32 dfs_put_file(dfs::Volume* vol, const String& hostPath, const String& path);
			static i32 dfs_write_data(dfs::Volume* vol, const stdvec<u8>& data, const String& path);
			static i32 dfs_resize(dfs::Volume* vol, const dfs::Device& dev, u64 partitionSize, const stdvec<String>& args);
			static i32 dfs_install_kernel(dfs::Volume* vol, const String& hostPath, const String& path, bool bootFlag);
			static i32 dfs_get(dfs::Volume* vol, const String& path, const String& hostPath);
			static i32 dfs_stat(dfs::Volume* vol, const String& path);
			static i32 dfs_fsck(dfs::Volume* vol);
			static i32 dfs_dump(dfs::Volume* vol, u32 number, bool inode);
			static void print_dfs_help(void);

		private:
			inline static ostd::ConsoleOutputHandler out;

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
			inline static constexpr i32 ErrorLoadProgDiskOverflow = 25;
			inline static constexpr i32 ErrorDfsTooFewArgs = 26;
			inline static constexpr i32 ErrorDfsFailed = 27;

	};
}
