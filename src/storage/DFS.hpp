#pragma once

#include "FileSystem.hpp"
#include "dfs/DfsCore.hpp"
#include "dfs/DfsCheck.hpp"

namespace dragon
{
	namespace storage
	{
		// ============================================================================
		// DFS — the Dragon File System (the core is in dfs/DfsCore.hpp, the format in
		// other/info/dfs.md). This class lets the Partition Manager detect it, and
		// makes a DFS device out of a partition.
		// ============================================================================
		class DFS : public FileSystem
		{
			public:
				inline String getName(void) const override { return "DFS"; }
				bool detect(Disk& partition) const override;

				// A device on the partition (address 0: the partition's start). The disk
				// must outlive the device.
				static dfs::Device makeDevice(Disk& partition);
		};
	}
}
