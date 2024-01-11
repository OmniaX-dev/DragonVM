#include "Tools.hpp"

#include <ostd/Utils.hpp>
#include <fstream>
#include "../hardware/VirtualHardDrive.hpp"

namespace dragon
{
	bool Tools::createVirtualHardDrive(uint32_t sizeInBytes, const ostd::String& dataFilePath)
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

	int32_t Tools::execute(int argc, char** argv)
	{
		ostd::String tool = "";
		int32_t rValue = get_tool(argc, argv, tool);
		if (rValue != ErrorNoError)
			return rValue;

		if (tool == "new-vdisk")
		{
			rValue = tool_new_virtual_disk(argc, argv);
			if (rValue != ErrorNoError)
				return rValue;
		}
		else if (tool == "load-binary")
		{
			rValue = tool_load_binary(argc, argv);
			if (rValue != ErrorNoError)
				return rValue;
		}
		else if (tool == "--help")
		{
			print_application_help();
			return ErrorNoError;
		}
		else
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Unknown tool.").reset().nl();
			out.fg(ostd::ConsoleColors::Red).p("Use the --help option for more info.").reset().nl();
			return ErrorTopLevelUnknownTool;
		}
		return ErrorNoError;
	}

	int32_t Tools::tool_new_virtual_disk(int argc, char** argv)
	{
		if (argc < 4)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
			out.fg(ostd::ConsoleColors::Red).p("  Usage: ./dtools new-vdisk <destination_file> <size_in_bytes>").reset().nl();
			return ErrorNewVDiskTooFewArgs;
		}
		ostd::String dest = argv[2];
		ostd::String str_size = argv[3];
		if (!ostd::Utils::isInt(str_size))
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: <size_in_bytes> parameter must be integer.").reset().nl();
			return ErrorNewVDiskNonIntSize;
		}
		bool result = createVirtualHardDrive(ostd::Utils::strToInt(str_size), dest);
		if (!result)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Unable to create virtual disk.").reset().nl();
			return ErrorNewVDiskUnable;
		}
		out.nl().fg(ostd::ConsoleColors::Green).p("Success. Virtual disk created:").nl();
		out.p("  Path: ").p(dest.cpp_str()).nl();
		out.p("  Size: ").p(str_size.cpp_str()).reset().nl();
		return ErrorNoError;
	}

	int32_t Tools::tool_load_binary(int argc, char** argv)
	{
		if (argc < 5)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
			out.fg(ostd::ConsoleColors::Red).p("  Usage: ./dtools load-binary <virtual_disk_file> <data_file> <destination_address>").reset().nl();
			return ErrorLoadProgTooFewArgs;
		}
		ostd::String vdisk_file = argv[2];
		ostd::String data_file = argv[3];
		ostd::String str_addr = argv[4];
		if (!ostd::Utils::isInt(str_addr))
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: <destination_address> parameter must be integer.").reset().nl();
			return ErrorLoadProgNonIntAddr;
		}
		dragon::hw::VirtualHardDrive vHDD(vdisk_file);
		if (!vHDD.isInitialized())
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Unable to load virtual disk.").reset().nl();
			return ErrorLoadProgUnableToLoadVDisk;
		}
		ostd::ByteStream code;
		if (!ostd::Utils::loadByteStreamFromFile(data_file, code))
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Unable to load data file.").reset().nl();
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
		out.nl().fg(ostd::ConsoleColors::Green).p("Success. Data writte to Virtual Disk:").nl();
		out.p("  Data Path: ").p(data_file.cpp_str()).nl();
		out.p("  Disk Path: ").p(vdisk_file.cpp_str()).nl();
		out.p("  Data Address: ").p(ostd::Utils::getHexStr(addr, true, 4).cpp_str()).nl();
		out.p("  Size: ").p(code.size()).reset().nl();
		return ErrorNoError;
	}

	void Tools::print_application_help(void)
	{
		out.nl().fg(ostd::ConsoleColors::Yellow).p("List of available tools:").nl().nl();

		out.fg(ostd::ConsoleColors::Blue).p("load-binary <virtual_disk_file> <data_file> <destination_address>").nl();
		out.fg(ostd::ConsoleColors::Green).p("The <load-binary> tool is used to load a binary file directly into a Virtual Disk.").nl();
		out.p("    <virtual_disk_file>          Path to the destination Virtual Disk file.").nl();
		out.p("    <data_file>                  Path to the source binary file.").nl();
		out.p("    <destination_address>        Destination address on the Virtual Disk.").nl().nl();

		out.fg(ostd::ConsoleColors::Blue).p("new-vdisk <destination_file> <size_in_bytes>").nl();
		out.fg(ostd::ConsoleColors::Green).p("The <new-vdisk> tool is used to create a new Virtual Disk File.").nl();
		out.p("    <destination_file>           Path of the destination Virtual Disk file to be created.").nl();
		out.p("    <size_in_bytes>              Size of the new Virtual Disk file, in bytes .").nl().nl();

		out.fg(ostd::ConsoleColors::Magenta).p("Usage: ./dtools <tool_name> [...arguments...]").nl().nl().reset();
	}

	int32_t Tools::get_tool(int argc, char** argv, ostd::String& outTool)
	{
		if (argc < 2)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
			out.fg(ostd::ConsoleColors::Red).p("Use the --help option for more info.").reset().nl();
			return ErrorTopLevelTooFewArgs;
		}
		ostd::String tool = argv[1];
		tool = tool.trim().toLower();
		outTool = tool;
		return ErrorNoError;
	}
}