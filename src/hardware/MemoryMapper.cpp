#include "MemoryMapper.hpp"
#include <ostd/Utils.hpp>
#include <iostream>

#include "../tools/GlobalData.hpp"

namespace dragon
{
	namespace hw
	{
		MemoryMapper::MemoryMapper(void)
		{
		}

		int8_t MemoryMapper::read8(uint16_t addr)
		{
			auto region = findRegion(addr);
			if (region == nullptr) return 0x0000; //TODO: Error
			uint16_t finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->read8(finalAddr);
		}

		int16_t MemoryMapper::read16(uint16_t addr)
		{
			auto region = findRegion(addr);
			if (region == nullptr) return 0x0000; //TODO: Error
			uint16_t finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->read16(finalAddr);
		}

		int8_t MemoryMapper::write8(uint16_t addr, int8_t value)
		{
			auto region = findRegion(addr);
			if (region == nullptr) return 0x0000; //TODO: Error
			uint16_t finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->write8(finalAddr, value);
		}

		int16_t MemoryMapper::write16(uint16_t addr, int16_t value)
		{
			auto region = findRegion(addr);
			if (region == nullptr) return 0x0000; //TODO: Error
			uint16_t finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->write16(finalAddr, value);
		}

		void MemoryMapper::mapDevice(IMemoryDevice& device, uint16_t startAddr, uint16_t endAddr, bool remap, ostd::String name)
		{
			m_regions.push_back({ &device, startAddr, endAddr, remap, name });
		}

		ostd::String MemoryMapper::getMemoryRegionName(uint16_t address)
		{
			tMemoryRegion* region = findRegion(address);
			if (region == nullptr) return "Invalid";
			return region->name;
		}

		ostd::ByteStream* MemoryMapper::getByteStream(void)
		{
			return nullptr;
		}

		MemoryMapper::tMemoryRegion* MemoryMapper::findRegion(uint16_t address)
		{
			for (auto& region : m_regions)
			{
				if (address >= region.startAddress && address <= region.endAddress)
					return &region;
			}
			data::ErrorHandler::pushError(data::ErrorCodes::MM_RegionNotFound, ostd::StringEditor("Memory device not found for address: ").add(ostd::Utils::getHexStr(address, true, 2)).str());
			return nullptr; //TODO: Error
		}
	}
}