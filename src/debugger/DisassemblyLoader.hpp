#pragma once

#include "../assembler/Assembler.hpp"

#include <ostd/data/Types.hpp>

namespace dragon
{
	class DisassemblyTable
	{
		public:
			inline DisassemblyTable(void) { m_initialized = false; }
			inline DisassemblyTable(const String& filePath) { init(filePath); }
			void init(const String& filePath);

			inline const std::vector<code::Assembler::tDisassemblyLine>& getCodeTable(void) const { return m_code; }
			inline const std::vector<code::Assembler::tDisassemblyLine>& getDataTable(void) const { return m_data; }
			inline const std::vector<code::Assembler::tDisassemblyLine>& getLabelTable(void) const { return m_labels; }

			inline bool isInitialized(void) const { return m_initialized; }
			inline const String& getFilePath(void) const { return m_filePath; }
			// Address range of the code (first / last instruction); 0 / 0 without code.
			inline u32 getCodeStart(void) const { return m_code.empty() ? 0 : m_code.front().addr; }
			inline u32 getCodeEnd(void) const { return m_code.empty() ? 0 : m_code.back().addr; }

		private:
			void load_data(ostd::ByteStream& stream);

		private:
			std::vector<code::Assembler::tDisassemblyLine> m_code;
			std::vector<code::Assembler::tDisassemblyLine> m_labels;
			std::vector<code::Assembler::tDisassemblyLine> m_data;
			bool m_initialized { false };
			String m_filePath { "" };

		public:
			static const DisassemblyTable DefaultObject;
	};

	class DisassemblyLoader
	{
		public:
			static void loadDirectory(const String& directoryPath);
			static const DisassemblyTable& loadFile(const String& filePath);

			static std::vector<code::Assembler::tDisassemblyLine> getCodeTable(void);
			static std::vector<code::Assembler::tDisassemblyLine> getDataTable(void);
			static std::vector<code::Assembler::tDisassemblyLine> getLabelTable(void);
			// Every loaded table (one per assembled program), in loading order.
			inline static const std::vector<DisassemblyTable>& getTables(void) { return m_tables; }

		private:
			inline static std::vector<DisassemblyTable> m_tables;
	};
}
