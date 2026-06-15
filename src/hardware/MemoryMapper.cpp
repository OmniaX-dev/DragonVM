#include "MemoryMapper.hpp"
#include <ostd/data/Types.hpp>

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
