/*
	DragonV2 - A collection of useful functionality
	Copyright (C) 2026  OmniaX-Dev

	This file is part of DragonV2.

	DragonV2 is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	DragonV2 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with DragonV2.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "BUS.hpp"

namespace dragon
{
	AtomicStorageMemoryDevice::AtomicStorageMemoryDevice(u64 size)
	{
		if (size == 0) // Special case, for classes like BUS, that are MemoryDevices but don't own any data
		{
			m_wordCount = 0;
			m_size = 0;
			m_words = nullptr;
			return;
		}
		m_wordCount = (size + 7) / 8; // Round up to multiple of 8 bytes for the word-granular backing
		m_size = size;
		m_words = new std::atomic<u64>[m_wordCount];
		for (u64 i = 0; i < m_wordCount; ++i)
			m_words[i].store(0, std::memory_order_relaxed);
	}

	AtomicStorageMemoryDevice::~AtomicStorageMemoryDevice(void)
	{
		delete[] m_words;
	}

	bool MemoryDevice::guest_cas_u32(AddressType addr, u32 expected, u32& actual, u32 desired)
	{
		data::ErrorHandler::pushError(data::ErrorCodes::MM_AtomicNotSupported, "MemoryDevice::guest_cas_u32: atomic not supported: ");
		fault(addr, data::ExceptionCause::AtomicNotSupported, "MemoryDevice::guest_cas_u32");
		return false;
	}





	namespace hw
	{
		bool BUS::guest_cas_u32(AddressType addr, u32 expected, u32& actual, u32 desired)
		{
			auto region = findRegion(addr);
			if (!region) { /* fault */ return false; }
			AddressType finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->guest_cas_u32(finalAddr, expected, actual, desired);
		}

		u8 BUS::load_u8(AddressType addr) const
		{
			auto region = findRegion(addr);
			if (region == nullptr) return 0x00; //TODO: Error
			AddressType finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->load_u8(finalAddr);
		}

		u16 BUS::load_u16(AddressType addr) const
		{
			auto region = findRegion(addr);
			if (region == nullptr) return 0x0000; //TODO: Error
			AddressType finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->load_u16(finalAddr);
		}

		u32 BUS::load_u32(AddressType addr) const
		{
			auto region = findRegion(addr);
			if (region == nullptr) return 0x0000'0000; //TODO: Error
			AddressType finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->load_u32(finalAddr);
		}

		u64 BUS::load_u64(AddressType addr) const
		{
			auto region = findRegion(addr);
			if (region == nullptr) return 0x0000'0000'0000'0000; //TODO: Error
			AddressType finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->load_u64(finalAddr);
		}

		bool BUS::store_u8(AddressType addr, u8  value)
		{
			auto region = findRegion(addr);
			if (region == nullptr) return false;
			AddressType finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->store_u8(finalAddr, value);
		}

		bool BUS::store_u16(AddressType addr, u16 value)
		{
			auto region = findRegion(addr);
			if (region == nullptr) return false;
			AddressType finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->store_u16(finalAddr, value);
		}

		bool BUS::store_u32(AddressType addr, u32 value)
		{
			auto region = findRegion(addr);
			if (region == nullptr) return false;
			AddressType finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->store_u32(finalAddr, value);
		}

		bool BUS::store_u64(AddressType addr, u64 value)
		{
			auto region = findRegion(addr);
			if (region == nullptr) return false;
			AddressType finalAddr = (region->remap ? addr - region->startAddress : addr);
			return region->device->store_u64(finalAddr, value);
		}

		void BUS::mapDevice(MemoryDevice& device, AddressType startAddr, AddressType endAddr, bool remap, ostd::String name)
		{
			m_regions.push_back({ &device, startAddr, endAddr, remap, name });
		}

		ostd::String BUS::getMemoryRegionName(AddressType address)
		{
			tMemoryRegion* region = findRegion(address);
			if (region == nullptr) return "Invalid";
			return region->name;
		}
	}
}
