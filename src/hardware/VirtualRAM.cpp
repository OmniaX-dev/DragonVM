#include "VirtualRAM.hpp"

#include "../tools/GlobalData.hpp"

namespace dragon
{
	namespace hw
	{
		VirtualRAM::VirtualRAM(void)
		{
			m_memory.init(0xFFFF);
		}

		int8_t VirtualRAM::read8(uint16_t addr)
		{
			int8_t outVal = 0;
			if (!m_memory.r_Byte(addr, outVal)) return 0x00; //TODO: Error
			return outVal;
		}

		int16_t VirtualRAM::read16(uint16_t addr)
		{
			int16_t outVal = 0;
			if (!m_memory.r_Word(addr, outVal)) return 0x00; //TODO: Error
			return outVal;
		}

		int8_t VirtualRAM::write8(uint16_t addr, int8_t value)
		{
			if (!m_memory.w_Byte(addr, value)) return 0; //TODO: Error
			return value;
		}

		int16_t VirtualRAM::write16(uint16_t addr, int16_t value)
		{
			if (!m_memory.w_Word(addr, value)) return 0; //TODO: Error
			return value;
		}

		ostd::ByteStream* VirtualRAM::getByteStream(void)
		{
			return &m_memory.getData();
		}
	}
}