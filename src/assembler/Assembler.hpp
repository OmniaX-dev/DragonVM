#pragma once

#include <ostd/Utils.hpp>
#include <ostd/IOHandlers.hpp>
#include <unordered_map>

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
                static std::vector<ostd::String> loadEntryFile(const ostd::String& filePath);

            private:
                static bool __can_file_be_included(std::vector<ostd::String>& lines);
                static bool __include_loop(void);
                static std::vector<ostd::String> __load_file(const ostd::String& filePath);

            private:
                inline static std::vector<ostd::String> m_lines;
                inline static std::vector<ostd::String> m_guards;
                inline static ostd::String m_directory { "" };
        }; 

		class Assembler
		{
			public: struct tDefine
			{
				ostd::String name;
				ostd::String value;
			};
			public: struct tDisassemblyLine
			{
				uint32_t addr = 0;
				ostd::String code = "";
				uint16_t size = 1;
				inline bool operator<(const tDisassemblyLine& second) const { return addr < second.addr; }
				inline bool operator>(const tDisassemblyLine& second) const { return addr > second.addr; }
			};
			public: struct tSymbol
			{
				std::vector<ostd::UByte> bytes;
				uint16_t address { 0 };
			};
			public: struct tLabel
			{
				std::vector<uint16_t> references;
				uint16_t address { 0 };
			};
			public: struct tStructMember
			{
				ostd::String name;
				std::vector<uint8_t> data;
				int32_t position;
				inline bool operator<(const tStructMember& second) const { return position < second.position; }
				inline bool operator>(const tStructMember& second) const { return position > second.position; }
			};
			public: struct tStructDefinition
			{
				ostd::String name;
				std::vector<tStructMember> members;
				int32_t size;
			};
			public: struct tExportSpec
			{
				ostd::String fileName { "" };
				std::vector<ostd::String> lines;
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
					ostd::String source_file_path { "" };
					ostd::String dest_file_path { "" };
					bool save_disassembly { false };
					int32_t verbose_level { 0xFF };
					bool debug_mode { false };
					bool save_exports { true };
					ostd::String disassembly_file_path { "" };
					std::vector<ostd::String> cpu_extensions;
				};

				public:
					static int32_t loadArguments(int argc, char** argv);
					static void print_application_help(void);

				public:
					inline static tCommandLineArgs args;
					inline static const int32_t RETURN_VAL_EXIT_SUCCESS = 0;
					inline static const int32_t RETURN_VAL_CLOSE_PROGRAM = 512;
					inline static const int32_t RETURN_VAL_TOO_FEW_ARGUMENTS = 1;
					inline static const int32_t RETURN_VAL_MISSING_PARAM = 2;
			};

			public:
				static ostd::ByteStream assembleFromFile(ostd::String fileName);
				static ostd::ByteStream assembleToFile(ostd::String sourceFileName, ostd::String binaryFileName);
				static ostd::ByteStream assembleToVirtualDisk(ostd::String fileName, hw::VirtualHardDrive& vhdd, uint32_t address);
				static bool saveDisassemblyToFile(ostd::String fileName);
				static void printProgramInfo(int32_t verbose_level = 1);

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

				static void parseSections(void);
				static void parseDataSection(void);
				static void parseCodeSection(void);

				static void parseDebugOperands(ostd::String line);
				static void parse0Operand(ostd::String line);
				static void parse1Operand(ostd::String line);
				static void parse2Operand(ostd::String line);
				static void parse3Operand(ostd::String line);
				static void combineDataAndCode(void);

				static ostd::String replaceSymbols(ostd::String line);
				static void replaceLabelRefs(void);
				static eOperandType parseOperand(ostd::String op, int16_t& outOp);
				static uint8_t parseRegister(ostd::String op);

			private:
				inline static ostd::String m_rawSource { "" };
				inline static ostd::ByteStream m_code;

				inline static std::vector<ostd::String> m_lines;
				inline static std::vector<ostd::String> m_rawDataSection;
				inline static std::vector<ostd::String> m_rawCodeSection;

				inline static std::unordered_map<ostd::String, tSymbol> m_symbolTable;
				inline static std::unordered_map<ostd::String, tLabel> m_labelTable;

				inline static uint16_t m_fixedSize { 0 };
				inline static uint8_t m_fixedFillValue { 0x00 };
				inline static uint16_t m_loadAddress { 0x0000 };
				inline static uint16_t m_currentDataAddr { 0x0000 };
				inline static uint16_t m_dataSize { 0x0000 };
				inline static uint16_t m_programSize { 0x0000 };
				inline static ostd::String m_entry_lbl { "" };
				inline static ostd::String m_headerStr { "" };

				inline static std::vector<tStructDefinition> m_structDefs;
				inline static std::vector<tDisassemblyLine> m_disassembly;

				inline static std::unordered_map<ostd::String, tExportSpec> m_exports; 

				inline static ostd::ConsoleOutputHandler out;

			public:
				inline static bool saveExports { false };
				inline static bool debugMode { false };
				inline static std::vector<ostd::String> cpuExtensions;
		};
	}
}