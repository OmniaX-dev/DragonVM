#pragma once

#include "DfsCore.hpp"
#include "DfsCheck.hpp"

namespace dfs
{
	// ============================================================================
	// Resizing — grows or shrinks a file system to a new partition size (host
	// side: uses the standard library, not part of the portable core).
	//
	// The layout is a function of the block count (other/info/dfs.md): the block
	// bitmap has one bit per block, then come the inode table and the data. So:
	//   - while the bitmap keeps its length, only the block count changes (plus the
	//     bitmap's bits past the end);
	//   - when it needs more (or fewer) blocks, the inode table moves after it, and
	//     the data blocks in the way are moved first (growing);
	//   - shrinking moves the data blocks past the new end into free blocks below it.
	// The inode count never changes (it was chosen by mkfs).
	//
	// Not crash safe: an interrupted resize leaves a broken file system. The file
	// system must be clean (fsck) before; it's checked again after.
	// ============================================================================
	struct ResizeInfo
	{
		u32 block_size { 0 };
		u32 old_blocks { 0 };
		u32 new_blocks { 0 };
		u32 used_blocks { 0 };		// data blocks in use (directories, files, pointer blocks)
		u32 new_free_blocks { 0 };
		u32 moved_blocks { 0 };		// data blocks that have to move
		bool table_moves { false };	// the block bitmap changes length: the inode table moves
		u32 min_size { 0 };			// the smallest partition the file system fits in (bytes)
		u32 max_size { 0 };			// the largest (bytes)
	};

	// What resizing the mounted volume (read only is fine) to new_partition_size
	// would do. Ok, NoSpace (smaller than info.min_size), or the error that stopped it.
	i32 resize_plan(Volume* vol, u32 new_partition_size, ResizeInfo& info);

	// Resizes the file system on dev to new_partition_size. The device must cover
	// both sizes (shrinking: before the partition shrinks; growing: after it grew).
	// vol is a workspace, left unmounted. Returns Ok, Corrupt (fsck found problems:
	// see check), NoSpace (too small), IOError... check gets the last fsck's result
	// (before, or after the resize).
	i32 resize(Volume* vol, const Device* dev, u32 new_partition_size, ResizeInfo* info = nullptr, FsckResult* check = nullptr);
}
