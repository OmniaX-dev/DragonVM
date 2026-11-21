#pragma once

#include "IMemoryDevice.hpp"
#include "../tools/GlobalData.hpp"
#include "../tools/LegacyOstdSerial.hpp"
#include <fstream>
#include <chrono>
#include <ostd/math/Random.hpp>
#include <unordered_map>
#include <random>

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
				i8 read8(u32 addr) override;
				i32 read32(u32 addr) override;
				i8 write8(u32 addr, i8 value) override;
				i32 write32(u32 addr, i32 value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
				ostd::ByteStream m_bios;
				bool m_initialized { false };
		};
		class InterruptVector : public IMemoryDevice
		{
			public:
				InterruptVector(void);
				// Power-on state (no handlers).
				inline void reset(void) { std::fill(m_data.begin(), m_data.end(), 0x00); }
				i8 read8(u32 addr) override;
				i32 read32(u32 addr) override;
				i8 write8(u32 addr, i8 value) override;
				i32 write32(u32 addr, i32 value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
				ostd::ByteStream m_data;
		};
		class VirtualKeyboard : public IMemoryDevice
		{
			public: enum class eKeys
			{
				Delete                                = (i16)'\x7F',
				Return                                = (i16)'\r',
				Escape                                = (i16)'\x1B',
				Backspace                            = (i16)'\b',
				Tab                                    = (i16)'\t',
				Spacebar                            = (i16)' ',
				ExclamationMark                        = (i16)'!',
				DoubleQuote                            = (i16)'"',
				Hash                                = (i16)'#',
				Percent                                = (i16)'%',
				DollarSign                            = (i16)'$',
				Ampersand                            = (i16)'&',
				SingleQuote                            = (i16)'\'',
				LeftParenthesis                        = (i16)'(',
				RightParenthesis                    = (i16)')',
				Asterisk                            = (i16)'*',
				Plus                                = (i16)'+',
				Comma                                = (i16)',',
				Minus                                = (i16)'-',
				Period                                = (i16)'.',
				ForwardSlash                        = (i16)'/',
				Num0                                = (i16)'0',
				Num1                                = (i16)'1',
				Num2                                = (i16)'2',
				Num3                                = (i16)'3',
				Num4                                = (i16)'4',
				Num5                                = (i16)'5',
				Num6                                = (i16)'6',
				Num7                                = (i16)'7',
				Num8                                = (i16)'8',
				Num9                                = (i16)'9',
				Colon                                = (i16)':',
				Semicolon                            = (i16)';',
				LessThan                            = (i16)'<',
				Equals                                = (i16)'=',
				GreaterThan                            = (i16)'>',
				QuestionMark                        = (i16)'?',
				AtSign                                = (i16)'@',

				UpperCase_A                            = (i16)'A',
				UpperCase_B                            = (i16)'B',
				UpperCase_C                            = (i16)'C',
				UpperCase_D                            = (i16)'D',
				UpperCase_E                            = (i16)'E',
				UpperCase_F                            = (i16)'F',
				UpperCase_G                            = (i16)'G',
				UpperCase_H                            = (i16)'H',
				UpperCase_I                            = (i16)'I',
				UpperCase_J                            = (i16)'J',
				UpperCase_K                            = (i16)'K',
				UpperCase_L                            = (i16)'L',
				UpperCase_M                            = (i16)'M',
				UpperCase_N                            = (i16)'N',
				UpperCase_O                            = (i16)'O',
				UpperCase_P                            = (i16)'P',
				UpperCase_Q                            = (i16)'Q',
				UpperCase_R                            = (i16)'R',
				UpperCase_S                            = (i16)'S',
				UpperCase_T                            = (i16)'T',
				UpperCase_U                            = (i16)'U',
				UpperCase_V                            = (i16)'V',
				UpperCase_W                            = (i16)'W',
				UpperCase_X                            = (i16)'X',
				UpperCase_Y                            = (i16)'Y',
				UpperCase_Z                            = (i16)'Z',

				LeftBracket                            = (i16)'[',
				BackSlash                            = (i16)'\\',
				RightBracket                        = (i16)']',
				Caret                                = (i16)'^',
				Underscore                            = (i16)'_',
				BackQuote                            = (i16)'`',
				LowerCase_a                            = (i16)'a',
				LowerCase_b                            = (i16)'b',
				LowerCase_c                            = (i16)'c',
				LowerCase_d                            = (i16)'d',
				LowerCase_e                            = (i16)'e',
				LowerCase_f                            = (i16)'f',
				LowerCase_g                            = (i16)'g',
				LowerCase_h                            = (i16)'h',
				LowerCase_i                            = (i16)'i',
				LowerCase_j                            = (i16)'j',
				LowerCase_k                            = (i16)'k',
				LowerCase_l                            = (i16)'l',
				LowerCase_m                            = (i16)'m',
				LowerCase_n                            = (i16)'n',
				LowerCase_o                            = (i16)'o',
				LowerCase_p                            = (i16)'p',
				LowerCase_q                            = (i16)'q',
				LowerCase_r                            = (i16)'r',
				LowerCase_s                            = (i16)'s',
				LowerCase_t                            = (i16)'t',
				LowerCase_u                            = (i16)'u',
				LowerCase_v                            = (i16)'v',
				LowerCase_w                            = (i16)'w',
				LowerCase_x                            = (i16)'x',
				LowerCase_y                            = (i16)'y',
				LowerCase_z                            = (i16)'z',

				LeftCTRL                             = (i16)0x0100,
				RightCTRL                             = (i16)0x0101,
				LeftALT                             = (i16)0x0102,
				RightALT                             = (i16)0x0103,
				LeftShift                             = (i16)0x0104,
				RightShift                             = (i16)0x0105,

				KeyPadDivide                        = (i16)0x0106,
				KeyPadMultiply                        = (i16)0x0107,
				KeyPadMinus                            = (i16)0x0108,
				KeyPadPlus                            = (i16)0x0109,
				KeyPadEnter                            = (i16)0x010A,
				KeyPad1                                = (i16)0x010B,
				KeyPad2                                = (i16)0x010C,
				KeyPad3                                = (i16)0x010D,
				KeyPad4                                = (i16)0x010E,
				KeyPad5                                = (i16)0x010F,
				KeyPad6                                = (i16)0x0110,
				KeyPad7                                = (i16)0x0111,
				KeyPad8                                = (i16)0x0112,
				KeyPad9                                = (i16)0x0113,
				KeyPad0                                = (i16)0x0114,
				KeyPadPeriod                        = (i16)0x0115,

				PrintScreen                            = (i16)0x0116,
				Insert                                = (i16)0x0117,
				Home                                = (i16)0x0118,
				PageUp                                = (i16)0x0119,
				End                                    = (i16)0x011A,
				PageDown                            = (i16)0x011B,
				RightArrow                            = (i16)0x011C,
				LeftArrow                            = (i16)0x011D,
				DownArrow                            = (i16)0x011E,
				UpArrow                                = (i16)0x011F,
				CapsLock                            = (i16)0x0120,
				NumLock                                = (i16)0x0121,
				ScrollLock                            = (i16)0x0122,

				F1                                    = (i16)0x0123,
				F2                                    = (i16)0x0124,
				F3                                    = (i16)0x0125,
				F4                                    = (i16)0x0126,
				F5                                    = (i16)0x0127,
				F6                                    = (i16)0x0128,
				F7                                    = (i16)0x0129,
				F8                                    = (i16)0x012A,
				F9                                    = (i16)0x012B,
				F10                                    = (i16)0x012C,
				F11                                    = (i16)0x012D,
				F12                                    = (i16)0x012E,

				LeftSuper                             = (i16)0x012F,
				RightSuper                             = (i16)0x0130,
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
			// The last event (Modifiers, KeyCode: what the interrupts read), and a buffer of the
			// events (a ring in the device's memory, so the debugger's history restores it too):
			// every key press, release and text character is kept until it is read, also while
			// interrupts are disabled, for code that polls instead.
			//   0x08 BufferCount: events waiting (read-only)
			//   0x0C BufferPop: reading a word removes the oldest event and gives it (0: none);
			//        bits 0-15 the key code (a text event: the character), 16-26 the modifiers
			//        (tModifierBits), 27-28 the kind (tEventKinds: never 0); positive as an i32
			//   0x10 BufferDropped: events lost because the buffer was full (read-only)
			//   0x14 BufferControl: write 1 (supervisor): the buffer emptied, BufferDropped 0
			//   0x18 BufferHead: where the oldest event is in the ring (read-only)
			//   0x20 the ring: BufferCapacity words
			public: struct tRegisters
			{
				inline static constexpr u32 Modifiers = 0x00;    // 4 bytes
				inline static constexpr u32 KeyCode = 0x04;        // 4 bytes
				inline static constexpr u32 BufferCount = 0x08;
				inline static constexpr u32 BufferPop = 0x0C;
				inline static constexpr u32 BufferDropped = 0x10;
				inline static constexpr u32 BufferControl = 0x14;
				inline static constexpr u32 BufferHead = 0x18;
				inline static constexpr u32 BufferRing = 0x20;
			};
			public: struct tEventKinds
			{
				inline static constexpr u32 Pressed = 1;
				inline static constexpr u32 Released = 2;
				inline static constexpr u32 Text = 3;
			};
			inline static constexpr u32 BufferCapacity = 48;	// (the ring fills the device's memory: 0x20..0xDF)

			public:
				inline VirtualKeyboard(void) {  }
				void init(void);
				// Power-on state of the registers (no key, no modifier); the host's signals stay connected.
				void resetState(void);
				i8 read8(u32 addr) override;
				i32 read32(u32 addr) override;
				i8 write8(u32 addr, i8 value) override;
				i32 write32(u32 addr, i32 value) override;

				ostd::ByteStream* getByteStream(void) override;

				void handleSignal(ostd::Signal& signal) override;
				// When disabled, key events are ignored (e.g. while typing in a debugger's text field).
				inline void enableInput(bool enable = true) { m_inputEnabled = enable; }
				inline bool isInputEnabled(void) const { return m_inputEnabled; }
				// An event into the buffer, as if typed (no interrupt; drun --keys).
				inline void pushEvent(u32 kind, u16 code, u16 modifiers) { __push_event(kind, code, modifiers); }

			private:
				ostd::BitField_16 __construct_modifiers_bitfield(void);
				i8 __write8(u32 addr, i8 value);
				i32 __write32(u32 addr, i32 value);
				i16 __sdl_key_code_convert(i32 keyCode);
				// An event into the buffer (dropped, and counted, when it is full).
				void __push_event(u32 kind, u16 code, u16 modifiers);
				u32 __pop_event(void);
				u32 __reg(u32 addr) const;

			private:
				ostd::ByteStream m_data;
				ostd::BitField_16 m_modifiersBitFiels;
				bool m_inputEnabled { true };
		};
		// The machine's clock: how long it has been running, and how many cycles it has executed,
		// since it started (or was reset). Read-only for the CPU (writes are errors, also in BIOS
		// mode). The owner calls cycleStep() once per CPU cycle.
		//   0x00 Milliseconds (4 bytes): wraps after about 49.7 days; compute intervals as
		//        now - start (unsigned), which stays right across the wrap.
		//   0x04 Cycles (8 bytes, big-endian: high word at 0x04, low word at 0x08).
		// The milliseconds come from the time source: WallClock (the host's real time; right at
		// any clock speed, also uncapped: the runtime) or MachineTime (cycles / clock rate:
		// reproducible, the debugger and drun). With MachineTime, the whole state is the device's
		// memory, so the debugger's history restores it.
		class VirtualTimer : public IMemoryDevice
		{
			public: struct tRegisters
			{
				inline static constexpr u32 Milliseconds = 0x00;	// 4 bytes
				inline static constexpr u32 CyclesHigh = 0x04;		// 4 bytes
				inline static constexpr u32 CyclesLow = 0x08;		// 4 bytes
				inline static constexpr u32 UnixTime = 0x0C;		// 4 bytes: seconds since 1970-01-01 00:00 UTC
				// The Timer interrupt every this many milliseconds (0: off). Writable in supervisor
				// mode only (the only writable register).
				inline static constexpr u32 InterruptInterval = 0x10;	// 4 bytes
				inline static constexpr u32 NextInterrupt = 0x14;	// 4 bytes: the time of the next one (ms)
			};
			public: enum class eTimeSource { WallClock = 0, MachineTime };

			public:
				void init(void);
				// Back to 0 (milliseconds and cycles).
				void reset(void);
				// clockRate: cycles per second (MachineTime). Restarts the wall clock.
				void setTimeSource(eTimeSource source, u32 clockRate);
				inline eTimeSource getTimeSource(void) const { return m_source; }
				void cycleStep(void);
				u64 getCycles(void) const;
				u32 getMilliseconds(void) const;
				// The Unix time at the machine's start (and resets) from now on, instead of the host's
				// clock (UnixTime then runs on from it with the machine's time): reproducible runs.
				void setUnixTime(u32 seconds);
				u32 getUnixTime(void) const;

				i8 read8(u32 addr) override;
				i32 read32(u32 addr) override;
				i8 write8(u32 addr, i8 value) override;
				i32 write32(u32 addr, i32 value) override;
				ostd::ByteStream* getByteStream(void) override;

			private:
				ostd::ByteStream m_data;
				eTimeSource m_source { eTimeSource::WallClock };
				u64 m_clockRate { 1 };
				std::chrono::steady_clock::time_point m_wallStart { std::chrono::steady_clock::now() };
				u32 m_unixStart { 0 };			// UnixTime at the start / reset
				bool m_fixedUnixTime { false };
				u32 m_fixedUnixStart { 0 };
		};

		// Random numbers from the host's generator. Reading Int (a word) gives a new number in
		// 0..INT_MAX, reading Float a new f32 in [0, 1) (its bits); a byte read gives a byte of the
		// last one. Writing a word to Seed seeds the generator: the numbers that follow are then
		// always the same. Seeded from the host clock at start. Reading Entropy gives 32 new bits
		// from the host's entropy source (std::random_device, mixed with the host clock): never
		// affected by Seed, for seeding generators.
		class VirtualRandom : public IMemoryDevice
		{
			public: struct tRegisters
			{
				inline static constexpr u32 Int = 0x00;		// 4 bytes
				inline static constexpr u32 Float = 0x04;	// 4 bytes
				inline static constexpr u32 Seed = 0x08;	// 4 bytes
				inline static constexpr u32 Entropy = 0x0C;	// 4 bytes
			};

			public:
				void init(void);
				void seed(u32 seed);
				// From the host clock.
				void autoSeed(void);
				i32 nextInt(void);
				f32 nextFloat(void);
				u32 nextEntropy(void);

				i8 read8(u32 addr) override;
				i32 read32(u32 addr) override;
				i8 write8(u32 addr, i8 value) override;
				i32 write32(u32 addr, i32 value) override;
				ostd::ByteStream* getByteStream(void) override;

			private:
				ostd::ByteStream m_data;
				ostd::RandomGenerator m_rng { false };
				std::random_device m_entropy;
				u64 m_entropyCounter { 0 };
		};

		// The CPU's control registers (big endian words). Readable by everyone, writable in
		// supervisor mode only (Mode never: it shows the CPU's mode). See VirtualCPU for how the
		// CPU uses them.
		class VirtualCPUControl : public IMemoryDevice
		{
			public: struct tRegisters
			{
				// Traps from user mode (interrupts, int, faults) switch to this stack (0: they stay
				// on the user's stack). The stack check also allows it (KernelStackSize bytes).
				inline static constexpr u32 KernelStackTop = 0x00;
				inline static constexpr u32 KernelStackSize = 0x04;
				// Another stack the stack check allows (0: none): a user program's.
				inline static constexpr u32 UserStackTop = 0x08;
				inline static constexpr u32 UserStackSize = 0x0C;
				// The last fault: its vector, the address of the instruction, and the address it
				// accessed (memory faults; the instruction's otherwise).
				inline static constexpr u32 FaultVector = 0x10;
				inline static constexpr u32 FaultIP = 0x14;
				inline static constexpr u32 FaultAddress = 0x18;
				// Read-only: 1 supervisor mode, 0 user mode.
				inline static constexpr u32 Mode = 0x1C;
				// The permission map's address (0: user mode may access everything): see
				// MemoryMapper::setUserChecks. 16 KiB, all in the RAM.
				inline static constexpr u32 PermissionMap = 0x20;
				// 0x24 - 0x3F: reserved.
			};

			public:
				void init(void);
				void reset(void);
				u32 get(u32 reg) const;
				// Without the supervisor check (the CPU's own writes: the fault registers).
				void set(u32 reg, u32 value);

				i8 read8(u32 addr) override;
				i32 read32(u32 addr) override;
				i8 write8(u32 addr, i8 value) override;
				i32 write32(u32 addr, i32 value) override;
				ostd::ByteStream* getByteStream(void) override;

			private:
				void update_mode(void);
				bool check_write(u32 addr, const char* what);

			private:
				ostd::ByteStream m_data;
		};

		class VirtualMouse : public IMemoryDevice
		{
			public:
				VirtualMouse(void);
				i8 read8(u32 addr) override;
				i32 read32(u32 addr) override;
				i8 write8(u32 addr, i8 value) override;
				i32 write32(u32 addr, i32 value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
		};

		namespace interface
		{
			class Disk : public IMemoryDevice
			{
				// The controller's registers. An operation copies DataSize bytes between the
				// disk (from DiskAddress) and the memory (from MemoryAddress), one block (at most
				// getBlockSize() bytes, machine config: disk_block_size) per cycle.
				public: struct tRegisters
				{
					inline static constexpr u32 Signal = 0x00;                // 1 byte
					inline static constexpr u32 ModeSelector = 0x01;        // 1 byte
					inline static constexpr u32 DiskSelector = 0x02;        // 1 byte
					inline static constexpr u32 DiskAddress = 0x04;            // 4 bytes
					inline static constexpr u32 DataSize = 0x08;            // 4 bytes
					inline static constexpr u32 MemoryAddress = 0x0C;        // 4 bytes

					inline static constexpr u32 FirstReadOnly = 0x10;

					inline static constexpr u32 Status = 0x10;                // 1 byte
					inline static constexpr u32 CurrentDisk = 0x11;            // 1 byte
					inline static constexpr u32 CurrentDiskAddress = 0x14;    // 4 bytes
					inline static constexpr u32 RestDataSize = 0x18;        // 4 bytes
					inline static constexpr u32 CurrentMemoryAddress = 0x1C;    // 4 bytes

					inline static constexpr u32 BlockSize = 0x50;    // 4 bytes
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
					i8 read8(u32 addr) override;
					i32 read32(u32 addr) override;
					i8 write8(u32 addr, i8 value) override;
					i32 write32(u32 addr, i32 value) override;

					ostd::ByteStream* getByteStream(void) override;

					void cycleStep(void);
					bool connectDisk(VirtualHardDrive& hdd, data::VDiskID disk_id);
					bool disconnectDisk(data::VDiskID diskID);

					inline bool isBusy(void) const { return m_busy; }
					// The controller's state beside its registers (for the debugger).
					inline void setBusy(bool busy) { m_busy = busy; }
					inline u32 getBlockSize(void) const { return m_blockSize; }
					inline void setBlockSize(u32 size) { m_blockSize = std::max<u32>(size, 1); }
					void writeBlockSize(void);
					// Power-on state: idle, registers cleared (the block size written again); the
					// disks stay connected.
					void reset(void);

				private:
					ostd::serial::SerialIO m_data { data::MemoryMapAddresses::DiskInterface_End - data::MemoryMapAddresses::DiskInterface_Start + 1 };
					bool m_busy { false };
					std::unordered_map<data::VDiskID, VirtualHardDrive*> m_connectedDisks;
					MemoryMapper& m_memory;
					VirtualCPU& m_cpu;
					u32 m_blockSize { data::DefaultValues::DefaultDiskBlockSize };
					stdvec<u8> m_block;    // the block being copied

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
					inline static constexpr u8 character    =            0x00;
					inline static constexpr u8 foreground    =            0x01;
					inline static constexpr u8 background    =            0x02;
					inline static constexpr u8 reserved    =            0x03;
				};
				public: struct tFlags
				{
					inline static constexpr u8 DoubleBufferingEnabled     =        0;
					inline static constexpr u8 ScreenRedrawDisabled     =        1;
				};
				// The card's state beside its memory (for the debugger).
				public: struct tState
				{
					u32 currentFrameAddr;
					u32 secondFrameAddr;
				};
				public:
					Graphics(void);
					i8 read8(u32 addr) override;
					i32 read32(u32 addr) override;
					i8 write8(u32 addr, i8 value) override;
					i32 write32(u32 addr, i32 value) override;

					bool readFlag(u8 flg);
					void setFlag(u8 flg, bool val = true);

					ostd::ByteStream* getByteStream(void) override;

					inline u32 getVRAMStart(void) { return m_vramStart; }
					// Power-on state: registers and video memory cleared, the frame buffers back in place.
					void reset(void);
					bool readVRAM_16Colors(u8 x, u8 y, tText16_Cell& outTextCell);
					bool writeVRAM_16Colors(u8 x, u8 y, u8 character = 0, u8 background = 0xFF, u8 foreground = 0xFF);
					bool writeVRAMFrame_16Colors(u32 newFrameAddr, u32 size, u32 yOffset);
					bool clearVRAM_16Colors(u8 character = 0, u8 background = 0x00, u8 foreground = 0xFF, u32 softwareBufferAddr = 0);
					void swapBuffers_16Colors(void);
					void scroll_16Colors(void);

					inline void saveState(tState& outState) const { outState = { m_16Color_currentFrameAddr, m_16Color_secondFrameAddr }; }
					inline void loadState(const tState& state) { m_16Color_currentFrameAddr = state.currentFrameAddr; m_16Color_secondFrameAddr = state.secondFrameAddr; }

				private:
					// The video memory at addr..addr + size - 1 (nullptr if out of range).
					u8* vram_span(u32 addr, u32 size);

					ostd::serial::SerialIO m_videoMemory;

					u32 m_vramStart { 0 };
					u8 m_16Color_cellSize { 4 };
					u32 m_16Color_frameSize { 0 };
					u32 m_16Color_secondFrameAddr { 0 };
					u32 m_16Color_currentFrameAddr { 0 };
					bool m_16Color_doubleBufferingEnabled { false };

					ostd::BitField_32 m_tempFlags;
			};
			class SerialPort : public IMemoryDevice
			{
				public:
					SerialPort(void);
					i8 read8(u32 addr) override;
					i32 read32(u32 addr) override;
					i8 write8(u32 addr, i8 value) override;
					i32 write32(u32 addr, i32 value) override;

					ostd::ByteStream* getByteStream(void) override;

				private:
			};
			class CMOS : public IMemoryDevice
			{
				public:
					inline CMOS(void) { m_initialized = false; }
					inline CMOS(const String& cmosFilePath) { init(cmosFilePath); }
					void init(const String& cmosFilePath);
					i8 read8(u32 addr) override;
					i32 read32(u32 addr) override;
					i8 write8(u32 addr, i8 value) override;
					i32 write32(u32 addr, i32 value) override;

					ostd::ByteStream* getByteStream(void) override;
					// Writes the whole chip back to its file.
					void syncByteStream(void) override;

				private:
					ostd::ByteStream m_data;
					u32 m_size { 0 };
					std::fstream m_dataFile;
					bool m_initialized { false };
					u64 m_fileSize { 0 };
			};
		}
	}
}
