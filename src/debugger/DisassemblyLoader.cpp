#include "DisassemblyLoader.hpp"

#include <ostd/File.hpp>
#include <ostd/Serial.hpp>

#include "../tools/Utils.hpp"

#include <algorithm>

namespace dragon
{
	const DisassemblyTable DisassemblyTable::DefaultObject;

	void DisassemblyTable::init(const ostd::String& filePath)
	{
		ostd::ByteStream stream;
		if (!ostd::Utils::loadByteStreamFromFile(filePath, stream))
		{
			m_initialized = false;
			return;
		}
		load_data(stream);
	}

	void DisassemblyTable::load_data(ostd::ByteStream& stream)
	{
		constexpr int32_t MODE_CODE = 0, MODE_DATA = 1, MODE_LABELS = 2;
		int32_t mode = MODE_CODE;
		ostd::serial::SerialIO serializer(stream, ostd::serial::SerialIO::tEndianness::BigEndian);
		ostd::StreamIndex addr = 0;
		int32_t line_addr = 0;
		int16_t data_size = 1;
		int8_t line_code_char = 0;
		ostd::String header_string = "";
		serializer.r_NullTerminatedString(0, header_string);
		if (header_string != "{ DRAGON_DEBUG_DISASSEMBLY }") return;
		addr += (header_string.len() + 1) * ostd::tTypeSize::BYTE;
		while (addr < serializer.size())
		{
			serializer.r_DWord(addr, line_addr);
			addr += ostd::tTypeSize::DWORD;
			serializer.r_Word(addr, data_size);
			addr += ostd::tTypeSize::WORD;
			ostd::String code_line = "";
			serializer.r_NullTerminatedString(addr, code_line);
			addr += (code_line.len() + 1) * ostd::tTypeSize::BYTE;
			if (code_line == "{ DATA }")
			{
				mode = MODE_DATA;
				continue;
			}
			else if (code_line == "{ LABELS }")
			{
				mode = MODE_LABELS;
				continue;
			}
			else if (code_line == "[----------DATA_SECTION----------]")
			{
				continue; //TODO: Store addresses for various data sections
			}

			code::Assembler::tDisassemblyLine line;
			line.addr = line_addr;
			line.code = code_line;
			line.size = data_size;
			if (mode == MODE_CODE)
			{
				ostd::String codeEdit(line.code);
				codeEdit.trim();
				if (codeEdit.contains(" "))
				{
					ostd::String part1 = codeEdit.new_substr(0, codeEdit.indexOf(" "));
					ostd::String part2 = codeEdit.new_substr(codeEdit.indexOf(" ") + 1);
					part1.trim();
					part2.trim();
					int32_t opCodeLen = 10;
					if (part1.len() < opCodeLen)
					{
						codeEdit = part1 + ostd::Utils::duplicateChar(' ', opCodeLen - part1.len()) + part2;
						line.code = codeEdit;
					}
				}
				m_code.push_back(line);
			}
			else if (mode == MODE_DATA)
				m_data.push_back(line);
			else if (mode == MODE_LABELS)
				m_labels.push_back(line);
		}
		m_initialized = true;
	}



	void DisassemblyLoader::loadDirectory(const ostd::String& directoryPath)
	{
		auto list = ostd::Utils::listFilesInDirectory(directoryPath);
		for (auto& path : list)
			loadFile(path.string());
	}

	const DisassemblyTable& DisassemblyLoader::loadFile(const ostd::String& filePath)
	{
		DisassemblyTable table(filePath);
		if (!table.isInitialized()) return DisassemblyTable::DefaultObject; //TODO: Error
		m_tables.push_back(table);
		return m_tables[m_tables.size() - 1];
	}

	std::vector<code::Assembler::tDisassemblyLine> DisassemblyLoader::getCodeTable(void)
	{
		std::vector<code::Assembler::tDisassemblyLine> fullTable;
		for (auto& table : m_tables)
			fullTable.insert(fullTable.end(), table.getCodeTable().begin(), table.getCodeTable().end());
		std::sort(fullTable.begin(), fullTable.end());
		return fullTable;
	}

	std::vector<code::Assembler::tDisassemblyLine> DisassemblyLoader::getDataTable(void)
	{
		std::vector<code::Assembler::tDisassemblyLine> fullTable;
		for (auto& table : m_tables)
			fullTable.insert(fullTable.end(), table.getDataTable().begin(), table.getDataTable().end());
		std::sort(fullTable.begin(), fullTable.end());
		return fullTable;
	}

	std::vector<code::Assembler::tDisassemblyLine> DisassemblyLoader::getLabelTable(void)
	{
		std::vector<code::Assembler::tDisassemblyLine> fullTable;
		for (auto& table : m_tables)
			fullTable.insert(fullTable.end(), table.getLabelTable().begin(), table.getLabelTable().end());
		std::sort(fullTable.begin(), fullTable.end());
		return fullTable;
	}
}