#include "MemoryMapper.hpp"
#include <ostd/data/Types.hpp>
#include <cstring>

#include "../tools/GlobalData.hpp"

namespace dragon
{
	namespace hw
	{
		MemoryMapper::MemoryMapper(void)
		{
		}

		i8 MemoryMapper::read8(u32 addr)
		{
			addr &= data::AddressMask;
			auto region = findRegion(addr);
			if (region == nullptr) return 0x00;
			const u32 finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->read8(finalAddr);
		}

		i16 MemoryMapper::read16(u32 addr)
		{
			addr &= data::AddressMask;
			auto region = findRegion(addr);
			if (region == nullptr) return 0x0000;
			if (cast<u64>(addr) + 1 > region->endAddress)
				return IMemoryDevice::read16(addr);
			const u32 finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->read16(finalAddr);
		}

		i16 MemoryMapper::write16(u32 addr, i16 value)
		{
			addr &= data::AddressMask;
			auto region = findRegion(addr);
			if (region == nullptr) return 0x0000;
			if (cast<u64>(addr) + 1 > region->endAddress)
				return IMemoryDevice::write16(addr, value);
			const u32 finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->write16(finalAddr, value);
		}

		i32 MemoryMapper::read32(u32 addr)
		{
			addr &= data::AddressMask;
			auto region = findRegion(addr);
			if (region == nullptr) return 0x00000000;
			if (cast<u64>(addr) + 3 > region->endAddress)
			{
				// The word crosses into the next region: byte by byte.
				return IMemoryDevice::read32(addr);
			}
			const u32 finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->read32(finalAddr);
		}

		i8 MemoryMapper::write8(u32 addr, i8 value)
		{
			addr &= data::AddressMask;
			auto region = findRegion(addr);
			if (region == nullptr) return 0x00;
			const u32 finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->write8(finalAddr, value);
		}

		i32 MemoryMapper::write32(u32 addr, i32 value)
		{
			addr &= data::AddressMask;
			auto region = findRegion(addr);
			if (region == nullptr) return 0x00000000;
			if (cast<u64>(addr) + 3 > region->endAddress)
				return IMemoryDevice::write32(addr, value);
			const u32 finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->write32(finalAddr, value);
		}

		void MemoryMapper::mapDevice(IMemoryDevice& device, u32 startAddr, u32 endAddr, bool remap, String name)
		{
			m_regions.push_back({ &device, startAddr, endAddr, remap, name });
			m_lastRegion = nullptr;
		}

		String MemoryMapper::getMemoryRegionName(u32 address)
		{
			tMemoryRegion* region = findRegion(address & data::AddressMask, false);
			if (region == nullptr) return "Invalid";
			return region->name;
		}

		u32 MemoryMapper::getMappedEnd(void) const
		{
			u32 end = 0;
			for (const auto& region : m_regions)
				end = std::max<u32>(end, region.endAddress + 1);
			return end;
		}

		ostd::ByteStream* MemoryMapper::getByteStream(void)
		{
			return nullptr;
		}

		void MemoryMapper::readBusImage(u8* dst, u32 size)
		{
			std::memset(dst, 0, size);
			for (auto& region : m_regions)
			{
				ostd::ByteStream* stream = region.device->getByteStream();
				if (stream == nullptr || region.startAddress >= size)
					continue;
				const u64 first = (region.remap ? 0 : region.startAddress);
				u64 count = cast<u64>(region.endAddress) - region.startAddress + 1;
				count = std::min<u64>(count, cast<u64>(size) - region.startAddress);
				if (first >= stream->size())
					continue;
				const u64 available = std::min<u64>(count, stream->size() - first);
				std::memcpy(dst + region.startAddress, stream->data() + first, available);
			}
		}

		u8 MemoryMapper::peek8(u32 addr)
		{
			addr &= data::AddressMask;
			tMemoryRegion* region = findRegion(addr, false);
			if (region == nullptr)
				return 0;
			ostd::ByteStream* stream = region->device->getByteStream();
			const u32 finalAddr = (region->remap ? addr - region->startAddress : addr);
			if (stream == nullptr || finalAddr >= stream->size())
				return 0;
			return cast<u8>((*stream)[finalAddr]);
		}

		u32 MemoryMapper::peek32(u32 addr)
		{
			// Big endian, like the devices.
			return (cast<u32>(peek8(addr)) << 24) | (cast<u32>(peek8(addr + 1)) << 16) | (cast<u32>(peek8(addr + 2)) << 8) | peek8(addr + 3);
		}

		bool MemoryMapper::poke8(u32 addr, u8 value)
		{
			addr &= data::AddressMask;
			tMemoryRegion* region = findRegion(addr, false);
			if (region == nullptr)
				return false;
			ostd::ByteStream* stream = region->device->getByteStream();
			const u32 finalAddr = (region->remap ? addr - region->startAddress : addr);
			if (stream == nullptr || finalAddr >= stream->size())
				return false;
			(*stream)[finalAddr] = cast<i8>(value);
			if (DirtyPageMap* dirty = region->device->getDirtyPages())
				dirty->mark(finalAddr);
			region->device->syncByteStream();
			return true;
		}

		i8* MemoryMapper::span(u32 addr, u32 size, tMemoryRegion*& region, u64& streamAddr)
		{
			addr &= data::AddressMask;
			if (size == 0)
				return nullptr;
			region = findRegion(addr, false);
			if (region == nullptr || cast<u64>(addr) + size - 1 > region->endAddress)
				return nullptr;
			ostd::ByteStream* stream = region->device->getByteStream();
			if (stream == nullptr)
				return nullptr;
			streamAddr = region->device->streamAddress(region->remap ? addr - region->startAddress : addr);
			if (streamAddr + size > stream->size())
				return nullptr;
			return stream->data() + streamAddr;
		}

		const u8* MemoryMapper::directSpan(u32 addr, u32 size)
		{
			tMemoryRegion* region = nullptr;
			u64 streamAddr = 0;
			return reinterpret_cast<const u8*>(span(addr, size, region, streamAddr));
		}

		u8* MemoryMapper::writableSpan(u32 addr, u32 size)
		{
			tMemoryRegion* region = nullptr;
			u64 streamAddr = 0;
			i8* bytes = span(addr, size, region, streamAddr);
			if (bytes != nullptr)
			{
				if (DirtyPageMap* dirty = region->device->getDirtyPages())
					dirty->markRange(streamAddr, size);
			}
			return reinterpret_cast<u8*>(bytes);
		}

		MemoryMapper::tMemoryRegion* MemoryMapper::findRegion(u32 address, bool pushError)
		{
			// Most accesses hit the same region as the previous one (the RAM).
			if (m_lastRegion != nullptr && address >= m_lastRegion->startAddress && address <= m_lastRegion->endAddress)
				return m_lastRegion;
			for (auto& region : m_regions)
			{
				if (address >= region.startAddress && address <= region.endAddress)
				{
					m_lastRegion = &region;
					return &region;
				}
			}
			if (pushError)
				data::ErrorHandler::pushError(data::ErrorCodes::MM_RegionNotFound, String("Memory device not found for address: ").add(String::getHexStr(address, true, 4)));
			return nullptr;
		}
	}
}
