#pragma once

#include "IMemoryDevice.hpp"
#include <ostd/string/String.hpp>
#include <vector>

namespace dragon
{
	namespace hw
	{
		class MemoryMapper : public IMemoryDevice
		{
			private: struct tMemoryRegion
			{
				IMemoryDevice* device { nullptr };
				u16 startAddress { 0x0000 };
				u16 endAddress { 0x0000 };
				bool remap { false };
				String name { "" };
			};

			public:
				MemoryMapper(void);
				i8 read8(u16 addr) override;
				i16 read16(u16 addr) override;
				i8 write8(u16 addr, i8 value) override;
				i16 write16(u16 addr, i16 value) override;

				void mapDevice(IMemoryDevice& device, u16 startAddr, u16 endAddr, bool remap = false, String name = "");

				String getMemoryRegionName(u16 address);

				ostd::ByteStream* getByteStream(void) override;

			private:
				tMemoryRegion* findRegion(u16 address);

			private:
				std::vector<tMemoryRegion> m_regions;
		};
	}
}
