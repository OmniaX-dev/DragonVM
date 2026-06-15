#pragma once

#include <ostd/data/Types.hpp>
#include <ostd/data/Color.hpp>

namespace dragon
{
	using AddressType = u16;
	namespace hw { class VirtualCPU; }
	namespace data
	{
		typedef u32 VDiskID;

		class ErrorCodes
		{
			public:
				inline static constexpr u64 NoError                             =             0x0000000000000000;
				inline static constexpr u64 AccessViolation_BiosModeRequired    =             0x0000000000000001;

				inline static constexpr u64 MM_RegionNotFound                     =             0x1000000000000000;
				inline static constexpr u64 MM_AtomicNotSupported                     =             0x1000000000000001;

				inline static constexpr u64 CPU_UnknownInstruction             =             0x2000000000000000;
				inline static constexpr u64 CPU_UnsupportedExtension             =             0x2000000000000001;
				inline static constexpr u64 CPU_StackOverflow                     =             0x2000000000000002;

				inline static constexpr u64 BIOS_FailedToLoad                     =             0x3000000000000000;
				inline static constexpr u64 BIOS_InvalidSize                     =             0x3000000000000001;
				inline static constexpr u64 BIOS_WriteAttempt                     =             0x3000000000000002;
				inline static constexpr u64 BIOS_InvalidAddress                 =             0x3000000000000003;

				inline static constexpr u64 HardDrive_UnableToMount            =             0x4000000000000000;
				inline static constexpr u64 HardDrive_Uninitialized            =             0x4000000000000001;
				inline static constexpr u64 HardDrive_ReadOverflow                =             0x4000000000000002;
				inline static constexpr u64 HardDrive_WriteOverflow            =             0x4000000000000003;
				inline static constexpr u64 HardDrive_BuffWriteOverflow        =             0x4000000000000004;
				inline static constexpr u64 HardDrive_EmptyBuffer                =             0x4000000000000005;
				inline static constexpr u64 HardDrive_InvalidConfiguration        =             0x4000000000000006;
				inline static constexpr u64 HardDrive_ReadFailed                =             0x4000000000000007;
				inline static constexpr u64 HardDrive_WriteFailed                =             0x4000000000000008;
				inline static constexpr u64 HardDrive_MemoryOverflow            =             0x4000000000000009;
				inline static constexpr u64 HardDrive_InvalidDiskSelected        =             0x400000000000000A;
				inline static constexpr u64 HardDrive_EndOfDisk                =             0x400000000000000B;
				inline static constexpr u64 HardDrive_ControllerReadFailed        =             0x400000000000000C;
				inline static constexpr u64 HardDrive_ControllerWriteFailed    =             0x400000000000000D;
				inline static constexpr u64 HardDrive_DisconnectInvalid        =             0x400000000000000E;
				inline static constexpr u64 HardDrive_DiskAlreadyConnected        =             0x400000000000000F;

				inline static constexpr u64 CMOS_InvalidAddress                =             0x5000000000000000;
				inline static constexpr u64 CMOS_UnableToMount                    =             0x5000000000000001;
				inline static constexpr u64 CMOS_InvalidSize                    =             0x5000000000000002;
				inline static constexpr u64 CMOS_Uninitialized                    =             0x5000000000000003;

				inline static constexpr u64 BIOSVideo_InvalidAddress             =             0x6000000000000000;

				inline static constexpr u64 IntVector_InvalidAddress             =             0x7000000000000000;

				inline static constexpr u64 Graphics_MemoryReadFailed            =             0x8000000000000000;
				inline static constexpr u64 Graphics_MemoryWriteFailed            =             0x8000000000000001;

		};

		class ErrorHandler
		{
			public: struct tError
			{
				u64 code;
				String text;
			};

			public:
				inline static void pushError(u64 code, String text) { m_errorStack.push_back({ code, text }); }
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
				inline static constexpr u16 BIOS_Start = 0x0000;
				inline static constexpr u16 BIOS_End   = 0x0FFF;

				inline static constexpr u16 CMOS_Start = 0x1000;
				inline static constexpr u16 CMOS_End   = 0x107F;

				inline static constexpr u16 IntVector_Start = 0x1080;
				inline static constexpr u16 IntVector_End   = 0x127F;

				inline static constexpr u16 Keyboard_Start = 0x1280;
				inline static constexpr u16 Keyboard_End   = 0x135F;

				inline static constexpr u16 Mouse_Start = 0x1360;
				inline static constexpr u16 Mouse_End   = 0x137F;

				inline static constexpr u16 MBR_Start = 0x1380;
				inline static constexpr u16 MBR_End   = 0x157F;

				inline static constexpr u16 DiskInterface_Start = 0x1580;
				inline static constexpr u16 DiskInterface_End   = 0x15FF;

				inline static constexpr u16 VideoCardInterface_Start = 0x1600;
				inline static constexpr u16 VideoCardInterface_End   = 0x16FF;

				inline static constexpr u16 SerialInterface_Start = 0x1700;
				inline static constexpr u16 SerialInterface_End   = 0x173F;

				inline static constexpr u16 Memory_Start = 0x1740;
				inline static constexpr u16 Memory_End   = 0xFFFF;
		};

		class IBiosVideoPalette
		{
			public:
				virtual ostd::Color getColor(u8 col) = 0;
		};

		class BiosVideoDefaultPalette : public IBiosVideoPalette
		{
			public:
				inline BiosVideoDefaultPalette(void)
				{
					m_colors.push_back({ 0, 0, 0 });                // Black
					m_colors.push_back({ 157, 157, 157 });            // Gray
					m_colors.push_back({ 255, 255, 255 });            // White
					m_colors.push_back({ 190, 38, 51 });            // Red
					m_colors.push_back({ 224, 111, 139 });            // Pink
					m_colors.push_back({ 73, 60, 43 });                // Brown
					m_colors.push_back({ 164, 100, 34 });            // Dark Orange
					m_colors.push_back({ 235, 137, 49 });            // Orange
					m_colors.push_back({ 247, 226, 107 });            // Yellow
					m_colors.push_back({ 47, 80, 42 });                // Dark Green
					m_colors.push_back({ 68, 137, 26 });            // Green
					m_colors.push_back({ 163, 206, 39 });            // Lime
					m_colors.push_back({ 27, 38, 50 });                // Dark Blue
					m_colors.push_back({ 0, 87, 132 });                // Blue
					m_colors.push_back({ 49, 162, 242 });            // Light Blue
					m_colors.push_back({ 178, 220, 239 });            // Sky
				}

				inline ostd::Color getColor(u8 col) override
				{
					if (col >= m_colors.size()) return { 0, 0, 0 };
					return m_colors[col];
				}

			protected:
				std::vector<ostd::Color> m_colors;
		};

		class Registers
		{
			public:
				inline static constexpr u8 IP = 0x00;
				inline static constexpr u8 SP = 0x01;
				inline static constexpr u8 FP = 0x02;
				inline static constexpr u8 RV = 0x03;
				inline static constexpr u8 PP = 0x04;
				inline static constexpr u8 FL = 0x05;
				inline static constexpr u8 ACC = 0x06;
				inline static constexpr u8 S1 = 0x07;
				inline static constexpr u8 S2 = 0x08;
				inline static constexpr u8 OFFSET = 0x09;
				inline static constexpr u8 R1 = 0x0A;
				inline static constexpr u8 R2 = 0x0B;
				inline static constexpr u8 R3 = 0x0C;
				inline static constexpr u8 R4 = 0x0D;
				inline static constexpr u8 R5 = 0x0E;
				inline static constexpr u8 R6 = 0x0F;
				inline static constexpr u8 R7 = 0x10;
				inline static constexpr u8 R8 = 0x11;
				inline static constexpr u8 R9 = 0x12;
				inline static constexpr u8 R10 = 0x13;

				inline static constexpr u8 Last = 0x14;
		};

		class Flags
		{
			public:
				inline static constexpr u8 InterruptsEnabled = 0;
				inline static constexpr u8 OffsetModeEnabled = 1;
		};

		class InterruptCodes
		{
			public:
				inline static constexpr u8 DiskInterfaceFFinished = 0x80;
				inline static constexpr u8 KeyPressed = 0xA0;
				inline static constexpr u8 KeyReleased = 0xA1;
				inline static constexpr u8 TextEntered = 0xA2;

				inline static constexpr u8 Text16ModeScreenRefreshed = 0xE0;

				inline static String getInterruptName(u8 code)
				{
					switch (code)
					{
						case DiskInterfaceFFinished:     return "Disk_Interface_Finished";
						case KeyPressed:                 return "Key_Pressed";
						case KeyReleased:                 return "Key_Released";
						case TextEntered:                 return "Text_Entered";
						case Text16ModeScreenRefreshed:    return "Text16_Mode_Screen_Refreshed";
						default:                        return "UNKNOWN";
					}
					return "UNKNOWN";
				}
		};

		class CMOSRegisters
		{
			public:
				inline static constexpr u8 MemoryStart            = 0x00;
				inline static constexpr u8 MemorySize             = 0x02;
				inline static constexpr u8 ClockSpeed             = 0x04;
				inline static constexpr u8 ScreenRedrawRate     = 0x06;
				inline static constexpr u8 ScreenWidth         = 0x07;
				inline static constexpr u8 ScreenHeight         = 0x09;
				inline static constexpr u8 BootDisk             = 0x10;
				inline static constexpr u8 StackSize             = 0x11;

				inline static constexpr u8 DiskList             = 0x7E;
		};

		class DPTStructure
		{
			public: struct tFlags {
				inline static constexpr u8 Boot             = 0;
			};
			public:

				inline static constexpr u16 DPTID                                = 0x000;
				inline static constexpr u16 DPTVersionMaj                        = 0x002;
				inline static constexpr u16 DPTVersionMin                        = 0x003;
				inline static constexpr u16 PartitionCount                        = 0x004;

				inline static constexpr u16 EntriesStart                        = 0x00C;

				inline static constexpr u16 EntryStartAddress                    = 0x000;
				inline static constexpr u16 EntryPartitionSize                    = 0x004;
				inline static constexpr u16 EntryFlags                            = 0x008;
				inline static constexpr u16 EntryPartitionLabel                = 0x024;



				inline static constexpr u16 HeaderReservedSizeBytes            = 7;
				inline static constexpr u16 DiskAddress                        = 0x200;
				inline static constexpr u16 DPT_ID_CODE                        = 0xF1CA;
				inline static constexpr u16 EntrySizeBytes                        = 100;
				inline static constexpr u16 EntryLabelSizeBytes                = 64;
				inline static constexpr u16 EntryReservedSizeBytes                = 26;
				inline static constexpr u16 HeaderSizeBytes                    = 12;
				inline static constexpr u16 DPTBlockSizeBytes                    = 512;
				inline static constexpr u8 CurrentDPTVersionMaj                 = 0x00;
				inline static constexpr u8 CurrentDPTVersionMin                 = 0x02;
				inline static constexpr u32 DiskStartAddr                         = 0x00000400;
				inline static constexpr u8 MaxPartCount                        = 5;



				inline static constexpr u16 BootPart_IDAddr                    = 0x0000;
				inline static constexpr u16 BootPart_CodeStart                    = 0x0020;
				inline static constexpr u16 BootPart_ID_CODE                    = 0xF1C4;
				inline static constexpr u16 BootPart_CodeSizeBytes                = 1024;
				inline static constexpr u8 BootPart_HeaderSizeBytes            = 32;
		};

		class CPUExtension
		{
			public:
				inline virtual ~CPUExtension(void) {  }
				inline CPUExtension(u8 code, String name) : m_code(code), m_name(name) {  }
				virtual String getOpCodeString(u8 opCode) = 0;
				virtual u8 getInstructionSIze(u8 opCode) = 0;
				virtual bool execute(hw::VirtualCPU& vcpu) = 0;

			public:
				u8 m_code { 0x00 };
				String m_name { "" };
		};

		class OpCodes
		{
			public:
				inline static constexpr u8 NoOp = 0x00;
				inline static constexpr u8 DEBUG_Break = 0x01;
				inline static constexpr u8 BIOSModeImm = 0x02;
				inline static constexpr u8 DEBUG_StartProfile = 0x03;
				inline static constexpr u8 DEBUG_StopProfile = 0x04;
				inline static constexpr u8 DEBUG_DumpRAM = 0x05;

				inline static constexpr u8 MovImmReg = 0x10;
				inline static constexpr u8 MovRegReg = 0x11;
				inline static constexpr u8 MovRegMem = 0x12;
				inline static constexpr u8 MovMemReg = 0x13;
				inline static constexpr u8 MovImmMem = 0x14;
				inline static constexpr u8 MovDerefRegReg = 0x15;
				// inline static constexpr u8 MovImmRegOffReg = 0x16;
				inline static constexpr u8 MovDerefRegMem = 0x17;
				inline static constexpr u8 MovRegDerefReg = 0x18;
				inline static constexpr u8 MovMemDerefReg = 0x19;
				inline static constexpr u8 MovImmDerefReg = 0x1A;
				inline static constexpr u8 MovDerefRegDerefReg = 0x1B;

				inline static constexpr u8 MovByteImmMem = 0x20;
				inline static constexpr u8 MovByteDerefRegMem = 0x21;
				inline static constexpr u8 MovByteImmDerefReg = 0x22;
				inline static constexpr u8 MovByteRegDerefReg = 0x23;
				inline static constexpr u8 MovByteMemDerefReg = 0x24;
				inline static constexpr u8 MovByteDerefRegDerefReg = 0x25;
				inline static constexpr u8 MovByteMemReg = 0x26;
				inline static constexpr u8 MovByteImmReg = 0x27;
				inline static constexpr u8 MovByteDerefRegReg = 0x28;
				inline static constexpr u8 MovByteRegMem = 0x29;

				inline static constexpr u8 AddRegReg = 0x30;
				inline static constexpr u8 AddImmReg = 0x31;
				inline static constexpr u8 SubRegReg = 0x32;
				inline static constexpr u8 SubImmReg = 0x33;
				inline static constexpr u8 MulRegReg = 0x34;
				inline static constexpr u8 MulImmReg = 0x35;
				inline static constexpr u8 DivRegReg = 0x36;
				inline static constexpr u8 DivImmReg = 0x37;

				inline static constexpr u8 IncReg = 0x40;
				inline static constexpr u8 DecReg = 0x41;

				inline static constexpr u8 PushImm = 0x50;
				inline static constexpr u8 PushReg = 0x51;
				inline static constexpr u8 PopReg = 0x52;
				inline static constexpr u8 CallImm = 0x53;
				inline static constexpr u8 CallReg = 0x54;
				inline static constexpr u8 Ret = 0x55;
				inline static constexpr u8 ArgReg = 0x56;

				inline static constexpr u8 LShiftRegImm = 0x60;
				inline static constexpr u8 LShiftRegReg = 0x61;
				inline static constexpr u8 RShiftRegImm = 0x62;
				inline static constexpr u8 RShiftRegReg = 0x63;
				inline static constexpr u8 AndRegImm = 0x64;
				inline static constexpr u8 AndRegReg = 0x65;
				inline static constexpr u8 OrRegImm = 0x66;
				inline static constexpr u8 OrRegReg = 0x67;
				inline static constexpr u8 XorRegImm = 0x68;
				inline static constexpr u8 XorRegReg = 0x69;
				inline static constexpr u8 NotReg = 0x6A;
				inline static constexpr u8 NegReg = 0x6B;
				inline static constexpr u8 NegByteReg = 0x6C;

				inline static constexpr u8 JmpNotEqImm = 0x70;
				inline static constexpr u8 JmpNotEqReg = 0x71;
				inline static constexpr u8 JmpEqImm = 0x72;
				inline static constexpr u8 JmpEqReg = 0x73;
				inline static constexpr u8 JmpGrImm = 0x74;
				inline static constexpr u8 JmpGrReg = 0x75;
				inline static constexpr u8 JmpLessImm = 0x76;
				inline static constexpr u8 JmpLessReg = 0x77;
				inline static constexpr u8 JmpGeImm = 0x78;
				inline static constexpr u8 JmpGeReg = 0x79;
				inline static constexpr u8 JmpLeImm = 0x7A;
				inline static constexpr u8 JmpLeReg = 0x7B;
				inline static constexpr u8 Jmp = 0x7C;

				inline static constexpr u8 Ext01 = 0xE0;
				inline static constexpr u8 Ext02 = 0xE1;
				inline static constexpr u8 Ext03 = 0xE2;
				inline static constexpr u8 Ext04 = 0xE3;
				inline static constexpr u8 Ext05 = 0xE4;
				inline static constexpr u8 Ext06 = 0xE5;
				inline static constexpr u8 Ext07 = 0xE6;
				inline static constexpr u8 Ext08 = 0xE7;
				inline static constexpr u8 Ext09 = 0xE8;
				inline static constexpr u8 Ext10 = 0xE9;
				inline static constexpr u8 Ext11 = 0xEA;
				inline static constexpr u8 Ext12 = 0xEB;
				inline static constexpr u8 Ext13 = 0xEC;
				inline static constexpr u8 Ext14 = 0xED;
				inline static constexpr u8 Ext15 = 0xEE;
				inline static constexpr u8 Ext16 = 0xEF;

				inline static constexpr u8 ZeroFlag = 0xF0;
				inline static constexpr u8 SetFlag = 0xF1;
				inline static constexpr u8 ToggleFlag = 0xF2;
				inline static constexpr u8 RetInt = 0xFD;
				inline static constexpr u8 Int = 0xFE;
				inline static constexpr u8 Halt = 0xFF;

				static String getOpCodeString(u8 opCode);
				static u8 getInstructionSIze(u8 opCode);
		};

		class DefaultValues
		{
			public:
				inline static constexpr u8 MaxMemoryExtensionPages = 255;
		};
	}
}
