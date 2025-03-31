#include "Tools.hpp"

#include <ostd/Utils.hpp>
#include <fstream>
#include "../hardware/VirtualHardDrive.hpp"
#include "GlobalData.hpp"
#include <ostd/Serial.hpp>

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
		out.nl().fg(ostd::ConsoleColors::Green).p("Success. Data written to Virtual Disk:").nl();
		out.p("  Data Path: ").p(data_file.cpp_str()).nl();
		out.p("  Disk Path: ").p(vdisk_file.cpp_str()).nl();
		out.p("  Data Address: ").p(ostd::Utils::getHexStr(addr, true, 4).cpp_str()).nl();
		out.p("  Size: ").p(code.size()).reset().nl();
		return ErrorNoError;
	}

	int32_t Tools::tool_read_dpt(int argc, char** argv)
	{
		if (argc < 3)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
			out.fg(ostd::ConsoleColors::Red).p("  Usage: ./dtools read-dpt <virtual_disk_file>").reset().nl();
			return ErrorReadDPTTooFewArgs;
		}
		ostd::String vdisk_file = argv[2];
		dragon::hw::VirtualHardDrive vHDD(vdisk_file);
		if (!vHDD.isInitialized())
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Unable to load virtual disk.").reset().nl();
			return ErrorReadDPTUnableToLoadVDisk;
		}
		if (vHDD.getSize() < 1024)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Disk too small.").reset().nl();
			return ErrorReadDPTSmallDisk;
		}
		ostd::ByteStream outData;
		if (!vHDD.read(data::DPTStructure::DiskAddress, data::DPTStructure::DPTBlockSizeBytes, outData))
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Unable to read from disk.").reset().nl();
			return ErrorReadDPTUnableToRead;
		}
		// ostd::Utils::printByteStream(outData, 0, 16, 32, out);
		ostd::serial::SerialIO dpt_block(outData);
		int8_t outData8 = 0;
		int16_t outData16 = 0;
		int32_t outData32 = 0;
		//TODO: Add errors for all read calls
		dpt_block.r_Word(data::DPTStructure::DPTID, outData16);
		uint16_t code = (uint16_t)outData16;
		if (code != data::DPTStructure::DPT_ID_CODE)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: No DPT partition table on virtual disk.").reset().nl();
			return ErrorReadDPTNoPartitionTable;
		}
		dpt_block.r_Byte(data::DPTStructure::DPTVersionMaj, outData8);
		uint32_t version_maj = (uint32_t)outData8;
		dpt_block.r_Byte(data::DPTStructure::DPTVersionMin, outData8);
		uint32_t version_min = (uint32_t)outData8;
		dpt_block.r_Byte(data::DPTStructure::PartitionCount, outData8);
		uint32_t part_count = (uint32_t)outData8;
		if (part_count > data::DPTStructure::MaxPartCount)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Too many partitions. Maximum is ").p(data::DPTStructure::MaxPartCount).p(".").reset().nl();
			return ErrorReadDPTNoPartitionTable;
		}
		struct tPartitionData {
			uint32_t startAddress { 0 };
			uint32_t size { 0 };
			ostd::BitField_16 flags { 0 };
			ostd::String label { "" };
		};
		std::vector<tPartitionData> partitionList;
		for (int32_t i = 0; i < part_count; i++)
		{
			tPartitionData pdata;
			uint32_t entry_addr = data::DPTStructure::EntriesStart + (data::DPTStructure::EntrySizeBytes * i);
			dpt_block.r_DWord(entry_addr + data::DPTStructure::EntryStartAddress, outData32);
			pdata.startAddress = (uint32_t)outData32;
			dpt_block.r_DWord(entry_addr + data::DPTStructure::EntryPartitionSize, outData32);
			pdata.size = (uint32_t)outData32;
			dpt_block.r_Word(entry_addr + data::DPTStructure::EntryFlags, outData16);
			pdata.flags.value = (uint16_t)outData16;
			dpt_block.r_NullTerminatedString(entry_addr + data::DPTStructure::EntryPartitionLabel, pdata.label);
			pdata.label.trim();
			partitionList.push_back(pdata);
		}
		out.fg(ostd::ConsoleColors::BrightRed).p("Disk: ").p(vdisk_file).p(" (").p(vHDD.getSize()).p(" bytes)").nl();
		auto print_part_size = [](uint32_t size, ostd::ConsoleOutputHandler& out, uint16_t line_len) {
			double dsize = size;
			ostd::String units[4] = { " bytes", " Kb", " Mb", " Gb" };
			int32_t unit_index = 0;
			while (dsize > 1024 && unit_index < 3)
			{
				unit_index++;
				dsize /= 1024.0;
			}
			out.p(ostd::String("").add(dsize, 2).add(units[unit_index]).new_fixedLength(line_len));
		};
		uint16_t len = 20;
		out.nl().fg(ostd::ConsoleColors::BrightGray);
		out.p(ostd::String("=").new_fixedLength(5 * len, '=')).nl();
		out.fg(ostd::ConsoleColors::Blue);
		out.p("  ");
		out.p(ostd::String("LABEL").new_fixedLength(len));
		out.p(ostd::String("SIZE").new_fixedLength(len));
		out.p(ostd::String("START").new_fixedLength(len));
		out.p(ostd::String("END").new_fixedLength(len));
		out.p(ostd::String("FLAGS").new_fixedLength(len));
		out.nl().fg(ostd::ConsoleColors::BrightGray);
		out.p(ostd::String("=").new_fixedLength(5 * len, '=')).nl();
		for (int32_t i = 0; i < partitionList.size(); i++)
		{
			auto& part = partitionList[i];
			if (part.label == "")
				part.label = "<NO-LABEL>";
			out.fg(ostd::ConsoleColors::Cyan).p("  ");
			out.p(part.label.new_fixedLength(len));
			print_part_size(part.size, out, len);
			out.p(ostd::Utils::getHexStr(part.startAddress, true, 4).new_fixedLength(len));
			out.p(ostd::Utils::getHexStr(part.startAddress + part.size, true, 4).new_fixedLength(len));
			ostd::String flags_str = "";
			for (uint8_t bit = 0; bit < sizeof(part.flags) * 8; bit++)
			{
				if (m_dpt_flags_str.count(bit) == 0)
					continue;
				if (ostd::Bits::get(part.flags, bit))
					flags_str += m_dpt_flags_str[bit] + ",";
			}
			if (flags_str.len() > 0)
				flags_str.substr(0, flags_str.len() - 1);
			
			out.fg(ostd::ConsoleColors::Yellow).p(flags_str).fg(ostd::ConsoleColors::Cyan).nl();
		}
		out.fg(ostd::ConsoleColors::BrightGray);
		out.p(ostd::String("=").new_fixedLength(5 * len, '=')).nl();
		out.reset().nl();
		return ErrorNoError;
	}

	int32_t Tools::tool_new_dpt(int argc, char** argv)
	{
		if (argc < 5)
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
			out.fg(ostd::ConsoleColors::Red).p("  Usage: ./dtools new-dpt <virtual_disk_file> -p -s SIZE [-l LABEL] [-f FLAG1] [-f FLAG2] [-p SIZE ...]").reset().nl();
			return ErrorLoadProgTooFewArgs;
		}
		ostd::String vdisk_file = argv[2];
		dragon::hw::VirtualHardDrive vHDD(vdisk_file);
		if (!vHDD.isInitialized())
		{
			out.fg(ostd::ConsoleColors::Red).p("Error: Unable to load virtual disk.").reset().nl();
			return ErrorLoadProgUnableToLoadVDisk;
		}
		uint64_t disk_size = vHDD.getSize();

		auto& _dpt_flags_str = m_dpt_flags_str;
		auto get_flag_from_str = [_dpt_flags_str](const ostd::String& flag_str) -> int8_t {
			for (auto& flag : _dpt_flags_str)
			{
				if (flag.second == flag_str)
					return flag.first;
			}
			return -1;
		};
		auto make_bytestream = [](uint16_t size, ostd::Byte value = 0xFF) -> ostd::ByteStream {
			
			ostd::ByteStream stream;
			for (int16_t i = 0; i < size; i++)
				stream.push_back(value);
			return stream;
		};
		struct tPartData {
			uint32_t size { 0 };
			uint32_t address { 0 };
			std::vector<uint8_t> flags;
			ostd::String label { "" };
		};

		std::vector<tPartData> partitions;

		int32_t arg_index = 3;
		bool has_args = true;
		bool part_started = false;
		tPartData _part_data;
		uint32_t part_start_addr = data::DPTStructure::DiskStartAddr; 
		while (has_args)
		{
			ostd::String arg = argv[arg_index];
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
					int8_t flag = get_flag_from_str(argv[arg_index]);
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
					else if (!ostd::Utils::isInt(argv[arg_index + 1]))
					{
						out.fg(ostd::ConsoleColors::Red).p("Error: Partition size must be an integer.").reset().nl();
						return ErrorNewDPTInvalidPartitionSize;
					}
					arg_index++;
					uint32_t part_size = ostd::String(argv[arg_index]).toInt();
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

		//HEADER
		ostd::StreamIndex addr = 0;
		ostd::serial::SerialIO dpt_block(data::DPTStructure::DPTBlockSizeBytes);
		dpt_block.enableAutoResize(false);
		dpt_block.w_Word(addr, data::DPTStructure::DPT_ID_CODE);
		addr += ostd::tTypeSize::WORD;
		dpt_block.w_Byte(addr, data::DPTStructure::CurrentDPTVersionMaj);
		addr += ostd::tTypeSize::BYTE;
		dpt_block.w_Byte(addr, data::DPTStructure::CurrentDPTVersionMin);
		addr += ostd::tTypeSize::BYTE;
		dpt_block.w_Byte(addr, (ostd::Byte)(partitions.size()));
		addr += ostd::tTypeSize::BYTE;
		uint16_t reserved_size = data::DPTStructure::HeaderReservedSizeBytes;
		dpt_block.w_ByteStream(addr, make_bytestream(reserved_size), false);
		addr += reserved_size;

		//PARTITIONS
		for (auto& part : partitions)
		{
			dpt_block.w_DWord(addr, part.address);
			addr += ostd::tTypeSize::DWORD;
			dpt_block.w_DWord(addr, part.size);
			addr += ostd::tTypeSize::DWORD;

			ostd::BitField_16 flags;
			flags.value = 0;
			for (auto& bit : part.flags)
				ostd::Bits::set(flags, bit);
			dpt_block.w_Word(addr, flags.value);
			addr += ostd::tTypeSize::WORD;

			reserved_size = data::DPTStructure::EntryReservedSizeBytes;
			dpt_block.w_ByteStream(addr, make_bytestream(reserved_size), false);
			addr += reserved_size;

			if (part.label.len() >= data::DPTStructure::EntryLabelSizeBytes)
				part.label.fixedLength(data::DPTStructure::EntryLabelSizeBytes - 1, ' ', "");
			dpt_block.w_String(addr, part.label, false, true);
			addr += data::DPTStructure::EntryLabelSizeBytes;
		}
		
		int16_t index = 0;
		for (auto& b : dpt_block.getData())
		{
			vHDD.write(data::DPTStructure::DiskAddress + index, b);
			index++;
		}
		vHDD.unmount();
		out.nl().fg(ostd::ConsoleColors::Green).p("Success. DPT Block created on Virtual Disk:").nl();
		out.p("  Disk Path: ").p(vdisk_file.cpp_str()).nl();
		out.p("  DPT Block Address: ").p(ostd::Utils::getHexStr(data::DPTStructure::DiskAddress, true, 4).cpp_str()).nl();
		out.p("  DPT Block Size: ").p(data::DPTStructure::DPTBlockSizeBytes).nl();
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