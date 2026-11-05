#include "DisassemblyLoader.hpp"

#include <ostd/io/File.hpp>
#include <ostd/io/FileSystem.hpp>
#include <ostd/io/Memory.hpp>
#include "../tools/LegacyOstdSerial.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <ostd/string/String.hpp>

namespace dragon
{
	const DisassemblyTable DisassemblyTable::DefaultObject;

	void DisassemblyTable::init(const String& filePath)
	{
		m_filePath = filePath;
		ostd::ByteStream stream;
		if (!ostd::Memory::loadByteStreamFromFile(filePath, stream))
		{
			m_initialized = false;
			return;
		}
		load_data(stream);
	}

	void DisassemblyTable::load_data(ostd::ByteStream& stream)
	{
		constexpr i32 MODE_CODE = 0, MODE_DATA = 1, MODE_LABELS = 2, MODE_LINES = 3, MODE_SOURCES = 4;
		struct tLoc { i32 file; i32 line; i32 depth; bool statement; };
		std::map<u32, tLoc> locations;
		std::map<i32, tSourceFile> sources;
		i32 mode = MODE_CODE;
		ostd::serial::SerialIO serializer(stream, ostd::serial::SerialIO::tEndianness::BigEndian);
		ostd::StreamIndex addr = 0;
		i32 line_addr = 0;
		i16 data_size = 1;
		i8 line_code_char = 0;
		String header_string = "";
		serializer.r_NullTerminatedString(0, header_string);
		if (header_string != "{ DRAGON_DEBUG_DISASSEMBLY }") return;
		addr += (header_string.len() + 1) * ostd::tTypeSize::BYTE;
		while (addr < serializer.size())
		{
			serializer.r_DWord(addr, line_addr);
			addr += ostd::tTypeSize::DWORD;
			serializer.r_Word(addr, data_size);
			addr += ostd::tTypeSize::WORD;
			String code_line = "";
			serializer.r_NullTerminatedString(addr, code_line);
			addr += (code_line.len() + 1) * ostd::tTypeSize::BYTE;
			if (code_line.startsWith("{ NAME } "))
			{
				m_name = code_line.new_substr(9).new_trim();
				continue;
			}
			if (code_line.startsWith("{ VERSION } "))
			{
				m_version = cast<i32>(code_line.new_substr(12).new_trim().toInt());
				continue;
			}
			if (code_line == "{ DATA }")
			{
				mode = MODE_DATA;
				continue;
			}
			else if (code_line == "{ LINES }")
			{
				mode = MODE_LINES;
				continue;
			}
			else if (code_line == "{ SOURCES }")
			{
				mode = MODE_SOURCES;
				continue;
			}
			if (mode == MODE_LINES)
			{
				tLoc loc { cast<i32>(cast<u16>(data_size)), 0, 0, false };
				i32 statement = 0;
				std::sscanf(code_line.cpp_str().c_str(), "%d %d %d", &loc.line, &loc.depth, &statement);
				loc.statement = (statement != 0);
				locations[cast<u32>(line_addr)] = loc;
				continue;
			}
			if (mode == MODE_SOURCES)
			{
				const std::string text = code_line.cpp_str();
				const size_t nl = text.find('\n');
				tSourceFile file;
				file.path = text.substr(0, nl);
				file.name = String(std::filesystem::path(file.path.cpp_str()).filename().string());
				size_t start = (nl == std::string::npos ? text.size() : nl + 1);
				while (start < text.size())
				{
					size_t end = text.find('\n', start);
					if (end == std::string::npos)
						end = text.size();
					std::string l = text.substr(start, end - start);
					if (!l.empty() && l.back() == '\r')
						l.pop_back();
					file.lines.push_back(String(l));
					start = end + 1;
				}
				sources[line_addr] = file;
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
				String codeEdit(line.code);
				codeEdit.trim();
				if (codeEdit.contains(" "))
				{
					String part1 = codeEdit.new_substr(0, codeEdit.indexOf(" "));
					String part2 = codeEdit.new_substr(codeEdit.indexOf(" ") + 1);
					part1.trim();
					part2.trim();
					i32 opCodeLen = 10;
					if (part1.len() < opCodeLen)
					{
						codeEdit = part1 + String::duplicateChar(' ', opCodeLen - part1.len()) + part2;
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
		// The source files, numbered from 0 here (the file's indices can have gaps).
		std::map<i32, i32> fileIndex;
		for (auto& [index, file] : sources)
		{
			fileIndex[index] = cast<i32>(m_sources.size());
			m_sources.push_back(file);
		}
		for (auto& line : m_code)
		{
			auto it = locations.find(cast<u32>(line.addr));
			if (it == locations.end() || fileIndex.count(it->second.file) == 0)
				continue;
			line.srcFile = fileIndex[it->second.file];
			line.srcLine = it->second.line;
			line.inlineDepth = cast<u8>(std::clamp(it->second.depth, 0, 255));
			line.statement = it->second.statement;
		}
		std::stable_sort(m_code.begin(), m_code.end());
		std::sort(m_labels.begin(), m_labels.end());
		std::sort(m_data.begin(), m_data.end());
		m_initialized = true;
	}



	void DisassemblyLoader::loadDirectory(const String& directoryPath)
	{
		auto list = ostd::FileSystem::listFilesInDirectory(directoryPath);
		for (auto& path : list)
			loadFile(path.string());
	}

	const DisassemblyTable& DisassemblyLoader::loadFile(const String& filePath)
	{
		DisassemblyTable table(filePath);
		if (!table.isInitialized()) return DisassemblyTable::DefaultObject; //TODO: Error
		m_tables.push_back(table);
		return m_tables[m_tables.size() - 1];
	}

	std::vector<tSourceFile> DisassemblyLoader::getSources(void)
	{
		std::vector<tSourceFile> sources;
		for (auto& table : m_tables)
			sources.insert(sources.end(), table.getSources().begin(), table.getSources().end());
		return sources;
	}

	i32 DisassemblyLoader::getSourceOffset(const DisassemblyTable& table)
	{
		i32 offset = 0;
		for (auto& t : m_tables)
		{
			if (&t == &table)
				return offset;
			offset += cast<i32>(t.getSources().size());
		}
		return 0;
	}

	std::vector<code::Assembler::tDisassemblyLine> DisassemblyLoader::getCodeTable(void)
	{
		std::vector<code::Assembler::tDisassemblyLine> fullTable;
		i32 offset = 0;
		for (auto& table : m_tables)
		{
			for (auto line : table.getCodeTable())
			{
				if (line.srcFile >= 0)
					line.srcFile += offset;
				fullTable.push_back(line);
			}
			offset += cast<i32>(table.getSources().size());
		}
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
