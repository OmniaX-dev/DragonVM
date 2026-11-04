#pragma once

#include "PartitionTable.hpp"

namespace dragon
{
	namespace storage
	{
		// ============================================================================
		// DPT — the Dragon Partition Table (layout in data::DPTStructure).
		//
		//   0x000000 - 0x3FFFFF   boot area (4 MiB): the BIOS loads it at the RAM start
		//                         and jumps to it (the boot loader)
		//   0x400000 - 0x4001FF   the DPT block:
		//       header (12 bytes): ID 0xF1CA, version (major, minor), partition
		//                          count (max 5), 7 reserved bytes
		//       5 entries (100 bytes each): start (4), size (4), flags (2),
		//                          26 reserved bytes, label (64, null terminated)
		//   0x400200 -            the partitions
		//
		// Everything is big endian.
		// ============================================================================
		class DPTScheme : public PartitionScheme
		{
			public:
				inline String getName(void) const override { return "DPT"; }
				inline String getDescription(void) const override { return "Dragon Partition Table"; }
				bool detect(Disk& disk) const override;
				bool read(Disk& disk, tPartitionTable& outTable, String& outError) const override;
				bool write(Disk& disk, const tPartitionTable& table, String& outError) const override;

				u32 getMaxPartitions(void) const override;
				u32 getMaxLabelLength(void) const override;
				u64 getMinDiskSize(void) const override;
				inline u64 getMaxAddress(void) const override { return 0x100000000ull; }
				u64 getFirstUsableAddress(u64 diskSize) const override;
				stdvec<tReservedArea> getReservedAreas(u64 diskSize) const override;
				stdvec<tPartitionFlag> getFlags(void) const override;

				// The 512 bytes of the block.
				stdvec<u8> encode(const tPartitionTable& table) const;
		};
	}
}
