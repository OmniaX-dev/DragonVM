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
		class Assembler
		{
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
					ostd::String dest_file_path = { "" };
					bool save_disassembly = { false };
					bool verbose = { false };
					ostd::String disassembly_file_path = { "" };
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
				static void loadSource(ostd::String source);
				static ostd::ByteStream assembleToFile(ostd::String sourceFileName, ostd::String binaryFileName);
				static ostd::ByteStream assembleToVirtualDisk(ostd::String fileName, hw::VirtualHardDrive& vhdd, uint32_t address);
				static bool saveDisassemblyToFile(ostd::String fileName);

				static void printProgramInfo(void);

			private:
				static void removeComments(void);
				static void replaceDefines(void);
				static void replaceGroupDefines(void);
				static void parseSections(void);
				static void parseStructInstances(void);
				static void parseStructures(void);
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
				inline static std::vector<tStructDefinition> m_structDefs;

				inline static std::vector<tDisassemblyLine> m_disassembly;

				inline static ostd::ConsoleOutputHandler out;
		};
	}
}