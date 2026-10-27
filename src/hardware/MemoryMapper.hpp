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
			public: struct tMemoryRegion
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
				inline const std::vector<tMemoryRegion>& getRegions(void) const { return m_regions; }

				// ===== Debugger access =====
				// These read/write the devices' memory directly (their ByteStreams), so they have
				// no side effects: no errors pushed, no BIOS mode checks, no CPU offset. Bytes of
				// devices without memory read as 0.
				// The whole 64 KiB address space as the CPU sees it (dst must hold 0x10000 bytes).
				void readBusImage(u8* dst);
				u8 peek8(u16 addr);
				u16 peek16(u16 addr);
				// Returns false if the address has no backing memory.
				bool poke8(u16 addr, u8 value);

				ostd::ByteStream* getByteStream(void) override;

			private:
				tMemoryRegion* findRegion(u16 address);

			private:
				std::vector<tMemoryRegion> m_regions;
		};
	}
}
