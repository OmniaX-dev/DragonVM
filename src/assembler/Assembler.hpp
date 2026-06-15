#pragma once

#include <ostd/io/IOHandlers.hpp>
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

            private:
                static bool __can_file_be_included(std::vector<String>& lines);
                static bool __include_loop(void);
                static std::vector<String> __load_file(const String& filePath);

            private:
                inline static std::vector<String> m_lines;
                inline static std::vector<String> m_guards;
                inline static String m_directory { "" };
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
				inline bool operator<(const tDisassemblyLine& second) const { return addr < second.addr; }
				inline bool operator>(const tDisassemblyLine& second) const { return addr > second.addr; }
			};
			public: struct tSymbol
			{
				std::vector<ostd::UByte> bytes;
				u16 address { 0 };
			};
			public: struct tLabel
			{
				std::vector<u16> references;
				u16 address { 0 };
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
				static ostd::ByteStream assembleToFile(String sourceFileName, String binaryFileName);
				static ostd::ByteStream assembleToVirtualDisk(String fileName, hw::VirtualHardDrive& vhdd, u32 address);
				static bool saveDisassemblyToFile(String fileName);
				static void printProgramInfo(i32 verbose_level = 1);

			private:
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
				static eOperandType parseOperand(String op, i16& outOp);
				static u8 parseRegister(String op);

			private:
				inline static String m_rawSource { "" };
				inline static ostd::ByteStream m_code;

				inline static std::vector<String> m_lines;
				inline static std::vector<String> m_rawDataSection;
				inline static std::vector<String> m_rawCodeSection;

				inline static std::unordered_map<String, tSymbol> m_symbolTable;
				inline static std::unordered_map<String, tLabel> m_labelTable;

				inline static u16 m_fixedSize { 0 };
				inline static u8 m_fixedFillValue { 0x00 };
				inline static u16 m_loadAddress { 0x0000 };
				inline static u16 m_currentDataAddr { 0x0000 };
				inline static u16 m_dataSize { 0x0000 };
				inline static u16 m_programSize { 0x0000 };
				inline static String m_entry_lbl { "" };
				inline static String m_headerStr { "" };

				inline static std::vector<tStructDefinition> m_structDefs;
				inline static std::vector<tDisassemblyLine> m_disassembly;

				inline static std::unordered_map<String, tExportSpec> m_exports;

				inline static ostd::ConsoleOutputHandler out;

			public:
				inline static bool saveExports { false };
				inline static bool debugMode { false };
				inline static std::vector<String> cpuExtensions;
		};
	}
}
