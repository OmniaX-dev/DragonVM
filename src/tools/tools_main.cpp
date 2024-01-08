#include <ostd/Utils.hpp>
#include <fstream>
#include <ostd/IOHandlers.hpp>

#include "../hardware/VirtualHardDrive.hpp"

constexpr int ErrorTopLevelTooFewArgs = 1;
constexpr int ErrorTopLevelUnknownTool = 4;

constexpr int ErrorNewVDiskTooFewArgs = 2;
constexpr int ErrorNewVDiskNonIntSize = 3;
constexpr int ErrorNewVDiskUnable = 5;

constexpr int ErrorLoadProgTooFewArgs = 6;
constexpr int ErrorLoadProgNonIntAddr = 7;
constexpr int ErrorLoadProgUnableToLoadVDisk = 8;
constexpr int ErrorLoadProgUnableToLoadDataFile = 8;

constexpr int ErrorNoError = 0;

ostd::ConsoleOutputHandler out;

bool createVirtualHardDrive(uint32_t sizeInBytes, const ostd::String& dataFilePath)
{
	std::ofstream rf(dataFilePath.cpp_str(), std::ios::out | std::ios::binary);
	if(!rf) return false;
	ostd::ByteStream stream;
	for (int32_t i = 0; i < sizeInBytes; i++)
		stream.push_back(0x00);
	rf.write((char*)(&stream[0]), stream.size());
	rf.close();
	return true;
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		out.fg("red").p("Error: too few arguments.").nl();
		out.fg("red").p("  Usage: ./dtools <tool_name> [...arguments...]").reset().nl();
		return ErrorTopLevelTooFewArgs;
	}
	ostd::String tool = argv[1];
	tool = tool.trim().toLower();

	//Nex Virtual Disk
	if (tool == "new-vdisk")
	{
		if (argc < 4)
		{
			out.fg("red").p("Error: too few arguments.").nl();
			out.fg("red").p("  Usage: ./dtools new-vdisk <destination_file> <size_in_bytes>").reset().nl();
			return ErrorNewVDiskTooFewArgs;
		}
		ostd::String dest = argv[2];
		ostd::String str_size = argv[3];
		if (!ostd::Utils::isInt(str_size))
		{
			out.fg("red").p("Error: <size_in_bytes> parameter must be integer.").reset().nl();
			return ErrorNewVDiskNonIntSize;
		}
		bool result = createVirtualHardDrive(ostd::Utils::strToInt(str_size), dest);
		if (!result)
		{
			out.fg("red").p("Error: Unable to create virtual disk.").reset().nl();
			return ErrorNewVDiskUnable;
		}
		out.fg("green").p("Success. Virtual disk created:").nl();
		out.p("  Path: ").p(dest.cpp_str()).nl();
		out.p("  Size: ").p(str_size.cpp_str()).reset().nl();
	}
	//Load Program
	else if (tool == "load-program")
	{
		if (argc < 5)
		{
			out.fg("red").p("Error: too few arguments.").nl();
			out.fg("red").p("  Usage: ./dtools load-program <virtual_disk_file> <data_file> <destination_address>").reset().nl();
			return ErrorLoadProgTooFewArgs;
		}
		ostd::String vdisk_file = argv[2];
		ostd::String data_file = argv[3];
		ostd::String str_addr = argv[4];
		if (!ostd::Utils::isInt(str_addr))
		{
			out.fg("red").p("Error: <destination_address> parameter must be integer.").reset().nl();
			return ErrorLoadProgNonIntAddr;
		}
		dragon::hw::VirtualHardDrive vHDD(vdisk_file);
		if (!vHDD.isInitialized())
		{
			out.fg("red").p("Error: Unable to load virtual disk.").reset().nl();
			return ErrorLoadProgUnableToLoadVDisk;
		}
		ostd::ByteStream code;
		if (!ostd::Utils::loadByteStreamFromFile(data_file, code))
		{
			out.fg("red").p("Error: Unable to load data file.").reset().nl();
			return ErrorLoadProgUnableToLoadDataFile;
		}
		int16_t index = 0;
		uint32_t addr = (uint32_t)ostd::Utils::strToInt(str_addr);
		for (auto& b : code)
		{
			vHDD.write(addr + index, b);
			index++;
		}
		vHDD.unmount();
		out.fg("green").p("Success. Data writte to Virtual Disk:").nl();
		out.p("  Data Path: ").p(data_file.cpp_str()).nl();
		out.p("  Disk Path: ").p(vdisk_file.cpp_str()).nl();
		out.p("  Data Address: ").p(ostd::Utils::getHexStr(addr, true, 4).cpp_str()).nl();
		out.p("  Size: ").p(code.size()).reset().nl();
	}
	//Unknown
	else
	{
		out.fg("red").p("Error: Unknown tool.").reset().nl();
		return ErrorTopLevelUnknownTool;
	}
	return ErrorNoError;
}