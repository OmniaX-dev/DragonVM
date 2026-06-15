#pragma once

#include "IMemoryDevice.hpp"
#include "../tools/GlobalData.hpp"
#include "../tools/LegacyOstdSerial.hpp"
#include <fstream>
#include <unordered_map>

namespace dragon
{
	namespace hw
	{
		class VirtualHardDrive;
		class MemoryMapper;
		class VirtualCPU;
		class VirtualRAM;

		class VirtualBIOS : public IMemoryDevice
		{
			public:
				inline VirtualBIOS(void) { m_initialized = 0; }
				inline VirtualBIOS(const String& biosFilePath) { init(biosFilePath); }
				void init(const String& biosFilePath);
				i8 read8(u16 addr) override;
				i16 read16(u16 addr) override;
				i8 write8(u16 addr, i8 value) override;
				i16 write16(u16 addr, i16 value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
				ostd::ByteStream m_bios;
				bool m_initialized { false };
		};
		class InterruptVector : public IMemoryDevice
		{
			public:
				InterruptVector(void);
				i8 read8(u16 addr) override;
				i16 read16(u16 addr) override;
				i8 write8(u16 addr, i8 value) override;
				i16 write16(u16 addr, i16 value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
				ostd::ByteStream m_data;
		};
		class VirtualKeyboard : public IMemoryDevice
		{
			public: enum class eKeys
			{
				Delete								= (i16)'\x7F',
				Return								= (i16)'\r',
				Escape								= (i16)'\x1B',
				Backspace							= (i16)'\b',
				Tab									= (i16)'\t',
				Spacebar							= (i16)' ',
				ExclamationMark						= (i16)'!',
				DoubleQuote							= (i16)'"',
				Hash								= (i16)'#',
				Percent								= (i16)'%',
				DollarSign							= (i16)'$',
				Ampersand							= (i16)'&',
				SingleQuote							= (i16)'\'',
				LeftParenthesis						= (i16)'(',
				RightParenthesis					= (i16)')',
				Asterisk							= (i16)'*',
				Plus								= (i16)'+',
				Comma								= (i16)',',
				Minus								= (i16)'-',
				Period								= (i16)'.',
				ForwardSlash						= (i16)'/',
				Num0								= (i16)'0',
				Num1								= (i16)'1',
				Num2								= (i16)'2',
				Num3								= (i16)'3',
				Num4								= (i16)'4',
				Num5								= (i16)'5',
				Num6								= (i16)'6',
				Num7								= (i16)'7',
				Num8								= (i16)'8',
				Num9								= (i16)'9',
				Colon								= (i16)':',
				Semicolon							= (i16)';',
				LessThan							= (i16)'<',
				Equals								= (i16)'=',
				GreaterThan							= (i16)'>',
				QuestionMark						= (i16)'?',
				AtSign								= (i16)'@',

				UpperCase_A							= (i16)'A',
				UpperCase_B							= (i16)'B',
				UpperCase_C							= (i16)'C',
				UpperCase_D							= (i16)'D',
				UpperCase_E							= (i16)'E',
				UpperCase_F							= (i16)'F',
				UpperCase_G							= (i16)'G',
				UpperCase_H							= (i16)'H',
				UpperCase_I							= (i16)'I',
				UpperCase_J							= (i16)'J',
				UpperCase_K							= (i16)'K',
				UpperCase_L							= (i16)'L',
				UpperCase_M							= (i16)'M',
				UpperCase_N							= (i16)'N',
				UpperCase_O							= (i16)'O',
				UpperCase_P							= (i16)'P',
				UpperCase_Q							= (i16)'Q',
				UpperCase_R							= (i16)'R',
				UpperCase_S							= (i16)'S',
				UpperCase_T							= (i16)'T',
				UpperCase_U							= (i16)'U',
				UpperCase_V							= (i16)'V',
				UpperCase_W							= (i16)'W',
				UpperCase_X							= (i16)'X',
				UpperCase_Y							= (i16)'Y',
				UpperCase_Z							= (i16)'Z',

				LeftBracket							= (i16)'[',
				BackSlash							= (i16)'\\',
				RightBracket						= (i16)']',
				Caret								= (i16)'^',
				Underscore							= (i16)'_',
				BackQuote							= (i16)'`',
				LowerCase_a							= (i16)'a',
				LowerCase_b							= (i16)'b',
				LowerCase_c							= (i16)'c',
				LowerCase_d							= (i16)'d',
				LowerCase_e							= (i16)'e',
				LowerCase_f							= (i16)'f',
				LowerCase_g							= (i16)'g',
				LowerCase_h							= (i16)'h',
				LowerCase_i							= (i16)'i',
				LowerCase_j							= (i16)'j',
				LowerCase_k							= (i16)'k',
				LowerCase_l							= (i16)'l',
				LowerCase_m							= (i16)'m',
				LowerCase_n							= (i16)'n',
				LowerCase_o							= (i16)'o',
				LowerCase_p							= (i16)'p',
				LowerCase_q							= (i16)'q',
				LowerCase_r							= (i16)'r',
				LowerCase_s							= (i16)'s',
				LowerCase_t							= (i16)'t',
				LowerCase_u							= (i16)'u',
				LowerCase_v							= (i16)'v',
				LowerCase_w							= (i16)'w',
				LowerCase_x							= (i16)'x',
				LowerCase_y							= (i16)'y',
				LowerCase_z							= (i16)'z',

				LeftCTRL 							= (i16)0x0100,
				RightCTRL 							= (i16)0x0101,
				LeftALT 							= (i16)0x0102,
				RightALT 							= (i16)0x0103,
				LeftShift 							= (i16)0x0104,
				RightShift 							= (i16)0x0105,

				KeyPadDivide						= (i16)0x0106,
				KeyPadMultiply						= (i16)0x0107,
				KeyPadMinus							= (i16)0x0108,
				KeyPadPlus							= (i16)0x0109,
				KeyPadEnter							= (i16)0x010A,
				KeyPad1								= (i16)0x010B,
				KeyPad2								= (i16)0x010C,
				KeyPad3								= (i16)0x010D,
				KeyPad4								= (i16)0x010E,
				KeyPad5								= (i16)0x010F,
				KeyPad6								= (i16)0x0110,
				KeyPad7								= (i16)0x0111,
				KeyPad8								= (i16)0x0112,
				KeyPad9								= (i16)0x0113,
				KeyPad0								= (i16)0x0114,
				KeyPadPeriod						= (i16)0x0115,

				PrintScreen							= (i16)0x0116,
				Insert								= (i16)0x0117,
				Home								= (i16)0x0118,
				PageUp								= (i16)0x0119,
				End									= (i16)0x011A,
				PageDown							= (i16)0x011B,
				RightArrow							= (i16)0x011C,
				LeftArrow							= (i16)0x011D,
				DownArrow							= (i16)0x011E,
				UpArrow								= (i16)0x011F,
				CapsLock							= (i16)0x0120,
				NumLock								= (i16)0x0121,
				ScrollLock							= (i16)0x0122,

				F1									= (i16)0x0123,
				F2									= (i16)0x0124,
				F3									= (i16)0x0125,
				F4									= (i16)0x0126,
				F5									= (i16)0x0127,
				F6									= (i16)0x0128,
				F7									= (i16)0x0129,
				F8									= (i16)0x012A,
				F9									= (i16)0x012B,
				F10									= (i16)0x012C,
				F11									= (i16)0x012D,
				F12									= (i16)0x012E,

				LeftSuper 							= (i16)0x012F,
				RightSuper 							= (i16)0x0130,
			};
			public: struct tModifierBits
			{
				inline static constexpr u8 LeftShift = 0;
				inline static constexpr u8 LeftControl = 1;
				inline static constexpr u8 LeftAlt = 2;
				inline static constexpr u8 LeftSuper = 3;
				inline static constexpr u8 RightShift = 4;
				inline static constexpr u8 RightControl = 5;
				inline static constexpr u8 RightAlt = 6;
				inline static constexpr u8 RightSuper = 7;
				inline static constexpr u8 CapsLock = 8;
				inline static constexpr u8 NumLock = 9;
				inline static constexpr u8 ScrolLLock = 10;
			};
			public: struct tRegisters
			{
				inline static constexpr u16 Modifiers = 0x00;
				inline static constexpr u16 KeyCode = 0x02;
			};

			public:
				inline VirtualKeyboard(void) {  }
				void init(void);
				i8 read8(u16 addr) override;
				i16 read16(u16 addr) override;
				i8 write8(u16 addr, i8 value) override;
				i16 write16(u16 addr, i16 value) override;

				ostd::ByteStream* getByteStream(void) override;

				void handleSignal(ostd::Signal& signal) override;

			private:
				ostd::BitField_16 __construct_modifiers_bitfield(void);
				i8 __write8(u16 addr, i8 value);
				i16 __write16(u16 addr, i16 value);
				i16 __sdl_key_code_convert(i32 keyCode);

			private:
				ostd::ByteStream m_data;
				ostd::BitField_16 m_modifiersBitFiels;
		};
		class VirtualMouse : public IMemoryDevice
		{
			public:
				VirtualMouse(void);
				i8 read8(u16 addr) override;
				i16 read16(u16 addr) override;
				i8 write8(u16 addr, i8 value) override;
				i16 write16(u16 addr, i16 value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
		};
		class VirtualBootloader : public IMemoryDevice
		{
			public:
				VirtualBootloader(void);
				i8 read8(u16 addr) override;
				i16 read16(u16 addr) override;
				i8 write8(u16 addr, i8 value) override;
				i16 write16(u16 addr, i16 value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
				ostd::ByteStream m_mbr;
		};

		namespace interface
		{
			class Disk : public IMemoryDevice
			{
				public: struct tRegisters
				{
					inline static constexpr u16 Signal = 0x00;
					inline static constexpr u16 ModeSelector = 0x01;
					inline static constexpr u16 DiskSelector = 0x02;
					inline static constexpr u16 SectorSelector = 0x03;
					inline static constexpr u16 AddressSelector = 0x05;
					inline static constexpr u16 DataSize = 0x07;
					inline static constexpr u16 DataSourceAddress = 0x09;

					inline static constexpr u16 FirstReadOnly = 0x0B;

					inline static constexpr u16 Status = 0x0B;
					inline static constexpr u16 CurrentDisk = 0x0C;
					inline static constexpr u16 CurrentSector = 0x0D;
					inline static constexpr u16 CurrentAddress = 0x0F;
					inline static constexpr u16 RestDataSize = 0x11;
					inline static constexpr u16 SourceData = 0x13;

				};

				public: struct tSignalValues
				{
					inline static constexpr u8 Start = 0x00;
					inline static constexpr u8 Cancel = 0x01;

					inline static constexpr u8 Ignore = 0xFF;
				};

				public: struct tModeValues
				{
					inline static constexpr u8 Read = 0x00;
					inline static constexpr u8 Write = 0x01;
				};

				public: struct tStatusValues
				{
					inline static constexpr u8 Free = 0x00;
					inline static constexpr u8 Writing = 0x01;
					inline static constexpr u8 Reading = 0x02;
				};

				public:
					Disk(MemoryMapper& memory, VirtualCPU& cpu);
					i8 read8(u16 addr) override;
					i16 read16(u16 addr) override;
					i8 write8(u16 addr, i8 value) override;
					i16 write16(u16 addr, i16 value) override;

					ostd::ByteStream* getByteStream(void) override;

					void cycleStep(void);
					bool connectDisk(VirtualHardDrive& hdd, data::VDiskID disk_id);
					bool disconnectDisk(data::VDiskID diskID);

					inline bool isBusy(void) const { return m_busy; }

				private:
					ostd::serial::SerialIO m_data { data::MemoryMapAddresses::DiskInterface_End - data::MemoryMapAddresses::DiskInterface_Start };
					bool m_busy { false };
					std::unordered_map<data::VDiskID, VirtualHardDrive*> m_connectedDisks;
					MemoryMapper& m_memory;
					VirtualCPU& m_cpu;

					// inline static data::VDiskID s_nextDiskID = 0;

			};
			class Graphics : public IMemoryDevice
			{
				public: struct tText16_Cell
				{
					u8 backgroundColor;
					u8 foregroundColor;
					u8 character;
				};
				public: struct tText16_CellStructure
				{
					inline static constexpr u8 character	=			0x00;
					inline static constexpr u8 foreground	=			0x01;
					inline static constexpr u8 background	=			0x02;
					inline static constexpr u8 reserved	=			0x03;
				};
				public: struct tFlags
				{
					inline static constexpr u8 DoubleBufferingEnabled 	=		0;
					inline static constexpr u8 ScreenRedrawDisabled 	=		1;
				};
				public:
					Graphics(void);
					i8 read8(u16 addr) override;
					i16 read16(u16 addr) override;
					i8 write8(u16 addr, i8 value) override;
					i16 write16(u16 addr, i16 value) override;

					bool readFlag(u8 flg);
					void setFlag(u8 flg, bool val = true);

					ostd::ByteStream* getByteStream(void) override;

					inline u16 getVRAMStart(void) { return m_vramStart; }
					bool readVRAM_16Colors(u8 x, u8 y, tText16_Cell& outTextCell);
					bool writeVRAM_16Colors(u8 x, u8 y, u8 character = 0, u8 background = 0xFF, u8 foreground = 0xFF);
					bool clearVRAM_16Colors(u8 character = 0, u8 background = 0x00, u8 foreground = 0xFF);
					void swapBuffers_16Colors(void);
					void scroll_16Colors(void);

				private:
					ostd::serial::SerialIO m_videoMemory;

					u16 m_vramStart { 0 };
					u8 m_16Color_cellSize { 4 };
					u16 m_16Color_frameSize { 0 };
					u16 m_16Color_secondFrameAddr { 0 };
					u16 m_16Color_currentFrameAddr { 0 };
					bool m_16Color_doubleBufferingEnabled { false };

					ostd::BitField_16 m_tempFlags;
			};
			class SerialPort : public IMemoryDevice
			{
				public:
					SerialPort(void);
					i8 read8(u16 addr) override;
					i16 read16(u16 addr) override;
					i8 write8(u16 addr, i8 value) override;
					i16 write16(u16 addr, i16 value) override;

					ostd::ByteStream* getByteStream(void) override;

				private:
			};
			class CMOS : public IMemoryDevice
			{
				public:
					inline CMOS(void) { m_initialized = false; }
					inline CMOS(const String& cmosFilePath) { init(cmosFilePath); }
					void init(const String& cmosFilePath);
					i8 read8(u16 addr) override;
					i16 read16(u16 addr) override;
					i8 write8(u16 addr, i8 value) override;
					i16 write16(u16 addr, i16 value) override;

					ostd::ByteStream* getByteStream(void) override;

				private:
					ostd::ByteStream m_data;
					u16 m_size { 0 };
					std::fstream m_dataFile;
					bool m_initialized { false };
					u64 m_fileSize { 0 };
			};
		}
	}
}
