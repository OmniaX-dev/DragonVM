#pragma once

#include <ostd/Serial.hpp>
#include "IMemoryDevice.hpp"

namespace dragon
{
	namespace hw
	{
		class VirtualRAM : public IMemoryDevice
		{
			public:
				VirtualRAM(void);
				int8_t read8(uint16_t addr) override;
				int16_t read16(uint16_t addr) override;
				int8_t write8(uint16_t addr, int8_t value) override;
				int16_t write16(uint16_t addr, int16_t value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
				ostd::serial::SerialIO m_memory;
		};
	}
}