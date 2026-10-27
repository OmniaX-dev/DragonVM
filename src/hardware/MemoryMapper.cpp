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

		i8 MemoryMapper::read8(u16 addr)
		{
			auto region = findRegion(addr);
			if (region == nullptr) return 0x0000; //TODO: Error
			u16 finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->read8(finalAddr);
		}

		i16 MemoryMapper::read16(u16 addr)
		{
			auto region = findRegion(addr);
			if (region == nullptr) return 0x0000; //TODO: Error
			u16 finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->read16(finalAddr);
		}

		i8 MemoryMapper::write8(u16 addr, i8 value)
		{
			auto region = findRegion(addr);
			if (region == nullptr) return 0x0000; //TODO: Error
			u16 finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->write8(finalAddr, value);
		}

		i16 MemoryMapper::write16(u16 addr, i16 value)
		{
			auto region = findRegion(addr);
			if (region == nullptr) return 0x0000; //TODO: Error
			u16 finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->write16(finalAddr, value);
		}

		void MemoryMapper::mapDevice(IMemoryDevice& device, u16 startAddr, u16 endAddr, bool remap, String name)
		{
			m_regions.push_back({ &device, startAddr, endAddr, remap, name });
		}

		String MemoryMapper::getMemoryRegionName(u16 address)
		{
			tMemoryRegion* region = findRegion(address);
			if (region == nullptr) return "Invalid";
			return region->name;
		}

		ostd::ByteStream* MemoryMapper::getByteStream(void)
		{
			return nullptr;
		}

		void MemoryMapper::readBusImage(u8* dst)
		{
			std::memset(dst, 0, 0x10000);
			for (auto& region : m_regions)
			{
				ostd::ByteStream* stream = region.device->getByteStream();
				if (stream == nullptr)
					continue;
				const u32 first = (region.remap ? 0 : region.startAddress);
				const u32 count = cast<u32>(region.endAddress) - region.startAddress + 1;
				if (first >= stream->size())
					continue;
				const u32 available = std::min<u32>(count, cast<u32>(stream->size()) - first);
				std::memcpy(dst + region.startAddress, stream->data() + first, available);
			}
		}

		u8 MemoryMapper::peek8(u16 addr)
		{
			for (auto& region : m_regions)
			{
				if (addr < region.startAddress || addr > region.endAddress)
					continue;
				ostd::ByteStream* stream = region.device->getByteStream();
				const u32 finalAddr = (region.remap ? addr - region.startAddress : addr);
				if (stream == nullptr || finalAddr >= stream->size())
					return 0;
				return cast<u8>((*stream)[finalAddr]);
			}
			return 0;
		}

		u16 MemoryMapper::peek16(u16 addr)
		{
			// Big endian, like the devices.
			return cast<u16>((cast<u16>(peek8(addr)) << 8) | peek8(cast<u16>(addr + 1)));
		}

		bool MemoryMapper::poke8(u16 addr, u8 value)
		{
			for (auto& region : m_regions)
			{
				if (addr < region.startAddress || addr > region.endAddress)
					continue;
				ostd::ByteStream* stream = region.device->getByteStream();
				const u32 finalAddr = (region.remap ? addr - region.startAddress : addr);
				if (stream == nullptr || finalAddr >= stream->size())
					return false;
				(*stream)[finalAddr] = cast<i8>(value);
				region.device->syncByteStream();
				return true;
			}
			return false;
		}

		MemoryMapper::tMemoryRegion* MemoryMapper::findRegion(u16 address)
		{
			for (auto& region : m_regions)
			{
				if (address >= region.startAddress && address <= region.endAddress)
					return &region;
			}
			data::ErrorHandler::pushError(data::ErrorCodes::MM_RegionNotFound, String("Memory device not found for address: ").add(String::getHexStr(address, true, 2)));
			return nullptr; //TODO: Error
		}
	}
}
