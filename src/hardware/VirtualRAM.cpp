#include "VirtualRAM.hpp"

#include "../tools/GlobalData.hpp"
#include "../runtime/DragonRuntime.hpp"

namespace dragon
{
	namespace hw
	{
		VirtualRAM::VirtualRAM(void)
		{
		}

		void VirtualRAM::init(u32 size)
		{
			m_memory.assign(size, 0);
			m_dirty.resize(size);
		}

		u32 VirtualRAM::final_address(u32 addr) const
		{
			return addr + DragonRuntime::cpu.getCurrentOffset();
		}

		i8 VirtualRAM::read8(u32 addr)
		{
			const u32 a = final_address(addr);
			if (a >= m_memory.size())
			{
				data::ErrorHandler::pushError(data::ErrorCodes::RAM_InvalidAddress, String("Invalid RAM read at address: ").add(String::getHexStr(a, true, 4)));
				return 0x00;
			}
			return m_memory[a];
		}

		i16 VirtualRAM::read16(u32 addr)
		{
			const u32 a = final_address(addr);
			i16 value = 0;
			if (!bytes::read16(m_memory, a, value))
			{
				data::ErrorHandler::pushError(data::ErrorCodes::RAM_InvalidAddress, String("Invalid RAM read at address: ").add(String::getHexStr(a, true, 4)));
				return 0x0000;
			}
			return value;
		}

		i16 VirtualRAM::write16(u32 addr, i16 value)
		{
			const u32 a = final_address(addr);
			if (!bytes::write16(m_memory, a, value))
			{
				data::ErrorHandler::pushError(data::ErrorCodes::RAM_InvalidAddress, String("Invalid RAM write at address: ").add(String::getHexStr(a, true, 4)));
				return 0x0000;
			}
			m_dirty.markRange(a, 2);
			return value;
		}

		i32 VirtualRAM::read32(u32 addr)
		{
			const u32 a = final_address(addr);
			i32 value = 0;
			if (!bytes::read32(m_memory, a, value))
			{
				data::ErrorHandler::pushError(data::ErrorCodes::RAM_InvalidAddress, String("Invalid RAM read at address: ").add(String::getHexStr(a, true, 4)));
				return 0x00;
			}
			return value;
		}

		i8 VirtualRAM::write8(u32 addr, i8 value)
		{
			const u32 a = final_address(addr);
			if (a >= m_memory.size())
			{
				data::ErrorHandler::pushError(data::ErrorCodes::RAM_InvalidAddress, String("Invalid RAM write at address: ").add(String::getHexStr(a, true, 4)));
				return 0x00;
			}
			m_memory[a] = value;
			m_dirty.mark(a);
			return value;
		}

		i32 VirtualRAM::write32(u32 addr, i32 value)
		{
			const u32 a = final_address(addr);
			if (!bytes::write32(m_memory, a, value))
			{
				data::ErrorHandler::pushError(data::ErrorCodes::RAM_InvalidAddress, String("Invalid RAM write at address: ").add(String::getHexStr(a, true, 4)));
				return 0x00;
			}
			m_dirty.markRange(a, 4);
			return value;
		}

		ostd::ByteStream* VirtualRAM::getByteStream(void)
		{
			return &m_memory;
		}
	}
}
