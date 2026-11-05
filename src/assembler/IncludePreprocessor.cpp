#include "Assembler.hpp"

#include <ostd/io/File.hpp>
#include <ostd/io/FileSystem.hpp>
#include <algorithm>
#include <cstdlib>

namespace dragon
{
	namespace code
	{
		std::vector<String> IncludePreprocessor::loadEntryFile(const String& filePath)
		{
			m_guards.clear();
			m_lines.clear();
			m_directory = "";
			m_nextSourceIndex = 0;
			m_lines = __load_file(filePath);

			if (m_lines.size() == 0) return {  }; //TODO: Error
			if (filePath.contains("/"))
				m_directory = filePath.new_substr(0, filePath.lastIndexOf("/") + 1);
			if (!__can_file_be_included(m_lines))
				return {  }; //TODO: Error
			if (!__include_loop()) return {  }; //TODO: Error
			return __finish();
		}

		std::vector<String> IncludePreprocessor::loadEntryLines(const std::vector<String>& lines, const String& directory)
		{
			m_guards.clear();
			m_lines = lines;
			m_directory = directory;
			// Lines with their own source files (dsc's): the included files are numbered after them.
			m_nextSourceIndex = 0;
			for (auto& line : m_lines)
			{
				String lineEdit = line.new_trim();
				if (lineEdit.startsWith(".srcfile "))
					m_nextSourceIndex = std::max(m_nextSourceIndex, cast<i32>(std::atoi(lineEdit.new_substr(9).cpp_str().c_str())) + 1);
			}
			if (m_directory != "" && !m_directory.endsWith("/"))
				m_directory.add("/");
			if (m_lines.size() == 0) return {  };
			if (!__can_file_be_included(m_lines))
				return {  };
			if (!__include_loop()) return {  };
			return __finish();
		}

		std::vector<String> IncludePreprocessor::__finish(void)
		{
			std::vector<String> newLines;
			for (auto& line : m_lines)
			{
				String lineEdit = line.new_trim();
				if (lineEdit != "")
					newLines.push_back(line);
			}
			m_lines.clear();
			m_lines = newLines;
			return m_lines;
		}

		bool IncludePreprocessor::__can_file_be_included(std::vector<String>& lines)
		{
			String guard_name = "";
			for (auto& line : lines)
			{
				String lineEdit = line.new_trim();
				if (lineEdit.new_toLower().startsWith("@guard "))
				{
					guard_name = lineEdit.new_substr(7).trim();
					line = "";
					break;
				}
			}
			if (guard_name == "") return true;
			for (auto& guard : m_guards)
			{
				if (guard == guard_name)
					return false;
			}
			m_guards.push_back(guard_name);
			return true;
		}

		bool IncludePreprocessor::__include_loop(void)
		{
			std::vector<String> lines = m_lines;
			bool included = false;
			do
			{
				included = false;
				u32 i = 0;
				for ( ; i < lines.size(); i++)
				{
					String line = lines[i];
					line.trim();
					if (line.new_toLower().startsWith("@include") && line.len() > 8)
					{
						line.substr(8).trim();
						if (!line.startsWith("<") || !line.endsWith(">"))
							return false;
						line.substr(1, line.len() - 1).trim();
						auto file_lines = __load_file(line);
						if (file_lines.size() == 0)
							return false;
						lines.erase(lines.begin() + i);
						if (__can_file_be_included(file_lines))
						{
							lines.insert(lines.begin() + i, file_lines.begin(), file_lines.end());
							included = true;
						}
						break;
					}
				}
			} while(included);
			m_lines = lines;
			return true;
		}

		std::vector<String> IncludePreprocessor::__split_with_markers(const String& path, const String& source)
		{
			// The lines that aren't blank (as before), with a .loc marker before each one that can
			// be an instruction: not a directive, a label, a data symbol, a comment, or inside a
			// @group / @struct / @raw_export block. The assembler keeps the markers of the code
			// section only (see Assembler::parseSourceMarker).
			const std::string text = source.cpp_str();
			const bool hasMarkers = (text.find(".srcfile ") != std::string::npos);
			const i32 index = m_nextSourceIndex;
			std::vector<String> lines;
			if (!hasMarkers)
			{
				m_nextSourceIndex++;
				lines.push_back(String(".srcfile ").add(index).add(" ").add(path));
			}
			i32 lineNr = 0;
			String blockEnd = "";
			size_t start = 0;
			while (start <= text.size())
			{
				size_t end = text.find('\n', start);
				if (end == std::string::npos)
					end = text.size();
				lineNr++;
				String line = String(text.substr(start, end - start)).replaceAll("\t", "    ");
				start = end + 1;
				String trimmed = line.new_trim();
				if (trimmed == "")
				{
					if (end == text.size())
						break;
					continue;
				}
				if (!hasMarkers)
				{
					const String lower = trimmed.new_toLower();
					if (blockEnd != "")
					{
						if (lower.startsWith(blockEnd))
							blockEnd = "";
					}
					else if (lower.startsWith("@group ") || lower.startsWith("@struct "))
						blockEnd = "@end";
					else if (lower.startsWith("@raw_export_start "))
						blockEnd = "@raw_export_end";
					else if (!trimmed.startsWith("@") && !trimmed.startsWith(".") && !trimmed.startsWith("$") && !trimmed.startsWith("##")
							&& !(trimmed.endsWith(":") && !trimmed.contains(" ") && !trimmed.contains(",")))
						lines.push_back(String(".loc ").add(index).add(" ").add(lineNr));
				}
				lines.push_back(line);
				if (end == text.size())
					break;
			}
			return lines;
		}

		std::vector<String> IncludePreprocessor::__load_file(const String& filePath)
		{
			const auto& include_dirs = Assembler::Application::args.include_directories;

			if (ostd::FileSystem::fileExists(m_directory + filePath))
			{
				ostd::TextFileBuffer file(m_directory + filePath);
				return __split_with_markers(m_directory + filePath, file.rawContent());
			}

			for (const auto& dir : include_dirs)
			{
				if (ostd::FileSystem::fileExists(dir + filePath))
				{
					ostd::TextFileBuffer file(dir + filePath);
					return __split_with_markers(dir + filePath, file.rawContent());
				}
			}

			return { }; //TODO: Error
		}

	}
}
