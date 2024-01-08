#pragma once

#include "IMemoryDevice.hpp"
#include <ostd/String.hpp>
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
				uint16_t startAddress { 0x0000 };
				uint16_t endAddress { 0x0000 };
				bool remap { false };
				ostd::String name { "" };
			};

			public:
				MemoryMapper(void);
				int8_t read8(uint16_t addr) override;
				int16_t read16(uint16_t addr) override;
				int8_t write8(uint16_t addr, int8_t value) override;
				int16_t write16(uint16_t addr, int16_t value) override;

				void mapDevice(IMemoryDevice& device, uint16_t startAddr, uint16_t endAddr, bool remap = false, ostd::String name = "");

				ostd::String getMemoryRegionName(uint16_t address);

				ostd::ByteStream* getByteStream(void) override;

			private:
				tMemoryRegion* findRegion(uint16_t address);

			private:
				std::vector<tMemoryRegion> m_regions;
		};
	}
}