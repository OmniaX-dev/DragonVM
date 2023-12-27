#pragma once

#include <ostd/Types.hpp>

namespace dragon
{
	namespace hw
	{
		class IMemoryDevice
		{
			public:
				virtual int8_t read8(uint16_t addr) = 0;
				virtual int16_t read16(uint16_t addr) = 0;
				virtual int8_t write8(uint16_t addr, int8_t value) = 0;
				virtual int16_t write16(uint16_t addr, int16_t value) = 0;
				virtual inline ostd::ByteStream* getByteStream(void) = 0;
		};
	}
}