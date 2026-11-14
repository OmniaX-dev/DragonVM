#pragma once

#include "../assembler/Assembler.hpp"

#include <ostd/data/Types.hpp>
#include <map>
#include <memory>

namespace dragon
{
	// A source file of a program (version 2 disassembly files): its lines, without the new lines.
	struct tSourceFile
	{
		String path { "" };
		String name { "" };		// the file name, without the directories
		std::vector<String> lines;
		// Line n (1-based); "" if out of range.
		inline const String& line(i32 n) const { static const String empty = ""; return (n >= 1 && n <= cast<i32>(lines.size())) ? lines[n - 1] : empty; }
	};

	// A sub of a program (the disassembly's { SUBS }, from dsc): for breakpoints by name.
	struct tSubInfo
	{
		String name { "" };			// the full name: dr::fs::FileSystem::info
		String label { "" };		// its code's label (without '$')
		String params { "" };		// the parameter types ("u8, u32"), to tell overloads apart
		bool isInline { false };	// inline: its code is copied into the callers (at its lines)
		String path { "" };			// its source file
		i32 line { 0 };				// where it is written: first / last line
		i32 lastLine { 0 };
		bool hasAddress { false };	// its label is in the program (inline subs: only if used as a pointer)
		u32 address { 0 };
	};

	class DisassemblyTable
	{
		public:
			inline DisassemblyTable(void) { m_initialized = false; }
			inline DisassemblyTable(const String& filePath) { init(filePath); }
			void init(const String& filePath);

			inline const std::vector<code::Assembler::tDisassemblyLine>& getCodeTable(void) const { return m_code; }
			inline const std::vector<code::Assembler::tDisassemblyLine>& getDataTable(void) const { return m_data; }
			inline const std::vector<code::Assembler::tDisassemblyLine>& getLabelTable(void) const { return m_labels; }
			// The code lines' srcFile are indices in this list (this table's own).
			inline const std::vector<tSourceFile>& getSources(void) const { return m_sources; }
			inline i32 getVersion(void) const { return m_version; }
			inline const std::vector<tSubInfo>& getSubs(void) const { return m_subs; }

			inline bool isInitialized(void) const { return m_initialized; }
			inline const String& getFilePath(void) const { return m_filePath; }
			// The name of the program (its binary) ("" for files saved by an older assembler).
			inline const String& getName(void) const { return m_name; }
			// Address range of the code (first / last instruction); 0 / 0 without code.
			inline u32 getCodeStart(void) const { return m_code.empty() ? 0 : m_code.front().addr; }
			inline u32 getCodeEnd(void) const { return m_code.empty() ? 0 : m_code.back().addr; }
			// A DRX program's (its { DRX } entry): the id in its header; 0: a program at fixed addresses.
			inline u64 getDrxId(void) const { return m_drxId; }
			// A DRX's table moved to where it was loaded (relocatedCopy): that address.
			inline bool isLoadedCopy(void) const { return m_loadedCopy; }
			inline u32 getLoadBase(void) const { return m_loadBase; }
			// This (a DRX's table, at offset 0) with every address moved by base.
			DisassemblyTable relocatedCopy(u32 base) const;

		private:
			void load_data(ostd::ByteStream& stream);

		private:
			std::vector<code::Assembler::tDisassemblyLine> m_code;
			std::vector<code::Assembler::tDisassemblyLine> m_labels;
			std::vector<code::Assembler::tDisassemblyLine> m_data;
			std::vector<tSourceFile> m_sources;
			std::vector<tSubInfo> m_subs;
			i32 m_version { 1 };
			bool m_initialized { false };
			String m_filePath { "" };
			String m_name { "" };
			u64 m_drxId { 0 };
			bool m_loadedCopy { false };
			u32 m_loadBase { 0 };

		public:
			static const DisassemblyTable DefaultObject;
	};

	// The disassembly files of a directory: the programs at fixed addresses (raw binaries, the
	// BIOS), always there; and the DRX programs' (at offset 0, by id), there only once a loader
	// says where one is (mapDrx: debug_drx_load), as a copy moved to that address.
	class DisassemblyLoader
	{
		public:
			// A DRX program in memory (and its moved table; nullptr: no disassembly has its id).
			struct tMapping
			{
				u32 base { 0 };
				u32 size { 0 };
				u64 id { 0 };
				const DisassemblyTable* table { nullptr };
			};

		public:
			static void loadDirectory(const String& directoryPath);
			static void loadFile(const String& filePath);

			// Every table's code; srcFile is an index in getSources() (all the tables' files).
			static std::vector<code::Assembler::tDisassemblyLine> getCodeTable(void);
			static std::vector<tSourceFile> getSources(void);
			// Where a table's source files start in getSources().
			static i32 getSourceOffset(const DisassemblyTable& table);
			static std::vector<code::Assembler::tDisassemblyLine> getDataTable(void);
			static std::vector<code::Assembler::tDisassemblyLine> getLabelTable(void);
			// The tables in use: the fixed ones (in loading order), then the loaded DRX programs'
			// (in loading order). The pointers stay valid for the whole session.
			static std::vector<const DisassemblyTable*> getTables(void);
			// The DRX programs' tables (at offset 0), by id.
			inline static const std::map<u64, std::unique_ptr<DisassemblyTable>>& getDrxTables(void) { return m_drx; }

			// A DRX program loaded at base: the mappings it overlaps are removed (their memory is
			// reused), then it is added. Its table (nullptr: no disassembly with that id).
			static const DisassemblyTable* mapDrx(u32 base, u32 size, u64 id);
			// The program loaded at base is gone: false if none was.
			static bool unmapDrx(u32 base);
			static void clearMappings(void);
			inline static const std::vector<tMapping>& getMappings(void) { return m_mappings; }
			// Bumped whenever getTables() changes.
			inline static u64 getVersion(void) { return m_version; }

		private:
			inline static std::vector<std::unique_ptr<DisassemblyTable>> m_fixed;
			inline static std::map<u64, std::unique_ptr<DisassemblyTable>> m_drx;
			inline static std::vector<std::unique_ptr<DisassemblyTable>> m_copies;	// (never freed: views may point at them)
			inline static std::vector<tMapping> m_mappings;
			inline static u64 m_version { 0 };
	};
}
