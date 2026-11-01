#include "Tools.hpp"

#include <ostd/data/Color.hpp>
#include <ostd/io/IOHandlers.hpp>
#include <fstream>
#include "../storage/DPT.hpp"
#include "GlobalData.hpp"
#include "../debugger/DisassemblyLoader.hpp"
#include <ostd/io/Memory.hpp>
#include "../tools/LegacyOstdSerial.hpp"

namespace dragon
{
	bool Tools::createVirtualHardDrive(u32 sizeInBytes, const String& dataFilePath)
	{
		String error = "";
		return storage::createDiskFile(dataFilePath, sizeInBytes, error);
	}

	i32 Tools::execute(int argc, char** argv)
	{
		String tool = "";
		i32 rValue = get_tool(argc, argv, tool);
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
		else if (tool == "read-dpt")
		{
			rValue = tool_read_dpt(argc, argv);
			if (rValue != ErrorNoError)
				return rValue;
		}
		else if (tool == "new-dpt")
		{
			rValue = tool_new_dpt(argc, argv);
			if (rValue != ErrorNoError)
				return rValue;
		}
		else if (tool == "print-disassembly")
		{
			rValue = tool_print_disassembly(argc, argv);
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

	i32 Tools::tool_new_virtual_disk(int argc, char** argv)
	{
		if (argc < 4)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
			out.fg(ostd::ConsoleColors::Red).p("  Usage: ./dtools new-vdisk <destination_file> <size_in_bytes>").reset().nl();
			return ErrorNewVDiskTooFewArgs;
		}
		String dest = argv[2];
		String str_size = argv[3];
		if (!str_size.isInt())
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: <size_in_bytes> parameter must be integer.").reset().nl();
			return ErrorNewVDiskNonIntSize;
		}
		bool result = createVirtualHardDrive(str_size.toInt(), dest);
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

	i32 Tools::tool_load_binary(int argc, char** argv)
	{
		if (argc < 5)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
			out.fg(ostd::ConsoleColors::Red).p("  Usage: ./dtools load-binary <virtual_disk_file> <data_file> <destination_address>").reset().nl();
			return ErrorLoadProgTooFewArgs;
		}
		String vdisk_file = argv[2];
		String data_file = argv[3];
		String str_addr = argv[4];
		if (!str_addr.isInt())
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: <destination_address> parameter must be integer.").reset().nl();
			return ErrorLoadProgNonIntAddr;
		}
		storage::FileDisk disk(vdisk_file);
		if (!disk.isOpen())
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Unable to load virtual disk.").reset().nl();
			return ErrorLoadProgUnableToLoadVDisk;
		}
		stdvec<u8> code;
		String error = "";
		if (!storage::readFile(data_file, code, error))
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Unable to load data file.").reset().nl();
			return ErrorLoadProgUnableToLoadDataFile;
		}
		u32 addr = (u32)str_addr.toInt();
		if (!disk.contains(addr, code.size()) || !disk.write(addr, code))
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: The data doesn't fit on the virtual disk at that address.").reset().nl();
			return ErrorLoadProgDiskOverflow;
		}
		disk.close();
		out.nl().fg(ostd::ConsoleColors::Green).p("Success. Data written to Virtual Disk:").nl();
		out.p("  Data Path: ").p(data_file.cpp_str()).nl();
		out.p("  Disk Path: ").p(vdisk_file.cpp_str()).nl();
		out.p("  Data Address: ").p(String::getHexStr(addr, true, 4).cpp_str()).nl();
		out.p("  Size: ").p(code.size()).reset().nl();
		return ErrorNoError;
	}

	i32 Tools::tool_read_dpt(int argc, char** argv)
	{
		if (argc < 3)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
			out.fg(ostd::ConsoleColors::Red).p("  Usage: ./dtools read-dpt <virtual_disk_file>").reset().nl();
			return ErrorReadDPTTooFewArgs;
		}
		String vdisk_file = argv[2];
		storage::FileDisk disk(vdisk_file);
		if (!disk.isOpen())
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Unable to load virtual disk.").reset().nl();
			return ErrorReadDPTUnableToLoadVDisk;
		}
		if (disk.getSize() < 1024)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Disk too small.").reset().nl();
			return ErrorReadDPTSmallDisk;
		}
		const storage::DPTScheme dpt;
		storage::tPartitionTable table;
		String error = "";
		if (!dpt.read(disk, table, error))
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: ").p(error).reset().nl();
			return ErrorReadDPTNoPartitionTable;
		}
		auto& partitionList = table.partitions;
		out.fg(ostd::ConsoleColors::BrightRed).p("Disk: ").p(vdisk_file).p(" (").p(disk.getSize()).p(" bytes)").nl();
		auto print_part_size = [](u32 size, ostd::ConsoleOutputHandler& out, u16 line_len) {
			f64 dsize = size;
			String units[4] = { " bytes", " Kb", " Mb", " Gb" };
			i32 unit_index = 0;
			while (dsize > 1024 && unit_index < 3)
			{
				unit_index++;
				dsize /= 1024.0;
			}
			out.p(String("").add(dsize, 2).add(units[unit_index]).new_fixedLength(line_len));
		};
		u16 len = 20;
		out.nl().fg(ostd::ConsoleColors::BrightGray);
		out.p(String("=").new_fixedLength(5 * len, '=')).nl();
		out.fg(ostd::ConsoleColors::Blue);
		out.p("  ");
		out.p(String("LABEL").new_fixedLength(len));
		out.p(String("SIZE").new_fixedLength(len));
		out.p(String("START").new_fixedLength(len));
		out.p(String("END").new_fixedLength(len));
		out.p(String("FLAGS").new_fixedLength(len));
		out.nl().fg(ostd::ConsoleColors::BrightGray);
		out.p(String("=").new_fixedLength(5 * len, '=')).nl();
		for (i32 i = 0; i < partitionList.size(); i++)
		{
			auto& part = partitionList[i];
			if (part.label == "")
				part.label = "<NO-LABEL>";
			out.fg(ostd::ConsoleColors::Cyan).p("  ");
			out.p(part.label.new_fixedLength(len));
			print_part_size(cast<u32>(part.size), out, len);
			out.p(String::getHexStr(part.start, true, 4).new_fixedLength(len));
			out.p(String::getHexStr(part.end(), true, 4).new_fixedLength(len));
			const String flags_str = dpt.flagsToString(part.flags);
			out.fg(ostd::ConsoleColors::Yellow).p(flags_str).fg(ostd::ConsoleColors::Cyan).nl();
		}
		out.fg(ostd::ConsoleColors::BrightGray);
		out.p(String("=").new_fixedLength(5 * len, '=')).nl();
		out.reset().nl();
		return ErrorNoError;
	}

	i32 Tools::tool_new_dpt(int argc, char** argv)
	{
		if (argc < 5)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
			out.fg(ostd::ConsoleColors::Red).p("  Usage: ./dtools new-dpt <virtual_disk_file> -p -s SIZE [-l LABEL] [-f FLAG1] [-f FLAG2] [-p SIZE ...]").reset().nl();
			return ErrorLoadProgTooFewArgs;
		}
		String vdisk_file = argv[2];
		storage::FileDisk disk(vdisk_file);
		if (!disk.isOpen())
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Unable to load virtual disk.").reset().nl();
			return ErrorLoadProgUnableToLoadVDisk;
		}
		u64 disk_size = disk.getSize();

		const storage::DPTScheme dpt;
		auto get_flag_from_str = [&dpt](const String& flag_str) -> i8 {
			for (auto& flag : dpt.getFlags())
			{
				if (flag.name == flag_str)
					return cast<i8>(flag.bit);
			}
			return -1;
		};
		struct tPartData {
			u32 size { 0 };
			u32 address { 0 };
			std::vector<u8> flags;
			String label { "" };
		};

		std::vector<tPartData> partitions;

		i32 arg_index = 3;
		bool has_args = true;
		bool part_started = false;
		tPartData _part_data;
		u32 part_start_addr = data::DPTStructure::DiskStartAddr;
		while (has_args)
		{
			String arg = argv[arg_index];
			arg.trim();
			if (part_started)
			{
				if (arg.new_toLower() == "-p")
				{
					part_started = false;
					partitions.push_back(_part_data);
					_part_data = tPartData();
					continue;
				}
				else if (arg.new_toLower() == "-l")
				{
					if (arg_index >= argc - 1)
					{
						out.fg(ostd::ConsoleColors::Red).p("Error: No partition label specified after -l parameter.").reset().nl();
						return ErrorNewDPTNoPartitionLabel;
					}
					arg_index++;
					_part_data.label = argv[arg_index];
				}
				else if (arg.new_toLower() == "-f")
				{
					if (arg_index >= argc - 1)
					{
						out.fg(ostd::ConsoleColors::Red).p("Error: No partition flag specified after -f parameter.").reset().nl();
						return ErrorNewDPTNoPartitionFlag;
					}
					arg_index++;
					i8 flag = get_flag_from_str(argv[arg_index]);
					if (flag < 0)
					{
						out.fg(ostd::ConsoleColors::Red).p("Error: Unknown partition flag.").reset().nl();
						return ErrorNewDPTUnknownFlag;
					}
					_part_data.flags.push_back(flag);
				}
			}
			else
			{
				if (arg.new_toLower() == "-p")
				{
					if (arg_index >= argc - 1)
					{
						out.fg(ostd::ConsoleColors::Red).p("Error: No partition size specified.").reset().nl();
						return ErrorNewDPTNoPartitionSize;
					}
					else if (!String(argv[arg_index + 1]).isInt())
					{
						out.fg(ostd::ConsoleColors::Red).p("Error: Partition size must be an integer.").reset().nl();
						return ErrorNewDPTInvalidPartitionSize;
					}
					arg_index++;
					u32 part_size = String(argv[arg_index]).toInt();
					if (part_start_addr + part_size > disk_size)
					{
						out.fg(ostd::ConsoleColors::Red).p("Error: Not enough space on disk.").reset().nl();
						return ErrorNewDPTDiskOverflow;
					}
					_part_data.size = part_size;
					_part_data.address = part_start_addr;
					part_start_addr += part_size;
					part_started = true;
				}
			}
			arg_index++;
			if (arg_index >= argc || partitions.size() > data::DPTStructure::MaxPartCount)
				has_args = false;
		}
		if (part_started)
			partitions.push_back(_part_data);

		if (partitions.size() > data::DPTStructure::MaxPartCount)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Too many partitions.").reset().nl();
			return ErrorNewDPTTooManyPartitions;
		}

		storage::tPartitionTable table;
		table.scheme = dpt.getName();
		for (auto& part : partitions)
		{
			storage::tPartition entry;
			entry.start = part.address;
			entry.size = part.size;
			for (auto& bit : part.flags)
				entry.setFlag(bit, true);
			entry.label = part.label;
			table.partitions.push_back(entry);
		}
		String error = "";
		if (!dpt.write(disk, table, error))
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: ").p(error).reset().nl();
			return ErrorNewDPTDiskOverflow;
		}
		disk.close();
		out.nl().fg(ostd::ConsoleColors::Green).p("Success. DPT Block created on Virtual Disk:").nl();
		out.p("  Disk Path: ").p(vdisk_file.cpp_str()).nl();
		out.p("  DPT Block Address: ").p(String::getHexStr(data::DPTStructure::DiskAddress, true, 4).cpp_str()).nl();
		out.p("  DPT Block Size: ").p(data::DPTStructure::DPTBlockSizeBytes).nl();
		return ErrorNoError;
	}

	i32 Tools::tool_print_disassembly(int argc, char** argv)
	{
		using TableList = std::vector<code::Assembler::tDisassemblyLine>;
		if (argc < 3)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
			out.fg(ostd::ConsoleColors::Red).p("  Usage: ./dtools print-disassembly <disassembly_file>").reset().nl();
			return ErrorPrintDisassemblyTooFewArgs;
		}
		String arg1 = argv[2];
		arg1.trim();
		TableList codeTable;
		TableList labelTable;
		TableList dataTable;
		if (arg1 == "-d")
		{
			if (argc < 4)
			{
				out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
				out.fg(ostd::ConsoleColors::Red).p("  Usage: ./dtools print-disassembly -d <disassembly_directory>").reset().nl();
				return ErrorPrintDisassemblyTooFewArgs;
			}
			dragon::DisassemblyLoader::loadDirectory(argv[3]);
			codeTable = dragon::DisassemblyLoader::getCodeTable();
			labelTable = dragon::DisassemblyLoader::getLabelTable();
			dataTable = dragon::DisassemblyLoader::getDataTable();
		}
		else if (arg1 == "-f")
		{
			const auto& table = dragon::DisassemblyLoader::loadFile(argv[3]);
			if (!table.isInitialized())
			{
				out.fg(ostd::ConsoleColors::Red).p("Error: Invalid disassembly file.").reset().nl();
				return ErrorPrintDisassemblyInvalidFile;
			}
			codeTable = table.getCodeTable();
			labelTable = table.getLabelTable();
			dataTable = table.getDataTable();
		}
		else
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: invalid argument.").nl();
			out.fg(ostd::ConsoleColors::Red).p("  Usage: ./dtools print-disassembly -d <disassembly_directory>").reset().nl();
			return ErrorPrintDisassemblyInvalidArg;
		}
		out.bg(ostd::ConsoleColors::White).fg(ostd::ConsoleColors::Black).p("DATA:").reset().nl();
		for (const auto& line : dataTable)
		{
			out.fg(ostd::ConsoleColors::BrightGray).p(String::getHexStr(line.addr, true, 4)).p("\t\t");
			out.fg(ostd::ConsoleColors::Green).p(line.code).p("\t\t");
			out.fg(ostd::ConsoleColors::Blue).p(line.size).p(" bytes\t\t");
			out.reset().nl();
		}
		out.nl();
		out.bg(ostd::ConsoleColors::White).fg(ostd::ConsoleColors::Black).p("LABELS:").reset().nl();
		for (const auto& line : labelTable)
		{
			out.fg(ostd::ConsoleColors::BrightGray).p(String::getHexStr(line.addr, true, 4)).p("\t\t");
			out.fg(ostd::ConsoleColors::Green).p(line.code).p("\t\t");
			out.reset().nl();
		}
		out.nl();
		out.bg(ostd::ConsoleColors::White).fg(ostd::ConsoleColors::Black).p("CODE:").reset().nl();
		for (const auto& line : codeTable)
		{
			out.fg(ostd::ConsoleColors::BrightGray).p(String::getHexStr(line.addr, true, 4)).p("\t\t");
			out.fg(ostd::ConsoleColors::Green).p(line.code).p("\t\t");
			out.reset().nl();
		}
		out.nl();
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

		out.fg(ostd::ConsoleColors::Blue).p("read-dpt <virtual_disk_file>").nl();
		out.fg(ostd::ConsoleColors::Green).p("The <read-dpt> tool is used to read the partition table of a Virtual Disk File.").nl();
		out.p("    <virtual_disk_file>          Path to the Virtual Disk file.").nl().nl();

		out.fg(ostd::ConsoleColors::Blue).p("new-dpt <virtual_disk_file> -p -s SIZE [-l LABEL] [-f FLAG1] [-f FLAG2] [-p SIZE ...]").nl();
		out.fg(ostd::ConsoleColors::Green).p("The <new-dpt> tool is used to create a new DPT-BLOCK on Virtual Disk File.").nl();
		out.p("    <virtual_disk_file>          Path to the Virtual Disk file.").nl();
		out.p("    -p                           Used to start creating a partition.").nl();
		out.p("    -s                           Used to specify the partition's size.").nl();
		out.p("    -l (optional)                Used to specify the partition's label.").nl();
		out.p("    -f (optional)                Used to specify one single flag for the partition. Use multiple -f parameters for multiple flags.").nl().nl();

		out.fg(ostd::ConsoleColors::Blue).p("print-disassembly [-d <disassembly_directory>] | [-f <disassembly_file>]").nl();
		out.fg(ostd::ConsoleColors::Green).p("The <print-disassembly> tool is used to parse and print disassembly tables.").nl();
		out.p("    <disassembly_directory>      Path to the directory containing the disassembly files to parse. (Used only for -d option)").nl();
		out.p("    <disassembly_file>           Path to the disassembly file to parse. (Used only for -f option)").nl();
		out.p("    -d                           Used to specify a directory path (mutually exclusive with -f).").nl();
		out.p("    -f                           Used to specify a file path (mutually exclusive with -d).").nl().nl();

		out.fg(ostd::ConsoleColors::Magenta).p("Usage: ./dtools <tool_name> [...arguments...]").nl().nl().reset();
	}

	i32 Tools::get_tool(int argc, char** argv, String& outTool)
	{
		if (argc < 2)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
			out.fg(ostd::ConsoleColors::Red).p("Use the --help option for more info.").reset().nl();
			return ErrorTopLevelTooFewArgs;
		}
		String tool = argv[1];
		tool = tool.trim().toLower();
		outTool = tool;
		return ErrorNoError;
	}
}
