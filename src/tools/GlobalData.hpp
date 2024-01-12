#pragma once

#include <ostd/Types.hpp>
#include <ostd/Color.hpp>

namespace dragon
{
	namespace data
	{
		typedef uint32_t VDiskID;

		class ErrorCodes
		{
			public:
				inline static constexpr uint64_t NoError 							= 			0x0000000000000000;

				inline static constexpr uint64_t MM_RegionNotFound 					= 			0x1000000000000000;

				inline static constexpr uint64_t CPU_UnknownInstruction 			= 			0x2000000000000000;

				inline static constexpr uint64_t BIOS_FailedToLoad 					= 			0x3000000000000000;
				inline static constexpr uint64_t BIOS_InvalidSize 					= 			0x3000000000000001;
				inline static constexpr uint64_t BIOS_WriteAttempt 					= 			0x3000000000000002;
				inline static constexpr uint64_t BIOS_InvalidAddress 				= 			0x3000000000000003;

				inline static constexpr uint64_t HardDrive_UnableToMount			= 			0x4000000000000000;
				inline static constexpr uint64_t HardDrive_Uninitialized			= 			0x4000000000000001;
				inline static constexpr uint64_t HardDrive_ReadOverflow				= 			0x4000000000000002;
				inline static constexpr uint64_t HardDrive_WriteOverflow			= 			0x4000000000000003;
				inline static constexpr uint64_t HardDrive_BuffWriteOverflow		= 			0x4000000000000004;
				inline static constexpr uint64_t HardDrive_EmptyBuffer				= 			0x4000000000000005;
				inline static constexpr uint64_t HardDrive_InvalidConfiguration		= 			0x4000000000000006;
				inline static constexpr uint64_t HardDrive_ReadFailed				= 			0x4000000000000007;
				inline static constexpr uint64_t HardDrive_WriteFailed				= 			0x4000000000000008;
				inline static constexpr uint64_t HardDrive_MemoryOverflow			= 			0x4000000000000009;
				inline static constexpr uint64_t HardDrive_InvalidDiskSelected		= 			0x400000000000000A;
				inline static constexpr uint64_t HardDrive_EndOfDisk				= 			0x400000000000000B;
				inline static constexpr uint64_t HardDrive_ControllerReadFailed		= 			0x400000000000000C;
				inline static constexpr uint64_t HardDrive_ControllerWriteFailed	= 			0x400000000000000D;
				inline static constexpr uint64_t HardDrive_DisconnectInvalid		= 			0x400000000000000E;
				inline static constexpr uint64_t HardDrive_DiskAlreadyConnected		= 			0x400000000000000F;

				inline static constexpr uint64_t CMOS_InvalidAddress				= 			0x5000000000000000;
				inline static constexpr uint64_t CMOS_UnableToMount					= 			0x5000000000000001;
				inline static constexpr uint64_t CMOS_InvalidSize					= 			0x5000000000000002;
				inline static constexpr uint64_t CMOS_Uninitialized					= 			0x5000000000000003;

				inline static constexpr uint64_t BIOSVideo_InvalidAddress 			= 			0x6000000000000000;

				inline static constexpr uint64_t IntVector_InvalidAddress 			= 			0x7000000000000000;

				inline static constexpr uint64_t Graphics_MemoryReadFailed			= 			0x8000000000000000;
				inline static constexpr uint64_t Graphics_MemoryWriteFailed			= 			0x8000000000000001;

		};

		class ErrorHandler
		{
			public: struct tError
			{
				uint64_t code;
				ostd::String text;
			};

			public:
				inline static void pushError(uint64_t code, ostd::String text) { m_errorStack.push_back({ code, text }); }
				inline static bool hasError(void) { return m_errorStack.size() > 0; }
				inline static tError popError(void)
				{
					if (m_errorStack.size() == 0)
						return { ErrorCodes::NoError, "No Errors." };
					tError err = m_errorStack[m_errorStack.size() - 1];
					m_errorStack.pop_back();
					return err;
				}

			private:
				inline static std::vector<tError> m_errorStack;
		};

		class MemoryMapAddresses
		{
			public:
				inline static constexpr uint16_t BIOS_Start = 0x0000;
				inline static constexpr uint16_t BIOS_End   = 0x0FFF;

				inline static constexpr uint16_t CMOS_Start = 0x1000;
				inline static constexpr uint16_t CMOS_End   = 0x107F;

				inline static constexpr uint16_t IntVector_Start = 0x1080;
				inline static constexpr uint16_t IntVector_End   = 0x127F;

				inline static constexpr uint16_t Keyboard_Start = 0x1280;
				inline static constexpr uint16_t Keyboard_End   = 0x135F;

				inline static constexpr uint16_t Mouse_Start = 0x1360;
				inline static constexpr uint16_t Mouse_End   = 0x137F;

				inline static constexpr uint16_t MBR_Start = 0x1380;
				inline static constexpr uint16_t MBR_End   = 0x157F;

				inline static constexpr uint16_t DiskInterface_Start = 0x1580;
				inline static constexpr uint16_t DiskInterface_End   = 0x15FF;

				inline static constexpr uint16_t VideoCardInterface_Start = 0x1600;
				inline static constexpr uint16_t VideoCardInterface_End   = 0x16FF;

				inline static constexpr uint16_t SerialInterface_Start = 0x1700;
				inline static constexpr uint16_t SerialInterface_End   = 0x173F;

				inline static constexpr uint16_t Memory_Start = 0x1740;
				inline static constexpr uint16_t Memory_End   = 0xFFFF;
		};

		class IBiosVideoPalette
		{
			public:
				virtual ostd::Color getColor(uint8_t col) = 0;
		};

		class BiosVideoDefaultPalette : public IBiosVideoPalette
		{
			public:
				inline BiosVideoDefaultPalette(void)
				{
					m_colors.push_back({ 0, 0, 0 });
					m_colors.push_back({ 157, 157, 157 });
					m_colors.push_back({ 255, 255, 255 });
					m_colors.push_back({ 190, 38, 51 });
					m_colors.push_back({ 224, 111, 139 });
					m_colors.push_back({ 73, 60, 43 });
					m_colors.push_back({ 164, 100, 34 });
					m_colors.push_back({ 235, 137, 49 });
					m_colors.push_back({ 247, 226, 107 });
					m_colors.push_back({ 47, 80, 42 });
					m_colors.push_back({ 68, 137, 26 });
					m_colors.push_back({ 163, 206, 39 });
					m_colors.push_back({ 27, 38, 50 });
					m_colors.push_back({ 0, 87, 132 });
					m_colors.push_back({ 49, 162, 242 });
					m_colors.push_back({ 178, 220, 239 });
				}

				inline ostd::Color getColor(uint8_t col) override
				{
					if (col >= m_colors.size()) return { 0, 0, 0 };
					return m_colors[col];
				}

			private:
				std::vector<ostd::Color> m_colors;
		};

		class Registers
		{
			public:
				inline static constexpr uint8_t IP = 0x00;
				inline static constexpr uint8_t SP = 0x01;
				inline static constexpr uint8_t FP = 0x02;
				inline static constexpr uint8_t RV = 0x03;
				inline static constexpr uint8_t PP = 0x04;
				inline static constexpr uint8_t FL = 0x05;
				inline static constexpr uint8_t ACC = 0x06;
				//0x07, 0x08, 0x09 Are hidden registers used internally, but can be normally accessed by address
				inline static constexpr uint8_t R1 = 0x0A;
				inline static constexpr uint8_t R2 = 0x0B;
				inline static constexpr uint8_t R3 = 0x0C;
				inline static constexpr uint8_t R4 = 0x0D;
				inline static constexpr uint8_t R5 = 0x0E;
				inline static constexpr uint8_t R6 = 0x0F;
				inline static constexpr uint8_t R7 = 0x10;
				inline static constexpr uint8_t R8 = 0x11;
				inline static constexpr uint8_t R9 = 0x12;
				inline static constexpr uint8_t R10 = 0x13;

				inline static constexpr uint8_t Last = 0x14;
		};

		class Flags
		{
			public:
				inline static constexpr uint8_t InterruptsEnabled = 0;
		};

		class InterruptCodes
		{
			public:
				inline static constexpr uint8_t DiskInterfaceFFinished = 0x80;
		};

		class OpCodes
		{
			public:
				inline static constexpr uint8_t NoOp = 0x00;

				inline static constexpr uint8_t DEBUG_Break = 0x01;
			
				inline static constexpr uint8_t MovImmReg = 0x10;
				inline static constexpr uint8_t MovRegReg = 0x11;
				inline static constexpr uint8_t MovRegMem = 0x12;
				inline static constexpr uint8_t MovMemReg = 0x13;
				inline static constexpr uint8_t MovImmMem = 0x14;
				inline static constexpr uint8_t MovDerefRegReg = 0x15;
				inline static constexpr uint8_t MovImmRegOffReg = 0x16;
				inline static constexpr uint8_t MovDerefRegMem = 0x17;
				inline static constexpr uint8_t MovRegDerefReg = 0x18;
				inline static constexpr uint8_t MovMemDerefReg = 0x19;
				inline static constexpr uint8_t MovImmDerefReg = 0x1A;
				inline static constexpr uint8_t MovDerefRegDerefReg = 0x1B;

				inline static constexpr uint8_t MovByteImmMem = 0x20;
				inline static constexpr uint8_t MovByteDerefRegMem = 0x21;
				inline static constexpr uint8_t MovByteImmDerefReg = 0x22;
				inline static constexpr uint8_t MovByteRegDerefReg = 0x23;
				inline static constexpr uint8_t MovByteMemDerefReg = 0x24;
				inline static constexpr uint8_t MovByteDerefRegDerefReg = 0x25;
				inline static constexpr uint8_t MovByteMemReg = 0x26;
				inline static constexpr uint8_t MovByteImmReg = 0x27;
				inline static constexpr uint8_t MovByteDerefRegReg = 0x28;
				inline static constexpr uint8_t MovByteRegMem = 0x29;

				inline static constexpr uint8_t AddRegReg = 0x30;
				inline static constexpr uint8_t AddImmReg = 0x31;
				inline static constexpr uint8_t SubRegReg = 0x32;
				inline static constexpr uint8_t SubImmReg = 0x33;
				inline static constexpr uint8_t MulRegReg = 0x34;
				inline static constexpr uint8_t MulImmReg = 0x35;
				inline static constexpr uint8_t DivRegReg = 0x36;
				inline static constexpr uint8_t DivImmReg = 0x37;

				inline static constexpr uint8_t IncReg = 0x40;
				inline static constexpr uint8_t DecReg = 0x41;

				inline static constexpr uint8_t PushImm = 0x50;
				inline static constexpr uint8_t PushReg = 0x51;
				inline static constexpr uint8_t PopReg = 0x52;
				inline static constexpr uint8_t CallImm = 0x53;
				inline static constexpr uint8_t CallReg = 0x54;
				inline static constexpr uint8_t Ret = 0x55;
				inline static constexpr uint8_t ArgReg = 0x56;

				inline static constexpr uint8_t LShiftRegImm = 0x60;
				inline static constexpr uint8_t LShiftRegReg = 0x61;
				inline static constexpr uint8_t RShiftRegImm = 0x62;
				inline static constexpr uint8_t RShiftRegReg = 0x63;
				inline static constexpr uint8_t AndRegImm = 0x64;
				inline static constexpr uint8_t AndRegReg = 0x65;
				inline static constexpr uint8_t OrRegImm = 0x66;
				inline static constexpr uint8_t OrRegReg = 0x67;
				inline static constexpr uint8_t XorRegImm = 0x68;
				inline static constexpr uint8_t XorRegReg = 0x69;
				inline static constexpr uint8_t NotReg = 0x6A;
				
				inline static constexpr uint8_t JmpNotEqImm = 0x70;
				inline static constexpr uint8_t JmpNotEqReg = 0x71;
				inline static constexpr uint8_t JmpEqImm = 0x72;
				inline static constexpr uint8_t JmpEqReg = 0x73;
				inline static constexpr uint8_t JmpGrImm = 0x74;
				inline static constexpr uint8_t JmpGrReg = 0x75;
				inline static constexpr uint8_t JmpLessImm = 0x76;
				inline static constexpr uint8_t JmpLessReg = 0x77;
				inline static constexpr uint8_t JmpGeImm = 0x78;
				inline static constexpr uint8_t JmpGeReg = 0x79;
				inline static constexpr uint8_t JmpLeImm = 0x7A;
				inline static constexpr uint8_t JmpLeReg = 0x7B;
				inline static constexpr uint8_t Jmp = 0x7C;
				
				inline static constexpr uint8_t RetInt = 0xFD;
				inline static constexpr uint8_t Int = 0xFE;
				inline static constexpr uint8_t Halt = 0xFF;

				inline static ostd::String getOpCodeString(uint8_t opCode)
				{
					switch (opCode)
					{
						case data::OpCodes::NoOp: return "NoOp";
						case data::OpCodes::DEBUG_Break: return "debug_break";
						case data::OpCodes::MovImmReg: return "MovImmReg";
						case data::OpCodes::MovImmMem: return "MovImmMem";
						case data::OpCodes::MovRegReg: return "MovRegReg";
						case data::OpCodes::MovRegMem: return "MovRegMem";
						case data::OpCodes::MovMemReg: return "MovMemReg";
						case data::OpCodes::MovDerefRegReg: return "MovDerefRegReg";
						case data::OpCodes::MovDerefRegMem: return "MovDerefRegMem";
						case data::OpCodes::MovImmRegOffReg: return "MovImmRegOffReg";
						case data::OpCodes::MovRegDerefReg: return "MovRegDerefReg";
						case data::OpCodes::MovMemDerefReg: return "MovMemDerefReg";
						case data::OpCodes::MovImmDerefReg: return "MovImmDerefReg";
						case data::OpCodes::MovDerefRegDerefReg: return "MovDerefRegDerefReg";
						case data::OpCodes::MovByteImmMem: return "MovByteImmMem";
						case data::OpCodes::MovByteRegMem: return "MovByteRegMem";
						case data::OpCodes::MovByteDerefRegMem: return "MovByteDerefRegMem";
						case data::OpCodes::MovByteImmDerefReg: return "MovByteImmDerefReg";
						case data::OpCodes::MovByteRegDerefReg: return "MovByteRegDerefReg";
						case data::OpCodes::MovByteMemDerefReg: return "MovByteMemDerefReg";
						case data::OpCodes::MovByteDerefRegDerefReg: return "MovByteDerefRegDerefReg";
						case data::OpCodes::MovByteMemReg: return "MovByteMemReg";
						case data::OpCodes::MovByteImmReg: return "MovByteImmReg";
						case data::OpCodes::MovByteDerefRegReg: return "MovByteDerefRegReg";
						case data::OpCodes::AddImmReg: return "AddImmReg";
						case data::OpCodes::AddRegReg: return "AddRegReg";
						case data::OpCodes::SubImmReg: return "SubImmReg";
						case data::OpCodes::SubRegReg: return "SubRegReg";
						case data::OpCodes::MulImmReg: return "MulImmReg";
						case data::OpCodes::MulRegReg: return "MulRegReg";
						case data::OpCodes::DivImmReg: return "DivImmReg";
						case data::OpCodes::DivRegReg: return "DivRegReg";
						case data::OpCodes::IncReg: return "IncReg";
						case data::OpCodes::DecReg: return "DecReg";
						case data::OpCodes::RShiftRegImm: return "RShiftRegImm";
						case data::OpCodes::RShiftRegReg: return "RShiftRegReg";
						case data::OpCodes::LShiftRegImm: return "LShiftRegImm";
						case data::OpCodes::LShiftRegReg: return "LShiftRegReg";
						case data::OpCodes::AndRegImm: return "AndRegImm";
						case data::OpCodes::AndRegReg: return "AndRegReg";
						case data::OpCodes::OrRegImm: return "OrRegImm";
						case data::OpCodes::OrRegReg: return "OrRegReg";
						case data::OpCodes::XorRegImm: return "XorRegImm";
						case data::OpCodes::XorRegReg: return "XorRegReg";
						case data::OpCodes::NotReg: return "NotReg";
						case data::OpCodes::JmpNotEqImm: return "JmpNotEqImm";
						case data::OpCodes::JmpNotEqReg: return "JmpNotEqReg";
						case data::OpCodes::JmpEqImm: return "JmpEqImm";
						case data::OpCodes::JmpEqReg: return "JmpEqReg";
						case data::OpCodes::JmpGrImm: return "JmpGrImm";
						case data::OpCodes::JmpGrReg: return "JmpGrReg";
						case data::OpCodes::JmpLessImm: return "JmpLessImm";
						case data::OpCodes::JmpLessReg: return "JmpLessReg";
						case data::OpCodes::JmpGeImm: return "JmpGeImm";
						case data::OpCodes::JmpGeReg: return "JmpGeReg";
						case data::OpCodes::JmpLeImm: return "JmpLeImm";
						case data::OpCodes::JmpLeReg: return "JmpLeReg";
						case data::OpCodes::Jmp: return "Jmp";
						case data::OpCodes::Halt: return "Halt";
						case data::OpCodes::PushImm: return "PushImm";
						case data::OpCodes::PushReg: return "PushReg";
						case data::OpCodes::PopReg: return "PopReg";
						case data::OpCodes::CallImm: return "CallImm";
						case data::OpCodes::CallReg: return "CallReg";
						case data::OpCodes::Ret: return "Ret";
						case data::OpCodes::ArgReg: return "ArgReg";
						case data::OpCodes::RetInt: return "RetInt";
						case data::OpCodes::Int: return "Int";
						default: return "UNKNOWN_INST";
					}
				}

				inline static uint8_t getInstructionSIze(uint8_t opCode)
				{
					switch (opCode)
					{
						case data::OpCodes::NoOp: return 1;
						case data::OpCodes::DEBUG_Break: return 1;
						case data::OpCodes::MovImmReg: return 4;
						case data::OpCodes::MovImmMem: return 5;
						case data::OpCodes::MovRegReg: return 3;
						case data::OpCodes::MovRegMem: return 4;
						case data::OpCodes::MovMemReg: return 4;
						case data::OpCodes::MovDerefRegReg: return 3;
						case data::OpCodes::MovDerefRegMem: return 4;
						case data::OpCodes::MovImmRegOffReg: return 5;
						case data::OpCodes::MovRegDerefReg: return 3;
						case data::OpCodes::MovMemDerefReg: return 4;
						case data::OpCodes::MovImmDerefReg: return 4;
						case data::OpCodes::MovDerefRegDerefReg: return 3;
						case data::OpCodes::MovByteImmMem: return 4;
						case data::OpCodes::MovByteRegMem: return 4;
						case data::OpCodes::MovByteDerefRegMem: return 4;
						case data::OpCodes::MovByteImmDerefReg: return 3;
						case data::OpCodes::MovByteRegDerefReg: return 3;
						case data::OpCodes::MovByteMemDerefReg: return 4;
						case data::OpCodes::MovByteDerefRegDerefReg: return 3;
						case data::OpCodes::MovByteMemReg: return 4;
						case data::OpCodes::MovByteImmReg: return 3;
						case data::OpCodes::MovByteDerefRegReg: return 3;
						case data::OpCodes::AddImmReg: return 4;
						case data::OpCodes::AddRegReg: return 3;
						case data::OpCodes::SubImmReg: return 4;
						case data::OpCodes::SubRegReg: return 3;
						case data::OpCodes::MulImmReg: return 4;
						case data::OpCodes::MulRegReg: return 3;
						case data::OpCodes::DivImmReg: return 4;
						case data::OpCodes::DivRegReg: return 3;
						case data::OpCodes::IncReg: return 2;
						case data::OpCodes::DecReg: return 2;
						case data::OpCodes::RShiftRegImm: return 4;
						case data::OpCodes::RShiftRegReg: return 3;
						case data::OpCodes::LShiftRegImm: return 4;
						case data::OpCodes::LShiftRegReg: return 3;
						case data::OpCodes::AndRegImm: return 4;
						case data::OpCodes::AndRegReg: return 3;
						case data::OpCodes::OrRegImm: return 4;
						case data::OpCodes::OrRegReg: return 3;
						case data::OpCodes::XorRegImm: return 4;
						case data::OpCodes::XorRegReg: return 3;
						case data::OpCodes::NotReg: return 2;
						case data::OpCodes::JmpNotEqImm: return 5;
						case data::OpCodes::JmpNotEqReg: return 4;
						case data::OpCodes::JmpEqImm: return 5;
						case data::OpCodes::JmpEqReg: return 4;
						case data::OpCodes::JmpGrImm: return 5;
						case data::OpCodes::JmpGrReg: return 4;
						case data::OpCodes::JmpLessImm: return 5;
						case data::OpCodes::JmpLessReg: return 4;
						case data::OpCodes::JmpGeImm: return 5;
						case data::OpCodes::JmpGeReg: return 4;
						case data::OpCodes::JmpLeImm: return 5;
						case data::OpCodes::JmpLeReg: return 4;
						case data::OpCodes::Jmp: return 3;
						case data::OpCodes::Halt: return 1;
						case data::OpCodes::PushImm: return 3;
						case data::OpCodes::PushReg: return 2;
						case data::OpCodes::PopReg: return 2;
						case data::OpCodes::CallImm: return 3;
						case data::OpCodes::CallReg: return 2;
						case data::OpCodes::Ret: return 1;
						case data::OpCodes::ArgReg: return 2;
						case data::OpCodes::RetInt: return 1;
						case data::OpCodes::Int: return 2;
						default: return 0;
					}
				}

		};
	}
}