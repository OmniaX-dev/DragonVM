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

#include "RAM.hpp"
#include <ostd/string/String.hpp>

#define DRAGON_TEST_FOR_MISALIGNED_OR_BOUNDS_FAULT_R(size, msg) \
			if (addr + size > m_size) \
			{ \
				fault(addr, data::ExceptionCause::LoadBounds, msg); \
				return 0; \
			} \
			if (addr & (size - 1)) \
			{ \
				fault(addr, data::ExceptionCause::MisalignedLoad, msg); \
				return 0; \
			} \

#define DRAGON_TEST_FOR_MISALIGNED_OR_BOUNDS_FAULT_W(size, msg) \
			if (addr + size > m_size) \
			{ \
				fault(addr, data::ExceptionCause::StoreBounds, msg); \
				return false; \
			} \
			if (addr & (size - 1)) \
			{ \
				fault(addr, data::ExceptionCause::MisalignedStore, msg); \
				return false; \
			} \

namespace dragon
{
	namespace hw
	{
		bool RAM::guest_cas_u32(AddressType addr, u32 expected, u32& actual, u32 desired)
		{
			DRAGON_TEST_FOR_MISALIGNED_OR_BOUNDS_FAULT_R(4, "RAM::guest_cas_u32");

			u32 shift = (addr & 4) * 8;
			u64 mask = u64(0xFFFFFFFFu) << shift;
			std::atomic<u64>& word = m_words[addr >> 3];

			u64 word_expected = word.load(std::memory_order_seq_cst);
			while (true)
			{
				u32 current_in_slot = static_cast<u32>(word_expected >> shift);
				if (current_in_slot != expected)
				{
					// CAS fails: report what we saw, don't modify
					actual = current_in_slot;
					return false;
				}
				// CAS could succeed: try to update
				u64 word_desired = (word_expected & ~mask) | (u64(desired) << shift);
				if (word.compare_exchange_weak(word_expected, word_desired, std::memory_order_seq_cst, std::memory_order_seq_cst))
				{
					actual = expected;  // tell caller we succeeded
					return true;
				}
				// CAS spuriously failed or someone else changed the word; word_expected
				// is updated. Loop and re-check.
			}
		}

		u64 RAM::load_u64(AddressType addr) const
		{
			DRAGON_TEST_FOR_MISALIGNED_OR_BOUNDS_FAULT_R(8, "RAM::load_u64");
			return m_words[addr >> 3].load(std::memory_order_seq_cst);
		}

		u32 RAM::load_u32(AddressType addr) const
		{
			DRAGON_TEST_FOR_MISALIGNED_OR_BOUNDS_FAULT_R(4, "RAM::load_u32");
			u64 word = m_words[addr >> 3].load(std::memory_order_seq_cst);
			u32 shift = (addr & 4) * 8;     // 0 or 32
			return static_cast<u32>(word >> shift);
		}

		u16 RAM::load_u16(AddressType addr) const
		{
			DRAGON_TEST_FOR_MISALIGNED_OR_BOUNDS_FAULT_R(2, "RAM::load_u16");
			u64 word = m_words[addr >> 3].load(std::memory_order_seq_cst);
			u32 shift = (addr & 6) * 8;     // 0, 16, 32, or 48
			return static_cast<u16>(word >> shift);
		}

		u8 RAM::load_u8(AddressType addr) const
		{
			if (addr >= m_size)
			{
				fault(addr, data::ExceptionCause::LoadBounds, "RAM::load_u8");
				return 0;
			}
			u64 word = m_words[addr >> 3].load(std::memory_order_seq_cst);
			u32 shift = (addr & 7) * 8;
			return static_cast<u8>(word >> shift);
		}

		bool RAM::store_u64(AddressType addr, u64 value)
		{
			DRAGON_TEST_FOR_MISALIGNED_OR_BOUNDS_FAULT_W(8, "RAM::store_u64");
			m_words[addr >> 3].store(value, std::memory_order_seq_cst);
			return true;
		}

		bool RAM::store_u32(AddressType addr, u32 value)
		{
			DRAGON_TEST_FOR_MISALIGNED_OR_BOUNDS_FAULT_W(4, "RAM::store_u32");
			u32 shift = (addr & 4) * 8;
			u64 mask = u64(0xFFFFFFFFu) << shift;
			std::atomic<u64>& word = m_words[addr >> 3];
			u64 expected = word.load(std::memory_order_relaxed);
			u64 desired;
			do
			{
				desired = (expected & ~mask) | (u64(value) << shift);
			} while (!word.compare_exchange_weak(expected, desired, std::memory_order_seq_cst, std::memory_order_relaxed));
			return true;
		}

		bool RAM::store_u16(AddressType addr, u16 value)
		{
			DRAGON_TEST_FOR_MISALIGNED_OR_BOUNDS_FAULT_W(2, "RAM::store_u16");
			u32 shift = (addr & 6) * 8;
			u64 mask = u64(0xFFFFu) << shift;
			std::atomic<u64>& word = m_words[addr >> 3];
			u64 expected = word.load(std::memory_order_relaxed);
			u64 desired;
			do
			{
				desired = (expected & ~mask) | (u64(value) << shift);
			} while (!word.compare_exchange_weak(expected, desired, std::memory_order_seq_cst, std::memory_order_relaxed));
			return true;
		}

		bool RAM::store_u8(AddressType addr, u8 value)
		{
			if (addr >= m_size)
			{
				fault(addr, data::ExceptionCause::StoreBounds, "RAM::store_u8");
				return false;
			}
			u32 shift = (addr & 7) * 8;
			u64 mask = u64(0xFFu) << shift;
			std::atomic<u64>& word = m_words[addr >> 3];
			u64 expected = word.load(std::memory_order_relaxed);
			u64 desired;
			do
			{
				desired = (expected & ~mask) | (u64(value) << shift);
			} while (!word.compare_exchange_weak(expected, desired, std::memory_order_seq_cst, std::memory_order_relaxed));
			return true;
		}
	}
}
