#pragma once

#include <ostd/Utils.hpp>
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
			public: struct tDisassemblyLine {
				uint32_t addr = 0;
				ostd::String code = "";
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
			public: enum class eOperandType {
				Register = 0,
				Immediate,
				DerefMemory,
				DerefRegister,
				Label,

				Error
			};

			public:
				static ostd::ByteStream assembleFromFile(ostd::String fileName);
				static void loadSource(ostd::String source);
				static ostd::ByteStream assembleToFile(ostd::String sourceFileName, ostd::String binaryFileName);
				static ostd::ByteStream assembleToVirtualDisk(ostd::String fileName, hw::VirtualHardDrive& vhdd, uint32_t address);
				static bool saveDisassemblyToFile(ostd::String fileName);

				static void tempPrint(void);

			private:
				static void removeComments(void);
				static void replaceDefines(void);
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

				inline static std::vector<tDisassemblyLine> m_disassembly;
		};
	}
}