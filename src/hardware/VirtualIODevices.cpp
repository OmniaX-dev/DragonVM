#include "VirtualIODevices.hpp"

#include "VirtualHardDrive.hpp"
#include "MemoryMapper.hpp"
#include "VirtualCPU.hpp"

#include "../runtime/DragonRuntime.hpp"
#include <ostd/io/Memory.hpp>

namespace dragon
{
	namespace hw
	{
		// Byte / word access to a plain ByteStream, pushing `errorCode` when out of bounds.
		static i8 stream_read8(ostd::ByteStream& data, u32 addr, u64 errorCode, const char* device)
		{
			if (addr >= data.size())
			{
				data::ErrorHandler::pushError(errorCode, String("Invalid Byte ").add(device).add(" location at address: ").add(String::getHexStr(addr, true, 4)));
				return 0;
			}
			return data[addr];
		}

		static i32 stream_read32(ostd::ByteStream& data, u32 addr, u64 errorCode, const char* device)
		{
			i32 value = 0;
			if (!bytes::read32(data, addr, value))
				data::ErrorHandler::pushError(errorCode, String("Invalid Word ").add(device).add(" location at address: ").add(String::getHexStr(addr, true, 4)));
			return value;
		}

		static i8 stream_write8(ostd::ByteStream& data, u32 addr, i8 value, u64 errorCode, const char* device)
		{
			if (addr >= data.size())
			{
				data::ErrorHandler::pushError(errorCode, String("Invalid Byte ").add(device).add(" location at address: ").add(String::getHexStr(addr, true, 4)));
				return 0;
			}
			data[addr] = value;
			return value;
		}

		static i32 stream_write32(ostd::ByteStream& data, u32 addr, i32 value, u64 errorCode, const char* device)
		{
			if (!bytes::write32(data, addr, value))
			{
				data::ErrorHandler::pushError(errorCode, String("Invalid Word ").add(device).add(" location at address: ").add(String::getHexStr(addr, true, 4)));
				return 0;
			}
			return value;
		}




		void VirtualBIOS::init(const String& biosFilePath)
		{
			bool loaded = ostd::Memory::loadByteStreamFromFile(biosFilePath, m_bios);
			if (!loaded)
				data::ErrorHandler::pushError(data::ErrorCodes::BIOS_FailedToLoad, "Failed to load BIOS data.");
			const u32 biosSize = data::MemoryMapAddresses::BIOS_End - data::MemoryMapAddresses::BIOS_Start + 1;
			if (m_bios.size() != biosSize)
				data::ErrorHandler::pushError(data::ErrorCodes::BIOS_InvalidSize, String("Invalid BIOS size: ").add(String::getHexStr(m_bios.size(), true, 4)).add(" (must be ").add(biosSize).add(" bytes)"));
			m_initialized = true;
		}

		i8 VirtualBIOS::read8(u32 addr)
		{
			return stream_read8(m_bios, addr, data::ErrorCodes::BIOS_InvalidAddress, "BIOS");
		}

		i32 VirtualBIOS::read32(u32 addr)
		{
			return stream_read32(m_bios, addr, data::ErrorCodes::BIOS_InvalidAddress, "BIOS");
		}

		i8 VirtualBIOS::write8(u32 addr, i8 value)
		{
			data::ErrorHandler::pushError(data::ErrorCodes::BIOS_WriteAttempt, String("Attempting to write to BIOS memory map: ").add(String::getHexStr(addr, true, 4)));
			return 0x00;
		}

		i32 VirtualBIOS::write32(u32 addr, i32 value)
		{
			data::ErrorHandler::pushError(data::ErrorCodes::BIOS_WriteAttempt, String("Attempting to write to BIOS memory map: ").add(String::getHexStr(addr, true, 4)));
			return 0x00000000;
		}

		ostd::ByteStream* VirtualBIOS::getByteStream(void)
		{
			return &m_bios;
		}





		InterruptVector::InterruptVector(void)
		{
			const u32 dataSize = data::MemoryMapAddresses::IntVector_End - data::MemoryMapAddresses::IntVector_Start + 1;
			m_data.assign(dataSize, 0x00);
		}

		i8 InterruptVector::read8(u32 addr)
		{
			return stream_read8(m_data, addr, data::ErrorCodes::IntVector_InvalidAddress, "IntVector");
		}

		i32 InterruptVector::read32(u32 addr)
		{
			return stream_read32(m_data, addr, data::ErrorCodes::IntVector_InvalidAddress, "IntVector");
		}

		i8 InterruptVector::write8(u32 addr, i8 value)
		{
			return stream_write8(m_data, addr, value, data::ErrorCodes::IntVector_InvalidAddress, "IntVector");
		}

		i32 InterruptVector::write32(u32 addr, i32 value)
		{
			return stream_write32(m_data, addr, value, data::ErrorCodes::IntVector_InvalidAddress, "IntVector");
		}

		ostd::ByteStream* InterruptVector::getByteStream(void)
		{
			return &m_data;
		}





		void VirtualKeyboard::init(void)
		{
			const u32 dataSize = data::MemoryMapAddresses::Keyboard_End - data::MemoryMapAddresses::Keyboard_Start + 1;
			m_data.assign(dataSize, 0x00);
			enableSignals();
			validate();
			connectSignal(ostd::BuiltinSignals::KeyPressed);
			connectSignal(ostd::BuiltinSignals::KeyReleased);
			connectSignal(ostd::BuiltinSignals::TextEntered);
		}

		i8 VirtualKeyboard::read8(u32 addr)
		{
			return stream_read8(m_data, addr, data::ErrorCodes::IntVector_InvalidAddress, "KeyboardController");
		}

		i32 VirtualKeyboard::read32(u32 addr)
		{
			return stream_read32(m_data, addr, data::ErrorCodes::IntVector_InvalidAddress, "KeyboardController");
		}

		i8 VirtualKeyboard::write8(u32 addr, i8 value)
		{
			if (!DragonRuntime::cpu.isInBIOSMOde())
			{
				data::ErrorHandler::pushError(data::ErrorCodes::AccessViolation_BiosModeRequired, String("Attempting to write byte to KeyboardController while not in BIOS mode. Address: ").add(String::getHexStr(addr, true, 4)));
				return 0;
			}
			return stream_write8(m_data, addr, value, data::ErrorCodes::IntVector_InvalidAddress, "KeyboardController");
		}

		i32 VirtualKeyboard::write32(u32 addr, i32 value)
		{
			if (!DragonRuntime::cpu.isInBIOSMOde())
			{
				data::ErrorHandler::pushError(data::ErrorCodes::AccessViolation_BiosModeRequired, String("Attempting to write word to KeyboardController while not in BIOS mode. Address: ").add(String::getHexStr(addr, true, 4)));
				return 0;
			}
			return stream_write32(m_data, addr, value, data::ErrorCodes::IntVector_InvalidAddress, "KeyboardController");
		}

		ostd::ByteStream* VirtualKeyboard::getByteStream(void)
		{
			return &m_data;
		}

		void VirtualKeyboard::handleSignal(ostd::Signal& signal)
		{
			if (!m_inputEnabled)
				return;
			auto& cpu = DragonRuntime::cpu;
			if (signal.ID == ostd::BuiltinSignals::KeyPressed || signal.ID == ostd::BuiltinSignals::KeyReleased)
			{
				if (!signal.userData)
					return;
				ogfx::KeyEventData& ked = (ogfx::KeyEventData&)*signal.userData;
				m_modifiersBitFiels = __construct_modifiers_bitfield();
				__write32(tRegisters::Modifiers, cast<i32>(cast<u16>(m_modifiersBitFiels.value)));
				__write32(tRegisters::KeyCode, cast<i32>(cast<u16>(__sdl_key_code_convert(ked.keyCode))));
				if (ked.eventType == ogfx::KeyEventData::eKeyEvent::Pressed)
					cpu.handleInterrupt(data::InterruptCodes::KeyPressed, true);
				else if (ked.eventType == ogfx::KeyEventData::eKeyEvent::Released)
					cpu.handleInterrupt(data::InterruptCodes::KeyReleased, true);
			}
			else if (signal.ID == ostd::BuiltinSignals::TextEntered)
			{
				if (!signal.userData)
					return;
				ogfx::KeyEventData& ked = (ogfx::KeyEventData&)*signal.userData;
				m_modifiersBitFiels = __construct_modifiers_bitfield();
				__write32(tRegisters::Modifiers, cast<i32>(cast<u16>(m_modifiersBitFiels.value)));
				__write32(tRegisters::KeyCode, cast<i32>(cast<u8>(ked.text[0])));
				cpu.handleInterrupt(data::InterruptCodes::TextEntered, true);
			}
		}

		ostd::BitField_16 VirtualKeyboard::__construct_modifiers_bitfield(void)
		{
			ostd::BitField_16 bitfield;
			bitfield.value = 0;
			auto mod_state = SDL_GetModState();
			ostd::Bits::val(bitfield, tModifierBits::LeftShift, (mod_state & SDL_KMOD_LSHIFT));
			ostd::Bits::val(bitfield, tModifierBits::LeftControl, (mod_state & SDL_KMOD_LCTRL));
			ostd::Bits::val(bitfield, tModifierBits::LeftAlt, (mod_state & SDL_KMOD_LALT));
			ostd::Bits::val(bitfield, tModifierBits::LeftSuper, (mod_state & SDL_KMOD_LGUI));
			ostd::Bits::val(bitfield, tModifierBits::RightShift, (mod_state & SDL_KMOD_RSHIFT));
			ostd::Bits::val(bitfield, tModifierBits::RightControl, (mod_state & SDL_KMOD_RCTRL));
			ostd::Bits::val(bitfield, tModifierBits::RightAlt, (mod_state & SDL_KMOD_RALT));
			ostd::Bits::val(bitfield, tModifierBits::RightSuper, (mod_state & SDL_KMOD_RGUI));
			ostd::Bits::val(bitfield, tModifierBits::CapsLock, (mod_state & SDL_KMOD_CAPS));
			ostd::Bits::val(bitfield, tModifierBits::NumLock, (mod_state & SDL_KMOD_NUM));
			ostd::Bits::val(bitfield, tModifierBits::ScrolLLock, (mod_state & SDL_KMOD_SCROLL));
			return bitfield;
		}

		i8 VirtualKeyboard::__write8(u32 addr, i8 value)
		{
			return stream_write8(m_data, addr, value, data::ErrorCodes::IntVector_InvalidAddress, "KeyboardController");
		}

		i32 VirtualKeyboard::__write32(u32 addr, i32 value)
		{
			return stream_write32(m_data, addr, value, data::ErrorCodes::IntVector_InvalidAddress, "KeyboardController");
		}

		i16 VirtualKeyboard::__sdl_key_code_convert(i32 keyCode)
		{
			switch (keyCode)
			{
				case SDLK_LCTRL: return (i16)eKeys::LeftCTRL;
				case SDLK_LSHIFT: return (i16)eKeys::LeftShift;
				case SDLK_LALT: return (i16)eKeys::LeftALT;
				case SDLK_LGUI: return (i16)eKeys::LeftSuper;
				case SDLK_RCTRL: return (i16)eKeys::RightCTRL;
				case SDLK_RSHIFT: return (i16)eKeys::RightShift;
				case SDLK_RGUI: return (i16)eKeys::RightSuper;
				case SDLK_RALT: return (i16)eKeys::RightALT;
				case SDLK_KP_DIVIDE: return (i16)eKeys::KeyPadDivide;
				case SDLK_KP_MULTIPLY: return (i16)eKeys::KeyPadMultiply;
				case SDLK_KP_MINUS: return (i16)eKeys::KeyPadMinus;
				case SDLK_KP_PLUS: return (i16)eKeys::KeyPadPlus;
				case SDLK_KP_ENTER: return (i16)eKeys::KeyPadEnter;
				case SDLK_KP_1: return (i16)eKeys::KeyPad1;
				case SDLK_KP_2: return (i16)eKeys::KeyPad2;
				case SDLK_KP_3: return (i16)eKeys::KeyPad3;
				case SDLK_KP_4: return (i16)eKeys::KeyPad4;
				case SDLK_KP_5: return (i16)eKeys::KeyPad5;
				case SDLK_KP_6: return (i16)eKeys::KeyPad6;
				case SDLK_KP_7: return (i16)eKeys::KeyPad7;
				case SDLK_KP_8: return (i16)eKeys::KeyPad8;
				case SDLK_KP_9: return (i16)eKeys::KeyPad9;
				case SDLK_KP_0: return (i16)eKeys::KeyPad0;
				case SDLK_KP_PERIOD: return (i16)eKeys::KeyPadPeriod;
				case SDLK_PRINTSCREEN: return (i16)eKeys::PrintScreen;
				case SDLK_INSERT: return (i16)eKeys::Insert;
				case SDLK_HOME: return (i16)eKeys::Home;
				case SDLK_PAGEUP: return (i16)eKeys::PageUp;
				case SDLK_END: return (i16)eKeys::End;
				case SDLK_PAGEDOWN: return (i16)eKeys::PageDown;
				case SDLK_RIGHT: return (i16)eKeys::RightArrow;
				case SDLK_LEFT: return (i16)eKeys::LeftArrow;
				case SDLK_DOWN: return (i16)eKeys::DownArrow;
				case SDLK_UP: return (i16)eKeys::UpArrow;
				case SDLK_CAPSLOCK: return (i16)eKeys::CapsLock;
				case SDLK_NUMLOCKCLEAR: return (i16)eKeys::NumLock;
				case SDLK_SCROLLLOCK: return (i16)eKeys::ScrollLock;
				case SDLK_F1: return (i16)eKeys::F1;
				case SDLK_F2: return (i16)eKeys::F2;
				case SDLK_F3: return (i16)eKeys::F3;
				case SDLK_F4: return (i16)eKeys::F4;
				case SDLK_F5: return (i16)eKeys::F5;
				case SDLK_F6: return (i16)eKeys::F6;
				case SDLK_F7: return (i16)eKeys::F7;
				case SDLK_F8: return (i16)eKeys::F8;
				case SDLK_F9: return (i16)eKeys::F9;
				case SDLK_F10: return (i16)eKeys::F10;
				case SDLK_F11: return (i16)eKeys::F11;
				case SDLK_F12: return (i16)eKeys::F12;
				case SDLK_DELETE: return (i16)eKeys::Delete;
				case SDLK_RETURN: return (i16)eKeys::Return;
				case SDLK_ESCAPE: return (i16)eKeys::Escape;
				case SDLK_BACKSPACE: return (i16)eKeys::Backspace;
				case SDLK_TAB: return (i16)eKeys::Tab;
				case SDLK_SPACE: return (i16)eKeys::Spacebar;
				case SDLK_EXCLAIM: return (i16)eKeys::ExclamationMark;
				case SDLK_DBLAPOSTROPHE: return (i16)eKeys::DoubleQuote;
				case SDLK_HASH: return (i16)eKeys::Hash;
				case SDLK_PERCENT: return (i16)eKeys::Percent;
				case SDLK_DOLLAR: return (i16)eKeys::DollarSign;
				case SDLK_AMPERSAND: return (i16)eKeys::Ampersand;
				case SDLK_APOSTROPHE: return (i16)eKeys::SingleQuote;
				case SDLK_LEFTPAREN: return (i16)eKeys::LeftParenthesis;
				case SDLK_RIGHTPAREN: return (i16)eKeys::RightParenthesis;
				case SDLK_ASTERISK: return (i16)eKeys::Asterisk;
				case SDLK_PLUS: return (i16)eKeys::Plus;
				case SDLK_COMMA: return (i16)eKeys::Comma;
				case SDLK_MINUS: return (i16)eKeys::Minus;
				case SDLK_PERIOD: return (i16)eKeys::Period;
				case SDLK_SLASH: return (i16)eKeys::ForwardSlash;
				case SDLK_0: return (i16)eKeys::Num0;
				case SDLK_1: return (i16)eKeys::Num1;
				case SDLK_2: return (i16)eKeys::Num2;
				case SDLK_3: return (i16)eKeys::Num3;
				case SDLK_4: return (i16)eKeys::Num4;
				case SDLK_5: return (i16)eKeys::Num5;
				case SDLK_6: return (i16)eKeys::Num6;
				case SDLK_7: return (i16)eKeys::Num7;
				case SDLK_8: return (i16)eKeys::Num8;
				case SDLK_9: return (i16)eKeys::Num9;
				case SDLK_COLON: return (i16)eKeys::Colon;
				case SDLK_SEMICOLON: return (i16)eKeys::Semicolon;
				case SDLK_LESS: return (i16)eKeys::LessThan;
				case SDLK_EQUALS: return (i16)eKeys::Equals;
				case SDLK_GREATER: return (i16)eKeys::GreaterThan;
				case SDLK_QUESTION: return (i16)eKeys::QuestionMark;
				case SDLK_AT: return (i16)eKeys::AtSign;
				case SDLK_LEFTBRACKET: return (i16)eKeys::LeftBracket;
				case SDLK_BACKSLASH: return (i16)eKeys::BackSlash;
				case SDLK_RIGHTBRACKET: return (i16)eKeys::RightBracket;
				case SDLK_CARET: return (i16)eKeys::Caret;
				case SDLK_UNDERSCORE: return (i16)eKeys::Underscore;
				case SDLK_GRAVE: return (i16)eKeys::BackQuote;
				case SDLK_A: return (i16)eKeys::LowerCase_a;
				case SDLK_B: return (i16)eKeys::LowerCase_b;
				case SDLK_C: return (i16)eKeys::LowerCase_c;
				case SDLK_D: return (i16)eKeys::LowerCase_d;
				case SDLK_E: return (i16)eKeys::LowerCase_e;
				case SDLK_F: return (i16)eKeys::LowerCase_f;
				case SDLK_G: return (i16)eKeys::LowerCase_g;
				case SDLK_H: return (i16)eKeys::LowerCase_h;
				case SDLK_I: return (i16)eKeys::LowerCase_i;
				case SDLK_J: return (i16)eKeys::LowerCase_j;
				case SDLK_K: return (i16)eKeys::LowerCase_k;
				case SDLK_L: return (i16)eKeys::LowerCase_l;
				case SDLK_M: return (i16)eKeys::LowerCase_m;
				case SDLK_N: return (i16)eKeys::LowerCase_n;
				case SDLK_O: return (i16)eKeys::LowerCase_o;
				case SDLK_P: return (i16)eKeys::LowerCase_p;
				case SDLK_Q: return (i16)eKeys::LowerCase_q;
				case SDLK_R: return (i16)eKeys::LowerCase_r;
				case SDLK_S: return (i16)eKeys::LowerCase_s;
				case SDLK_T: return (i16)eKeys::LowerCase_t;
				case SDLK_U: return (i16)eKeys::LowerCase_u;
				case SDLK_V: return (i16)eKeys::LowerCase_v;
				case SDLK_W: return (i16)eKeys::LowerCase_w;
				case SDLK_X: return (i16)eKeys::LowerCase_x;
				case SDLK_Y: return (i16)eKeys::LowerCase_y;
				case SDLK_Z: return (i16)eKeys::LowerCase_z;
				default: return (i16)eKeys::UpperCase_A;
			}
			return (i16)eKeys::UpperCase_A;
		}






		VirtualMouse::VirtualMouse(void)
		{
		}

		i8 VirtualMouse::read8(u32 addr)
		{
			return 0x00;
		}

		i32 VirtualMouse::read32(u32 addr)
		{
			return 0x00000000;
		}

		i8 VirtualMouse::write8(u32 addr, i8 value)
		{
			return 0x00;
		}

		i32 VirtualMouse::write32(u32 addr, i32 value)
		{
			return 0x00000000;
		}

		ostd::ByteStream* VirtualMouse::getByteStream(void)
		{
			return nullptr;
		}









		namespace interface
		{
			Disk::Disk(MemoryMapper& memory, VirtualCPU& cpu) : m_memory(memory), m_cpu(cpu)
			{
				m_data.w_Byte(tRegisters::Signal, tSignalValues::Ignore);
			}

			i8 Disk::read8(u32 addr)
			{
				i8 value = 0;
				if (!m_data.r_Byte(addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerReadFailed, "Failed to read byte from HardDrive Controller");
					return 0;
				}
				return value;
			}

			i32 Disk::read32(u32 addr)
			{
				i32 value = 0;
				if (!m_data.r_DWord(addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerReadFailed, "Failed to read word from HardDrive Controller");
					return 0;
				}
				return value;
			}

			i8 Disk::write8(u32 addr, i8 value)
			{
				if (addr >= tRegisters::FirstReadOnly)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerWriteFailed, "Attempt to write byte to ReadOnly part of HardDrive Controller");
					return 0;
				}
				if (!m_data.w_Byte(addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerWriteFailed, "Failed to write byte to HardDrive Controller");
					return 0;
				}
				return value;
			}

			i32 Disk::write32(u32 addr, i32 value)
			{
				if (addr + 3 >= tRegisters::FirstReadOnly)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerWriteFailed, "Attempt to write word to ReadOnly part of HardDrive Controller");
					return 0;
				}
				if (!m_data.w_DWord(addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerWriteFailed, "Failed to write word to HardDrive Controller");
					return 0;
				}
				return value;
			}

			ostd::ByteStream* Disk::getByteStream(void)
			{
				return &m_data.getData();
			}

			void Disk::cycleStep(void)
			{
				u8 signal = tSignalValues::Ignore;
				m_data.r_Byte(tRegisters::Signal, (i8&)signal);
				if (m_busy)
				{
					if (signal == tSignalValues::Cancel)
					{
						m_data.w_Byte(tRegisters::Status, tStatusValues::Free);
						m_data.w_Byte(tRegisters::Signal, tSignalValues::Ignore);
						m_busy = false;
						return;
					}
					if (signal != tSignalValues::Ignore)
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_InvalidConfiguration, "Invalid HardDrive configuration: <signal> register must be set to <ignore> while busy.");
						m_busy = false;
						return;
					}
					u8 status = 0;
					m_data.r_Byte(tRegisters::Status, (i8&)status);
					if (status == tStatusValues::Free)
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_InvalidConfiguration, "Invalid HardDrive configuration: <status> register set to <free> while busy.");
						m_busy = false;
						return;
					}
					u8 currentDisk = 0;
					u32 diskAddress = 0, restDataSize = 0, memoryAddress = 0;
					m_data.r_Byte(tRegisters::CurrentDisk, (i8&)currentDisk);
					m_data.r_DWord(tRegisters::CurrentDiskAddress, (i32&)diskAddress);
					m_data.r_DWord(tRegisters::RestDataSize, (i32&)restDataSize);
					m_data.r_DWord(tRegisters::CurrentMemoryAddress, (i32&)memoryAddress);
					if (m_connectedDisks.count((data::VDiskID)currentDisk) == 0)
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_InvalidDiskSelected, "Invalid HardDrive configuration: selected Disk not found.");
						m_busy = false;
						return;
					}
					auto& disk = *m_connectedDisks[currentDisk];
					if (diskAddress >= disk.getSize())
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_EndOfDisk, "HardDrive Error: Reached end of selected Disk.");
						m_busy = false;
						return;
					}
					if (memoryAddress > data::AddressMask)
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_MemoryOverflow, "HardDrive Error: Reached end of Memory.");
						m_busy = false;
						return;
					}
					// One block per cycle: at most m_blockSize bytes, and never past the end of the
					// disk or of the memory (the next cycle then stops with the error, as it would
					// one byte at a time).
					u32 count = std::min(m_blockSize, restDataSize);
					count = cast<u32>(std::min<u64>(count, disk.getSize() - diskAddress));
					count = cast<u32>(std::min<u64>(count, cast<u64>(data::AddressMask) - memoryAddress + 1));
					m_block.resize(count);
					if (status == tStatusValues::Reading)
					{
						if (!disk.readBlock(diskAddress, count, m_block.data()))
						{
							data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ReadFailed, "HardDrive Error: Failed to read data.");
							m_busy = false;
							return;
						}
						for (u32 i = 0; i < count; i++)
							m_memory.write8(memoryAddress + i, cast<i8>(m_block[i]));
					}
					else if (status == tStatusValues::Writing)
					{
						for (u32 i = 0; i < count; i++)
							m_block[i] = cast<u8>(m_memory.read8(memoryAddress + i));
						if (!disk.writeBlock(diskAddress, count, m_block.data()))
						{
							data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_WriteFailed, "HardDrive Error: Failed to write data.");
							m_busy = false;
							return;
						}
					}
					memoryAddress += count;
					diskAddress += count;
					restDataSize -= count;
					m_data.w_DWord(tRegisters::CurrentDiskAddress, cast<i32>(diskAddress));
					m_data.w_DWord(tRegisters::RestDataSize, cast<i32>(restDataSize));
					m_data.w_DWord(tRegisters::CurrentMemoryAddress, cast<i32>(memoryAddress));
					if (restDataSize == 0)
					{
						m_data.w_Byte(tRegisters::Status, tStatusValues::Free);
						m_data.w_Byte(tRegisters::Signal, tSignalValues::Ignore);
						m_busy = false;
						m_cpu.handleInterrupt(data::InterruptCodes::DiskInterfaceFFinished, true);
					}
					return;
				}
				if (signal != tSignalValues::Start) return;
				u8 mode = 0, disk = 0;
				u32 diskAddress = 0, size = 0, memoryAddress = 0;
				m_data.r_Byte(tRegisters::ModeSelector, (i8&)mode);
				m_data.r_Byte(tRegisters::DiskSelector, (i8&)disk);
				m_data.r_DWord(tRegisters::DiskAddress, (i32&)diskAddress);
				m_data.r_DWord(tRegisters::DataSize, (i32&)size);
				m_data.r_DWord(tRegisters::MemoryAddress, (i32&)memoryAddress);
				m_data.w_Byte(tRegisters::Signal, tSignalValues::Ignore);
				if (mode == tModeValues::Read)
					m_data.w_Byte(tRegisters::Status, tStatusValues::Reading);
				else if (mode == tModeValues::Write)
					m_data.w_Byte(tRegisters::Status, tStatusValues::Writing);
				else
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_InvalidConfiguration, "Invalid HardDrive configuration: <mode> must be set to <read> or <write> befor starting operations.");
					m_busy = false;
					return;
				}
				m_data.w_Byte(tRegisters::CurrentDisk, disk);
				m_data.w_DWord(tRegisters::CurrentDiskAddress, cast<i32>(diskAddress));
				m_data.w_DWord(tRegisters::RestDataSize, cast<i32>(size));
				m_data.w_DWord(tRegisters::CurrentMemoryAddress, cast<i32>(memoryAddress));
				if (size == 0)
				{
					// Nothing to copy: done right away.
					m_data.w_Byte(tRegisters::Status, tStatusValues::Free);
					m_cpu.handleInterrupt(data::InterruptCodes::DiskInterfaceFFinished, true);
					return;
				}
				m_busy = true;
			}

			bool Disk::connectDisk(VirtualHardDrive& hdd, data::VDiskID disk_id)
			{
				for (auto& disk : m_connectedDisks)
				{
					if (disk.second->isSame(hdd))
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_DiskAlreadyConnected, "Attempt to connect already connected Disk to Controller");
						return false;
					}
				}
				m_connectedDisks[disk_id] = &hdd;
				return true;
			}

			bool Disk::disconnectDisk(data::VDiskID diskID)
			{
				if (m_connectedDisks.count(diskID) == 0)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_DisconnectInvalid, "Attempt to disconnect invalid Disk from Controller");
					return false;
				}
				m_connectedDisks.erase(diskID);
				return true;
			}

			void Disk::writeBlockSize(void)
			{
				m_data.w_DWord(tRegisters::BlockSize, m_blockSize);
			}





			Graphics::Graphics(void)
			{
				m_videoMemory.init(0x10000);
				m_vramStart = data::MemoryMapAddresses::VideoCardInterface_End - data::MemoryMapAddresses::VideoCardInterface_Start + 1;
				m_16Color_frameSize = VirtualDisplay::Columns * VirtualDisplay::Rows * m_16Color_cellSize;
				m_16Color_currentFrameAddr = m_vramStart;
				m_16Color_secondFrameAddr = m_vramStart + m_16Color_frameSize;
			}

			i8 Graphics::read8(u32 addr)
			{
				i8 outVal = 0;
				if (!m_videoMemory.r_Byte(addr, outVal))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::Graphics_MemoryReadFailed, "Failed to read byte from Graphics Memory");
					return 0;
				}
				return outVal;
			}

			i32 Graphics::read32(u32 addr)
			{
				i32 outVal = 0;
				if (!m_videoMemory.r_DWord(addr, outVal))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::Graphics_MemoryReadFailed, "Failed to read word from Graphics Memory");
					return 0;
				}
				return outVal;
			}

			i8 Graphics::write8(u32 addr, i8 value)
			{
				if (!m_videoMemory.w_Byte(addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::Graphics_MemoryWriteFailed, "Failed to write byte to Graphics Memory");
					return 0;
				}
				return value;
			}

			i32 Graphics::write32(u32 addr, i32 value)
			{
				if (!m_videoMemory.w_DWord(addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::Graphics_MemoryWriteFailed, "Failed to write word to Graphics Memory");
					return 0;
				}
				return value;
			}

			bool Graphics::readFlag(u8 flg)
			{
				if (flg >= 32) return false;
				i32 outValue = 0;
				if (!m_videoMemory.r_DWord(VirtualDisplay::tRegisters::Flags, outValue))
					return false; //TODO: Error
				m_tempFlags.value = cast<u32>(outValue);
				return ostd::Bits::get(m_tempFlags, flg);
			}

			void Graphics::setFlag(u8 flg, bool val)
			{
				if (flg >= 32) return;
				i32 outValue = 0;
				if (!m_videoMemory.r_DWord(VirtualDisplay::tRegisters::Flags, outValue))
					return; //TODO: Error
				m_tempFlags.value = cast<u32>(outValue);
				ostd::Bits::val(m_tempFlags, flg, val);
				if (!m_videoMemory.w_DWord(VirtualDisplay::tRegisters::Flags, cast<i32>(m_tempFlags.value)))
					return; //TODO: Error
			}

			ostd::ByteStream* Graphics::getByteStream(void)
			{
				return &m_videoMemory.getData();
			}

			bool Graphics::readVRAM_16Colors(u8 x, u8 y, Graphics::tText16_Cell& outTextCell)
			{
				u32 cellOffset = static_cast<u32>(CONVERT_2D_1D(x, y, VirtualDisplay::Columns)) * 4;
				cellOffset += m_16Color_currentFrameAddr;
				i8 outVal = 0;
				if (!m_videoMemory.r_Byte(cellOffset + tText16_CellStructure::character, outVal))
					return false; //TODO: Error
				outTextCell.character = outVal;
				if (!m_videoMemory.r_Byte(cellOffset + tText16_CellStructure::background, outVal))
					return false; //TODO: Error
				outTextCell.backgroundColor = outVal;
				if (!m_videoMemory.r_Byte(cellOffset + tText16_CellStructure::foreground, outVal))
					return false; //TODO: Error
				outTextCell.foregroundColor = outVal;
				return true;
			}

			bool Graphics::writeVRAM_16Colors(u8 x, u8 y, u8 character, u8 background, u8 foreground)
			{
				u32 cellOffset = static_cast<u32>(CONVERT_2D_1D(x, y, VirtualDisplay::Columns)) * 4;
				if (!readFlag(tFlags::DoubleBufferingEnabled))
					cellOffset += m_16Color_currentFrameAddr;
				else
					cellOffset += m_16Color_secondFrameAddr;
				if (!m_videoMemory.w_Byte(cellOffset + tText16_CellStructure::character, character))
					return false; //TODO: Error
				if (!m_videoMemory.w_Byte(cellOffset + tText16_CellStructure::background, background))
					return false; //TODO: Error
				if (!m_videoMemory.w_Byte(cellOffset + tText16_CellStructure::foreground, foreground))
					return false; //TODO: Error
				return true;
			}

			bool Graphics::clearVRAM_16Colors(u8 character, u8 background, u8 foreground)
			{
				for (i32 i = m_16Color_currentFrameAddr; i < m_16Color_currentFrameAddr + m_16Color_frameSize; i += 4)
				{
					m_videoMemory.w_Byte(i + tText16_CellStructure::character, character);
					m_videoMemory.w_Byte(i + tText16_CellStructure::background, background);
					m_videoMemory.w_Byte(i + tText16_CellStructure::foreground, foreground);
				}
				return true;
			}

			void Graphics::swapBuffers_16Colors(void)
			{
				if (!readFlag(tFlags::DoubleBufferingEnabled))
					return;
				for (i32 i = m_16Color_currentFrameAddr, j = 0; i < m_16Color_currentFrameAddr + m_16Color_frameSize; i += 4, j += 4)
				{
					i8 outByte = 0;
					m_videoMemory.r_Byte(m_16Color_secondFrameAddr + j, outByte);
					m_videoMemory.w_Byte(i + tText16_CellStructure::character, outByte);
					m_videoMemory.r_Byte(m_16Color_secondFrameAddr + j + 1, outByte);
					m_videoMemory.w_Byte(i + tText16_CellStructure::background, outByte);
					m_videoMemory.r_Byte(m_16Color_secondFrameAddr + j + 2, outByte);
					m_videoMemory.w_Byte(i + tText16_CellStructure::foreground, outByte);
				}
				u32 tmp = m_16Color_currentFrameAddr;
				m_16Color_currentFrameAddr = m_16Color_secondFrameAddr;
				m_16Color_secondFrameAddr = tmp;
			}

			void Graphics::scroll_16Colors(void)
			{
				i32 line_len = (VirtualDisplay::Columns * 4);
				for (i32 i = m_16Color_currentFrameAddr + line_len; i < m_16Color_currentFrameAddr + m_16Color_frameSize; i += 4)
				{
					i32 k = i;
					i8 outByte = 0;
					m_videoMemory.r_Byte(k, outByte);
					m_videoMemory.w_Byte(k - line_len, outByte);
					k++;
					m_videoMemory.r_Byte(k, outByte);
					m_videoMemory.w_Byte(k - line_len, outByte);
					k++;
					m_videoMemory.r_Byte(k, outByte);
					m_videoMemory.w_Byte(k - line_len, outByte);
				}
				for (i32 i = m_16Color_currentFrameAddr + m_16Color_frameSize - line_len; i < m_16Color_currentFrameAddr + m_16Color_frameSize; i += 4)
				{
					m_videoMemory.w_Byte(i, 0x20);
				}
			}





			SerialPort::SerialPort(void)
			{
			}

			i8 SerialPort::read8(u32 addr)
			{
				return 0x00;
			}

			i32 SerialPort::read32(u32 addr)
			{
				return 0x0000;
			}

			i8 SerialPort::write8(u32 addr, i8 value)
			{
				return 0x00;
			}

			i32 SerialPort::write32(u32 addr, i32 value)
			{
				return 0x0000;
			}

			ostd::ByteStream* SerialPort::getByteStream(void)
			{
				return nullptr;
			}





			void CMOS::init(const String& cmosFilePath)
			{
				m_size = data::MemoryMapAddresses::CMOS_End - data::MemoryMapAddresses::CMOS_Start + 1;
				m_dataFile.open(cmosFilePath.cpp_str(), std::ios::out | std::ios::in | std::ios::binary);
				if(!m_dataFile)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_UnableToMount, "Unable to mount virtual CMOS chip.");
					return;
				}
				m_fileSize = m_dataFile.tellg();
				m_dataFile.seekg( 0, std::ios::end );
				m_fileSize = (i64)m_dataFile.tellg() - m_fileSize;
				m_dataFile.seekg( 0, std::ios::beg );
				if (m_fileSize != m_size)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidSize, String("Invalid virtual CMOS chhip size: ").add(m_fileSize));
					return;
				}
				// The chip is kept in memory too (written through to the file): reads don't touch
				// the file, and the debugger can see (and restore) its content.
				m_data.assign(m_size, 0);
				m_dataFile.read((char*)m_data.data(), m_size);
				m_dataFile.clear();
				m_initialized = true;
			}

			i8 CMOS::read8(u32 addr)
			{
				if (!m_initialized)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_Uninitialized, "Attempt to read uninitialized CMOS chip.");
					return 0;
				}
				if (addr >= m_size)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidAddress, String("Invalid Byte CMOS location at address: ").add(String::getHexStr(addr, true, 4)));
					return 0;
				}
				return m_data[addr];
			}

			i32 CMOS::read32(u32 addr)
			{
				if (!m_initialized)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_Uninitialized, "Attempt to read uninitialized CMOS chip.");
					return 0;
				}
				i32 value = 0;
				if (!bytes::read32(m_data, addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidAddress, String("Invalid Word CMOS location at address: ").add(String::getHexStr(addr, true, 4)));
					return 0;
				}
				return value;
			}

			i8 CMOS::write8(u32 addr, i8 value)
			{
				if (!m_initialized)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_Uninitialized, "Attempt to read uninitialized CMOS chip.");
					return 0;
				}
				if (addr >= m_size)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidAddress, String("Invalid Byte CMOS location at address: ").add(String::getHexStr(addr, true, 4)));
					return 0;
				}
				if (!DragonRuntime::cpu.isInBIOSMOde())
				{
					data::ErrorHandler::pushError(data::ErrorCodes::AccessViolation_BiosModeRequired, String("Attempting to write byte to CMOS while not in BIOS mode. Address: ").add(String::getHexStr(addr, true, 4)));
					return 0;
				}
				m_data[addr] = value;
				m_dataFile.seekp(addr);
				m_dataFile.write((char*)(&value), sizeof(value));
				m_dataFile.flush();
				return value;
			}

			void CMOS::syncByteStream(void)
			{
				if (!m_initialized)
					return;
				m_dataFile.seekp(0);
				m_dataFile.write((char*)m_data.data(), m_data.size());
				m_dataFile.flush();
			}

			i32 CMOS::write32(u32 addr, i32 value)
			{
				if (!m_initialized)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_Uninitialized, "Attempt to read uninitialized CMOS chip.");
					return 0;
				}
				if (addr + 4 > m_size)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidAddress, String("Invalid Word CMOS location at address: ").add(String::getHexStr(addr, true, 4)));
					return 0;
				}
				if (!DragonRuntime::cpu.isInBIOSMOde())
				{
					data::ErrorHandler::pushError(data::ErrorCodes::AccessViolation_BiosModeRequired, String("Attempting to write word to CMOS while not in BIOS mode. Address: ").add(String::getHexStr(addr, true, 4)));
					return 0;
				}
				const u32 v = cast<u32>(value);
				write8(addr, cast<i8>(v >> 24));
				write8(addr + 1, cast<i8>(v >> 16));
				write8(addr + 2, cast<i8>(v >> 8));
				write8(addr + 3, cast<i8>(v));
				return value;
			}

			ostd::ByteStream* CMOS::getByteStream(void)
			{
				return &m_data;
			}
		}
	}
}
