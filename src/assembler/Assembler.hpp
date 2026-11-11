#pragma once

#include <ostd/io/IOHandlers.hpp>
#include <map>
#include <unordered_map>
#include <vector>

namespace dragon
{
	namespace hw
	{
		class VirtualHardDrive;
	}
	namespace code
	{
		class IncludePreprocessor
        {
            public:
                static std::vector<String> loadEntryFile(const String& filePath);
                static std::vector<String> loadEntryLines(const std::vector<String>& lines, const String& directory);

            private:
                // Splits a file into lines and puts source markers in it (.srcfile once,
                // .loc before each line that can be an instruction), unless it has its own
                // (dsc's output).
                static std::vector<String> __split_with_markers(const String& path, const String& source);
                static bool __can_file_be_included(std::vector<String>& lines);
                static bool __include_loop(void);
                static std::vector<String> __finish(void);
                static std::vector<String> __load_file(const String& filePath);

            private:
                inline static std::vector<String> m_lines;
                inline static std::vector<String> m_guards;
                inline static String m_directory { "" };
                inline static i32 m_nextSourceIndex { 0 };
        };

		class Assembler
		{
			public: struct tDefine
			{
				String name;
				String value;
			};
			public: struct tDisassemblyLine
			{
				u32 addr = 0;
				String code = "";
				u16 size = 1;
				// Where the instruction comes from (.loc markers): an index in the source files
				// (-1: unknown), the line (1-based) and how many inlined subs deep it is.
				// `statement`: the first instruction of a marker (the start of a statement).
				i32 srcFile = -1;
				i32 srcLine = 0;
				u8 inlineDepth = 0;
				bool statement = false;
				inline bool operator<(const tDisassemblyLine& second) const { return addr < second.addr; }
				inline bool operator>(const tDisassemblyLine& second) const { return addr > second.addr; }
			};
			public: struct tSymbol
			{
				std::vector<ostd::UByte> bytes;
				u32 address { 0 };
			};
			public: struct tLabel
			{
				std::vector<u32> references;	// offsets in the code of the (forward) references
				u32 address { 0 };
			};
			public: struct tStructMember
			{
				String name;
				std::vector<u8> data;
				i32 position;
				inline bool operator<(const tStructMember& second) const { return position < second.position; }
				inline bool operator>(const tStructMember& second) const { return position > second.position; }
			};
			public: struct tStructDefinition
			{
				String name;
				std::vector<tStructMember> members;
				i32 size;
			};
			public: struct tExportSpec
			{
				String fileName { "" };
				std::vector<String> lines;
			};
			// A source file of the program (.srcfile <index> <path>), saved in the disassembly.
			public: struct tSourceFile
			{
				String path;
				String text;
				bool loaded { false };
			};
			public: enum class eOperandType
			{
				Register = 0,
				Immediate,
				DerefMemory,
				DerefRegister,
				Label,

				Error
			};

			public: class Application
			{
				public: struct tCommandLineArgs
				{
					inline tCommandLineArgs(void) {  }
					String source_file_path { "" };
					String dest_file_path { "" };
					bool save_disassembly { false };
					i32 verbose_level { 0xFF };
					bool debug_mode { false };
					bool save_exports { true };
					String disassembly_file_path { "" };
					String final_stage_path { "" };
					std::vector<String> cpu_extensions;
					std::vector<String> include_directories;
				};

				public:
					static i32 loadArguments(int argc, char** argv);
					static void print_application_help(void);

				public:
					inline static tCommandLineArgs args;
					inline static const i32 RETURN_VAL_EXIT_SUCCESS = 0;
					inline static const i32 RETURN_VAL_CLOSE_PROGRAM = 512;
					inline static const i32 RETURN_VAL_TOO_FEW_ARGUMENTS = 1;
					inline static const i32 RETURN_VAL_MISSING_PARAM = 2;
					inline static const i32 RETURN_VAL_INVALID_PARAM = 3;
			};

			public:
				static ostd::ByteStream assembleFromFile(String fileName);
				// Assembles source lines (@include paths relative to `directory`).
				static ostd::ByteStream assembleFromLines(const std::vector<String>& lines, const String& directory = "");
				static ostd::ByteStream assembleToFile(String sourceFileName, String binaryFileName);
				static ostd::ByteStream assembleToVirtualDisk(String fileName, hw::VirtualHardDrive& vhdd, u32 address);
				static bool saveDisassemblyToFile(String fileName, String programName = "");
				// What saveDisassemblyToFile writes (after an assembly; empty if there is nothing),
				// and writing such a list (dsc adds the code of the libraries it links to it).
				static std::vector<tDisassemblyLine> disassemblyEntries(String programName = "");
				static bool saveDisassemblyEntries(String fileName, const std::vector<tDisassemblyLine>& entries);
				static void printProgramInfo(i32 verbose_level = 1);
				// The .fixed values to report: dsc pads the image itself when it links libraries.
				// The bytes of a .header (e.g. "KERNEL0_BOOT"); empty for an unknown name.
				static ostd::ByteStream headerBytes(String name);
				inline static void setFixedInfo(u32 size, u8 fill) { m_fixedSize = size; m_fixedFillValue = fill; }
				// dsc links libraries after the assembly: their size (with alignment) goes in the report.
				inline static void setLibrariesInfo(u32 size) { m_librariesSize = size; m_programSize += size; }
				// After an assembly: every label and data symbol (without '$') and its address.
				static std::vector<std::pair<std::string, u32>> symbolAddresses(void);

			private:
				static ostd::ByteStream assemble(const std::vector<String>& sourceLines);
				static void insertHeader(void);

				static void removeComments(void);
				static void replaceDefines(void);
				static void replaceGroupDefines(void);
				static void parseStructures(void);
				static void parseStructInstances(void);

				static void parseExportSpecifications(void);
				static void createExports(void);
				static void replaceExportBuiltinVars(void);
				static void createExportFiles(void);
				static void saveCurrentStageToFile(void);

				static void parseSections(void);
				// .srcfile / .loc lines (false if the line is something else).
				static bool parseSourceMarker(const String& line, bool inCode);
				static void parseDataSection(void);
				static void parseCodeSection(void);

				static void parseDebugOperands(String line);
				static void parse0Operand(String line);
				static void parse1Operand(String line);
				static void parse2Operand(String line);
				static void parse3Operand(String line);
				static void combineDataAndCode(void);

				static String replaceSymbols(String line);
				static void replaceLabelRefs(void);
				static eOperandType parseOperand(String op, i32& outOp);
				// An extfpu immediate: a number (1.5, -2, 1e-3, 2.5f, inf, -inf, nan), as the bits
				// of the f32 it rounds to, or 0x... (the bits themselves). False if it isn't one.
				static bool parseFloatImmediate(String op, i32& outBits);
				// extfpu mnemonics (true if the line was one).
				static bool parseFpu1Operand(const String& line, const String& inst, const String& op);
				static bool parseFpu2Operand(const String& line, const String& inst, const String& op1, const String& op2);
				// Appends a 32-bit word (big endian) to the code.
				static void pushWord(i32 word);
				// Appends a 16-bit value (big endian) to the code.
				static void pushHalf(i32 half);
				static u8 parseRegister(String op);

			private:
				inline static String m_rawSource { "" };
				inline static ostd::ByteStream m_code;

				inline static std::vector<String> m_lines;
				inline static std::vector<String> m_rawDataSection;
				inline static std::vector<String> m_rawCodeSection;

				inline static std::unordered_map<String, tSymbol> m_symbolTable;
				inline static std::unordered_map<String, tLabel> m_labelTable;

				inline static u32 m_fixedSize { 0 };
				inline static u32 m_librariesSize { 0 };
				inline static u8 m_fixedFillValue { 0x00 };
				inline static u32 m_loadAddress { 0x00000000 };
				inline static u32 m_currentDataAddr { 0x00000000 };
				inline static u32 m_dataSize { 0x00000000 };
				inline static u32 m_programSize { 0x00000000 };
				inline static String m_entry_lbl { "" };
				inline static String m_headerStr { "" };

				inline static std::vector<tStructDefinition> m_structDefs;
				inline static std::vector<tDisassemblyLine> m_disassembly;
				inline static std::map<i32, tSourceFile> m_sourceFiles;
				// The location the next instructions get (from the last .loc marker).
				inline static i32 m_locFile { -1 };
				inline static i32 m_locLine { 0 };
				inline static u8 m_locInline { 0 };
				inline static bool m_locStatement { false };

				inline static std::unordered_map<String, tExportSpec> m_exports;

				inline static ostd::ConsoleOutputHandler out;

			public:
				// Every program starts with a jump to its entry point (opcode + 32-bit address).
				inline static constexpr u32 EntryJumpSize = 5;
				// The disassembly file's version: 2 adds the source lines and files.
				inline static constexpr i32 DisassemblyVersion = 2;
				inline static bool saveExports { false };
				// The load address of programs without a .load directive.
				inline static u32 defaultLoadAddress { 0x00000000 };
				inline static bool debugMode { false };
				inline static std::vector<String> cpuExtensions;
		};
	}
}
