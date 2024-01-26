#include "VirtualIODevices.hpp"
#include <ostd/Utils.hpp>

#include "VirtualHardDrive.hpp"
#include "MemoryMapper.hpp"
#include "VirtualCPU.hpp"
#include "VirtualRAM.hpp"

#include "../runtime/DragonRuntime.hpp"

//TODO: Fix all access functions (reads and writes) ensuring the address is not out of bounds.
//		Right now the check is done, but just to push an error if out of bounds; the address 
//		gets still used even in that case, which is really dumb and will probably crash the
//		runtime most of the time.

namespace dragon
{
	namespace hw
	{
		void VirtualBIOS::init(const ostd::String& biosFilePath)
		{
			bool loaded = ostd::Utils::loadByteStreamFromFile(biosFilePath, m_bios);
			if (!loaded)
				data::ErrorHandler::pushError(data::ErrorCodes::BIOS_FailedToLoad, "Failed to load BIOS data.");
			if (m_bios.size() != 4096) //TODO: Hardcoded
				data::ErrorHandler::pushError(data::ErrorCodes::BIOS_InvalidSize, ostd::String("Invalid BIOS size: ").add(ostd::Utils::getHexStr(m_bios.size(), true, 2)));
			m_initialized = true;
		}

		int8_t VirtualBIOS::read8(uint16_t addr)
		{
			if (addr >= m_bios.size())
				data::ErrorHandler::pushError(data::ErrorCodes::BIOS_InvalidAddress, ostd::String("Invalid Byte BIOS location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
			return m_bios[addr];
		}

		int16_t VirtualBIOS::read16(uint16_t addr)
		{
			if (addr >= m_bios.size() - 1)
				data::ErrorHandler::pushError(data::ErrorCodes::BIOS_InvalidAddress, ostd::String("Invalid Word BIOS location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
			return ((m_bios[addr + 0] <<  8) & 0xFF00U)
				 | ( m_bios[addr + 1]        & 0x00FFU);
		}

		int8_t VirtualBIOS::write8(uint16_t addr, int8_t value)
		{
			data::ErrorHandler::pushError(data::ErrorCodes::BIOS_WriteAttempt, "Attempting to write to BIOS memory map.");
			return 0x00;
		}

		int16_t VirtualBIOS::write16(uint16_t addr, int16_t value)
		{
			data::ErrorHandler::pushError(data::ErrorCodes::BIOS_WriteAttempt, "Attempting to write to BIOS memory map.");
			return 0x0000;
		}

		ostd::ByteStream* VirtualBIOS::getByteStream(void)
		{
			return &m_bios;
		}




		
		InterruptVector::InterruptVector(void)
		{
			uint32_t dataSize = data::MemoryMapAddresses::IntVector_End - data::MemoryMapAddresses::IntVector_Start;
			for (int32_t i = 0; i < dataSize; i++)
				m_data.push_back(0x00);
		}

		int8_t InterruptVector::read8(uint16_t addr)
		{
			if (addr >= m_data.size())
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::String("Invalid Byte IntVector location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
			return m_data[addr];
		}

		int16_t InterruptVector::read16(uint16_t addr)
		{
			if (addr >= m_data.size() - 1)
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::String("Invalid Word IntVector location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
			return ((m_data[addr + 0] <<  8) & 0xFF00U)
				 | ( m_data[addr + 1]        & 0x00FFU);
		}

		int8_t InterruptVector::write8(uint16_t addr, int8_t value)
		{
			if (addr >= m_data.size())
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::String("Invalid Word IntVector location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
			m_data[addr] = value;
			return value;
		}

		int16_t InterruptVector::write16(uint16_t addr, int16_t value)
		{
			if (addr >= m_data.size() - 1)
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::String("Invalid Word IntVector location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
			m_data[addr + 0] = (value >> 8) & 0xFF;
			m_data[addr + 1] = value & 0xFF;
			return value;
		}

		ostd::ByteStream* InterruptVector::getByteStream(void)
		{
			return &m_data;
		}




		
		void VirtualKeyboard::init(void)
		{
			uint32_t dataSize = data::MemoryMapAddresses::Keyboard_End - data::MemoryMapAddresses::Keyboard_Start;
			for (int32_t i = 0; i < dataSize; i++)
				m_data.push_back(0x00);
			enableSignals();
			validate();
			connectSignal(ostd::tBuiltinSignals::KeyPressed);
			connectSignal(ostd::tBuiltinSignals::KeyReleased);
			connectSignal(ostd::tBuiltinSignals::TextEntered);
		}

		int8_t VirtualKeyboard::read8(uint16_t addr)
		{
			if (addr >= m_data.size())
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::String("Invalid Byte KeyboardController location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
			return m_data[addr];
		}

		int16_t VirtualKeyboard::read16(uint16_t addr)
		{
			if (addr >= m_data.size() - 1)
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::String("Invalid Word KeyboardController location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
			return ((m_data[addr + 0] <<  8) & 0xFF00U)
				 | ( m_data[addr + 1]        & 0x00FFU);
		}

		int8_t VirtualKeyboard::write8(uint16_t addr, int8_t value)
		{
			if (addr >= m_data.size())
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::String("Invalid Word KeyboardController location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
			if (!DragonRuntime::cpu.isInBIOSMOde())
			{
				data::ErrorHandler::pushError(data::ErrorCodes::AccessViolation_BiosModeRequired, ostd::String("Attempting to write byte to KeyboardController while not in BIOS mode. Address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
				return 0;
			}
			return __write8(addr, value);
		}

		int16_t VirtualKeyboard::write16(uint16_t addr, int16_t value)
		{
			if (addr >= m_data.size() - 1)
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::String("Invalid Word KeyboardController location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
			if (!DragonRuntime::cpu.isInBIOSMOde())
			{
				data::ErrorHandler::pushError(data::ErrorCodes::AccessViolation_BiosModeRequired, ostd::String("Attempting to write word to KeyboardController while not in BIOS mode. Address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
				return 0;
			}
			return __write16(addr, value);
		}

		ostd::ByteStream* VirtualKeyboard::getByteStream(void)
		{
			return &m_data;
		}

		void VirtualKeyboard::handleSignal(ostd::tSignal& signal)
		{
			auto& cpu = DragonRuntime::cpu;
			const auto& state = SDL_GetKeyboardState(nullptr);
			if (signal.ID == ostd::tBuiltinSignals::KeyPressed || signal.ID == ostd::tBuiltinSignals::KeyReleased)
			{
				KeyEventData& ked = (KeyEventData&)signal.userData;
				m_modifiersBitFiels = __construct_modifiers_bitfield();
				__write16(tRegisters::Modifiers, (int16_t)m_modifiersBitFiels.value);
				__write16(tRegisters::KeyCode, __sdl_key_code_convert(ked.keyCode));
				if (ked.eventType == KeyEventData::eKeyEvent::Pressed)
					cpu.handleInterrupt(data::InterruptCodes::KeyPressed, true);
				else if (ked.eventType == KeyEventData::eKeyEvent::Pressed)
					cpu.handleInterrupt(data::InterruptCodes::KeyReleased, true);
			}
			else if (signal.ID == ostd::tBuiltinSignals::TextEntered)
			{
				KeyEventData& ked = (KeyEventData&)signal.userData;
				m_modifiersBitFiels = __construct_modifiers_bitfield();
				__write16(tRegisters::Modifiers, (int16_t)m_modifiersBitFiels.value);
				__write16(tRegisters::KeyCode, (int16_t)ked.text);
				cpu.handleInterrupt(data::InterruptCodes::TextEntered, true);
			}
		}

		ostd::BitField_16 VirtualKeyboard::__construct_modifiers_bitfield(void)
		{
			ostd::BitField_16 bitfield;
			auto mod_state = SDL_GetModState();
			ostd::Bits::val(bitfield, tModifierBits::LeftShift, (mod_state & KMOD_LSHIFT));
			ostd::Bits::val(bitfield, tModifierBits::LeftControl, (mod_state & KMOD_LCTRL));
			ostd::Bits::val(bitfield, tModifierBits::LeftAlt, (mod_state & KMOD_LALT));
			ostd::Bits::val(bitfield, tModifierBits::LeftSuper, (mod_state & KMOD_LGUI));
			ostd::Bits::val(bitfield, tModifierBits::RightShift, (mod_state & KMOD_RSHIFT));
			ostd::Bits::val(bitfield, tModifierBits::RightControl, (mod_state & KMOD_RCTRL));
			ostd::Bits::val(bitfield, tModifierBits::RightAlt, (mod_state & KMOD_RALT));
			ostd::Bits::val(bitfield, tModifierBits::RightSuper, (mod_state & KMOD_RGUI));
			ostd::Bits::val(bitfield, tModifierBits::CapsLock, (mod_state & KMOD_CAPS));
			ostd::Bits::val(bitfield, tModifierBits::NumLock, (mod_state & KMOD_NUM));
			ostd::Bits::val(bitfield, tModifierBits::ScrolLLock, (mod_state & KMOD_SCROLL));
			return bitfield;
		}

		int8_t VirtualKeyboard::__write8(uint16_t addr, int8_t value)
		{
			m_data[addr] = value;
			return value;
		}

		int16_t VirtualKeyboard::__write16(uint16_t addr, int16_t value)
		{
			m_data[addr + 0] = (value >> 8) & 0xFF;
			m_data[addr + 1] = value & 0xFF;
			return value;
		}
		
		int16_t VirtualKeyboard::__sdl_key_code_convert(int32_t keyCode)
		{
			switch (keyCode)
			{
				case SDLK_LCTRL: return (int16_t)eKeys::LeftCTRL;
				case SDLK_LSHIFT: return (int16_t)eKeys::LeftShift;
				case SDLK_LALT: return (int16_t)eKeys::LeftALT;
				case SDLK_LGUI: return (int16_t)eKeys::LeftSuper;
				case SDLK_RCTRL: return (int16_t)eKeys::RightCTRL;
				case SDLK_RSHIFT: return (int16_t)eKeys::RightShift;
				case SDLK_RGUI: return (int16_t)eKeys::RightSuper;
				case SDLK_RALT: return (int16_t)eKeys::RightALT;
				case SDLK_KP_DIVIDE: return (int16_t)eKeys::KeyPadDivide;
				case SDLK_KP_MULTIPLY: return (int16_t)eKeys::KeyPadMultiply;
				case SDLK_KP_MINUS: return (int16_t)eKeys::KeyPadMinus;
				case SDLK_KP_PLUS: return (int16_t)eKeys::KeyPadPlus;
				case SDLK_KP_ENTER: return (int16_t)eKeys::KeyPadEnter;
				case SDLK_KP_1: return (int16_t)eKeys::KeyPad1;
				case SDLK_KP_2: return (int16_t)eKeys::KeyPad2;
				case SDLK_KP_3: return (int16_t)eKeys::KeyPad3;
				case SDLK_KP_4: return (int16_t)eKeys::KeyPad4;
				case SDLK_KP_5: return (int16_t)eKeys::KeyPad5;
				case SDLK_KP_6: return (int16_t)eKeys::KeyPad6;
				case SDLK_KP_7: return (int16_t)eKeys::KeyPad7;
				case SDLK_KP_8: return (int16_t)eKeys::KeyPad8;
				case SDLK_KP_9: return (int16_t)eKeys::KeyPad9;
				case SDLK_KP_0: return (int16_t)eKeys::KeyPad0;
				case SDLK_KP_PERIOD: return (int16_t)eKeys::KeyPadPeriod;
				case SDLK_PRINTSCREEN: return (int16_t)eKeys::PrintScreen;
				case SDLK_INSERT: return (int16_t)eKeys::Insert;
				case SDLK_HOME: return (int16_t)eKeys::Home;
				case SDLK_PAGEUP: return (int16_t)eKeys::PageUp;
				case SDLK_END: return (int16_t)eKeys::End;
				case SDLK_PAGEDOWN: return (int16_t)eKeys::PageDown;
				case SDLK_RIGHT: return (int16_t)eKeys::RightArrow;
				case SDLK_LEFT: return (int16_t)eKeys::LeftArrow;
				case SDLK_DOWN: return (int16_t)eKeys::DownArrow;
				case SDLK_UP: return (int16_t)eKeys::UpArrow;
				case SDLK_CAPSLOCK: return (int16_t)eKeys::CapsLock;
				case SDLK_NUMLOCKCLEAR: return (int16_t)eKeys::NumLock;
				case SDLK_SCROLLLOCK: return (int16_t)eKeys::ScrollLock;
				case SDLK_F1: return (int16_t)eKeys::F1;
				case SDLK_F2: return (int16_t)eKeys::F2;
				case SDLK_F3: return (int16_t)eKeys::F3;
				case SDLK_F4: return (int16_t)eKeys::F4;
				case SDLK_F5: return (int16_t)eKeys::F5;
				case SDLK_F6: return (int16_t)eKeys::F6;
				case SDLK_F7: return (int16_t)eKeys::F7;
				case SDLK_F8: return (int16_t)eKeys::F8;
				case SDLK_F9: return (int16_t)eKeys::F9;
				case SDLK_F10: return (int16_t)eKeys::F10;
				case SDLK_F11: return (int16_t)eKeys::F11;
				case SDLK_F12: return (int16_t)eKeys::F12;
				case SDLK_DELETE: return (int16_t)eKeys::Delete;
				case SDLK_RETURN: return (int16_t)eKeys::Return;
				case SDLK_ESCAPE: return (int16_t)eKeys::Escape;
				case SDLK_BACKSPACE: return (int16_t)eKeys::Backspace;
				case SDLK_TAB: return (int16_t)eKeys::Tab;
				case SDLK_SPACE: return (int16_t)eKeys::Spacebar;
				case SDLK_EXCLAIM: return (int16_t)eKeys::ExclamationMark;
				case SDLK_QUOTEDBL: return (int16_t)eKeys::DoubleQuote;
				case SDLK_HASH: return (int16_t)eKeys::Hash;
				case SDLK_PERCENT: return (int16_t)eKeys::Percent;
				case SDLK_DOLLAR: return (int16_t)eKeys::DollarSign;
				case SDLK_AMPERSAND: return (int16_t)eKeys::Ampersand;
				case SDLK_QUOTE: return (int16_t)eKeys::SingleQuote;
				case SDLK_LEFTPAREN: return (int16_t)eKeys::LeftParenthesis;
				case SDLK_RIGHTPAREN: return (int16_t)eKeys::RightParenthesis;
				case SDLK_ASTERISK: return (int16_t)eKeys::Asterisk;
				case SDLK_PLUS: return (int16_t)eKeys::Plus;
				case SDLK_COMMA: return (int16_t)eKeys::Comma;
				case SDLK_MINUS: return (int16_t)eKeys::Minus;
				case SDLK_PERIOD: return (int16_t)eKeys::Period;
				case SDLK_SLASH: return (int16_t)eKeys::ForwardSlash;
				case SDLK_0: return (int16_t)eKeys::Num0;
				case SDLK_1: return (int16_t)eKeys::Num1;
				case SDLK_2: return (int16_t)eKeys::Num2;
				case SDLK_3: return (int16_t)eKeys::Num3;
				case SDLK_4: return (int16_t)eKeys::Num4;
				case SDLK_5: return (int16_t)eKeys::Num5;
				case SDLK_6: return (int16_t)eKeys::Num6;
				case SDLK_7: return (int16_t)eKeys::Num7;
				case SDLK_8: return (int16_t)eKeys::Num8;
				case SDLK_9: return (int16_t)eKeys::Num9;
				case SDLK_COLON: return (int16_t)eKeys::Colon;
				case SDLK_SEMICOLON: return (int16_t)eKeys::Semicolon;
				case SDLK_LESS: return (int16_t)eKeys::LessThan;
				case SDLK_EQUALS: return (int16_t)eKeys::Equals;
				case SDLK_GREATER: return (int16_t)eKeys::GreaterThan;
				case SDLK_QUESTION: return (int16_t)eKeys::QuestionMark;
				case SDLK_AT: return (int16_t)eKeys::AtSign;
				case SDLK_LEFTBRACKET: return (int16_t)eKeys::LeftBracket;
				case SDLK_BACKSLASH: return (int16_t)eKeys::BackSlash;
				case SDLK_RIGHTBRACKET: return (int16_t)eKeys::RightBracket;
				case SDLK_CARET: return (int16_t)eKeys::Caret;
				case SDLK_UNDERSCORE: return (int16_t)eKeys::Underscore;
				case SDLK_BACKQUOTE: return (int16_t)eKeys::BackQuote;
				case SDLK_a: return (int16_t)eKeys::LowerCase_a;
				case SDLK_b: return (int16_t)eKeys::LowerCase_b;
				case SDLK_c: return (int16_t)eKeys::LowerCase_c;
				case SDLK_d: return (int16_t)eKeys::LowerCase_d;
				case SDLK_e: return (int16_t)eKeys::LowerCase_e;
				case SDLK_f: return (int16_t)eKeys::LowerCase_f;
				case SDLK_g: return (int16_t)eKeys::LowerCase_g;
				case SDLK_h: return (int16_t)eKeys::LowerCase_h;
				case SDLK_i: return (int16_t)eKeys::LowerCase_i;
				case SDLK_j: return (int16_t)eKeys::LowerCase_j;
				case SDLK_k: return (int16_t)eKeys::LowerCase_k;
				case SDLK_l: return (int16_t)eKeys::LowerCase_l;
				case SDLK_m: return (int16_t)eKeys::LowerCase_m;
				case SDLK_n: return (int16_t)eKeys::LowerCase_n;
				case SDLK_o: return (int16_t)eKeys::LowerCase_o;
				case SDLK_p: return (int16_t)eKeys::LowerCase_p;
				case SDLK_q: return (int16_t)eKeys::LowerCase_q;
				case SDLK_r: return (int16_t)eKeys::LowerCase_r;
				case SDLK_s: return (int16_t)eKeys::LowerCase_s;
				case SDLK_t: return (int16_t)eKeys::LowerCase_t;
				case SDLK_u: return (int16_t)eKeys::LowerCase_u;
				case SDLK_v: return (int16_t)eKeys::LowerCase_v;
				case SDLK_w: return (int16_t)eKeys::LowerCase_w;
				case SDLK_x: return (int16_t)eKeys::LowerCase_x;
				case SDLK_y: return (int16_t)eKeys::LowerCase_y;
				case SDLK_z: return (int16_t)eKeys::LowerCase_z;
				default: return (int16_t)eKeys::UpperCase_A;
			}
			return (int16_t)eKeys::UpperCase_A;
		}




		

		VirtualMouse::VirtualMouse(void)
		{
		}

		int8_t VirtualMouse::read8(uint16_t addr)
		{
			return 0x00;
		}

		int16_t VirtualMouse::read16(uint16_t addr)
		{
			return 0x0000;
		}

		int8_t VirtualMouse::write8(uint16_t addr, int8_t value)
		{
			return 0x00;
		}

		int16_t VirtualMouse::write16(uint16_t addr, int16_t value)
		{
			return 0x0000;
		}

		ostd::ByteStream* VirtualMouse::getByteStream(void)
		{
			return nullptr;
		}




		
		VirtualBootloader::VirtualBootloader(void)
		{
			for (int32_t i = 0; i < 512; i++)
				m_mbr.push_back(0);
		}

		int8_t VirtualBootloader::read8(uint16_t addr)
		{
			if (addr >= m_mbr.size())
				data::ErrorHandler::pushError(data::ErrorCodes::BIOS_InvalidAddress, ostd::String("Invalid Byte MBR location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
			return m_mbr[addr];
		}

		int16_t VirtualBootloader::read16(uint16_t addr)
		{
			if (addr >= m_mbr.size() - 1)
				data::ErrorHandler::pushError(data::ErrorCodes::BIOS_InvalidAddress, ostd::String("Invalid Word MBR location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
			return ((m_mbr[addr + 0] <<  8) & 0xFF00U)
				 | ( m_mbr[addr + 1]        & 0x00FFU);
		}

		int8_t VirtualBootloader::write8(uint16_t addr, int8_t value)
		{
			if (addr >= m_mbr.size())
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::String("Invalid Word IntVector location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
			m_mbr[addr] = value;
			return value;
		}

		int16_t VirtualBootloader::write16(uint16_t addr, int16_t value)
		{
			if (addr >= m_mbr.size() - 1)
				data::ErrorHandler::pushError(data::ErrorCodes::IntVector_InvalidAddress, ostd::String("Invalid Word IntVector location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
			m_mbr[addr + 0] = (value >> 8) & 0xFF;
			m_mbr[addr + 1] = value & 0xFF;
			return value;
		}

		ostd::ByteStream* VirtualBootloader::getByteStream(void)
		{
			return &m_mbr;
		}




		namespace interface
		{
			Disk::Disk(MemoryMapper& memory, VirtualCPU& cpu) : m_memory(memory), m_cpu(cpu)
			{
				m_data.w_Byte(tRegisters::Signal, tSignalValues::Ignore);
			}

			int8_t Disk::read8(uint16_t addr)
			{
				int8_t value = 0;
				if (!m_data.r_Byte(addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerReadFailed, "Failed to read byte from HardDrive Controller");
					return 0;
				}
				return value;
			}

			int16_t Disk::read16(uint16_t addr)
			{
				int16_t value = 0;
				if (!m_data.r_Word(addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerReadFailed, "Failed to read word from HardDrive Controller");
					return 0;
				}
				return value;
			}

			int8_t Disk::write8(uint16_t addr, int8_t value)
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

			int16_t Disk::write16(uint16_t addr, int16_t value)
			{
				if (addr >= tRegisters::FirstReadOnly)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ControllerWriteFailed, "Attempt to write word to ReadOnly part of HardDrive Controller");
					return 0;
				}
				if (!m_data.w_Word(addr, value))
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
				uint8_t signal = tSignalValues::Ignore;
				m_data.r_Byte(tRegisters::Signal, (int8_t&)signal);
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
					uint8_t status = 0;
					m_data.r_Byte(tRegisters::Status, (int8_t&)status);
					if (status == tStatusValues::Free)
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_InvalidConfiguration, "Invalid HardDrive configuration: <status> register set to <free> while busy.");
						m_busy = false;
						return;
					}
					uint8_t currentDisk = 0;
					uint16_t currentSector = 0, currentAddress = 0, restDataSize = 0, memoryAddress = 0;
					m_data.r_Byte(tRegisters::CurrentDisk, (int8_t&)currentDisk);
					m_data.r_Word(tRegisters::CurrentSector, (int16_t&)currentSector);
					m_data.r_Word(tRegisters::CurrentAddress, (int16_t&)currentAddress);
					m_data.r_Word(tRegisters::RestDataSize, (int16_t&)restDataSize);
					m_data.r_Word(tRegisters::SourceData, (int16_t&)memoryAddress);
					if (m_connectedDisks.count((data::VDiskID)currentDisk) == 0)
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_InvalidDiskSelected, "Invalid HardDrive configuration: selected Disk not found.");
						m_busy = false;
						return;
					}
					auto& disk = *m_connectedDisks[currentDisk];
					uint32_t hddAddress = 0;
					if (currentAddress == 0xFFFF)
					{
						if (currentSector == 0xFFFF)
						{
							data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_EndOfDisk, "HardDrive Error: Reached end of selected Disk.");
							m_busy = false;
							return;
						}
						currentSector++;
						currentAddress = 0x0000;
					}
					hddAddress = (currentSector << 16) | currentAddress;
					if (status == tStatusValues::Reading)
					{
						ostd::ByteStream _data;
						if (!disk.read(hddAddress, 1, _data))
						{
							data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_ReadFailed, "HardDrive Error: Failed to read data.");
							m_busy = false;
							return;
						}
						m_memory.write8(memoryAddress, _data[0]);
					}
					else if (status == tStatusValues::Writing)
					{
						int8_t dataRead = m_memory.read8(memoryAddress);
						if (!disk.write(hddAddress, dataRead))
						{
							data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_WriteFailed, "HardDrive Error: Failed to write data.");
							m_busy = false;
							return;
						}
					}
					memoryAddress++;
					if (memoryAddress == 0xFFFF)
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_MemoryOverflow, "HardDrive Error: Reached end of Memory.");
						m_busy = false;
						return;
					}
					restDataSize--;
					if (restDataSize == 0)
					{
						m_data.w_Byte(tRegisters::Status, tStatusValues::Free);
						m_data.w_Byte(tRegisters::Signal, tSignalValues::Ignore);
						m_busy = false;
						m_cpu.handleInterrupt(data::InterruptCodes::DiskInterfaceFFinished, true);
						return;
					}
					currentAddress++;
					m_data.w_Word(tRegisters::CurrentSector, currentSector);
					m_data.w_Word(tRegisters::CurrentAddress, currentAddress);
					m_data.w_Word(tRegisters::RestDataSize, restDataSize);
					m_data.w_Word(tRegisters::SourceData, memoryAddress);
					return;
				}
				if (signal != tSignalValues::Start) return;
				uint8_t mode = 0, disk = 0;
				uint16_t sector = 0, address = 0, size = 0, srcAddr = 0;
				m_data.r_Byte(tRegisters::ModeSelector, (int8_t&)mode);
				m_data.r_Byte(tRegisters::DiskSelector, (int8_t&)disk);
				m_data.r_Word(tRegisters::SectorSelector, (int16_t&)sector);
				m_data.r_Word(tRegisters::AddressSelector, (int16_t&)address);
				m_data.r_Word(tRegisters::DataSize, (int16_t&)size);
				m_data.r_Word(tRegisters::DataSourceAddress, (int16_t&)srcAddr);
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
				m_data.w_Word(tRegisters::CurrentSector, sector);
				m_data.w_Word(tRegisters::CurrentAddress, address);
				m_data.w_Word(tRegisters::RestDataSize, size);
				m_data.w_Word(tRegisters::SourceData, srcAddr);

				m_data.w_Byte(tRegisters::Signal, tSignalValues::Ignore);
				m_busy = true;
			}

			data::VDiskID Disk::connectDisk(VirtualHardDrive& hdd)
			{
				for (auto& disk : m_connectedDisks)
				{
					if (disk.second->isSame(hdd))
					{
						data::ErrorHandler::pushError(data::ErrorCodes::HardDrive_DiskAlreadyConnected, "Attempt to connect already connected Disk to Controller");
						return 0;
					}
				}
				m_connectedDisks[Disk::s_nextDiskID] = &hdd;
				return Disk::s_nextDiskID++;
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





			Graphics::Graphics(void)
			{
				m_videoMemory.init(0xFFFF);
			}

			int8_t Graphics::read8(uint16_t addr)
			{
				int8_t outVal = 0;
				if (!m_videoMemory.r_Byte(addr, outVal))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::Graphics_MemoryReadFailed, "Failed to read byte from Graphics Memory");
					return 0;
				}
				return outVal;
			}

			int16_t Graphics::read16(uint16_t addr)
			{
				int16_t outVal = 0;
				if (!m_videoMemory.r_Word(addr, outVal))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::Graphics_MemoryReadFailed, "Failed to read word from Graphics Memory");
					return 0;
				}
				return outVal;
			}

			int8_t Graphics::write8(uint16_t addr, int8_t value)
			{
				if (!m_videoMemory.w_Byte(addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::Graphics_MemoryWriteFailed, "Failed to write byte to Graphics Memory");
					return 0;
				}
				return value;
			}

			int16_t Graphics::write16(uint16_t addr, int16_t value)
			{
				if (!m_videoMemory.w_Word(addr, value))
				{
					data::ErrorHandler::pushError(data::ErrorCodes::Graphics_MemoryWriteFailed, "Failed to word byte to Graphics Memory");
					return 0;
				}
				return value;
			}

			ostd::ByteStream* Graphics::getByteStream(void)
			{
				return &m_videoMemory.getData();
			}





			SerialPort::SerialPort(void)
			{
			}

			int8_t SerialPort::read8(uint16_t addr)
			{
				return 0x00;
			}

			int16_t SerialPort::read16(uint16_t addr)
			{
				return 0x0000;
			}

			int8_t SerialPort::write8(uint16_t addr, int8_t value)
			{
				return 0x00;
			}

			int16_t SerialPort::write16(uint16_t addr, int16_t value)
			{
				return 0x0000;
			}

			ostd::ByteStream* SerialPort::getByteStream(void)
			{
				return nullptr;
			}





			void CMOS::init(const ostd::String& cmosFilePath)
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
				m_fileSize = (int64_t)m_dataFile.tellg() - m_fileSize;
				m_dataFile.seekg( 0, std::ios::beg );
				if (m_fileSize != m_size)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidSize, ostd::String("Invalid virtual CMOS chhip size: ").add(m_fileSize));
					return;
				}
				m_initialized = true;
			}

			int8_t CMOS::read8(uint16_t addr)
			{
				if (!m_initialized)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_Uninitialized, "Attempt to read uninitialized CMOS chip.");
					return false;
				}
				if (addr >= m_size)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidAddress, ostd::String("Invalid Byte CMOS location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
					return false;
				}
				int8_t value = 0;
				m_dataFile.seekg(addr);
				m_dataFile.read((char*)&value, sizeof(value));
				return value;
			}

			int16_t CMOS::read16(uint16_t addr)
			{
				if (!m_initialized)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_Uninitialized, "Attempt to read uninitialized CMOS chip.");
					return false;
				}
				if (addr >= m_size - 1)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidAddress, ostd::String("Invalid Word CMOS location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
					return 0;
				}
				int8_t b1 = read8(addr);
				int8_t b2 = read8(addr + 1);
				return ((b1 <<  8) & 0xFF00U)
					  | (b2 	   & 0x00FFU);
			}

			int8_t CMOS::write8(uint16_t addr, int8_t value)
			{
				if (!m_initialized)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_Uninitialized, "Attempt to read uninitialized CMOS chip.");
					return false;
				}
				if (addr >= m_size)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidAddress, ostd::String("Invalid Byte CMOS location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
					return 0;
				}
				if (!DragonRuntime::cpu.isInBIOSMOde())
				{
					data::ErrorHandler::pushError(data::ErrorCodes::AccessViolation_BiosModeRequired, ostd::String("Attempting to write byte to CMOS while not in BIOS mode. Address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
					return 0;
				}
				m_dataFile.seekp(addr);
				m_dataFile.write((char*)(&value), sizeof(value));
				return value;
			}

			int16_t CMOS::write16(uint16_t addr, int16_t value)
			{
				if (!m_initialized)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_Uninitialized, "Attempt to read uninitialized CMOS chip.");
					return false;
				}
				if (addr >= m_size - 1)
				{
					data::ErrorHandler::pushError(data::ErrorCodes::CMOS_InvalidAddress, ostd::String("Invalid Word CMOS location at address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
					return 0;
				}
				if (!DragonRuntime::cpu.isInBIOSMOde())
				{
					data::ErrorHandler::pushError(data::ErrorCodes::AccessViolation_BiosModeRequired, ostd::String("Attempting to write word to CMOS while not in BIOS mode. Address: ").add(ostd::Utils::getHexStr(addr, true, 2)));
					return 0;
				}
				int8_t b1 = (value >> 8) & 0xFF;
				int8_t b2 = (value & 0xFF);
				write8(addr, b1);
				write8(addr + 1, b2);
				return value;
			}

			ostd::ByteStream* CMOS::getByteStream(void)
			{
				return &m_data;
			}
		}
	}
}