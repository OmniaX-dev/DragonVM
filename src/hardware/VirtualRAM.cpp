#include "VirtualRAM.hpp"

#include "../tools/GlobalData.hpp"
#include "../runtime/DragonRuntime.hpp"

namespace dragon
{
	namespace hw
	{
		VirtualRAM::VirtualRAM(void)
		{
			m_memory.init(0xFFFF);
			m_memory.enableAutoResize(false);
		}

		i8 VirtualRAM::read8(u16 addr)
		{
			i8 outVal = 0;
			u16 offset = DragonRuntime::cpu.getCurrentOffset();
			if (!m_memory.r_Byte(addr + offset, outVal)) return 0x00; //TODO: Error
			return outVal;
		}

		i16 VirtualRAM::read16(u16 addr)
		{
			i16 outVal = 0;
			u16 offset = DragonRuntime::cpu.getCurrentOffset();
			if (!m_memory.r_Word(addr + offset, outVal)) return 0x00; //TODO: Error
			return outVal;
		}

		i8 VirtualRAM::write8(u16 addr, i8 value)
		{
			u16 offset = DragonRuntime::cpu.getCurrentOffset();
			if (!m_memory.w_Byte(addr + offset, value)) return 0; //TODO: Error
			return value;
		}

		i16 VirtualRAM::write16(u16 addr, i16 value)
		{
			u16 offset = DragonRuntime::cpu.getCurrentOffset();
			if (!m_memory.w_Word(addr + offset, value)) return 0; //TODO: Error
			return value;
		}

		ostd::ByteStream* VirtualRAM::getByteStream(void)
		{
			return &m_memory.getData();
		}
	}
}