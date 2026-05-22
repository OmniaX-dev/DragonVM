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

#pragma once

#include <ostd/data/Types.hpp>

#include "BUS.hpp"

namespace dragon
{
	namespace hw
	{
		class RAM : public AtomicStorageMemoryDevice
		{
			public:
				explicit RAM(u64 size) : AtomicStorageMemoryDevice(size) {  }

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
		};
	}
}
