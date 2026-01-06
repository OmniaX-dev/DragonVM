#pragma once

#include "../assembler/Assembler.hpp"

#include <ostd/Types.hpp>

namespace dragon
{
	class DisassemblyTable
	{
		public:
			inline DisassemblyTable(void) { m_initialized = false; }
			inline DisassemblyTable(const ostd::String& filePath) { init(filePath); }
			void init(const ostd::String& filePath);

			inline const std::vector<code::Assembler::tDisassemblyLine>& getCodeTable(void) const { return m_code; }
			inline const std::vector<code::Assembler::tDisassemblyLine>& getDataTable(void) const { return m_data; }
			inline const std::vector<code::Assembler::tDisassemblyLine>& getLabelTable(void) const { return m_labels; }

			inline bool isInitialized(void) const { return m_initialized; }

		private:
			void load_data(ostd::ByteStream& stream);

		private:
			std::vector<code::Assembler::tDisassemblyLine> m_code;
			std::vector<code::Assembler::tDisassemblyLine> m_labels;
			std::vector<code::Assembler::tDisassemblyLine> m_data;
			bool m_initialized { false };
			ostd::String m_filePath { "" };

		public:
			static const DisassemblyTable DefaultObject;
	};

	class DisassemblyLoader
	{
		public:
			static void loadDirectory(const ostd::String& directoryPath);
			static const DisassemblyTable& loadFile(const ostd::String& filePath);

			static std::vector<code::Assembler::tDisassemblyLine> getCodeTable(void);
			static std::vector<code::Assembler::tDisassemblyLine> getDataTable(void);
			static std::vector<code::Assembler::tDisassemblyLine> getLabelTable(void);

		private:
			inline static std::vector<DisassemblyTable> m_tables;
	};
}
