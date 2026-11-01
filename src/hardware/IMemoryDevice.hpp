#pragma once

#include <ostd/data/Object.hpp>
#include <vector>

namespace dragon
{
	namespace hw
	{
		// Pages of a device's memory written since the last clear() (the debugger's history
		// diffs only these, instead of the whole RAM, every cycle).
		class DirtyPageMap
		{
			public:
				inline static constexpr u32 PageShift = 8;	// 256-byte pages
				inline static constexpr u32 PageSize = 1u << PageShift;

			public:
				inline void resize(u64 bytes)
				{
					m_pageCount = cast<u32>((bytes + PageSize - 1) >> PageShift);
					m_bits.assign((m_pageCount + 63) / 64, ~0ull);
					m_any = true;
				}
				inline void mark(u64 address)
				{
					const u64 page = address >> PageShift;
					if (page >= m_pageCount) return;
					m_bits[page >> 6] |= (1ull << (page & 63));
					m_any = true;
				}
				inline void markRange(u64 address, u64 size)
				{
					if (size == 0) return;
					for (u64 page = address >> PageShift; page <= ((address + size - 1) >> PageShift) && page < m_pageCount; page++)
						m_bits[page >> 6] |= (1ull << (page & 63));
					m_any = true;
				}
				inline void markAll(void) { for (auto& word : m_bits) word = ~0ull; m_any = true; }
				inline void clear(void) { if (!m_any) return; for (auto& word : m_bits) word = 0; m_any = false; }
				inline bool any(void) const { return m_any; }
				inline u32 getPageCount(void) const { return m_pageCount; }
				// Calls f(firstByte, byteCount) for every run of dirty pages, in address order.
				template<typename F> inline void forEachRun(F&& f) const
				{
					if (!m_any) return;
					u32 page = 0;
					while (page < m_pageCount)
					{
						const u64 word = m_bits[page >> 6] >> (page & 63);
						if (word == 0) { page = (page | 63) + 1; continue; }
						if ((word & 1) == 0) { page += cast<u32>(__builtin_ctzll(word)); continue; }
						const u32 start = page;
						while (page < m_pageCount && (m_bits[page >> 6] & (1ull << (page & 63))) != 0)
							page++;
						f(cast<u64>(start) << PageShift, cast<u64>(page - start) << PageShift);
					}
				}

			private:
				std::vector<u64> m_bits;
				u32 m_pageCount { 0 };
				bool m_any { true };
		};

		// A device on the memory bus. Addresses are relative to the device's region if the
		// region is remapped, absolute otherwise. Words are 32 bits, big endian; read32() and
		// write32() default to four byte accesses.
		class IMemoryDevice : public ostd::Object
		{
			public:
				virtual i8 read8(u32 addr) = 0;
				virtual i8 write8(u32 addr, i8 value) = 0;
				inline virtual i32 read32(u32 addr)
				{
					return cast<i32>((cast<u32>(cast<u8>(read8(addr))) << 24)
								   | (cast<u32>(cast<u8>(read8(addr + 1))) << 16)
								   | (cast<u32>(cast<u8>(read8(addr + 2))) << 8)
								   |  cast<u32>(cast<u8>(read8(addr + 3))));
				}
				inline virtual i32 write32(u32 addr, i32 value)
				{
					const u32 v = cast<u32>(value);
					write8(addr, cast<i8>(v >> 24));
					write8(addr + 1, cast<i8>(v >> 16));
					write8(addr + 2, cast<i8>(v >> 8));
					write8(addr + 3, cast<i8>(v));
					return value;
				}
				virtual inline ostd::ByteStream* getByteStream(void) = 0;
				// Called after the ByteStream returned by getByteStream() was modified from the
				// outside (e.g. by the debugger): devices with state derived from it update it.
				inline virtual void syncByteStream(void) {  }
				// Devices with a big memory track the pages written (nullptr: not tracked).
				inline virtual DirtyPageMap* getDirtyPages(void) { return nullptr; }
		};

		// Helpers for devices backed by a ByteStream (big endian, bounds checked).
		namespace bytes
		{
			inline bool read32(const ostd::ByteStream& data, u64 addr, i32& out)
			{
				if (addr + 4 > data.size()) return false;
				out = cast<i32>((cast<u32>(cast<u8>(data[addr])) << 24) | (cast<u32>(cast<u8>(data[addr + 1])) << 16)
							  | (cast<u32>(cast<u8>(data[addr + 2])) << 8) | cast<u32>(cast<u8>(data[addr + 3])));
				return true;
			}
			inline bool write32(ostd::ByteStream& data, u64 addr, i32 value)
			{
				if (addr + 4 > data.size()) return false;
				const u32 v = cast<u32>(value);
				data[addr] = cast<i8>(v >> 24);
				data[addr + 1] = cast<i8>(v >> 16);
				data[addr + 2] = cast<i8>(v >> 8);
				data[addr + 3] = cast<i8>(v);
				return true;
			}
		}
	}
}
