#pragma once

#include "DfsCore.hpp"
#include <string>
#include <vector>

namespace dfs
{
	// ============================================================================
	// fsck — checks a mounted volume, read only (host side: uses the standard
	// library, not part of the portable core).
	//
	// It walks the tree from the root, marking every reachable inode and block in
	// "seen" maps (one byte per block / inode: a 128 MiB partition with 4 KiB blocks
	// needs 32 KiB), then compares them with the bitmaps. It checks:
	//   - every reachable block and inode is marked used, no block has two owners,
	//     no block is mapped past the end of its file, directories have no holes
	//   - marked but unreachable blocks and inodes (leaks)
	//   - link counts (files: the entries pointing to them; directories: 2 + their
	//     subdirectories), no directory has two parents
	//   - every directory has . (itself) and .. (its parent), entries are valid
	//   - the superblock's free counters match the bitmaps
	// ============================================================================
	struct FsckResult
	{
		u32 errors { 0 };
		u32 leaked_blocks { 0 };
		u32 leaked_inodes { 0 };
		u32 files { 0 };
		u32 directories { 0 };
		std::vector<std::string> messages;	// one per problem (the first 200)

		inline bool clean(void) const { return errors == 0 && leaked_blocks == 0 && leaked_inodes == 0; }
	};

	// Ok (whatever it found, see result), or the error that stopped it (e.g. IOError).
	i32 fsck(Volume* vol, FsckResult& result);
}
