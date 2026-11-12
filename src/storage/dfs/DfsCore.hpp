#pragma once

// ============================================================================
// DFS — the Dragon File System: the portable core.
//
// A simplified ext2: superblock, inode bitmap, block bitmap, inode table, data
// blocks. Inodes have 12 direct pointers, a single-indirect and a double-
// indirect one. Directories are files of fixed size entries.
//
// The format is documented in other/info/dfs.md.
//
// This file and DfsCore.cpp are written in the subset of C++ that ports to
// DScript (the SDK's dfs.ds) almost line by line:
//   - no standard library, no heap, no exceptions, no templates, no virtuals,
//     no references, no 64 bit integers, no floating point
//   - every fallible function returns an i32: >= 0 success (or a value), < 0 an
//     Error code; struct results go through out pointers
//   - the device is a struct of sub pointers
//   - on disk integers are big endian (like the machine) and go through
//     get16/get32/put16/put32, which are plain loads/stores on the VM
// ============================================================================

namespace dfs
{
	typedef unsigned char u8;
	typedef unsigned short u16;
	typedef unsigned int u32;
	typedef int i32;

	enum Error : i32
	{
		Ok = 0,
		NotFound = -1,
		Exists = -2,
		NoSpace = -3,
		NotDir = -4,
		IsDir = -5,
		NotEmpty = -6,
		Invalid = -7,
		IOError = -8,
		Corrupt = -9,
		TooBig = -10,
		NameTooLong = -11,
		ReadOnly = -12,
		Unsupported = -13,
		TooManyLinks = -14
	};

	// ---- Fixed format constants ----
	const u32 Magic = 0x44465331;			// 'DFS1'
	const u32 Version = 1;
	const u32 BootHeaderSize = 0x20;		// the start of the partition, never written by DFS (DPT boot header)
	const u32 SuperblockOffset = 0x20;		// fixed byte offset, whatever the block size
	const u32 SuperblockSize = 256;
	const u32 InodeSize = 128;				// what mkfs writes (mount uses the superblock's)
	const u32 DirEntrySize = 64;			// same
	const u32 DirectPointers = 12;
	const u32 RootInode = 1;
	const u32 FirstInode = 8;				// 0 is null, 1 the root, 2-7 reserved (2: future journal)
	const u32 MinBlockSizeLog2 = 9;			// 512 B
	const u32 MaxBlockSizeLog2 = 12;		// 4 KiB
	const u32 MaxBlockSize = 4096;
	const u32 MaxNameLength = 56;
	const u32 StateClean = 1;
	const u32 StateDirty = 2;

	// Inode modes (the Unix encoding: the type in the high 4 bits, permissions in the low 12).
	const u16 ModeTypeMask = 0xF000;
	const u16 ModeFile = 0x8000;
	const u16 ModeDir = 0x4000;
	const u16 ModeCharDevice = 0x2000;
	const u16 ModeSymlink = 0xA000;			// reserved, not implemented
	const u16 ModePermMask = 0x0FFF;

	// Directory entry: inode (u32), name length (u8), type (u8, mode >> 12), reserved (u16), name (56).
	const u32 DirEntInode = 0;
	const u32 DirEntNameLen = 4;
	const u32 DirEntType = 5;
	const u32 DirEntName = 8;

	// ---- The device: partition relative byte offsets (the backend adds the partition's start) ----
	struct Device
	{
		void* user;
		i32 (*read)(void* user, u32 offset, u8* buffer, u32 size);			// Ok or IOError
		i32 (*write)(void* user, u32 offset, const u8* buffer, u32 size);	// Ok or IOError
		u32 (*now)(void* user);												// seconds (any epoch), may be null
	};

	// ---- On disk structures (in memory: host byte order) ----
	struct Superblock			// 256 bytes on disk, at SuperblockOffset
	{
		u32 magic;
		u32 version;
		u32 block_size_log2;
		u32 total_blocks;
		u32 inode_count;
		u32 inode_size;
		u32 dirent_size;
		u32 inode_bitmap_block, inode_bitmap_len;
		u32 block_bitmap_block, block_bitmap_len;
		u32 inode_table_block, inode_table_len;
		u32 first_data_block;
		u32 root_inode;
		u32 first_inode;
		u32 free_blocks, free_inodes;
		u32 feature_compat;			// unknown bits: ignored
		u32 feature_ro_compat;		// unknown bits: mounted read only
		u32 feature_incompat;		// unknown bits: not mounted
		u32 state;					// StateClean / StateDirty
		u32 mount_count;
		u32 created, last_mount, last_write;
		u32 journal_inode;			// 0 (reserved)
		u32 checksum;				// 0 (reserved)
		u8 uuid[16];
		u8 label[16];				// not null terminated when 16 long
		u8 reserved[112];
	};
	const u32 SuperblockWords = 28;	// the u32 fields before uuid
	static_assert(sizeof(Superblock) == SuperblockSize, "DFS: superblock size");

	struct Inode				// 128 bytes on disk
	{
		u16 mode;				// 0x00
		u16 links;				// 0x02
		u16 uid;				// 0x04
		u16 gid;				// 0x06
		u32 flags;				// 0x08
		u32 size;				// 0x0C
		u32 reserved0;			// 0x10  reserved (would be size_hi)
		u32 atime;				// 0x14  set at creation only (reads don't write the inode)
		u32 mtime;				// 0x18
		u32 ctime;				// 0x1C
		u32 crtime;				// 0x20
		u32 direct[12];			// 0x24
		u32 indirect;			// 0x54
		u32 dindirect;			// 0x58
		u32 reserved1;			// 0x5C  reserved (would be tindirect)
		u32 xattr;				// 0x60  reserved
		u32 checksum;			// 0x64  reserved
		u8 reserved[24];		// 0x68
	};
	const u32 InodeHalfWords = 4;	// the u16 fields at the start
	const u32 InodeWords = 24;		// the u32 fields after them
	static_assert(sizeof(Inode) == InodeSize, "DFS: inode size");

	struct DirEntry				// a decoded directory entry
	{
		u32 inode;
		u8 type;
		u8 name_len;
		u8 name[MaxNameLength + 1];		// null terminated
	};

	struct Layout
	{
		u32 block_size_log2;
		u32 block_size;
		u32 block_mask;			// block_size - 1
		u32 total_blocks;
		u32 inode_count;
		u32 inode_size;
		u32 dirent_size;
		u32 inodes_per_block;
		u32 pointers_per_block;
		u32 dirents_per_block;
		u32 bits_per_block;
		u32 inode_bitmap_block, inode_bitmap_len;
		u32 block_bitmap_block, block_bitmap_len;
		u32 inode_table_block, inode_table_len;
		u32 first_data_block;
	};

	struct MkfsParams
	{
		u32 partition_size;		// bytes
		u32 block_size_log2;	// 9..12 (12 by default)
		u32 bytes_per_inode;	// 16384 by default
		const u8* label;		// null terminated, may be null
		const u8* uuid;			// 16 bytes, may be null
	};

	// The mounted volume. All the memory DFS needs is in here: the block buffers
	// have a role each (which functions use which is noted on the functions).
	struct Volume
	{
		Device dev;
		Layout layout;
		Superblock sb;
		u32 block_hint;				// where the next block search starts (RAM only)
		u32 inode_hint;
		u32 ind_block;				// the pointer block buf_ind holds (0: none), see read_pointer_block
		u32 ind2_block;				// the double indirect block buf_ind2 holds (0: none)

		u8 buf_meta[MaxBlockSize];	// bitmaps, superblock (allocators)
		u8 buf_inode[MaxBlockSize];	// inode table blocks (read_inode / write_inode)
		u8 buf_ind[MaxBlockSize];	// a pointer block (bmap, truncate): cached, ind_block says which
		u8 buf_ind2[MaxBlockSize];	// a double indirect block (bmap, truncate): cached, ind2_block says which
		u8 buf_data[MaxBlockSize];	// partial data blocks (inode_read / inode_write), truncate's scratch
		u8 buf_dir[MaxBlockSize];	// the directory block being looked at (dir_*)
		u8 buf_zero[MaxBlockSize];	// always zero

		// Last, as in dfs.ds (where structs are packed and the buffers must stay 4 byte aligned).
		bool read_only;
		bool mounted;
		u16 uid, gid;				// given to new inodes
	};

	// ---- Helpers ----
	void mem_copy(void* dst, const void* src, u32 size);
	void mem_set(void* dst, u8 value, u32 size);
	u32 str_len(const u8* str);
	u16 get16(const u8* p);
	u32 get32(const u8* p);
	void put16(u8* p, u16 value);
	void put32(u8* p, u32 value);

	// ---- Layout ----
	i32 layout_from_params(Layout* out, u32 partition_size, u32 block_size_log2, u32 bytes_per_inode);
	i32 layout_from_superblock(Layout* out, const Superblock* sb);

	// ---- L1: volume ----
	// mkfs formats the partition (and leaves vol unmounted; vol is only its workspace).
	i32 mkfs(Volume* vol, const Device* dev, const MkfsParams* params);
	i32 mount(Volume* vol, const Device* dev, bool read_only);
	i32 unmount(Volume* vol);
	i32 sync(Volume* vol);
	u32 now(Volume* vol);

	// ---- L2: allocators (buf_meta) ----
	i32 alloc_block(Volume* vol, u32* out_block);
	i32 free_block(Volume* vol, u32 block);
	i32 alloc_inode(Volume* vol, u32* out_ino);
	i32 free_inode(Volume* vol, u32 ino);

	// ---- L3: inodes ----
	i32 read_inode(Volume* vol, u32 ino, Inode* out);				// buf_inode
	i32 write_inode(Volume* vol, u32 ino, const Inode* node);		// buf_inode
	// The disk block of the file's block index, 0 for a hole (when not allocating).
	// Returns 1 when the data block was just allocated (its content is garbage),
	// 0 otherwise. May change node's pointers: the caller writes the inode.
	i32 bmap(Volume* vol, Inode* node, u32 file_block, bool allocate, u32* out_block);	// buf_ind, buf_ind2, buf_zero, buf_meta
	// Frees the blocks after new_size (pointer blocks included) and writes the inode.
	i32 truncate(Volume* vol, u32 ino, Inode* node, u32 new_size);	// buf_ind, buf_ind2, buf_data, buf_meta, buf_inode

	// ---- L4: inode data ----
	// Bytes read (stops at the size; holes read as zeros).
	i32 inode_read(Volume* vol, Inode* node, u32 offset, u8* buffer, u32 size);	// buf_data, buf_ind, buf_ind2
	// Bytes written (fewer when the disk fills up); grows the size and writes the inode.
	i32 inode_write(Volume* vol, u32 ino, Inode* node, u32 offset, const u8* buffer, u32 size);	// buf_data, buf_ind, buf_ind2, buf_zero, buf_meta, buf_inode

	// ---- L5: directories (buf_dir, plus what inode_write uses) ----
	i32 dir_lookup(Volume* vol, Inode* dir, const u8* name, u32 name_len, u32* out_ino);
	i32 dir_add(Volume* vol, u32 dir_ino, Inode* dir, const u8* name, u32 name_len, u32 ino, u16 mode);
	i32 dir_remove(Volume* vol, u32 dir_ino, Inode* dir, const u8* name, u32 name_len, u32* out_ino);
	// Fills out with the entry at or after *cursor (start at 0) and moves the cursor past it. NotFound at the end.
	i32 dir_next(Volume* vol, Inode* dir, u32* cursor, DirEntry* out);
	i32 dir_is_empty(Volume* vol, Inode* dir);		// 1 when only . and .. are left, 0 if not

	// ---- L6: paths (absolute only; repeated '/' are collapsed) ----
	i32 resolve(Volume* vol, const u8* path, u32* out_ino);
	// The parent directory of the path's last component, which is returned in out_name / out_name_len.
	i32 resolve_parent(Volume* vol, const u8* path, u32* out_parent, const u8** out_name, u32* out_name_len);

	// ---- L7: file system operations ----
	i32 create(Volume* vol, const u8* path, u16 perm, u32* out_ino);
	i32 mkdir(Volume* vol, const u8* path, u16 perm, u32* out_ino);
	i32 unlink(Volume* vol, const u8* path);
	i32 rmdir(Volume* vol, const u8* path);
	i32 rename(Volume* vol, const u8* from, const u8* to);
	i32 link(Volume* vol, const u8* existing, const u8* new_path);
	i32 stat(Volume* vol, const u8* path, Inode* out, u32* out_ino);
	i32 chmod(Volume* vol, const u8* path, u16 perm);
	i32 chown(Volume* vol, const u8* path, u16 uid, u16 gid);

	// The error's description ("not found").
	const char* error_name(i32 error);
}
