#include "Assembler.hpp"
#include <ostd/File.hpp>
#include <iostream>
#include <fstream>
#include <ostd/Utils.hpp>
#include <ostd/Serial.hpp>
#include <ostd/IOHandlers.hpp>

#include "../tools/GlobalData.hpp"
#include "../hardware/VirtualHardDrive.hpp"
#include "../tools/Utils.hpp"

namespace dragon
{
	namespace code
	{
		int32_t Assembler::Application::loadArguments(int argc, char** argv)
		{
			if (argc < 2)
			{
				out.fg(ostd::ConsoleColors::Red).p("Error: too few arguments.").nl();
				out.fg(ostd::ConsoleColors::Red).p("Use the --help option for more info.").reset().nl();
				return RETURN_VAL_TOO_FEW_ARGUMENTS;
			}
			else
			{
				args.source_file_path = argv[1];
				if (args.source_file_path == "--help")
				{
					print_application_help();
					return RETURN_VAL_CLOSE_PROGRAM;
				}
				for (int32_t i = 2; i < argc; i++)
				{
					ostd::String edit(argv[i]);
					if (edit == "-o")
					{
						if (i == argc - 1)
							return RETURN_VAL_MISSING_PARAM;
						i++;
						args.dest_file_path = argv[i];
					}
					else if (edit == "--save-disassembly")
					{
						if (i == argc - 1)
							return RETURN_VAL_MISSING_PARAM;
						i++;
						args.disassembly_file_path = argv[i];
						args.save_disassembly = true;
					}
					else if (edit == "--help")
					{
						print_application_help();
						return RETURN_VAL_CLOSE_PROGRAM;
					}
					else if (edit == "--verbose")
						args.verbose = true;
				}
			}
			return RETURN_VAL_EXIT_SUCCESS;
		}

		void Assembler::Application::print_application_help(void)
		{
			int32_t commandLength = 46;

			out.nl().fg(ostd::ConsoleColors::Yellow).p("List of available parameters:").reset().nl();
			ostd::String tmpCommand = "--save-disassembly <destination-directory>";
			tmpCommand.addRightPadding(commandLength);
			out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Saves debug information in the destination directory.").reset().nl();
			tmpCommand = "--verbose";
			tmpCommand.addRightPadding(commandLength);
			out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Shows more information about the assembled program.").reset().nl();
			tmpCommand = "-o <destination-binary-file>";
			tmpCommand.addRightPadding(commandLength);
			out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Used to specify the output binary file.").reset().nl();
			tmpCommand = "--help";
			tmpCommand.addRightPadding(commandLength);
			out.fg(ostd::ConsoleColors::Blue).p(tmpCommand).fg(ostd::ConsoleColors::Green).p("Displays this help message.").reset().nl();
			
			out.nl().fg(ostd::ConsoleColors::Magenta).p("Usage: ./dasm <source> -o <destination> [...options...]").reset().nl();
			out.nl();
		}




		ostd::ByteStream Assembler::assembleFromFile(ostd::String fileName)
		{
			m_rawSource = "";
			m_code.clear();
			m_lines.clear();
			m_rawDataSection.clear();
			m_rawCodeSection.clear();
			m_symbolTable.clear();
			m_labelTable.clear();
			m_disassembly.clear();
			m_structDefs.clear();
			m_fixedSize = 0;
			m_fixedFillValue = 0x00;
			m_loadAddress = 0x0000;
			m_currentDataAddr = 0x0000;
			m_dataSize = 0x0000;
			ostd::TextFileBuffer file(fileName);
			loadSource(file.rawContent());
			removeComments();
			replaceGroupDefines();
			replaceDefines();
			parseStructures();
			replaceDefines();
			parseSections();
			parseStructInstances();
			parseDataSection();
			parseCodeSection();
			replaceLabelRefs();
			combineDataAndCode();
			m_programSize = m_code.size();
			if (m_fixedSize > 0 && m_code.size() > m_fixedSize)
				std::cout << "Warning: Fixed size specified but exceeded: (" << (int)m_code.size() << "/" << (int)m_fixedSize << " bytes)\n";
			else if (m_fixedSize > 0 && m_code.size() < m_fixedSize)
			{
				for (int16_t i = m_code.size(); i < m_fixedSize; i++)
					m_code.push_back(m_fixedFillValue);
			}
			return m_code;
		}

		void Assembler::loadSource(ostd::String source)
		{
			m_rawSource = source;
			m_lines = ostd::String(source).tokenize("\n").getRawData();
		}

		ostd::ByteStream Assembler::assembleToFile(ostd::String sourceFileName, ostd::String binaryFileName)
		{
			assembleFromFile(sourceFileName);
			if (m_code.size() == 0) return {  };
            ostd::Utils::saveByteStreamToFile(m_code, binaryFileName);
			return m_code;
		}

		ostd::ByteStream Assembler::assembleToVirtualDisk(ostd::String fileName, hw::VirtualHardDrive& vhdd, uint32_t address)
		{
			assembleFromFile(fileName);
			if (m_code.size() == 0) return {  };
			for (int32_t i = 0; i < m_code.size(); i++)
				vhdd.write(address + i, m_code[i]);
			return m_code;
		}

		bool Assembler::saveDisassemblyToFile(ostd::String fileName)
		{
			if (m_code.size() == 0 || m_disassembly.size() == 0) return false;
			ostd::String header_string = "{ DRAGON_DEBUG_DISASSEMBLY }";
			uint64_t da_size = 0;
			da_size += (header_string.len() + 1) * ostd::tTypeSize::BYTE;
			da_size += m_disassembly.size() * ostd::tTypeSize::DWORD; //Addresses
			da_size += m_disassembly.size() * ostd::tTypeSize::WORD; //Data Size
			da_size += m_disassembly.size() * ostd::tTypeSize::BYTE; //Null Termination
			for (auto& da : m_disassembly)
				da_size += da.code.len() * ostd::tTypeSize::BYTE; //Code Strings
			ostd::serial::SerialIO serializer(da_size, ostd::serial::SerialIO::tEndianness::BigEndian);
			ostd::StreamIndex stream_addr = 0;
			serializer.w_String(stream_addr, header_string);
			stream_addr += (header_string.len() + 1) * ostd::tTypeSize::BYTE;
			for (auto& da : m_disassembly)
			{
				serializer.w_DWord(stream_addr, da.addr);
				stream_addr += ostd::tTypeSize::DWORD;
				serializer.w_Word(stream_addr, (int16_t)da.size);
				stream_addr += ostd::tTypeSize::WORD;
				serializer.w_String(stream_addr, da.code);
				stream_addr += (da.code.len() + 1) * ostd::tTypeSize::BYTE;
			}
			return serializer.saveToFile(fileName);
		}

		void Assembler::removeComments(void)
		{
			std::vector<ostd::String> newLines;
			ostd::String lineEdit;
			for (auto& line : m_lines)
			{
				lineEdit = line;
				lineEdit.trim();
				if (lineEdit.startsWith("##"))
					continue;
				if (lineEdit.contains("##"))
				{
					lineEdit = lineEdit.substr(0, lineEdit.indexOf("##"));
					lineEdit.trim();
				}
				newLines.push_back(lineEdit);
			}
			m_lines.clear();
			m_lines = newLines;
		}

		void Assembler::replaceDefines(void)
		{
			struct tDefine {
				ostd::String name;
				ostd::String value;
			};
			auto listContainsDefine = [](const std::vector<tDefine>& list, const ostd::String& name) -> bool {
				for (auto& def : list)
				{
					if (def.name == name)
						return true;
				}
				return false;
			};
			std::vector<tDefine> defines;
			std::vector<ostd::String> newLines;
			ostd::String lineEdit;
			ostd::String tmpLineEdit;
			for (auto& line : m_lines)
			{
				lineEdit = line;
				lineEdit.trim();
				tmpLineEdit = lineEdit;
				if (tmpLineEdit.toLower().startsWith("@define "))
				{
					lineEdit = lineEdit.substr(8);
					lineEdit.trim();
					if (!lineEdit.contains(" "))
					{
						std::cout << "Invalid @define directive: " << line << "\n";
						return;
					}
					ostd::String define_name = lineEdit.new_substr(0, lineEdit.indexOf(" "));
					define_name = ostd::String(define_name).trim();
					ostd::String define_value = lineEdit.new_substr(lineEdit.indexOf(" ") + 1);
					if (listContainsDefine(defines, define_name))
					{
						std::cout << "Redefinition of @define value: " << line << "\n";
						return;
					}
					defines.push_back({ define_name, define_value });
					continue;
				}
				newLines.push_back(lineEdit);
			}
			for (auto& line : newLines)
			{
				ostd::String lineEdit(line);
				for (int32_t i = defines.size() - 1; i >= 0; i--)
					lineEdit.replaceAll(defines[i].name, defines[i].value);
				line = lineEdit;
			}
			m_lines.clear();
			m_lines = newLines;
		}

		void Assembler::replaceGroupDefines(void)
		{
			std::vector<ostd::String> newLines;
			ostd::String lineEdit;
			bool in_group = false;
			ostd::String group_name = "";
			for (auto& line : m_lines)
			{
				lineEdit = line;
				lineEdit.trim();
				if (lineEdit.new_toLower().startsWith("@group "))
				{
					if (in_group)
					{
						std::cout << "Nested define groups not allowed: " << line << "\n";
						return;
					}
					lineEdit.substr(6).trim();
					if (lineEdit == "")
					{
						std::cout << "Group name cannot be empty: " << line << "\n";
						return;
					}
					group_name = lineEdit;
					in_group = true;
					continue;
				}
				else if (in_group && lineEdit.new_toLower() == "@end")
				{
					if (!in_group)
					{
						std::cout << "Missing group declaration for @end directive: " << line << "\n";
						return;
					}
					in_group = false;
					continue;
				}
				if (!in_group)
				{
					newLines.push_back(lineEdit);
					continue;
				}
				if (!lineEdit.contains(" "))
				{
					std::cout << "Invalid definition inside group: " << line << "\n";
					return;
				}
				ostd::String newLine = "@define ";
				newLine.add(group_name).add(".").add(lineEdit);
				newLines.push_back(newLine);
			}
			m_lines.clear();
			m_lines = newLines;
		}

		void Assembler::parseSections(void)
		{
			constexpr uint8_t DATA_SECTION = 0x01;
			constexpr uint8_t CODE_SECTION = 0x02;

			uint8_t currentSection = 0x00;

			for (auto& line : m_lines)
			{
				ostd::String lineEdit(line);
				if (lineEdit.startsWith(".data"))
					currentSection = DATA_SECTION;
				else if (lineEdit.startsWith(".code"))
					currentSection = CODE_SECTION;
				else if (lineEdit.startsWith(".fixed "))
				{
					if (lineEdit.len() < 8)
					{
						//TODO: Error 
						std::cout << "Invalid .fixed value: " << lineEdit << "\n";
						return;
					}
					lineEdit = lineEdit.substr(7);
					lineEdit.trim();
					if (!lineEdit.contains(","))
					{
						//TODO: Error 
						std::cout << "Invalid .fixed value: " << lineEdit << "\n";
						return;
					}
					ostd::String fixedSizeEdit = lineEdit.new_substr(0, lineEdit.indexOf(","));
					fixedSizeEdit.trim();
					lineEdit = lineEdit.substr(lineEdit.indexOf(",") + 1);
					lineEdit.trim();
					if (!lineEdit.isNumeric())
					{
						//TODO: Error 
						std::cout << "Invalid .fixed size value: " << lineEdit << "\n";
						return;
					}
					m_fixedFillValue = lineEdit.toInt();
					if (!fixedSizeEdit.isNumeric())
					{
						//TODO: Error 
						std::cout << "Invalid .fixed fill value: " << lineEdit << "\n";
						return;
					}
					m_fixedSize = fixedSizeEdit.toInt();
					continue;
				}
				else if (lineEdit.startsWith(".load "))
				{
					if (lineEdit.len() < 7)
					{
						//TODO: Error 
						std::cout << "Invalid .load value: " << lineEdit << "\n";
						return;
					}
					lineEdit = lineEdit.substr(6);
					lineEdit.trim();
					if (!lineEdit.isNumeric())
					{
						//TODO: Error 
						std::cout << "Invalid .load value: " << lineEdit << "\n";
						return;
					}
					m_loadAddress = lineEdit.toInt();
					m_currentDataAddr = m_loadAddress + 3;
					continue;
				}
				else if (lineEdit.startsWith("."))
				{
					//TODO: Error 
					std::cout << "Invalid section: " << lineEdit << "\n";
					return;
				}
				else
				{
					if (currentSection == DATA_SECTION)
						m_rawDataSection.push_back(line);
					else if (currentSection == CODE_SECTION)
						m_rawCodeSection.push_back(line);
					else
					{
						//TODO: Error 
						std::cout << "Invalid section: " << lineEdit << "\n";
						return;
					}
				}
			}
		}

		void Assembler::parseStructInstances(void)
		{
			std::vector<ostd::String> newLines;
			ostd::String lineEdit;
			bool in_group = false;
			ostd::String group_name = "";
			for (auto& line : m_rawDataSection)
			{
				lineEdit = line;
				lineEdit.trim();
				if (!lineEdit.startsWith("$") || lineEdit.indexOf(" ") < 2)
				{
					std::cout << "Invalid data entry: " << lineEdit << "\n";
					return;
				}
				ostd::String symbolName = lineEdit.new_substr(0, lineEdit.indexOf(" "));
				symbolName.trim();
				lineEdit.substr(lineEdit.indexOf(" ") + 1);
				lineEdit.trim();
				ostd::String initialization_data = "";
				bool has_init_data = false;
				if (lineEdit.startsWith("<") && lineEdit.contains("="))
				{
					initialization_data = lineEdit.new_substr(lineEdit.indexOf("=") + 1).trim();
					if (initialization_data.startsWith("(") && initialization_data.endsWith(")"))
					{
						initialization_data.substr(1, initialization_data.len() - 1).trim();
						has_init_data = initialization_data != "";
					}
					else
					{
						std::cout << "Invalid initialization data: " << lineEdit << "\n";
						return;
					}
					lineEdit.substr(0, lineEdit.indexOf("=")).trim();
				}
				if (lineEdit.startsWith("<") && lineEdit.endsWith(">") && lineEdit.len() > 2)
				{
					lineEdit = lineEdit.substr(1, lineEdit.len() - 1);
					std::vector<uint8_t> init_data;
					if (has_init_data)
					{
						auto tokens = initialization_data.tokenize(",");
						while (tokens.hasNext())
						{
							ostd::String tok = tokens.next();
							if (!tok.isNumeric())
							{
								std::cout << "Invalid initialization data: " << lineEdit << "\n";
								return;
							}
							init_data.push_back((uint8_t)tok.toInt());
						}
					}
					tStructDefinition struct_def;
					bool found = false;
					for (auto& _struct : m_structDefs)
					{
						if (_struct.name == lineEdit)
						{
							struct_def = _struct;
							found = true;
							break;
						}
					}
					if (!found)
					{
						std::cout << "Unknown Structure name: " << lineEdit << "\n";
						return;
					}
					if (has_init_data && struct_def.size != init_data.size())
					{
						std::cout << "Structure size must match initialization data size: " << lineEdit << "\n";
						return;
					}
					int32_t data_index = 0;
					for (auto& member : struct_def.members)
					{
						ostd::String newLine = symbolName;
						newLine.add(".").add(member.name).add(" ");
						for (int32_t i = 0; i < member.data.size(); i++, data_index++)
						{
							if (has_init_data)
								newLine.add(ostd::Utils::getHexStr(init_data[data_index], true, 2));
							else
								newLine.add(ostd::Utils::getHexStr(member.data[i], true, 2));
							newLine.add(",");
						}
						newLine.substr(0, newLine.len() - 1);
						newLines.push_back(newLine);
					}
					continue;
				}
				newLines.push_back(line);
			}
			m_rawDataSection.clear();
			m_rawDataSection = newLines;
		}

		void Assembler::parseStructures(void)
		{
			std::vector<ostd::String> newLines;
			ostd::String lineEdit;
			bool in_struct = false;
			ostd::String struct_name = "";
			tStructDefinition struct_def;
			int32_t member_index = 0;
			for (auto& line : m_lines)
			{
				lineEdit = line;
				lineEdit.trim();
				if (lineEdit.new_toLower().startsWith("@struct "))
				{
					if (in_struct)
					{
						std::cout << "Nested structs not allowed: " << line << "\n";
						return;
					}
					lineEdit.substr(7).trim();
					if (lineEdit == "")
					{
						std::cout << "Struct name cannot be empty: " << line << "\n";
						return;
					}
					struct_name = lineEdit;
					struct_def.members.clear();
					struct_def.name = struct_name;
					member_index = 0;
					in_struct = true;
					continue;
				}
				else if (in_struct && lineEdit.new_toLower() == "@end")
				{
					if (!in_struct)
					{
						std::cout << "Missing struct declaration for @end directive: " << line << "\n";
						return;
					}
					struct_def.size = 0;
					for (auto& data : struct_def.members)
						struct_def.size += data.data.size();
					std::sort(struct_def.members.begin(), struct_def.members.end());
					m_structDefs.push_back(struct_def);
					ostd::String size_def = "@define ";
					size_def.add(struct_def.name).add(".").add("SIZE").add(" ");
					size_def.add(ostd::Utils::getHexStr(struct_def.size, true, 2));
					newLines.push_back(size_def);
					in_struct = false;
					continue;
				}
				if (!in_struct)
				{
					newLines.push_back(lineEdit);
					continue;
				}
				if (!lineEdit.contains(":"))
				{
					std::cout << "Invalid definition inside struct. Size specification missing: " << line << "\n";
					return;
				}
				ostd::String member_name = lineEdit.new_substr(0, lineEdit.indexOf(":")).trim();
				ostd::String member_data = "";
				lineEdit.substr(lineEdit.indexOf(":") + 1).trim();
				if (member_name.contains(" "))
				{
					std::cout << "Invalid definition inside struct. Member name cannot contain spaces: " << line << "\n";
					return;
				}
				if (lineEdit.contains(">"))
				{
					member_data = lineEdit.new_substr(lineEdit.indexOf(">") + 1).trim();
					lineEdit.substr(0, lineEdit.indexOf(">")).trim();
				}
				if (!lineEdit.isNumeric())
				{
					std::cout << "Invalid definition inside struct. Member size must be numeric: " << line << "\n";
					return;
				}
				int32_t bytes = lineEdit.toInt();
				if (bytes < 1)
				{
					std::cout << "Invalid definition inside struct. Member must be at least 1 Byte: " << line << "\n";
					return;
				}
				tStructMember member;
				member.position = member_index++;
				member.name = member_name;
				if (member_data == "")
				{
					for (int32_t i = 0; i < bytes; i++)
						member.data.push_back(0x00);
				}
				else
				{
					auto tokens = member_data.tokenize(",");
					if (tokens.count() == 1)
					{
						ostd::String tok = tokens.next();
						if (tok.isNumeric())
						{
							uint8_t data = (uint8_t)tok.toInt();
							for (int32_t i = 0; i < bytes; i++)
								member.data.push_back(data);
						}
						else
						{
							std::cout << "Invalid definition inside struct. Member data must be numeric: " << line << "\n";
							return;
						}
					}
					else
					{
						if (tokens.count() != bytes)
						{
							std::cout << "Invalid definition inside struct. Member data count and size mismatch: " << line << "\n";
							return;
						}
						while (tokens.hasNext())
						{
							ostd::String tok = tokens.next();
							if (tok.isNumeric())
							{
								uint8_t data = (uint8_t)tok.toInt();
								member.data.push_back(data);
							}
							else
							{
								std::cout << "Invalid definition inside struct. Member data must be numeric: " << line << "\n";
								return;
							}
						}
					}
				}
				struct_def.members.push_back(member);
			}
			m_lines.clear();
			m_lines = newLines;

			// for (auto& str : m_structDefs)
			// {
			// 	std::cout << str.name << "\n";
			// 	for(auto& d : str.memberData)
			// 	{
			// 		std::cout << "  " << d.first << "  ";
			// 		for (auto& b : d.second)
			// 			std::cout << ostd::Utils::getHexStr(b) << " "; 
			// 	}
			// 	std::cout << "\n";
			// }
			// std::cin.get();
		}

		void Assembler::parseDataSection(void)
		{
			for (auto& line : m_rawDataSection)
			{
				ostd::String lineEdit(line);
				tSymbol symbol;
				if (!lineEdit.startsWith("$") || lineEdit.indexOf(" ") < 2)
				{
					std::cout << "Invalid data entry: " << lineEdit << "\n";
					continue;
				}
				ostd::String symbolName = lineEdit.new_substr(0, lineEdit.indexOf(" "));
				symbolName.trim();
				lineEdit.substr(lineEdit.indexOf(" ") + 1);
				lineEdit.trim();
				if (lineEdit.isNumeric())
				{
					// union valueSplit {
					// 	uint16_t value;
					// 	uint8_t bytes[2];
					// } split;
					// split.value = lineEdit.toInt();
					// symbol.bytes.push_back(split.bytes[0]);
					// symbol.bytes.push_back(split.bytes[1]);
					// symbol.address = m_currentDataAddr;
					// m_currentDataAddr += 2;
					int8_t value = lineEdit.toInt();
					symbol.bytes.push_back(value);
					symbol.address = m_currentDataAddr;
					m_currentDataAddr += 1;
					m_symbolTable[symbolName] = symbol;
					continue;
				}
				else if (lineEdit.startsWith("\"") && lineEdit.endsWith("\"") && lineEdit.len() > 2)
				{
					lineEdit = lineEdit.substr(1, lineEdit.len() - 1);
					for (auto c : lineEdit)
						symbol.bytes.push_back((uint8_t)c);
					symbol.bytes.push_back((uint8_t)0);   //NULL Termination
					symbol.address = m_currentDataAddr;
					m_currentDataAddr += symbol.bytes.size();
					m_symbolTable[symbolName] = symbol;
					continue;
				}
				else if (lineEdit.contains(","))
				{
					auto tokens = lineEdit.tokenize(",");
					if (tokens.count() < 2)
					{
						std::cout << "Invalid data entry: " << lineEdit << "\n";
						return;
					}
					for (auto& token : tokens)
					{
						lineEdit = token;
						if (!lineEdit.isNumeric())
						{
							std::cout << "Invalid byte in data entry: " << lineEdit << "\n";
							return;
						}
						symbol.bytes.push_back((uint8_t)lineEdit.toInt());
					}
					symbol.address = m_currentDataAddr;
					m_currentDataAddr += symbol.bytes.size();
					m_symbolTable[symbolName] = symbol;
					continue;
				}
				else
				{
					std::cout << "Invalid data entry: " << lineEdit << "\n";
					continue;
				}
			}

			for (auto& symbol : m_symbolTable)
			{
				m_dataSize += symbol.second.bytes.size();
			}
		}

		void Assembler::parseCodeSection(void)
		{
			for (auto& line : m_rawCodeSection)
			{
				ostd::String lineEdit(line);
				uint32_t commaCount = lineEdit.count(",");
				uint32_t spaceCount = lineEdit.count(" ");
				if (lineEdit.endsWith(":") && commaCount == 0 && spaceCount == 0) //Labels
				{
					lineEdit = lineEdit.substr(0, lineEdit.len() - 1);
					lineEdit.trim();
					m_labelTable["$" + lineEdit] = { { }, 0x0000 };
					continue;
				}
			}

			tDisassemblyLine _disassembly_line;
			for (auto& line : m_rawCodeSection)
			{
				ostd::String lineEdit(line);
				uint32_t commaCount = lineEdit.count(",");
				uint32_t spaceCount = lineEdit.count(" ");
				if (lineEdit.endsWith(":") && commaCount == 0 && spaceCount == 0) //Labels
				{
					// _disassembly_line.addr = m_dataSize + m_loadAddress + m_code.size() + 3;
					// _disassembly_line.code = lineEdit;
					// m_disassembly.push_back(_disassembly_line);
					lineEdit = lineEdit.substr(0, lineEdit.len() - 1);
					lineEdit.trim();
					m_labelTable["$" + lineEdit].address = m_dataSize + m_loadAddress + m_code.size() + 3;
					continue;
				}
				else if (commaCount == 0 && spaceCount <= 0) //0 Operands
				{
					_disassembly_line.addr = m_dataSize + m_loadAddress + m_code.size() + 3;
					ostd::String _tmp_edit(lineEdit);
					if (_tmp_edit.toLower().trim().startsWith("debug_"))
						parseDebugOperands(lineEdit);
					else
						parse0Operand(lineEdit);
					_disassembly_line.code = lineEdit;
					m_disassembly.push_back(_disassembly_line);
					continue;
				}
				else if (commaCount == 0) //1 Operands
				{
					_disassembly_line.addr = m_dataSize + m_loadAddress + m_code.size() + 3;
					lineEdit = replaceSymbols(lineEdit);
					parse1Operand(lineEdit);
					_disassembly_line.code = lineEdit;
					m_disassembly.push_back(_disassembly_line);
					continue;
				}
				else if (commaCount == 1) //2 Operands
				{
					_disassembly_line.addr = m_dataSize + m_loadAddress + m_code.size() + 3;
					lineEdit = replaceSymbols(lineEdit);
					parse2Operand(lineEdit);
					_disassembly_line.code = lineEdit;
					m_disassembly.push_back(_disassembly_line);
					continue;
				}
				else if (commaCount == 2) // 3 Operands
				{
					_disassembly_line.addr = m_dataSize + m_loadAddress + m_code.size() + 3;
					lineEdit = replaceSymbols(lineEdit);
					parse3Operand(lineEdit);
					_disassembly_line.code = lineEdit;
					m_disassembly.push_back(_disassembly_line);
					continue;
				}
				else
				{
					std::cout << "Invalid syntax: " << lineEdit << "\n";
					return;
				}
			}
		}

		void Assembler::parse0Operand(ostd::String line)
		{
			if (ostd::String(line).toLower().startsWith("nop"))
			{
				m_code.push_back(data::OpCodes::NoOp);
				return;
			}
			else if (ostd::String(line).toLower().startsWith("ret"))
			{
				m_code.push_back(data::OpCodes::Ret);
				return;
			}
			else if (ostd::String(line).toLower().startsWith("rti"))
			{
				m_code.push_back(data::OpCodes::RetInt);
				return;
			}
			else if (ostd::String(line).toLower().startsWith("hlt"))
			{
				m_code.push_back(data::OpCodes::Halt);
				return;
			}
		}

		void Assembler::parseDebugOperands(ostd::String line)
		{
			if (ostd::String(line).toLower().startsWith("debug_break"))
			{
				m_code.push_back(data::OpCodes::DEBUG_Break);
				return;
			}
		}

		void Assembler::parse1Operand(ostd::String line)
		{
			ostd::String lineEdit(line);
			ostd::String instEdit(lineEdit.new_substr(0, lineEdit.indexOf(" ")));
			instEdit.trim().toLower();
			ostd::String opEdit(lineEdit.new_substr(lineEdit.indexOf(" ") + 1));
			opEdit.trim();
			int16_t word = 0x0000;
			if (instEdit == "inc")
			{
				eOperandType opType = parseOperand(opEdit, word);
				if (opType != eOperandType::Register)
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Register required\n";
					exit(0);
					return;
				}
				m_code.push_back(data::OpCodes::IncReg);
				m_code.push_back((uint8_t)word);
				return;
			}
			else if (instEdit == "dec")
			{
				eOperandType opType = parseOperand(opEdit, word);
				if (opType != eOperandType::Register)
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Register required\n";
					exit(0);
					return;
				}
				m_code.push_back(data::OpCodes::DecReg);
				m_code.push_back((uint8_t)word);
				return;
			}
			else if (instEdit == "arg")
			{
				eOperandType opType = parseOperand(opEdit, word);
				if (opType != eOperandType::Register)
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Register required\n";
					exit(0);
					return;
				}
				m_code.push_back(data::OpCodes::ArgReg);
				m_code.push_back((uint8_t)word);
				return;
			}
			else if (instEdit == "push")
			{
				eOperandType opType = parseOperand(opEdit, word);
				if (opType == eOperandType::Immediate || opType == eOperandType::Label)
				{
					m_code.push_back(data::OpCodes::PushImm);
					m_code.push_back((uint8_t)((word & 0xFF00) >> 8));
					m_code.push_back((uint8_t)(word & 0x00FF));
				}
				else if (opType == eOperandType::Register)
				{
					m_code.push_back(data::OpCodes::PushReg);
					m_code.push_back((uint8_t)word);
				}
				else
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Register or Immediate required\n";
					exit(0);
				}
				return;
			}
			else if (instEdit == "pop")
			{
				eOperandType opType = parseOperand(opEdit, word);
				if (opType != eOperandType::Register)
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Register required\n";
					exit(0);
					return;
				}
				m_code.push_back(data::OpCodes::PopReg);
				m_code.push_back((uint8_t)word);
				return;
			}
			else if (instEdit == "call")
			{
				m_code.push_back(0x00);
				eOperandType opType = parseOperand(opEdit, word);
				if (opType == eOperandType::Immediate || opType == eOperandType::Label)
				{
					m_code[m_code.size() - 1] = data::OpCodes::CallImm;
					m_code.push_back((uint8_t)((word & 0xFF00) >> 8));
					m_code.push_back((uint8_t)(word & 0x00FF));
				}
				else if (opType == eOperandType::Register)
				{
					m_code[m_code.size() - 1] = data::OpCodes::CallReg;
					m_code.push_back((uint8_t)word);
				}
				else
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Register or Immediate (Label) required\n";
					exit(0);
				}
				return;
			}
			else if (instEdit == "not")
			{
				eOperandType opType = parseOperand(opEdit, word);
				if (opType != eOperandType::Register)
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Register required\n";
					exit(0);
					return;
				}
				m_code.push_back(data::OpCodes::NotReg);
				m_code.push_back((uint8_t)word);
				return;
			}
			else if (instEdit == "jmp")
			{
				m_code.push_back(data::OpCodes::Jmp);
				eOperandType opType = parseOperand(opEdit, word);
				if (opType != eOperandType::Label && opType != eOperandType::Immediate)
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Immediate (or label) required\n";
					exit(0);
					return;
				}
				m_code.push_back((uint8_t)((word & 0xFF00) >> 8));
				m_code.push_back((uint8_t)(word & 0x00FF));
				return;
			}
			else if (instEdit == "int")
			{
				eOperandType opType = parseOperand(opEdit, word);
				if (opType != eOperandType::Immediate)
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Immediate required\n";
					exit(0);
					return;
				}
				m_code.push_back(data::OpCodes::Int);
				m_code.push_back((uint8_t)word);
				return;
			}
			else
			{
				std::cout << "Unknown instruction; " << line << " (" << instEdit << ")\n";
				exit(0);
			}
		}

		void Assembler::parse2Operand(ostd::String line)
		{
			ostd::String lineEdit(line);
			ostd::String instEdit(lineEdit.new_substr(0, lineEdit.indexOf(" ")));
			instEdit.trim().toLower();
			ostd::String opEdit(lineEdit.new_substr(lineEdit.indexOf(" ") + 1));
			opEdit.trim();
			int16_t word = 0x0000;
			auto st = opEdit.tokenize(",");
			if (instEdit == "mov")
			{
				m_code.push_back(0x00);
				int16_t word1 = 0x0000;
				int16_t word2 = 0x0000;
				eOperandType opType1 = parseOperand(st.next(), word1);
				if (opType1 == eOperandType::Register)
				{
					m_code.push_back((uint8_t)word1);
					eOperandType opType2 = parseOperand(st.next(), word2);
					switch (opType2)
					{
						case eOperandType::Immediate:
						case eOperandType::Label:
							m_code[m_code.size() - 2] = data::OpCodes::MovImmReg;
							m_code.push_back((uint8_t)((word2 & 0xFF00) >> 8));
							m_code.push_back((uint8_t)(word2 & 0x00FF));
						break;
						case eOperandType::Register:
							m_code[m_code.size() - 2] = data::OpCodes::MovRegReg;
							m_code.push_back((uint8_t)word2);
						break;
						case eOperandType::DerefMemory:
							m_code[m_code.size() - 2] = data::OpCodes::MovMemReg;
							m_code.push_back((uint8_t)((word2 & 0xFF00) >> 8));
							m_code.push_back((uint8_t)(word2 & 0x00FF));
						break;
						case eOperandType::DerefRegister:
							m_code[m_code.size() - 2] = data::OpCodes::MovDerefRegReg;
							m_code.push_back((uint8_t)word2);
							return;
						break;
						default:
							std::cout << "Invalid operand type; " << line << " (" << opEdit << ")\n";
							exit(0);
						break;
					}
				}
				else if (opType1 == eOperandType::DerefMemory)
				{
					m_code.push_back((uint8_t)((word1 & 0xFF00) >> 8));
					m_code.push_back((uint8_t)(word1 & 0x00FF));
					eOperandType opType2 = parseOperand(st.next(), word2);
					switch (opType2)
					{
						case eOperandType::Register:
							m_code[m_code.size() - 3] = data::OpCodes::MovRegMem;
							m_code.push_back((uint8_t)word2);
						break;
						case eOperandType::DerefRegister:
							m_code[m_code.size() - 3] = data::OpCodes::MovDerefRegMem;
							m_code.push_back((uint8_t)word2);
						break;
						case eOperandType::Immediate:
						case eOperandType::Label:
							m_code[m_code.size() - 3] = data::OpCodes::MovImmMem;
							m_code.push_back((uint8_t)((word2 & 0xFF00) >> 8));
							m_code.push_back((uint8_t)(word2 & 0x00FF));
						break;
						default:
							std::cout << "Invalid operand type; " << line << " (" << opEdit << ")\n";
							exit(0);
						break;
					}
				}
				else if (opType1 == eOperandType::DerefRegister)
				{
					m_code.push_back((uint8_t)word1);
					eOperandType opType2 = parseOperand(st.next(), word2);
					switch (opType2)
					{
						case eOperandType::Register:
							m_code[m_code.size() - 2] = data::OpCodes::MovRegDerefReg;
							m_code.push_back((uint8_t)word2);
						break;
						case eOperandType::DerefMemory:
							m_code[m_code.size() - 2] = data::OpCodes::MovMemDerefReg;
							m_code.push_back((uint8_t)((word2 & 0xFF00) >> 8));
							m_code.push_back((uint8_t)(word2 & 0x00FF));
						break;
						case eOperandType::Immediate:
						case eOperandType::Label:
							m_code[m_code.size() - 2] = data::OpCodes::MovImmDerefReg;
							m_code.push_back((uint8_t)((word2 & 0xFF00) >> 8));
							m_code.push_back((uint8_t)(word2 & 0x00FF));
						break;
						case eOperandType::DerefRegister:
							m_code[m_code.size() - 2] = data::OpCodes::MovDerefRegDerefReg;
							m_code.push_back((uint8_t)word2);
							return;
						break;
						default:
							std::cout << "Invalid operand type; " << line << " (" << opEdit << ")\n";
							exit(0);
						break;
					}
				}
				return;
			}
			else if (instEdit == "movb")
			{
				m_code.push_back(0x00);
				int16_t word1 = 0x0000;
				int16_t word2 = 0x0000;
				eOperandType opType1 = parseOperand(st.next(), word1);
				if (opType1 == eOperandType::DerefMemory)
				{
					m_code.push_back((uint8_t)((word1 & 0xFF00) >> 8));
					m_code.push_back((uint8_t)(word1 & 0x00FF));
					eOperandType opType2 = parseOperand(st.next(), word2);
					switch (opType2)
					{
						case eOperandType::Immediate:
							m_code[m_code.size() - 3] = data::OpCodes::MovByteImmMem;
							m_code.push_back((uint8_t)word2);
						break;
						case eOperandType::DerefRegister:
							m_code[m_code.size() - 3] = data::OpCodes::MovByteDerefRegMem;
							m_code.push_back((uint8_t)word2);
						break;
						case eOperandType::Register:
							m_code[m_code.size() - 3] = data::OpCodes::MovByteRegMem;
							m_code.push_back((uint8_t)word2);
						break;
						default:
							std::cout << "Invalid operand type; " << line << " (" << opEdit << ")\n";
							exit(0);
						break;
					}
				}
				else if (opType1 == eOperandType::DerefRegister)
				{
					m_code.push_back((uint8_t)word1);
					eOperandType opType2 = parseOperand(st.next(), word2);
					switch (opType2)
					{
						case eOperandType::Register:
							m_code[m_code.size() - 2] = data::OpCodes::MovByteRegDerefReg;
							m_code.push_back((uint8_t)word2);
						break;
						case eOperandType::DerefMemory:
							m_code[m_code.size() - 2] = data::OpCodes::MovByteMemDerefReg;
							m_code.push_back((uint8_t)((word2 & 0xFF00) >> 8));
							m_code.push_back((uint8_t)(word2 & 0x00FF));
						break;
						case eOperandType::Immediate:
							m_code[m_code.size() - 2] = data::OpCodes::MovByteImmDerefReg;
							m_code.push_back((uint8_t)word2);
						break;
						case eOperandType::DerefRegister:
							m_code[m_code.size() - 2] = data::OpCodes::MovByteDerefRegDerefReg;
							m_code.push_back((uint8_t)word2);
							return;
						break;
						default:
							std::cout << "Invalid operand type; " << line << " (" << opEdit << ")\n";
							exit(0);
						break;
					}
				}
				else if (opType1 == eOperandType::Register)
				{
					m_code.push_back((uint8_t)word1);
					eOperandType opType2 = parseOperand(st.next(), word2);
					switch (opType2)
					{
						case eOperandType::Immediate:
							m_code[m_code.size() - 2] = data::OpCodes::MovByteImmReg;
							m_code.push_back((uint8_t)word2);
						break;
						case eOperandType::DerefMemory:
							m_code[m_code.size() - 2] = data::OpCodes::MovByteMemReg;
							m_code.push_back((uint8_t)((word2 & 0xFF00) >> 8));
							m_code.push_back((uint8_t)(word2 & 0x00FF));
						break;
						case eOperandType::DerefRegister:
							m_code[m_code.size() - 2] = data::OpCodes::MovByteDerefRegReg;
							m_code.push_back((uint8_t)word2);
							return;
						break;
						default:
							std::cout << "Invalid operand type; " << line << " (" << opEdit << ")\n";
							exit(0);
						break;
					}
				}
				return;
			}
			else if (instEdit == "add" || instEdit == "sub" || instEdit == "mul")
			{
				m_code.push_back(0x00);
				eOperandType opType = parseOperand(st.next(), word);
				if (opType != eOperandType::Register)
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Register required\n";
					exit(0);
					return;
				}
				m_code.push_back((uint8_t)word);
				opType = parseOperand(st.next(), word);

				if (opType == eOperandType::Immediate)
				{
					if (instEdit == "add") m_code[m_code.size() - 2] = data::OpCodes::AddImmReg;
					else if (instEdit == "sub") m_code[m_code.size() - 2] = data::OpCodes::SubImmReg;
					else if (instEdit == "mul") m_code[m_code.size() - 2] = data::OpCodes::MulImmReg;
					m_code.push_back((uint8_t)((word & 0xFF00) >> 8));
					m_code.push_back((uint8_t)(word & 0x00FF));
				}
				else if (opType == eOperandType::Register)
				{
					if (instEdit == "add") m_code[m_code.size() - 2] = data::OpCodes::AddRegReg;
					else if (instEdit == "sub") m_code[m_code.size() - 2] = data::OpCodes::SubRegReg;
					else if (instEdit == "mul") m_code[m_code.size() - 2] = data::OpCodes::MulRegReg;
					m_code.push_back((uint8_t)word);
				}
				else
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Immediate or register required\n";
					exit(0);
					return;
				}
				return;
			}
			else if (instEdit == "lsh" || instEdit == "rsh" || instEdit == "and" || instEdit == "or" || instEdit == "xor")
			{
				eOperandType opType = parseOperand(st.next(), word);
				if (opType != eOperandType::Register)
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Register required\n";
					exit(0);
					return;
				}
				uint8_t regAddr = (uint8_t)word;
				opType = parseOperand(st.next(), word);
				if (opType == eOperandType::Immediate)
				{
					if (instEdit == "lsh") m_code.push_back(data::OpCodes::LShiftRegImm);
					else if (instEdit == "rsh") m_code.push_back(data::OpCodes::RShiftRegImm);
					else if (instEdit == "and") m_code.push_back(data::OpCodes::AndRegImm);
					else if (instEdit == "or") m_code.push_back(data::OpCodes::OrRegImm);
					else if (instEdit == "xor") m_code.push_back(data::OpCodes::XorRegImm);
					m_code.push_back((uint8_t)((word & 0xFF00) >> 8));
					m_code.push_back((uint8_t)(word & 0x00FF));
					m_code.push_back((uint8_t)regAddr);
				}
				else if (opType == eOperandType::Register)
				{
					if (instEdit == "lsh") m_code.push_back(data::OpCodes::LShiftRegReg);
					else if (instEdit == "rsh") m_code.push_back(data::OpCodes::RShiftRegReg);
					else if (instEdit == "and") m_code.push_back(data::OpCodes::AndRegReg);
					else if (instEdit == "or") m_code.push_back(data::OpCodes::OrRegReg);
					else if (instEdit == "xor") m_code.push_back(data::OpCodes::XorRegReg);
					m_code.push_back((uint8_t)word);
					m_code.push_back((uint8_t)regAddr);
				}
				else
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Register or immediate required\n";
					exit(0);
					return;
				}
				return;
			}
			else if (instEdit == "jne" || instEdit == "jeq" || instEdit == "jgr" || instEdit == "jls" || instEdit == "jge" || instEdit == "jle")
			{
				m_code.push_back(0x00);
				eOperandType opType = parseOperand(st.next(), word);
				if (opType != eOperandType::Immediate && opType != eOperandType::Label)
				{
					std::cout << (int)opType << "\n";
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Immediate (or label) required\n";
					exit(0);
					return;
				}
				m_code.push_back((uint8_t)((word & 0xFF00) >> 8));
				m_code.push_back((uint8_t)(word & 0x00FF));
				opType = parseOperand(st.next(), word);

				if (opType == eOperandType::Immediate)
				{
					if (instEdit == "jeq") m_code[m_code.size() - 3] = data::OpCodes::JmpEqImm;
					else if (instEdit == "jne") m_code[m_code.size() - 3] = data::OpCodes::JmpNotEqImm;
					else if (instEdit == "jgr") m_code[m_code.size() - 3] = data::OpCodes::JmpGrImm;
					else if (instEdit == "jls") m_code[m_code.size() - 3] = data::OpCodes::JmpLessImm;
					else if (instEdit == "jge") m_code[m_code.size() - 3] = data::OpCodes::JmpGeImm;
					else if (instEdit == "jle") m_code[m_code.size() - 3] = data::OpCodes::JmpLeImm;
					m_code.push_back((uint8_t)((word & 0xFF00) >> 8));
					m_code.push_back((uint8_t)(word & 0x00FF));
				}
				else if (opType == eOperandType::Register)
				{
					if (instEdit == "jeq") m_code[m_code.size() - 3] = data::OpCodes::JmpEqReg;
					else if (instEdit == "jne") m_code[m_code.size() - 3] = data::OpCodes::JmpNotEqReg;
					else if (instEdit == "jgr") m_code[m_code.size() - 3] = data::OpCodes::JmpGrReg;
					else if (instEdit == "jls") m_code[m_code.size() - 3] = data::OpCodes::JmpLessReg;
					else if (instEdit == "jge") m_code[m_code.size() - 3] = data::OpCodes::JmpGeReg;
					else if (instEdit == "jle") m_code[m_code.size() - 3] = data::OpCodes::JmpLeReg;
					m_code.push_back((uint8_t)word);
				}
				else
				{
					std::cout << "Invalid operand type: " << line << " (" << opEdit << ")  ->  Immediate or register required\n";
					exit(0);
					return;
				}
				return;
			}
			else
			{
				std::cout << "Unknown instruction: " << line << " (" << instEdit << ")\n";
				exit(0);
			}
		}

		void Assembler::parse3Operand(ostd::String line)
		{
			ostd::String lineEdit(line);
			ostd::String instEdit(lineEdit.new_substr(0, lineEdit.indexOf(" ")));
			instEdit.trim().toLower();
			ostd::String opEdit(lineEdit.new_substr(lineEdit.indexOf(" ") + 1));
			opEdit.trim();
			int16_t word = 0x0000;
			if (instEdit == "mov")
			{
				auto st = opEdit.tokenize(",");
				eOperandType opType = parseOperand(st.next(), word);
				if (opType != eOperandType::Register)
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Register required\n";
					exit(0);
					return;
				}
				m_code.push_back(data::OpCodes::MovImmRegOffReg);
				m_code.push_back((uint8_t)word);
				opType = parseOperand(st.next(), word);
				if (opType != eOperandType::DerefMemory)
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Pointer required\n";
					exit(0);
					return;
				}
				m_code.push_back((uint8_t)((word & 0xFF00) >> 8));
				m_code.push_back((uint8_t)(word & 0x00FF));
				opType = parseOperand(st.next(), word);
				if (opType != eOperandType::Register)
				{
					std::cout << "Invalid operand type; " << line << " (" << opEdit << ")  ->  Register required\n";
					exit(0);
					return;
				}
				m_code.push_back((uint8_t)word);
				return;
			}
			else
			{
				std::cout << "Unknown instruction; " << line << " (" << instEdit << ")\n";
				exit(0);
			}
		}

		void Assembler::combineDataAndCode(void)
		{
			uint16_t entryAddr = m_dataSize + m_loadAddress + 3;
			std::vector<tSymbol> symbols;
			ostd::ByteStream newCode;
			newCode.push_back(data::OpCodes::Jmp);
			newCode.push_back((uint8_t)((entryAddr & 0xFF00) >> 8));
			newCode.push_back((uint8_t)(entryAddr & 0x00FF));
			if (m_dataSize > 0)
				m_disassembly.insert(m_disassembly.begin(), { (uint16_t)(m_loadAddress + 3), "[----------DATA_SECTION----------]" });
			m_disassembly.insert(m_disassembly.begin(), { m_loadAddress, ostd::String("jmp ").add(ostd::Utils::getHexStr(entryAddr, true, 2)) });
			for (auto& d : m_symbolTable)
				symbols.push_back(d.second);
			for (int32_t i = symbols.size() - 1; i >= 0; i--)
			{
				for (auto& dd : symbols[i].bytes)
					newCode.push_back(dd);
			}
			for (auto& b : m_code)
				newCode.push_back(b);
			m_code = newCode;

			m_disassembly.push_back({ 0xFFFFFF00, "{ DATA }" });
			for (auto& d : m_symbolTable)
			{
				m_disassembly.push_back({ d.second.address, d.first, (uint16_t)d.second.bytes.size() });
			}
			m_disassembly.push_back({ 0xFFFFFF01, "{ LABELS }" });
			for (auto& l : m_labelTable)
			{
				m_disassembly.push_back({ l.second.address, l.first });
			}
		}

		ostd::String Assembler::replaceSymbols(ostd::String line)
		{
			ostd::String lineEdit(line);
			for (auto& symbol : m_symbolTable)
				lineEdit.replaceAll(symbol.first, ostd::Utils::getHexStr(symbol.second.address, true, 2));
			return lineEdit;
		}

		void Assembler::replaceLabelRefs(void)
		{
			for (auto& label : m_labelTable)
			{
				for (auto& addr : label.second.references)
				{
					m_code[addr] = (uint8_t)((label.second.address & 0xFF00) >> 8);
					m_code[addr + 1] = (uint8_t)(label.second.address & 0x00FF);
				}
			}
		}

		Assembler::eOperandType Assembler::parseOperand(ostd::String op, int16_t& outOp)
		{
			ostd::String opEdit(op);
			bool derefReg = false;
			if (opEdit.startsWith("*"))
			{
				opEdit = opEdit.substr(1);
				opEdit.trim();
				derefReg = true;
			}
			int8_t reg = parseRegister(opEdit);
			if (reg != data::Registers::Last)
			{
				outOp = (int16_t)reg;
				if (derefReg)
					return eOperandType::DerefRegister;
				return eOperandType::Register;
			}
			if (derefReg)
			{
				std::cout << "Invalid Register Dereferencing: " << op << "\n";
				return eOperandType::Error;
			}
			if (opEdit.isNumeric())
			{
				outOp = (int16_t)opEdit.toInt();
				return eOperandType::Immediate;
			}
			if (opEdit.startsWith("$"))
			{
				if (m_labelTable.count(opEdit) == 0)
				{
					std::cout << "Unknown symbol: " << opEdit << "\n";
					return eOperandType::Error;
				}
				uint16_t labelAddr = m_labelTable[opEdit].address;
				if (labelAddr == 0x0000)
					m_labelTable[opEdit].references.push_back(m_code.size());
				outOp = (int16_t)labelAddr;
				return eOperandType::Label;
			}
			if (opEdit.startsWith("{") && opEdit.endsWith("}"))
			{
				opEdit = opEdit.substr(1, opEdit.len() - 1);
				opEdit.trim();
				outOp = (int16_t)ostd::Utils::solveIntegerExpression(opEdit);
				return eOperandType::Immediate;
			}
			if (opEdit.startsWith("[") && opEdit.endsWith("]"))
			{
				opEdit = opEdit.substr(1, opEdit.len() - 1);
				opEdit.trim();
				if (opEdit.startsWith("{") && opEdit.endsWith("}"))
				{
					opEdit = opEdit.substr(1, opEdit.len() - 1);
					opEdit.trim();
					outOp = (int16_t)ostd::Utils::solveIntegerExpression(opEdit);
					return eOperandType::DerefMemory;
				}
				if (!opEdit.isNumeric())
				{
					std::cout << "Invalid numeric value: " << opEdit << "\n";
					return eOperandType::Error;
				}
				outOp = (int16_t)opEdit.toInt();
				return eOperandType::DerefMemory;
			}
			return eOperandType::Error;
		}

		uint8_t Assembler::parseRegister(ostd::String op)
		{
			ostd::String opEdit(op);
			opEdit.trim().toLower();
			if (opEdit == "r1") return data::Registers::R1;
			if (opEdit == "r2") return data::Registers::R2;
			if (opEdit == "r3") return data::Registers::R3;
			if (opEdit == "r4") return data::Registers::R4;
			if (opEdit == "r5") return data::Registers::R5;
			if (opEdit == "r6") return data::Registers::R6;
			if (opEdit == "r7") return data::Registers::R7;
			if (opEdit == "r8") return data::Registers::R8;
			if (opEdit == "r9") return data::Registers::R9;
			if (opEdit == "r10") return data::Registers::R10;
			if (opEdit == "acc") return data::Registers::ACC;
			if (opEdit == "fp") return data::Registers::FP;
			if (opEdit == "sp") return data::Registers::SP;
			if (opEdit == "ip") return data::Registers::IP;
			if (opEdit == "pp") return data::Registers::PP;
			if (opEdit == "rv") return data::Registers::RV;
			if (opEdit == "fl") return data::Registers::FL;
			return data::Registers::Last;
		}

		void Assembler::printProgramInfo(void)
		{
			int32_t symbol_len = 30;
			out.nl();

			if (m_symbolTable.size() > 0)
				out.fg(ostd::ConsoleColors::Yellow).p("Symbols:").nl();
			for (auto& symbol : m_symbolTable)
			{
				out.fg(ostd::ConsoleColors::Red).p(ostd::Utils::getHexStr(symbol.second.address, true, 2));
				out.fg(ostd::ConsoleColors::Magenta).p("  ").p(symbol.first);
				out.fg(ostd::ConsoleColors::Blue).nl().p("  ");
				for (auto& b : symbol.second.bytes)
					out.p(ostd::Utils::getHexStr(b, false, 1)).p(".");
				out.nl();
			}
			if (m_symbolTable.size() > 0)
				out.nl();
			
			if (m_labelTable.size() > 0)
				out.fg(ostd::ConsoleColors::Yellow).p("Labels:").nl();
			for (auto& label : m_labelTable)
			{
				out.fg(ostd::ConsoleColors::Magenta).p(label.first.new_fixedLength(symbol_len));
				out.fg(ostd::ConsoleColors::Red).p(ostd::Utils::getHexStr(label.second.address, true, 2)).nl();
			}
			if (m_labelTable.size() > 0)
				out.nl();
			
			if (m_structDefs.size() > 0)
				out.fg(ostd::ConsoleColors::Yellow).p("Structures:").nl();
			for (auto& str : m_structDefs)
			{
				out.fg(ostd::ConsoleColors::Magenta).p(str.name.new_fixedLength(symbol_len));
				out.fg(ostd::ConsoleColors::Red).p(str.size).p(" bytes").nl();
			}
			if (m_structDefs.size() > 0)
				out.nl();


			out.fg(ostd::ConsoleColors::Yellow).p("Program data:").nl();
			out.fg(ostd::ConsoleColors::Cyan).p(ostd::String("Fixed Size:  ").new_fixedLength(symbol_len)).fg(ostd::ConsoleColors::BrightRed).p((int)m_fixedSize).p(" bytes").nl();
			out.fg(ostd::ConsoleColors::Cyan).p(ostd::String("Program Size:").new_fixedLength(symbol_len)).fg(ostd::ConsoleColors::BrightRed).p((int)m_programSize).p(" bytes").nl();
			out.fg(ostd::ConsoleColors::Cyan).p(ostd::String("Data Size:   ").new_fixedLength(symbol_len)).fg(ostd::ConsoleColors::BrightRed).p((int)m_dataSize).p(" bytes").nl();
			out.fg(ostd::ConsoleColors::Cyan).p(ostd::String("Fixed Fill:  ").new_fixedLength(symbol_len)).fg(ostd::ConsoleColors::BrightRed).p(ostd::Utils::getHexStr(m_fixedFillValue, true, 1)).nl();
			out.fg(ostd::ConsoleColors::Cyan).p(ostd::String("Load Address:").new_fixedLength(symbol_len)).fg(ostd::ConsoleColors::BrightRed).p(ostd::Utils::getHexStr(m_loadAddress, true, 2)).nl();
			out.fg(ostd::ConsoleColors::Cyan).p(ostd::String("Entry Point: ").new_fixedLength(symbol_len)).fg(ostd::ConsoleColors::BrightRed).p(ostd::Utils::getHexStr(m_dataSize + m_loadAddress + 3, true, 2)).nl();

			out.nl();
		}
	}
}