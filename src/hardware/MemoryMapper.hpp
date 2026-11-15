#pragma once

#include "IMemoryDevice.hpp"
#include "../tools/GlobalData.hpp"
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
				i16 read16(u32 addr) override;
				i32 read32(u32 addr) override;
				i8 write8(u32 addr, i8 value) override;
				i16 write16(u32 addr, i16 value) override;
				i32 write32(u32 addr, i32 value) override;

				void mapDevice(IMemoryDevice& device, u32 startAddr, u32 endAddr, bool remap = false, String name = "");

				// The permission map: 2 bits for each 4 KiB page of the address space (65536 pages,
				// 16 KiB), page p in byte p / 4, bits (p % 4) * 2: bit 0 user mode may read (and
				// execute), bit 1 user mode may write. The CPU turns the checks on while it runs a
				// user-mode instruction (so the devices' own accesses aren't checked), with the map's
				// bytes (nullptr: every access is denied). A denied access pushes
				// AccessViolation_PageProtection and doesn't reach the device (reads give 0).
				inline static constexpr u32 PageShift = 12;
				inline static constexpr u32 PageSize = 1u << PageShift;
				inline static constexpr u32 PermissionMapSize = (data::AddressMask + 1) >> PageShift >> 2;
				inline static constexpr u8 PermissionRead = 0x01;
				inline static constexpr u8 PermissionWrite = 0x02;
				inline void setUserChecks(bool on, const u8* map) { m_userChecks = on; m_permissionMap = map; }
				inline bool areUserChecksOn(void) const { return m_userChecks; }
				// Removes every mapping.
				inline void clear(void) { m_regions.clear(); m_lastRegion = nullptr; }

				String getMemoryRegionName(u32 address);
				inline const std::vector<tMemoryRegion>& getRegions(void) const { return m_regions; }
				// The last mapped address + 1 (the size of the used address space).
				u32 getMappedEnd(void) const;

				// ===== Debugger access =====
				// These read/write the devices' memory directly (their ByteStreams), so they have
				// no side effects: no errors pushed, no supervisor mode checks, no CPU offset. Bytes of
				// devices without memory read as 0.
				// The address space [0, size) as the CPU sees it (dst must hold size bytes).
				void readBusImage(u8* dst, u32 size);
				u8 peek8(u32 addr);
				u32 peek32(u32 addr);
				// Returns false if the address has no backing memory.
				bool poke8(u32 addr, u8 value);
				// The bytes at addr..addr + size - 1, read in place (as read8 would see them), if
				// they are all in one device's ByteStream; nullptr otherwise (several devices, a
				// device without a ByteStream, out of range): read them with read8 then.
				const u8* directSpan(u32 addr, u32 size);
				// Same, to write them in place (the pages are marked as written, as poke8 does).
				u8* writableSpan(u32 addr, u32 size);

				ostd::ByteStream* getByteStream(void) override;

			private:
				// The checks above: false (with the error) when user mode may not access the bytes.
				bool user_allowed(u32 addr, u32 size, bool write);
				tMemoryRegion* findRegion(u32 address, bool pushError = true);
				// directSpan / writableSpan: the region and where addr is in its device's ByteStream.
				i8* span(u32 addr, u32 size, tMemoryRegion*& region, u64& streamAddr);

			private:
				std::vector<tMemoryRegion> m_regions;
				bool m_userChecks { false };
				const u8* m_permissionMap { nullptr };
				tMemoryRegion* m_lastRegion { nullptr };
		};
	}
}
