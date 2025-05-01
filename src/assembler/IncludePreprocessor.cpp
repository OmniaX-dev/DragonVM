#include "Assembler.hpp"

#include <ostd/File.hpp>

namespace dragon
{
	namespace code
	{
		std::vector<ostd::String> IncludePreprocessor::loadEntryFile(const ostd::String& filePath)
        {
            m_guards.clear();
            m_lines.clear();
            m_directory = "";
            m_lines = __load_file(filePath);
                
            if (m_lines.size() == 0) return {  }; //TODO: Error
            if (filePath.contains("/"))
                m_directory = filePath.new_substr(0, filePath.lastIndexOf("/") + 1);
            if (!__can_file_be_included(m_lines))
                return {  }; //TODO: Error
            if (!__include_loop()) return {  }; //TODO: Error
			std::vector<ostd::String> newLines;
			for (auto& line : m_lines)
			{
				ostd::String lineEdit = line.new_trim();
				if (lineEdit != "")
					newLines.push_back(line);
			}
			m_lines.clear();
			m_lines = newLines;
            return m_lines;
        }

        bool IncludePreprocessor::__can_file_be_included(std::vector<ostd::String>& lines)
        {
            ostd::String guard_name = "";
            for (auto& line : lines)
            {
                ostd::String lineEdit = line.new_trim();
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
            std::vector<ostd::String> lines = m_lines;
            bool included = false;
            do
            {
                included = false;
                uint32_t i = 0;
                for ( ; i < lines.size(); i++)
                {
                    ostd::String line = lines[i];
                    line.trim();
                    if (line.new_toLower().startsWith("@include") && line.len() > 8)
                    {
                        line.substr(8).trim();
                        if (!line.startsWith("<") || !line.endsWith(">"))
                            return false;
                        line.substr(1, line.len() - 1).trim();
                        auto file_lines = __load_file(m_directory + line);
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

        std::vector<ostd::String> IncludePreprocessor::__load_file(const ostd::String& filePath)
        {
            ostd::TextFileBuffer file(filePath);
            if (!file.exists())
                return {  }; //TODO: Error
            ostd::String source = file.rawContent();
			return ostd::String(source.replaceAll("\t", "    ")).tokenize("\n", false).getRawData();
        }

	}
}