#pragma once

#include "IMemoryDevice.hpp"
#include "../tools/GlobalData.hpp"
#include <ostd/Serial.hpp>
#include <fstream>

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
				inline VirtualBIOS(const ostd::String& biosFilePath) { init(biosFilePath); }
				void init(const ostd::String& biosFilePath);
				int8_t read8(uint16_t addr) override;
				int16_t read16(uint16_t addr) override;
				int8_t write8(uint16_t addr, int8_t value) override;
				int16_t write16(uint16_t addr, int16_t value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
				ostd::ByteStream m_bios;
				bool m_initialized { false };
		};
		class InterruptVector : public IMemoryDevice
		{
			public:
				InterruptVector(void);
				int8_t read8(uint16_t addr) override;
				int16_t read16(uint16_t addr) override;
				int8_t write8(uint16_t addr, int8_t value) override;
				int16_t write16(uint16_t addr, int16_t value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
				ostd::ByteStream m_data;
		};
		class VirtualKeyboard : public IMemoryDevice
		{
			public: enum class eKeys
			{
				Delete								= (int16_t)'\x7F',
				Return								= (int16_t)'\r',
				Escape								= (int16_t)'\x1B',
				Backspace							= (int16_t)'\b',
				Tab									= (int16_t)'\t',
				Spacebar							= (int16_t)' ',
				ExclamationMark						= (int16_t)'!',
				DoubleQuote							= (int16_t)'"',
				Hash								= (int16_t)'#',
				Percent								= (int16_t)'%',
				DollarSign							= (int16_t)'$',
				Ampersand							= (int16_t)'&',
				SingleQuote							= (int16_t)'\'',
				LeftParenthesis						= (int16_t)'(',
				RightParenthesis					= (int16_t)')',
				Asterisk							= (int16_t)'*',
				Plus								= (int16_t)'+',
				Comma								= (int16_t)',',
				Minus								= (int16_t)'-',
				Period								= (int16_t)'.',
				ForwardSlash						= (int16_t)'/',
				Num0								= (int16_t)'0',
				Num1								= (int16_t)'1',
				Num2								= (int16_t)'2',
				Num3								= (int16_t)'3',
				Num4								= (int16_t)'4',
				Num5								= (int16_t)'5',
				Num6								= (int16_t)'6',
				Num7								= (int16_t)'7',
				Num8								= (int16_t)'8',
				Num9								= (int16_t)'9',
				Colon								= (int16_t)':',
				Semicolon							= (int16_t)';',
				LessThan							= (int16_t)'<',
				Equals								= (int16_t)'=',
				GreaterThan							= (int16_t)'>',
				QuestionMark						= (int16_t)'?',
				AtSign								= (int16_t)'@',

				UpperCase_A							= (int16_t)'A',
				UpperCase_B							= (int16_t)'B',
				UpperCase_C							= (int16_t)'C',
				UpperCase_D							= (int16_t)'D',
				UpperCase_E							= (int16_t)'E',
				UpperCase_F							= (int16_t)'F',
				UpperCase_G							= (int16_t)'G',
				UpperCase_H							= (int16_t)'H',
				UpperCase_I							= (int16_t)'I',
				UpperCase_J							= (int16_t)'J',
				UpperCase_K							= (int16_t)'K',
				UpperCase_L							= (int16_t)'L',
				UpperCase_M							= (int16_t)'M',
				UpperCase_N							= (int16_t)'N',
				UpperCase_O							= (int16_t)'O',
				UpperCase_P							= (int16_t)'P',
				UpperCase_Q							= (int16_t)'Q',
				UpperCase_R							= (int16_t)'R',
				UpperCase_S							= (int16_t)'S',
				UpperCase_T							= (int16_t)'T',
				UpperCase_U							= (int16_t)'U',
				UpperCase_V							= (int16_t)'V',
				UpperCase_W							= (int16_t)'W',
				UpperCase_X							= (int16_t)'X',
				UpperCase_Y							= (int16_t)'Y',
				UpperCase_Z							= (int16_t)'Z',

				LeftBracket							= (int16_t)'[',
				BackSlash							= (int16_t)'\\',
				RightBracket						= (int16_t)']',
				Caret								= (int16_t)'^',
				Underscore							= (int16_t)'_',
				BackQuote							= (int16_t)'`',
				LowerCase_a							= (int16_t)'a',
				LowerCase_b							= (int16_t)'b',
				LowerCase_c							= (int16_t)'c',
				LowerCase_d							= (int16_t)'d',
				LowerCase_e							= (int16_t)'e',
				LowerCase_f							= (int16_t)'f',
				LowerCase_g							= (int16_t)'g',
				LowerCase_h							= (int16_t)'h',
				LowerCase_i							= (int16_t)'i',
				LowerCase_j							= (int16_t)'j',
				LowerCase_k							= (int16_t)'k',
				LowerCase_l							= (int16_t)'l',
				LowerCase_m							= (int16_t)'m',
				LowerCase_n							= (int16_t)'n',
				LowerCase_o							= (int16_t)'o',
				LowerCase_p							= (int16_t)'p',
				LowerCase_q							= (int16_t)'q',
				LowerCase_r							= (int16_t)'r',
				LowerCase_s							= (int16_t)'s',
				LowerCase_t							= (int16_t)'t',
				LowerCase_u							= (int16_t)'u',
				LowerCase_v							= (int16_t)'v',
				LowerCase_w							= (int16_t)'w',
				LowerCase_x							= (int16_t)'x',
				LowerCase_y							= (int16_t)'y',
				LowerCase_z							= (int16_t)'z',

				LeftCTRL 							= (int16_t)0x0100,
				RightCTRL 							= (int16_t)0x0101,
				LeftALT 							= (int16_t)0x0102,
				RightALT 							= (int16_t)0x0103,
				LeftShift 							= (int16_t)0x0104,
				RightShift 							= (int16_t)0x0105,

				KeyPadDivide						= (int16_t)0x0106,
				KeyPadMultiply						= (int16_t)0x0107,
				KeyPadMinus							= (int16_t)0x0108,
				KeyPadPlus							= (int16_t)0x0109,
				KeyPadEnter							= (int16_t)0x010A,
				KeyPad1								= (int16_t)0x010B,
				KeyPad2								= (int16_t)0x010C,
				KeyPad3								= (int16_t)0x010D,
				KeyPad4								= (int16_t)0x010E,
				KeyPad5								= (int16_t)0x010F,
				KeyPad6								= (int16_t)0x0110,
				KeyPad7								= (int16_t)0x0111,
				KeyPad8								= (int16_t)0x0112,
				KeyPad9								= (int16_t)0x0113,
				KeyPad0								= (int16_t)0x0114,
				KeyPadPeriod						= (int16_t)0x0115,

				PrintScreen							= (int16_t)0x0116,
				Insert								= (int16_t)0x0117,
				Home								= (int16_t)0x0118,
				PageUp								= (int16_t)0x0119,
				End									= (int16_t)0x011A,
				PageDown							= (int16_t)0x011B,
				RightArrow							= (int16_t)0x011C,
				LeftArrow							= (int16_t)0x011D,
				DownArrow							= (int16_t)0x011E,
				UpArrow								= (int16_t)0x011F,
				CapsLock							= (int16_t)0x0120,
				NumLock								= (int16_t)0x0121,
				ScrollLock							= (int16_t)0x0122,

				F1									= (int16_t)0x0123,
				F2									= (int16_t)0x0124,
				F3									= (int16_t)0x0125,
				F4									= (int16_t)0x0126,
				F5									= (int16_t)0x0127,
				F6									= (int16_t)0x0128,
				F7									= (int16_t)0x0129,
				F8									= (int16_t)0x012A,
				F9									= (int16_t)0x012B,
				F10									= (int16_t)0x012C,
				F11									= (int16_t)0x012D,
				F12									= (int16_t)0x012E,

				LeftSuper 							= (int16_t)0x012F,
				RightSuper 							= (int16_t)0x0130,
			};
			public: struct tModifierBits
			{
				inline static constexpr uint8_t LeftShift = 0;
				inline static constexpr uint8_t LeftControl = 1;
				inline static constexpr uint8_t LeftAlt = 2;
				inline static constexpr uint8_t LeftSuper = 3;
				inline static constexpr uint8_t RightShift = 4;
				inline static constexpr uint8_t RightControl = 5;
				inline static constexpr uint8_t RightAlt = 6;
				inline static constexpr uint8_t RightSuper = 7;
				inline static constexpr uint8_t CapsLock = 8;
				inline static constexpr uint8_t NumLock = 9;
				inline static constexpr uint8_t ScrolLLock = 10;
			};
			public: struct tRegisters
			{
				inline static constexpr uint16_t Modifiers = 0x00;
				inline static constexpr uint16_t KeyCode = 0x02;
			};

			public:
				inline VirtualKeyboard(void) {  }
				void init(void);
				int8_t read8(uint16_t addr) override;
				int16_t read16(uint16_t addr) override;
				int8_t write8(uint16_t addr, int8_t value) override;
				int16_t write16(uint16_t addr, int16_t value) override;

				ostd::ByteStream* getByteStream(void) override;

				void handleSignal(ostd::tSignal& signal) override;

			private:
				ostd::BitField_16 __construct_modifiers_bitfield(void);
				int8_t __write8(uint16_t addr, int8_t value);
				int16_t __write16(uint16_t addr, int16_t value);
				int16_t __sdl_key_code_convert(int32_t keyCode);

			private:
				ostd::ByteStream m_data;
				ostd::BitField_16 m_modifiersBitFiels;
		};
		class VirtualMouse : public IMemoryDevice
		{
			public:
				VirtualMouse(void);
				int8_t read8(uint16_t addr) override;
				int16_t read16(uint16_t addr) override;
				int8_t write8(uint16_t addr, int8_t value) override;
				int16_t write16(uint16_t addr, int16_t value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
		};
		class VirtualBootloader : public IMemoryDevice
		{
			public:
				VirtualBootloader(void);
				int8_t read8(uint16_t addr) override;
				int16_t read16(uint16_t addr) override;
				int8_t write8(uint16_t addr, int8_t value) override;
				int16_t write16(uint16_t addr, int16_t value) override;

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
					inline static constexpr uint16_t Signal = 0x00;
					inline static constexpr uint16_t ModeSelector = 0x01;
					inline static constexpr uint16_t DiskSelector = 0x02;
					inline static constexpr uint16_t SectorSelector = 0x03;
					inline static constexpr uint16_t AddressSelector = 0x05;
					inline static constexpr uint16_t DataSize = 0x07;
					inline static constexpr uint16_t DataSourceAddress = 0x09;

					inline static constexpr uint16_t FirstReadOnly = 0x0B;

					inline static constexpr uint16_t Status = 0x0B;
					inline static constexpr uint16_t CurrentDisk = 0x0C;
					inline static constexpr uint16_t CurrentSector = 0x0D;
					inline static constexpr uint16_t CurrentAddress = 0x0F;
					inline static constexpr uint16_t RestDataSize = 0x11;
					inline static constexpr uint16_t SourceData = 0x13;

				};

				public: struct tSignalValues
				{
					inline static constexpr uint8_t Start = 0x00;
					inline static constexpr uint8_t Cancel = 0x01;

					inline static constexpr uint8_t Ignore = 0xFF;
				};

				public: struct tModeValues
				{
					inline static constexpr uint8_t Read = 0x00;
					inline static constexpr uint8_t Write = 0x01;
				};

				public: struct tStatusValues
				{
					inline static constexpr uint8_t Free = 0x00;
					inline static constexpr uint8_t Writing = 0x01;
					inline static constexpr uint8_t Reading = 0x02;
				};

				public:
					Disk(MemoryMapper& memory, VirtualCPU& cpu);
					int8_t read8(uint16_t addr) override;
					int16_t read16(uint16_t addr) override;
					int8_t write8(uint16_t addr, int8_t value) override;
					int16_t write16(uint16_t addr, int16_t value) override;

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
					uint8_t backgroundColor;
					uint8_t foregroundColor;
					uint8_t character;
				};
				public: struct tText16_CellStructure
				{
					inline static constexpr uint8_t character	=			0x00;
					inline static constexpr uint8_t foreground	=			0x01;
					inline static constexpr uint8_t background	=			0x02;
					inline static constexpr uint8_t reserved	=			0x03;
				};
				public:
					Graphics(void);
					int8_t read8(uint16_t addr) override;
					int16_t read16(uint16_t addr) override;
					int8_t write8(uint16_t addr, int8_t value) override;
					int16_t write16(uint16_t addr, int16_t value) override;

					ostd::ByteStream* getByteStream(void) override;

					inline uint16_t getVRAMStart(void) { return m_vramStart; }
					bool readVRAM_16Colors(uint8_t x, uint8_t y, tText16_Cell& outTextCell);
					bool writeVRAM_16Colors(uint8_t x, uint8_t y, uint8_t character = 0, uint8_t background = 0xFF, uint8_t foreground = 0xFF);
					bool clearVRAM_16Colors(uint8_t character = 0, uint8_t background = 0x00, uint8_t foreground = 0xFF);

				private:
					ostd::serial::SerialIO m_videoMemory;

					uint16_t m_vramStart { 0 };
					uint8_t m_16Color_cellSize { 4 };
			};
			class SerialPort : public IMemoryDevice
			{
				public:
					SerialPort(void);
					int8_t read8(uint16_t addr) override;
					int16_t read16(uint16_t addr) override;
					int8_t write8(uint16_t addr, int8_t value) override;
					int16_t write16(uint16_t addr, int16_t value) override;

					ostd::ByteStream* getByteStream(void) override;

				private:
			};
			class CMOS : public IMemoryDevice
			{
				public:
					inline CMOS(void) { m_initialized = false; }
					inline CMOS(const ostd::String& cmosFilePath) { init(cmosFilePath); }
					void init(const ostd::String& cmosFilePath);
					int8_t read8(uint16_t addr) override;
					int16_t read16(uint16_t addr) override;
					int8_t write8(uint16_t addr, int8_t value) override;
					int16_t write16(uint16_t addr, int16_t value) override;

					ostd::ByteStream* getByteStream(void) override;

				private:
					ostd::ByteStream m_data;
					uint16_t m_size { 0 };
					std::fstream m_dataFile;
					bool m_initialized { false };
					uint64_t m_fileSize { 0 };
			};
		}
	}
}