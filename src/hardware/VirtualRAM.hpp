#pragma once

#include "../tools/LegacyOstdSerial.hpp"
#include "IMemoryDevice.hpp"

namespace dragon
{
	namespace hw
	{
		class VirtualRAM : public IMemoryDevice
		{
			public:
				VirtualRAM(void);
				i8 read8(u16 addr) override;
				i16 read16(u16 addr) override;
				i8 write8(u16 addr, i8 value) override;
				i16 write16(u16 addr, i16 value) override;

				ostd::ByteStream* getByteStream(void) override;

			private:
				ostd::serial::SerialIO m_memory;
		};
	}
}
