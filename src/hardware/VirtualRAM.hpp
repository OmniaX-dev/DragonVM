#pragma once

#include "IMemoryDevice.hpp"

namespace dragon
{
	namespace hw
	{
		// The RAM. It is mapped remapped (address 0 is its first byte); the CPU's offset
		// register is added to every access, when offset addressing is enabled.
		class VirtualRAM : public IMemoryDevice
		{
			public:
				VirtualRAM(void);
				// Allocates (and zeroes) `size` bytes.
				void init(u32 size);
				i8 read8(u32 addr) override;
				i16 read16(u32 addr) override;
				i32 read32(u32 addr) override;
				i8 write8(u32 addr, i8 value) override;
				i16 write16(u32 addr, i16 value) override;
				i32 write32(u32 addr, i32 value) override;

				inline u32 getSize(void) const { return cast<u32>(m_memory.size()); }
				ostd::ByteStream* getByteStream(void) override;
				inline u32 streamAddress(u32 addr) const override { return final_address(addr); }
				inline void syncByteStream(void) override { m_dirty.markAll(); }
				inline DirtyPageMap* getDirtyPages(void) override { return &m_dirty; }

			private:
				u32 final_address(u32 addr) const;

			private:
				ostd::ByteStream m_memory;
				DirtyPageMap m_dirty;
		};
	}
}
