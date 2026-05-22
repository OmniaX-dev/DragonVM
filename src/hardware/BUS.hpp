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

#include <ostd/data/BaseObject.hpp>
#include <atomic>
#include <span>
#include "../tools/GlobalData.hpp"

#pragma once

namespace dragon
{
	class MemoryDevice : public ostd::BaseObject
	{
		public:
			MemoryDevice(void) = default;
			MemoryDevice(const MemoryDevice&) = delete;
			MemoryDevice& operator=(const MemoryDevice&) = delete;
			MemoryDevice(MemoryDevice&&) = delete;
			MemoryDevice& operator=(MemoryDevice&&) = delete;
			virtual ~MemoryDevice() = default;

			virtual bool guest_cas_u32(AddressType addr, u32 expected, u32& actual, u32 desired);

			// ---- Aligned loads ----
			virtual u64 load_u64(AddressType addr) const = 0;
			virtual u32 load_u32(AddressType addr) const = 0;
			virtual u16 load_u16(AddressType addr) const = 0;
			virtual u8  load_u8 (AddressType addr) const = 0;

			inline i64 load_i64(AddressType addr) const { return cast<i64>(load_u64(addr)); }
			inline i32 load_i32(AddressType addr) const { return cast<i32>(load_u32(addr)); }
			inline i16 load_i16(AddressType addr) const { return cast<i16>(load_u16(addr)); }
			inline i8  load_i8 (AddressType addr) const { return cast<i8 >(load_u8 (addr)); }

			// ---- Aligned stores ----
			virtual bool store_u64(AddressType addr, u64 value) = 0;
			virtual bool store_u32(AddressType addr, u32 value) = 0;
			virtual bool store_u16(AddressType addr, u16 value) = 0;
			virtual bool store_u8 (AddressType addr, u8  value) = 0;

			inline bool store_i64(AddressType addr, u64 value) { return store_u64(addr, cast<i64>(value)); }
			inline bool store_i32(AddressType addr, u32 value) { return store_u32(addr, cast<i32>(value)); }
			inline bool store_i16(AddressType addr, u16 value) { return store_u16(addr, cast<i16>(value)); }
			inline bool store_i8 (AddressType addr, u8  value) { return store_u8 (addr, cast<i8 >(value)); }

			[[noreturn]] virtual void fault(AddressType addr, data::ExceptionCause cause, const String& msg = "") const { throw data::GuestException{ cause, addr, msg }; }; // Message can be useful for debugging
	};

	class AtomicStorageMemoryDevice : public MemoryDevice
	{
		public:
			explicit AtomicStorageMemoryDevice(u64 size);
			virtual ~AtomicStorageMemoryDevice(void);
			AtomicStorageMemoryDevice(const AtomicStorageMemoryDevice&) = delete;
			AtomicStorageMemoryDevice& operator=(const AtomicStorageMemoryDevice&) = delete;
			AtomicStorageMemoryDevice(AtomicStorageMemoryDevice&&) = delete;
			AtomicStorageMemoryDevice& operator=(AtomicStorageMemoryDevice&&) = delete;

			// ---- Helpers ----
			inline u64 getWordCount(void) const { return m_wordCount; }
			inline u64 getSize(void) const { return m_size; }
			inline virtual std::span<std::atomic<u64>> getRawData(void) { return { m_words, m_size }; }
			inline virtual std::span<const std::atomic<u64>> getRawData(void) const { return { m_words, m_size }; }
			// Both getRawData() can be overridden to return empty spans for devices where internal memory shouldn't be exposed

		protected:
			std::atomic<u64>* m_words;
			u64 m_wordCount;
			u64 m_size;
	};
	namespace hw
	{
		class BUS : public MemoryDevice
		{
			public:
				private: struct tMemoryRegion
				{
					MemoryDevice* device { nullptr };
					AddressType startAddress { 0x0000'0000 };
					AddressType endAddress { 0x0000'0000 };
					bool remap { false };
					ostd::String name { "" };
				};

				public:
					BUS(void) = default;

					bool guest_cas_u32(AddressType addr, u32 expected, u32& actual, u32 desired) override;

					// ---- Aligned loads ----
					u64 load_u64(AddressType addr) const override;
					u32 load_u32(AddressType addr) const override;
					u16 load_u16(AddressType addr) const override;
					u8  load_u8 (AddressType addr) const override;

					// ---- Aligned stores ----
					bool store_u64(AddressType addr, u64 value) override;
					bool store_u32(AddressType addr, u32 value) override;
					bool store_u16(AddressType addr, u16 value) override;
					bool store_u8 (AddressType addr, u8  value) override;

					void mapDevice(MemoryDevice& device, AddressType startAddr, AddressType endAddr, bool remap = false, ostd::String name = "");
					ostd::String getMemoryRegionName(AddressType address);

				private:
					template <typename Self>
					auto findRegion(this Self&& self, AddressType address)
					{
						for (auto& region : self.m_regions)
						{       // works whether self is const or not
							if (address >= region.startAddress && address <= region.endAddress)
								return &region;
						}
						data::ErrorHandler::pushError(data::ErrorCodes::MM_RegionNotFound, "Memory device not found");
						return decltype(&self.m_regions[0]){nullptr};
					}

				private:
					std::vector<tMemoryRegion> m_regions;

				private:
		};
	}
}
