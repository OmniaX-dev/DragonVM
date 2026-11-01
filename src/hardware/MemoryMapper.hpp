#pragma once

#include "IMemoryDevice.hpp"
#include <ostd/string/String.hpp>
#include <vector>

namespace dragon
{
	namespace hw
	{
		// The address bus: routes each access to the device mapped at the address. Addresses
		// are masked to the bus width (data::AddressMask).
		class MemoryMapper : public IMemoryDevice
		{
			public: struct tMemoryRegion
			{
				IMemoryDevice* device { nullptr };
				u32 startAddress { 0x00000000 };
				u32 endAddress { 0x00000000 };
				bool remap { false };
				String name { "" };
			};

			public:
				MemoryMapper(void);
				i8 read8(u32 addr) override;
				i32 read32(u32 addr) override;
				i8 write8(u32 addr, i8 value) override;
				i32 write32(u32 addr, i32 value) override;

				void mapDevice(IMemoryDevice& device, u32 startAddr, u32 endAddr, bool remap = false, String name = "");
				// Removes every mapping.
				inline void clear(void) { m_regions.clear(); m_lastRegion = nullptr; }

				String getMemoryRegionName(u32 address);
				inline const std::vector<tMemoryRegion>& getRegions(void) const { return m_regions; }
				// The last mapped address + 1 (the size of the used address space).
				u32 getMappedEnd(void) const;

				// ===== Debugger access =====
				// These read/write the devices' memory directly (their ByteStreams), so they have
				// no side effects: no errors pushed, no BIOS mode checks, no CPU offset. Bytes of
				// devices without memory read as 0.
				// The address space [0, size) as the CPU sees it (dst must hold size bytes).
				void readBusImage(u8* dst, u32 size);
				u8 peek8(u32 addr);
				u32 peek32(u32 addr);
				// Returns false if the address has no backing memory.
				bool poke8(u32 addr, u8 value);

				ostd::ByteStream* getByteStream(void) override;

			private:
				tMemoryRegion* findRegion(u32 address, bool pushError = true);

			private:
				std::vector<tMemoryRegion> m_regions;
				tMemoryRegion* m_lastRegion { nullptr };
		};
	}
}
