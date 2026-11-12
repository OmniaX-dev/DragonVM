#include "DfsCore.hpp"

// The portable core (see DfsCore.hpp for the rules it follows).

namespace dfs
{
	// ============================================================================
	// Helpers
	// ============================================================================
	void mem_copy(void* dst, const void* src, u32 size)
	{
		u8* d = (u8*)dst;
		const u8* s = (const u8*)src;
		for (u32 i = 0; i < size; i++)
			d[i] = s[i];
	}

	void mem_set(void* dst, u8 value, u32 size)
	{
		u8* d = (u8*)dst;
		for (u32 i = 0; i < size; i++)
			d[i] = value;
	}

	static bool mem_equal(const u8* a, const u8* b, u32 size)
	{
		for (u32 i = 0; i < size; i++)
		{
			if (a[i] != b[i])
				return false;
		}
		return true;
	}

	u32 str_len(const u8* str)
	{
		u32 len = 0;
		while (str[len] != 0)
			len++;
		return len;
	}

	u16 get16(const u8* p)
	{
		return (u16)(((u32)p[0] << 8) | (u32)p[1]);
	}

	u32 get32(const u8* p)
	{
		return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
	}

	void put16(u8* p, u16 value)
	{
		p[0] = (u8)(value >> 8);
		p[1] = (u8)value;
	}

	void put32(u8* p, u32 value)
	{
		p[0] = (u8)(value >> 24);
		p[1] = (u8)(value >> 16);
		p[2] = (u8)(value >> 8);
		p[3] = (u8)value;
	}

	static u32 div_up(u32 a, u32 b)
	{
		return a / b + (a % b != 0 ? 1 : 0);
	}

	// On the VM these are plain copies (the structs have the on-disk byte order).
	static void decode_superblock(Superblock* sb, const u8* raw)
	{
		u32* words = (u32*)sb;
		for (u32 i = 0; i < SuperblockWords; i++)
			words[i] = get32(raw + i * 4);
		mem_copy(sb->uuid, raw + SuperblockWords * 4, SuperblockSize - SuperblockWords * 4);
	}

	static void encode_superblock(u8* raw, const Superblock* sb)
	{
		const u32* words = (const u32*)sb;
		for (u32 i = 0; i < SuperblockWords; i++)
			put32(raw + i * 4, words[i]);
		mem_copy(raw + SuperblockWords * 4, sb->uuid, SuperblockSize - SuperblockWords * 4);
	}

	static void decode_inode(Inode* node, const u8* raw)
	{
		u16* halves = (u16*)node;
		for (u32 i = 0; i < InodeHalfWords; i++)
			halves[i] = get16(raw + i * 2);
		u32* words = (u32*)((u8*)node + InodeHalfWords * 2);
		for (u32 i = 0; i < InodeWords; i++)
			words[i] = get32(raw + InodeHalfWords * 2 + i * 4);
		const u32 head = InodeHalfWords * 2 + InodeWords * 4;
		mem_copy((u8*)node + head, raw + head, InodeSize - head);
	}

	static void encode_inode(u8* raw, const Inode* node)
	{
		const u16* halves = (const u16*)node;
		for (u32 i = 0; i < InodeHalfWords; i++)
			put16(raw + i * 2, halves[i]);
		const u32* words = (const u32*)((const u8*)node + InodeHalfWords * 2);
		for (u32 i = 0; i < InodeWords; i++)
			put32(raw + InodeHalfWords * 2 + i * 4, words[i]);
		const u32 head = InodeHalfWords * 2 + InodeWords * 4;
		mem_copy(raw + head, (const u8*)node + head, InodeSize - head);
	}

	static bool is_dir(const Inode* node)
	{
		return (node->mode & ModeTypeMask) == ModeDir;
	}

	static bool is_dot_name(const u8* name, u32 len)
	{
		return (len == 1 && name[0] == '.') || (len == 2 && name[0] == '.' && name[1] == '.');
	}

	const char* error_name(i32 error)
	{
		switch (error)
		{
			case Ok: return "ok";
			case NotFound: return "not found";
			case Exists: return "already exists";
			case NoSpace: return "no space left";
			case NotDir: return "not a directory";
			case IsDir: return "is a directory";
			case NotEmpty: return "directory not empty";
			case Invalid: return "invalid argument";
			case IOError: return "I/O error";
			case Corrupt: return "corrupted file system";
			case TooBig: return "file too big";
			case NameTooLong: return "name too long";
			case ReadOnly: return "read only file system";
			case Unsupported: return "unsupported file system";
			case TooManyLinks: return "too many links";
			default: return "unknown error";
		}
	}

	// ============================================================================
	// L0: device
	// ============================================================================
	u32 now(Volume* vol)
	{
		if (vol->dev.now == nullptr)
			return 0;
		return vol->dev.now(vol->dev.user);
	}

	static i32 dev_read(Volume* vol, u32 offset, u8* buffer, u32 size)
	{
		if (vol->dev.read(vol->dev.user, offset, buffer, size) < 0)
			return IOError;
		return Ok;
	}

	// Every write goes through here, so a read only volume can't write anything.
	static i32 dev_write(Volume* vol, u32 offset, const u8* buffer, u32 size)
	{
		if (vol->read_only)
			return ReadOnly;
		if (vol->dev.write(vol->dev.user, offset, buffer, size) < 0)
			return IOError;
		return Ok;
	}

	static i32 read_block(Volume* vol, u32 block, u8* buffer)
	{
		if (block == 0 || block >= vol->layout.total_blocks)
			return Corrupt;
		return dev_read(vol, block << vol->layout.block_size_log2, buffer, vol->layout.block_size);
	}

	// Every block write goes through here: a block the pointer caches hold, written from
	// another buffer (e.g. freed, then reused for data), is dropped from them.
	static i32 write_block(Volume* vol, u32 block, const u8* buffer)
	{
		if (block == 0 || block >= vol->layout.total_blocks)
			return Corrupt;
		if (block == vol->ind_block && buffer != vol->buf_ind)
			vol->ind_block = 0;
		if (block == vol->ind2_block && buffer != vol->buf_ind2)
			vol->ind2_block = 0;
		return dev_write(vol, block << vol->layout.block_size_log2, buffer, vol->layout.block_size);
	}

	// The pointer block caches: buf_ind (single indirect blocks and the blocks a double
	// indirect one points to) and buf_ind2 (double indirect blocks) keep the last block read,
	// so reading a file in order reads each pointer block once instead of once per data block.
	// They stay right because the blocks are only changed in these buffers (then written) or
	// through write_block, which drops them.
	static u8* read_pointer_block(Volume* vol, u32 block, bool dindirect, i32* out_result)
	{
		u8* buffer = (dindirect ? vol->buf_ind2 : vol->buf_ind);
		u32* cached = (dindirect ? &vol->ind2_block : &vol->ind_block);
		*out_result = Ok;
		if (*cached == block && block != 0)
			return buffer;
		*cached = 0;
		*out_result = read_block(vol, block, buffer);
		if (*out_result == Ok)
			*cached = block;
		return buffer;
	}

	// ============================================================================
	// Layout
	// ============================================================================
	static i32 layout_compute(Layout* l, u32 log2, u32 total_blocks, u32 inode_count, u32 inode_size, u32 dirent_size)
	{
		l->block_size_log2 = log2;
		l->block_size = 1u << log2;
		l->block_mask = l->block_size - 1;
		l->total_blocks = total_blocks;
		l->inode_count = inode_count;
		l->inode_size = inode_size;
		l->dirent_size = dirent_size;
		l->inodes_per_block = l->block_size / inode_size;
		l->pointers_per_block = l->block_size / 4;
		l->dirents_per_block = l->block_size / dirent_size;
		l->bits_per_block = l->block_size * 8;
		if (inode_count <= FirstInode || inode_count % l->inodes_per_block != 0)
			return Invalid;
		l->inode_bitmap_block = 1;
		l->inode_bitmap_len = div_up(inode_count, l->bits_per_block);
		l->block_bitmap_block = l->inode_bitmap_block + l->inode_bitmap_len;
		l->block_bitmap_len = div_up(total_blocks, l->bits_per_block);
		l->inode_table_block = l->block_bitmap_block + l->block_bitmap_len;
		l->inode_table_len = inode_count / l->inodes_per_block;
		l->first_data_block = l->inode_table_block + l->inode_table_len;
		if (l->first_data_block >= total_blocks || l->inode_table_len > total_blocks)
			return Invalid;
		return Ok;
	}

	i32 layout_from_params(Layout* out, u32 partition_size, u32 block_size_log2, u32 bytes_per_inode)
	{
		if (block_size_log2 < MinBlockSizeLog2 || block_size_log2 > MaxBlockSizeLog2 || bytes_per_inode < InodeSize)
			return Invalid;
		const u32 block_size = 1u << block_size_log2;
		const u32 inodes_per_block = block_size / InodeSize;
		u32 count = partition_size / bytes_per_inode;
		if (count < inodes_per_block)
			count = inodes_per_block;
		if (count <= FirstInode)
			count = FirstInode * 2;
		count = div_up(count, inodes_per_block) * inodes_per_block;
		return layout_compute(out, block_size_log2, partition_size >> block_size_log2, count, InodeSize, DirEntrySize);
	}

	i32 layout_from_superblock(Layout* out, const Superblock* sb)
	{
		if (sb->block_size_log2 < MinBlockSizeLog2 || sb->block_size_log2 > MaxBlockSizeLog2)
			return Corrupt;
		const u32 block_size = 1u << sb->block_size_log2;
		if (sb->inode_size < InodeSize || block_size % sb->inode_size != 0)
			return Corrupt;
		if (sb->dirent_size < DirEntrySize || block_size % sb->dirent_size != 0)
			return Corrupt;
		if (layout_compute(out, sb->block_size_log2, sb->total_blocks, sb->inode_count, sb->inode_size, sb->dirent_size) < 0)
			return Corrupt;
		if (out->inode_bitmap_block != sb->inode_bitmap_block || out->inode_bitmap_len != sb->inode_bitmap_len
				|| out->block_bitmap_block != sb->block_bitmap_block || out->block_bitmap_len != sb->block_bitmap_len
				|| out->inode_table_block != sb->inode_table_block || out->inode_table_len != sb->inode_table_len
				|| out->first_data_block != sb->first_data_block)
			return Corrupt;
		return Ok;
	}

	// ============================================================================
	// L2: allocators (buf_meta)
	// ============================================================================
	// Finds a clear bit (searching from *hint, wrapping around), sets it and writes the bitmap.
	static i32 bitmap_alloc(Volume* vol, u32 bitmap_block, u32 bitmap_len, u32 count, u32* hint, u32* out)
	{
		const u32 bits = vol->layout.bits_per_block;
		const u32 start = (*hint < count ? *hint : 0);
		const u32 first = start / bits;
		// One more round than there are blocks: the last one looks at the start of the first block.
		for (u32 k = 0; k <= bitmap_len; k++)
		{
			const u32 b = (first + k) % bitmap_len;
			i32 r = read_block(vol, bitmap_block + b, vol->buf_meta);
			if (r < 0)
				return r;
			const u32 first_byte = (k == 0 ? (start % bits) / 8 : 0);
			for (u32 i = first_byte; i < vol->layout.block_size; i++)
			{
				const u8 byte = vol->buf_meta[i];
				if (byte == 0xFF)
					continue;
				u32 bit = 0;
				while ((byte & (1u << bit)) != 0)
					bit++;
				const u32 n = b * bits + i * 8 + bit;
				if (n >= count)
					break;
				vol->buf_meta[i] = (u8)(byte | (1u << bit));
				r = write_block(vol, bitmap_block + b, vol->buf_meta);
				if (r < 0)
					return r;
				*hint = n + 1;
				*out = n;
				return Ok;
			}
		}
		return NoSpace;
	}

	static i32 bitmap_free(Volume* vol, u32 bitmap_block, u32 n)
	{
		const u32 block = bitmap_block + n / vol->layout.bits_per_block;
		const u32 bit = n % vol->layout.bits_per_block;
		i32 r = read_block(vol, block, vol->buf_meta);
		if (r < 0)
			return r;
		const u8 mask = (u8)(1u << (bit % 8));
		if ((vol->buf_meta[bit / 8] & mask) == 0)
			return Corrupt;		// already free
		vol->buf_meta[bit / 8] = (u8)(vol->buf_meta[bit / 8] & ~mask);
		return write_block(vol, block, vol->buf_meta);
	}

	// Sets the bits [from, to) (mkfs).
	static i32 bitmap_mark(Volume* vol, u32 bitmap_block, u32 from, u32 to)
	{
		const u32 bits = vol->layout.bits_per_block;
		u32 current = 0xFFFFFFFF;
		i32 r = Ok;
		for (u32 n = from; n < to; n++)
		{
			if (n / bits != current)
			{
				if (current != 0xFFFFFFFF && (r = write_block(vol, bitmap_block + current, vol->buf_meta)) < 0)
					return r;
				current = n / bits;
				if ((r = read_block(vol, bitmap_block + current, vol->buf_meta)) < 0)
					return r;
			}
			if ((n & 7) == 0 && to - n >= 8)
			{
				vol->buf_meta[(n % bits) / 8] = 0xFF;		// a whole byte at once
				n += 7;
				continue;
			}
			vol->buf_meta[(n % bits) / 8] = (u8)(vol->buf_meta[(n % bits) / 8] | (1u << (n % 8)));
		}
		if (current != 0xFFFFFFFF)
			r = write_block(vol, bitmap_block + current, vol->buf_meta);
		return r;
	}

	i32 alloc_block(Volume* vol, u32* out_block)
	{
		if (vol->sb.free_blocks == 0)
			return NoSpace;
		Layout* l = &vol->layout;
		i32 r = bitmap_alloc(vol, l->block_bitmap_block, l->block_bitmap_len, l->total_blocks, &vol->block_hint, out_block);
		if (r < 0)
			return r;
		if (*out_block < l->first_data_block)
			return Corrupt;		// mkfs marks the metadata blocks as used
		vol->sb.free_blocks--;
		return Ok;
	}

	i32 free_block(Volume* vol, u32 block)
	{
		if (block < vol->layout.first_data_block || block >= vol->layout.total_blocks)
			return Corrupt;
		i32 r = bitmap_free(vol, vol->layout.block_bitmap_block, block);
		if (r < 0)
			return r;
		vol->sb.free_blocks++;
		return Ok;
	}

	i32 alloc_inode(Volume* vol, u32* out_ino)
	{
		if (vol->sb.free_inodes == 0)
			return NoSpace;
		Layout* l = &vol->layout;
		i32 r = bitmap_alloc(vol, l->inode_bitmap_block, l->inode_bitmap_len, l->inode_count, &vol->inode_hint, out_ino);
		if (r < 0)
			return r;
		if (*out_ino < FirstInode)
			return Corrupt;		// mkfs marks the reserved inodes as used
		vol->sb.free_inodes--;
		return Ok;
	}

	i32 free_inode(Volume* vol, u32 ino)
	{
		if (ino < FirstInode || ino >= vol->layout.inode_count)
			return Corrupt;
		i32 r = bitmap_free(vol, vol->layout.inode_bitmap_block, ino);
		if (r < 0)
			return r;
		vol->sb.free_inodes++;
		return Ok;
	}

	static i32 alloc_zeroed_block(Volume* vol, u32* out_block)
	{
		i32 r = alloc_block(vol, out_block);
		if (r < 0)
			return r;
		r = write_block(vol, *out_block, vol->buf_zero);
		if (r < 0)
		{
			free_block(vol, *out_block);
			*out_block = 0;
		}
		return r;
	}

	// ============================================================================
	// L1: volume
	// ============================================================================
	static i32 write_superblock(Volume* vol)
	{
		encode_superblock(vol->buf_meta, &vol->sb);
		return dev_write(vol, SuperblockOffset, vol->buf_meta, SuperblockSize);
	}

	i32 mount(Volume* vol, const Device* dev, bool read_only)
	{
		vol->dev = *dev;
		vol->mounted = false;
		vol->read_only = true;
		vol->uid = 0;
		vol->gid = 0;
		vol->block_hint = 0;
		vol->inode_hint = 0;
		vol->ind_block = 0;
		vol->ind2_block = 0;
		mem_set(vol->buf_zero, 0, MaxBlockSize);
		i32 r = dev_read(vol, SuperblockOffset, vol->buf_meta, SuperblockSize);
		if (r < 0)
			return r;
		decode_superblock(&vol->sb, vol->buf_meta);
		Superblock* sb = &vol->sb;
		if (sb->magic != Magic)
			return Unsupported;
		if (sb->version != Version || sb->feature_incompat != 0)
			return Unsupported;
		r = layout_from_superblock(&vol->layout, sb);
		if (r < 0)
			return r;
		if (sb->root_inode != RootInode || sb->first_inode != FirstInode)
			return Corrupt;
		vol->read_only = (read_only || sb->feature_ro_compat != 0);
		vol->mounted = true;
		if (!vol->read_only)
		{
			sb->state = StateDirty;
			sb->mount_count++;
			sb->last_mount = now(vol);
			r = write_superblock(vol);
			if (r < 0)
			{
				vol->mounted = false;
				return r;
			}
		}
		return Ok;
	}

	i32 sync(Volume* vol)
	{
		if (!vol->mounted || vol->read_only)
			return Ok;
		vol->sb.last_write = now(vol);
		return write_superblock(vol);
	}

	i32 unmount(Volume* vol)
	{
		if (!vol->mounted)
			return Ok;
		i32 r = Ok;
		if (!vol->read_only)
		{
			vol->sb.state = StateClean;
			vol->sb.last_write = now(vol);
			r = write_superblock(vol);
		}
		vol->mounted = false;
		return r;
	}

	static void fill_dir_entry(u8* entry, u32 size, u32 ino, const u8* name, u32 name_len, u16 mode)
	{
		mem_set(entry, 0, size);
		put32(entry + DirEntInode, ino);
		entry[DirEntNameLen] = (u8)name_len;
		entry[DirEntType] = (u8)(mode >> 12);
		mem_copy(entry + DirEntName, name, name_len);
	}

	// A new directory's first block (. and ..) in buf_dir.
	static void make_dir_block(Volume* vol, u32 ino, u32 parent)
	{
		const u8 dots[2] = { '.', '.' };
		mem_set(vol->buf_dir, 0, vol->layout.block_size);
		fill_dir_entry(vol->buf_dir, vol->layout.dirent_size, ino, dots, 1, ModeDir);
		fill_dir_entry(vol->buf_dir + vol->layout.dirent_size, vol->layout.dirent_size, parent, dots, 2, ModeDir);
	}

	static void init_inode(Volume* vol, Inode* node, u16 mode, u16 links)
	{
		mem_set(node, 0, sizeof(Inode));
		node->mode = mode;
		node->links = links;
		node->uid = vol->uid;
		node->gid = vol->gid;
		const u32 t = now(vol);
		node->atime = t;
		node->mtime = t;
		node->ctime = t;
		node->crtime = t;
	}

	i32 mkfs(Volume* vol, const Device* dev, const MkfsParams* params)
	{
		vol->dev = *dev;
		vol->mounted = false;
		vol->read_only = false;
		vol->uid = 0;
		vol->gid = 0;
		vol->ind_block = 0;
		vol->ind2_block = 0;
		mem_set(vol->buf_zero, 0, MaxBlockSize);
		u32 log2 = (params->block_size_log2 != 0 ? params->block_size_log2 : 12);
		u32 bytes_per_inode = (params->bytes_per_inode != 0 ? params->bytes_per_inode : 16384);
		Layout* l = &vol->layout;
		i32 r = layout_from_params(l, params->partition_size, log2, bytes_per_inode);
		if (r < 0)
			return r;

		// Bitmaps and inode table: zeros, then the used bits (the reserved inodes, the
		// metadata blocks, and the bits past the end of the last bitmap block).
		for (u32 b = l->inode_bitmap_block; b < l->first_data_block; b++)
		{
			if ((r = write_block(vol, b, vol->buf_zero)) < 0)
				return r;
		}
		if ((r = bitmap_mark(vol, l->inode_bitmap_block, 0, FirstInode)) < 0)
			return r;
		if ((r = bitmap_mark(vol, l->inode_bitmap_block, l->inode_count, l->inode_bitmap_len * l->bits_per_block)) < 0)
			return r;
		if ((r = bitmap_mark(vol, l->block_bitmap_block, 0, l->first_data_block)) < 0)
			return r;
		if ((r = bitmap_mark(vol, l->block_bitmap_block, l->total_blocks, l->block_bitmap_len * l->bits_per_block)) < 0)
			return r;

		// Block 0: the boot header stays, the rest is the superblock and zeros.
		const u32 tail = SuperblockOffset + SuperblockSize;
		if ((r = dev_write(vol, tail, vol->buf_zero, l->block_size - tail)) < 0)
			return r;
		Superblock* sb = &vol->sb;
		mem_set(sb, 0, sizeof(Superblock));
		sb->magic = Magic;
		sb->version = Version;
		sb->block_size_log2 = l->block_size_log2;
		sb->total_blocks = l->total_blocks;
		sb->inode_count = l->inode_count;
		sb->inode_size = l->inode_size;
		sb->dirent_size = l->dirent_size;
		sb->inode_bitmap_block = l->inode_bitmap_block;
		sb->inode_bitmap_len = l->inode_bitmap_len;
		sb->block_bitmap_block = l->block_bitmap_block;
		sb->block_bitmap_len = l->block_bitmap_len;
		sb->inode_table_block = l->inode_table_block;
		sb->inode_table_len = l->inode_table_len;
		sb->first_data_block = l->first_data_block;
		sb->root_inode = RootInode;
		sb->first_inode = FirstInode;
		sb->free_blocks = l->total_blocks - l->first_data_block;
		sb->free_inodes = l->inode_count - FirstInode;
		sb->state = StateClean;
		sb->created = now(vol);
		if (params->label != nullptr)
		{
			for (u32 i = 0; i < 16 && params->label[i] != 0; i++)
				sb->label[i] = params->label[i];
		}
		if (params->uuid != nullptr)
			mem_copy(sb->uuid, params->uuid, 16);
		if ((r = write_superblock(vol)) < 0)
			return r;

		// The root directory (its inode is one of the reserved ones, already marked).
		if ((r = mount(vol, dev, false)) < 0)
			return r;
		Inode root;
		init_inode(vol, &root, (u16)(ModeDir | 0755), 2);
		make_dir_block(vol, RootInode, RootInode);
		r = inode_write(vol, RootInode, &root, 0, vol->buf_dir, l->block_size);
		const i32 r2 = unmount(vol);
		if (r < 0)
			return r;
		return r2;
	}

	// ============================================================================
	// L3: inodes
	// ============================================================================
	static i32 inode_location(Volume* vol, u32 ino, u32* out_block, u32* out_offset)
	{
		if (ino == 0 || ino >= vol->layout.inode_count)
			return Invalid;
		const u32 byte = ino * vol->layout.inode_size;
		*out_block = vol->layout.inode_table_block + (byte >> vol->layout.block_size_log2);
		*out_offset = byte & vol->layout.block_mask;
		return Ok;
	}

	i32 read_inode(Volume* vol, u32 ino, Inode* out)
	{
		u32 block = 0, offset = 0;
		i32 r = inode_location(vol, ino, &block, &offset);
		if (r < 0)
			return r;
		if ((r = read_block(vol, block, vol->buf_inode)) < 0)
			return r;
		decode_inode(out, vol->buf_inode + offset);
		return Ok;
	}

	i32 write_inode(Volume* vol, u32 ino, const Inode* node)
	{
		u32 block = 0, offset = 0;
		i32 r = inode_location(vol, ino, &block, &offset);
		if (r < 0)
			return r;
		if ((r = read_block(vol, block, vol->buf_inode)) < 0)
			return r;
		encode_inode(vol->buf_inode + offset, node);
		return write_block(vol, block, vol->buf_inode);
	}

	i32 bmap(Volume* vol, Inode* node, u32 file_block, bool allocate, u32* out_block)
	{
		const u32 P = vol->layout.pointers_per_block;
		*out_block = 0;
		i32 r = Ok;
		if (file_block < DirectPointers)
		{
			if (node->direct[file_block] != 0 || !allocate)
			{
				*out_block = node->direct[file_block];
				return 0;
			}
			if ((r = alloc_block(vol, &node->direct[file_block])) < 0)
				return r;
			*out_block = node->direct[file_block];
			return 1;
		}

		// The pointer blocks: the path from the inode to the data block.
		u32 index = file_block - DirectPointers;
		u32 levels = 1;
		u32* top = &node->indirect;
		u32 path[2];
		path[0] = index;
		if (index >= P)
		{
			index -= P;
			if (index / P >= P)
				return TooBig;
			levels = 2;
			top = &node->dindirect;
			path[0] = index / P;
			path[1] = index % P;
		}
		if (*top == 0)
		{
			if (!allocate)
				return 0;
			if ((r = alloc_zeroed_block(vol, top)) < 0)
				return r;
		}
		u32 block = *top;
		i32 fresh = 0;
		for (u32 level = 0; level < levels; level++)
		{
			// The double indirect block in buf_ind2, the block with the data pointers in buf_ind.
			u8* pointers = read_pointer_block(vol, block, level + 1 < levels, &r);
			if (r < 0)
				return r;
			u8* slot = pointers + path[level] * 4;
			u32 next = get32(slot);
			if (next == 0)
			{
				if (!allocate)
					return 0;
				if (level + 1 < levels)
					r = alloc_zeroed_block(vol, &next);
				else
				{
					r = alloc_block(vol, &next);
					fresh = 1;
				}
				if (r < 0)
					return r;
				put32(slot, next);
				if ((r = write_block(vol, block, pointers)) < 0)
					return r;
			}
			block = next;
		}
		*out_block = block;
		return fresh;
	}

	// Frees the entries from start of a pointer block of the given depth (1: its
	// entries are data blocks, 2: they are depth 1 pointer blocks). With start 0 the
	// block was already detached from its parent, and is freed too; otherwise it
	// stays, and the entries are cleared on disk before their blocks are freed.
	// depth 1: buf_ind (and buf_data when start > 0); depth 2: buf_ind2 too.
	static i32 free_tree(Volume* vol, u32 block, u32 depth, u32 start)
	{
		const u32 P = vol->layout.pointers_per_block;
		const u32 size = vol->layout.block_size;
		i32 r = Ok;
		u8* entries = nullptr;
		u32 first = start;
		if (depth == 1)
		{
			if (start >= P)
				return Ok;
			entries = read_pointer_block(vol, block, false, &r);
			if (r < 0)
				return r;
		}
		else
		{
			first = start / P;
			if (first >= P)
				return Ok;
			entries = read_pointer_block(vol, block, true, &r);
			if (r < 0)
				return r;
			if (start % P != 0)
			{
				// The first child keeps some of its entries.
				const u32 child = get32(entries + first * 4);
				if (child != 0 && (r = free_tree(vol, child, 1, start % P)) < 0)
					return r;
				first++;
			}
		}
		if (start > 0 && first < P)
		{
			// Detach the entries on disk, keeping a copy to free them from.
			mem_copy(vol->buf_data, entries, size);
			mem_set(entries + first * 4, 0, (P - first) * 4);
			if ((r = write_block(vol, block, entries)) < 0)
				return r;
			entries = vol->buf_data;
		}
		for (u32 i = first; i < P; i++)
		{
			const u32 child = get32(entries + i * 4);
			if (child == 0)
				continue;
			if (depth == 1)
				r = free_block(vol, child);
			else
				r = free_tree(vol, child, 1, 0);		// buf_ind only: buf_data (entries) stays
			if (r < 0)
				return r;
		}
		if (start == 0)
			r = free_block(vol, block);
		return r;
	}

	i32 truncate(Volume* vol, u32 ino, Inode* node, u32 new_size)
	{
		if (vol->read_only)
			return ReadOnly;
		const Layout* l = &vol->layout;
		const u32 P = l->pointers_per_block;
		i32 r = Ok;
		if (new_size >= node->size)
		{
			// Growing: the new bytes are a hole.
			node->size = new_size;
			node->mtime = now(vol);
			node->ctime = node->mtime;
			return write_inode(vol, ino, node);
		}
		const u32 keep = (new_size >> l->block_size_log2) + ((new_size & l->block_mask) != 0 ? 1 : 0);

		// Zero the tail of the last block kept (so growing again reads zeros).
		if ((new_size & l->block_mask) != 0)
		{
			u32 block = 0;
			if ((r = bmap(vol, node, new_size >> l->block_size_log2, false, &block)) < 0)
				return r;
			if (block != 0)
			{
				if ((r = read_block(vol, block, vol->buf_data)) < 0)
					return r;
				const u32 offset = new_size & l->block_mask;
				mem_set(vol->buf_data + offset, 0, l->block_size - offset);
				if ((r = write_block(vol, block, vol->buf_data)) < 0)
					return r;
			}
		}

		// Detach in the inode what goes entirely, write it, then free.
		u32 old_direct[DirectPointers];
		for (u32 i = 0; i < DirectPointers; i++)
		{
			old_direct[i] = 0;
			if (i >= keep)
			{
				old_direct[i] = node->direct[i];
				node->direct[i] = 0;
			}
		}
		u32 old_indirect = 0, old_dindirect = 0;
		if (keep <= DirectPointers)
		{
			old_indirect = node->indirect;
			node->indirect = 0;
		}
		if (keep <= DirectPointers + P)
		{
			old_dindirect = node->dindirect;
			node->dindirect = 0;
		}
		node->size = new_size;
		node->mtime = now(vol);
		node->ctime = node->mtime;
		if ((r = write_inode(vol, ino, node)) < 0)
			return r;

		for (u32 i = 0; i < DirectPointers; i++)
		{
			if (old_direct[i] != 0 && (r = free_block(vol, old_direct[i])) < 0)
				return r;
		}
		if (old_indirect != 0)
			r = free_tree(vol, old_indirect, 1, 0);
		else if (node->indirect != 0)
			r = free_tree(vol, node->indirect, 1, keep - DirectPointers);
		if (r < 0)
			return r;
		if (old_dindirect != 0)
			r = free_tree(vol, old_dindirect, 2, 0);
		else if (node->dindirect != 0)
			r = free_tree(vol, node->dindirect, 2, keep - DirectPointers - P);
		return r;
	}

	// ============================================================================
	// L4: inode data
	// ============================================================================
	i32 inode_read(Volume* vol, Inode* node, u32 offset, u8* buffer, u32 size)
	{
		const Layout* l = &vol->layout;
		if (offset >= node->size)
			return 0;
		if (size > node->size - offset)
			size = node->size - offset;
		if (size > 0x7FFFFFFF)
			size = 0x7FFFFFFF;
		u32 done = 0;
		while (done < size)
		{
			const u32 pos = offset + done;
			const u32 in_block = pos & l->block_mask;
			u32 n = l->block_size - in_block;
			if (n > size - done)
				n = size - done;
			u32 block = 0;
			i32 r = bmap(vol, node, pos >> l->block_size_log2, false, &block);
			if (r < 0)
				return r;
			if (block == 0)
				mem_set(buffer + done, 0, n);
			else if (n == l->block_size)
				r = read_block(vol, block, buffer + done);
			else
			{
				r = read_block(vol, block, vol->buf_data);
				mem_copy(buffer + done, vol->buf_data + in_block, n);
			}
			if (r < 0)
				return r;
			done += n;
		}
		return (i32)done;
	}

	i32 inode_write(Volume* vol, u32 ino, Inode* node, u32 offset, const u8* buffer, u32 size)
	{
		const Layout* l = &vol->layout;
		if (vol->read_only)
			return ReadOnly;
		if (size > 0x7FFFFFFF)
			return Invalid;
		if (offset + size < offset)
			return TooBig;
		u32 done = 0;
		i32 r = Ok;
		while (done < size)
		{
			const u32 pos = offset + done;
			const u32 in_block = pos & l->block_mask;
			u32 n = l->block_size - in_block;
			if (n > size - done)
				n = size - done;
			u32 block = 0;
			r = bmap(vol, node, pos >> l->block_size_log2, true, &block);
			if (r < 0)
				break;
			if (n == l->block_size)
				r = write_block(vol, block, buffer + done);
			else
			{
				if (r == 1)
					mem_set(vol->buf_data, 0, l->block_size);		// just allocated
				else if ((r = read_block(vol, block, vol->buf_data)) < 0)
					break;
				mem_copy(vol->buf_data + in_block, buffer + done, n);
				r = write_block(vol, block, vol->buf_data);
			}
			if (r < 0)
				break;
			r = Ok;
			done += n;
			if (pos + n > node->size)
				node->size = pos + n;
		}
		node->mtime = now(vol);
		node->ctime = node->mtime;
		const i32 w = write_inode(vol, ino, node);
		if (done == 0 && r < 0)
			return r;
		if (w < 0)
			return w;
		return (i32)done;
	}

	// ============================================================================
	// L5: directories (buf_dir)
	// ============================================================================
	const u32 MatchName = 0;
	const u32 MatchEmpty = 1;
	const u32 MatchUsed = 2;
	const u32 MatchNotDot = 3;		// used, and not . or ..

	// The one loop over a directory's entries: finds the first entry at or after
	// *index that matches. On success buf_dir holds its block, *index is the entry's
	// index, and out_block / out_offset say where it is.
	static i32 dir_find(Volume* vol, Inode* dir, u32* index, u32 match, const u8* name, u32 name_len, u32* out_block, u32* out_offset)
	{
		const Layout* l = &vol->layout;
		if ((dir->size & l->block_mask) != 0)
			return Corrupt;
		const u32 count = dir->size / l->dirent_size;
		u32 loaded = 0xFFFFFFFF;
		u32 block = 0;
		for (u32 i = *index; i < count; i++)
		{
			const u32 file_block = i / l->dirents_per_block;
			if (file_block != loaded)
			{
				i32 r = bmap(vol, dir, file_block, false, &block);
				if (r < 0)
					return r;
				if (block == 0)
					return Corrupt;		// directories have no holes
				if ((r = read_block(vol, block, vol->buf_dir)) < 0)
					return r;
				loaded = file_block;
			}
			const u32 offset = (i % l->dirents_per_block) * l->dirent_size;
			const u8* entry = vol->buf_dir + offset;
			const u32 ino = get32(entry + DirEntInode);
			const u32 len = entry[DirEntNameLen];
			bool hit = false;
			if (match == MatchEmpty)
				hit = (ino == 0);
			else if (ino == 0)
				hit = false;
			else if (match == MatchUsed)
				hit = true;
			else if (match == MatchNotDot)
				hit = !is_dot_name(entry + DirEntName, len);
			else
				hit = (len == name_len && mem_equal(entry + DirEntName, name, len));
			if (hit)
			{
				*index = i;
				*out_block = block;
				*out_offset = offset;
				return Ok;
			}
		}
		*index = count;
		return NotFound;
	}

	static i32 touch_dir(Volume* vol, u32 dir_ino, Inode* dir)
	{
		dir->mtime = now(vol);
		dir->ctime = dir->mtime;
		return write_inode(vol, dir_ino, dir);
	}

	i32 dir_lookup(Volume* vol, Inode* dir, const u8* name, u32 name_len, u32* out_ino)
	{
		u32 index = 0, block = 0, offset = 0;
		i32 r = dir_find(vol, dir, &index, MatchName, name, name_len, &block, &offset);
		if (r < 0)
			return r;
		*out_ino = get32(vol->buf_dir + offset + DirEntInode);
		return Ok;
	}

	i32 dir_add(Volume* vol, u32 dir_ino, Inode* dir, const u8* name, u32 name_len, u32 ino, u16 mode)
	{
		const Layout* l = &vol->layout;
		if (name_len == 0)
			return Invalid;
		if (name_len > MaxNameLength)
			return NameTooLong;
		for (u32 i = 0; i < name_len; i++)
		{
			if (name[i] == '/' || name[i] == 0)
				return Invalid;
		}
		u32 index = 0, block = 0, offset = 0;
		i32 r = dir_find(vol, dir, &index, MatchName, name, name_len, &block, &offset);
		if (r == Ok)
			return Exists;
		if (r != NotFound)
			return r;
		index = 0;
		r = dir_find(vol, dir, &index, MatchEmpty, name, name_len, &block, &offset);
		if (r == Ok)
		{
			fill_dir_entry(vol->buf_dir + offset, l->dirent_size, ino, name, name_len, mode);
			if ((r = write_block(vol, block, vol->buf_dir)) < 0)
				return r;
			return touch_dir(vol, dir_ino, dir);
		}
		if (r != NotFound)
			return r;
		// Full: append a block (inode_write writes the inode).
		mem_set(vol->buf_dir, 0, l->block_size);
		fill_dir_entry(vol->buf_dir, l->dirent_size, ino, name, name_len, mode);
		r = inode_write(vol, dir_ino, dir, dir->size, vol->buf_dir, l->block_size);
		if (r < 0)
			return r;
		if ((u32)r != l->block_size)
			return NoSpace;
		return Ok;
	}

	i32 dir_remove(Volume* vol, u32 dir_ino, Inode* dir, const u8* name, u32 name_len, u32* out_ino)
	{
		u32 index = 0, block = 0, offset = 0;
		i32 r = dir_find(vol, dir, &index, MatchName, name, name_len, &block, &offset);
		if (r < 0)
			return r;
		*out_ino = get32(vol->buf_dir + offset + DirEntInode);
		mem_set(vol->buf_dir + offset, 0, vol->layout.dirent_size);
		if ((r = write_block(vol, block, vol->buf_dir)) < 0)
			return r;
		return touch_dir(vol, dir_ino, dir);
	}

	// Points the entry to another inode (a moved directory's ..).
	static i32 dir_set(Volume* vol, Inode* dir, const u8* name, u32 name_len, u32 ino)
	{
		u32 index = 0, block = 0, offset = 0;
		i32 r = dir_find(vol, dir, &index, MatchName, name, name_len, &block, &offset);
		if (r < 0)
			return r;
		put32(vol->buf_dir + offset + DirEntInode, ino);
		return write_block(vol, block, vol->buf_dir);
	}

	i32 dir_next(Volume* vol, Inode* dir, u32* cursor, DirEntry* out)
	{
		u32 block = 0, offset = 0;
		i32 r = dir_find(vol, dir, cursor, MatchUsed, nullptr, 0, &block, &offset);
		if (r < 0)
			return r;
		const u8* entry = vol->buf_dir + offset;
		out->inode = get32(entry + DirEntInode);
		out->type = entry[DirEntType];
		out->name_len = entry[DirEntNameLen];
		if (out->name_len > MaxNameLength)
			return Corrupt;
		mem_copy(out->name, entry + DirEntName, out->name_len);
		out->name[out->name_len] = 0;
		*cursor = *cursor + 1;
		return Ok;
	}

	i32 dir_is_empty(Volume* vol, Inode* dir)
	{
		u32 index = 0, block = 0, offset = 0;
		i32 r = dir_find(vol, dir, &index, MatchNotDot, nullptr, 0, &block, &offset);
		if (r == Ok)
			return 0;
		if (r == NotFound)
			return 1;
		return r;
	}

	// ============================================================================
	// L6: paths
	// ============================================================================
	static i32 walk(Volume* vol, const u8* path, bool parent, u32* out_ino, const u8** out_name, u32* out_name_len)
	{
		if (path == nullptr || path[0] != '/')
			return Invalid;
		u32 ino = RootInode;
		Inode node;
		const u8* p = path;
		while (true)
		{
			while (*p == '/')
				p++;
			if (*p == 0)
			{
				if (parent)
					return Invalid;		// "/" has no parent
				*out_ino = ino;
				return Ok;
			}
			const u8* name = p;
			u32 len = 0;
			while (p[len] != 0 && p[len] != '/')
				len++;
			if (len > MaxNameLength)
				return NameTooLong;
			const u8* next = p + len;
			while (*next == '/')
				next++;
			i32 r = read_inode(vol, ino, &node);
			if (r < 0)
				return r;
			if (!is_dir(&node))
				return NotDir;
			if (parent && *next == 0)
			{
				*out_ino = ino;
				*out_name = name;
				*out_name_len = len;
				return Ok;
			}
			if ((r = dir_lookup(vol, &node, name, len, &ino)) < 0)
				return r;
			p = next;
		}
	}

	i32 resolve(Volume* vol, const u8* path, u32* out_ino)
	{
		return walk(vol, path, false, out_ino, nullptr, nullptr);
	}

	i32 resolve_parent(Volume* vol, const u8* path, u32* out_parent, const u8** out_name, u32* out_name_len)
	{
		return walk(vol, path, true, out_parent, out_name, out_name_len);
	}

	// ============================================================================
	// L7: file system operations
	// ============================================================================
	// Frees an inode that nothing points to anymore: its blocks, its record, its bit.
	static i32 release_inode(Volume* vol, u32 ino, Inode* node)
	{
		i32 r = truncate(vol, ino, node, 0);
		if (r < 0)
			return r;
		mem_set(node, 0, sizeof(Inode));
		if ((r = write_inode(vol, ino, node)) < 0)
			return r;
		return free_inode(vol, ino);
	}

	static i32 make_node(Volume* vol, const u8* path, u16 mode, u32* out_ino)
	{
		if (vol->read_only)
			return ReadOnly;
		u32 parent = 0, ino = 0;
		const u8* name = nullptr;
		u32 name_len = 0;
		i32 r = resolve_parent(vol, path, &parent, &name, &name_len);
		if (r < 0)
			return r;
		Inode parent_node, node;
		if ((r = read_inode(vol, parent, &parent_node)) < 0)
			return r;
		r = dir_lookup(vol, &parent_node, name, name_len, &ino);
		if (r == Ok)
			return Exists;
		if (r != NotFound)
			return r;
		const bool dir = ((mode & ModeTypeMask) == ModeDir);
		if (dir && parent_node.links == 0xFFFF)
			return TooManyLinks;

		if ((r = alloc_inode(vol, &ino)) < 0)
			return r;
		init_inode(vol, &node, mode, (u16)(dir ? 2 : 1));
		if (dir)
		{
			make_dir_block(vol, ino, parent);
			r = inode_write(vol, ino, &node, 0, vol->buf_dir, vol->layout.block_size);
			if (r >= 0 && (u32)r != vol->layout.block_size)
				r = NoSpace;
		}
		else
			r = write_inode(vol, ino, &node);
		if (r >= 0)
			r = dir_add(vol, parent, &parent_node, name, name_len, ino, mode);
		if (r < 0)
		{
			release_inode(vol, ino, &node);
			return r;
		}
		if (dir)
		{
			parent_node.links++;
			if ((r = write_inode(vol, parent, &parent_node)) < 0)
				return r;
		}
		if (out_ino != nullptr)
			*out_ino = ino;
		return Ok;
	}

	i32 create(Volume* vol, const u8* path, u16 perm, u32* out_ino)
	{
		return make_node(vol, path, (u16)(ModeFile | (perm & ModePermMask)), out_ino);
	}

	i32 mkdir(Volume* vol, const u8* path, u16 perm, u32* out_ino)
	{
		return make_node(vol, path, (u16)(ModeDir | (perm & ModePermMask)), out_ino);
	}

	// The parent directory and the inode of the path's last component.
	static i32 lookup_entry(Volume* vol, const u8* path, u32* out_parent, Inode* out_parent_node, const u8** out_name, u32* out_name_len, u32* out_ino, Inode* out_node)
	{
		i32 r = resolve_parent(vol, path, out_parent, out_name, out_name_len);
		if (r < 0)
			return r;
		if ((r = read_inode(vol, *out_parent, out_parent_node)) < 0)
			return r;
		if ((r = dir_lookup(vol, out_parent_node, *out_name, *out_name_len, out_ino)) < 0)
			return r;
		return read_inode(vol, *out_ino, out_node);
	}

	i32 unlink(Volume* vol, const u8* path)
	{
		if (vol->read_only)
			return ReadOnly;
		u32 parent = 0, ino = 0, name_len = 0;
		const u8* name = nullptr;
		Inode parent_node, node;
		i32 r = lookup_entry(vol, path, &parent, &parent_node, &name, &name_len, &ino, &node);
		if (r < 0)
			return r;
		if (is_dir(&node))
			return IsDir;
		if ((r = dir_remove(vol, parent, &parent_node, name, name_len, &ino)) < 0)
			return r;
		if (node.links > 0)
			node.links--;
		if (node.links == 0)
			return release_inode(vol, ino, &node);
		node.ctime = now(vol);
		return write_inode(vol, ino, &node);
	}

	i32 rmdir(Volume* vol, const u8* path)
	{
		if (vol->read_only)
			return ReadOnly;
		u32 parent = 0, ino = 0, name_len = 0;
		const u8* name = nullptr;
		Inode parent_node, node;
		i32 r = lookup_entry(vol, path, &parent, &parent_node, &name, &name_len, &ino, &node);
		if (r < 0)
			return r;
		if (is_dot_name(name, name_len))
			return Invalid;
		if (!is_dir(&node))
			return NotDir;
		if ((r = dir_is_empty(vol, &node)) < 0)
			return r;
		if (r == 0)
			return NotEmpty;
		if ((r = dir_remove(vol, parent, &parent_node, name, name_len, &ino)) < 0)
			return r;
		if (parent_node.links > 0)
			parent_node.links--;
		if ((r = write_inode(vol, parent, &parent_node)) < 0)
			return r;
		return release_inode(vol, ino, &node);
	}

	i32 rename(Volume* vol, const u8* from, const u8* to)
	{
		if (vol->read_only)
			return ReadOnly;
		u32 src_parent = 0, ino = 0, src_len = 0, dst_parent = 0, dst_len = 0, other = 0;
		const u8* src_name = nullptr;
		const u8* dst_name = nullptr;
		Inode src_node, dst_node, node;
		i32 r = lookup_entry(vol, from, &src_parent, &src_node, &src_name, &src_len, &ino, &node);
		if (r < 0)
			return r;
		if ((r = resolve_parent(vol, to, &dst_parent, &dst_name, &dst_len)) < 0)
			return r;
		if (is_dot_name(src_name, src_len) || is_dot_name(dst_name, dst_len))
			return Invalid;
		Inode* dst = &src_node;		// the same directory: one copy of its inode
		if (dst_parent != src_parent)
		{
			if ((r = read_inode(vol, dst_parent, &dst_node)) < 0)
				return r;
			dst = &dst_node;
		}
		r = dir_lookup(vol, dst, dst_name, dst_len, &other);
		if (r == Ok)
			return Exists;
		if (r != NotFound)
			return r;
		const bool moving_dir = (is_dir(&node) && dst_parent != src_parent);
		if (moving_dir)
		{
			// Not into itself: walk up from the destination to the root.
			const u8 dots[2] = { '.', '.' };
			u32 current = dst_parent;
			Inode walk_node;
			for (u32 steps = 0; current != RootInode; steps++)
			{
				if (current == ino)
					return Invalid;
				if (steps >= vol->layout.inode_count)
					return Corrupt;
				if ((r = read_inode(vol, current, &walk_node)) < 0)
					return r;
				if ((r = dir_lookup(vol, &walk_node, dots, 2, &current)) < 0)
					return r;
			}
			if (dst->links == 0xFFFF)
				return TooManyLinks;
		}
		if (node.links == 0xFFFF)
			return TooManyLinks;

		// One more link while both entries exist (a crash can only leak it).
		node.links++;
		if ((r = write_inode(vol, ino, &node)) < 0)
			return r;
		if ((r = dir_add(vol, dst_parent, dst, dst_name, dst_len, ino, node.mode)) < 0)
		{
			node.links--;
			write_inode(vol, ino, &node);
			return r;
		}
		if ((r = dir_remove(vol, src_parent, &src_node, src_name, src_len, &other)) < 0)
			return r;
		if (moving_dir)
		{
			const u8 dots[2] = { '.', '.' };
			if ((r = dir_set(vol, &node, dots, 2, dst_parent)) < 0)
				return r;
			dst->links++;
			if ((r = write_inode(vol, dst_parent, dst)) < 0)
				return r;
			src_node.links--;
			if ((r = write_inode(vol, src_parent, &src_node)) < 0)
				return r;
		}
		node.links--;
		node.ctime = now(vol);
		return write_inode(vol, ino, &node);
	}

	i32 link(Volume* vol, const u8* existing, const u8* new_path)
	{
		if (vol->read_only)
			return ReadOnly;
		u32 ino = 0, parent = 0, name_len = 0;
		const u8* name = nullptr;
		Inode node, parent_node;
		i32 r = resolve(vol, existing, &ino);
		if (r < 0)
			return r;
		if ((r = read_inode(vol, ino, &node)) < 0)
			return r;
		if (is_dir(&node))
			return IsDir;
		if (node.links == 0xFFFF)
			return TooManyLinks;
		if ((r = resolve_parent(vol, new_path, &parent, &name, &name_len)) < 0)
			return r;
		if ((r = read_inode(vol, parent, &parent_node)) < 0)
			return r;
		// The link count first (a crash can only leak it).
		node.links++;
		node.ctime = now(vol);
		if ((r = write_inode(vol, ino, &node)) < 0)
			return r;
		if ((r = dir_add(vol, parent, &parent_node, name, name_len, ino, node.mode)) < 0)
		{
			node.links--;
			write_inode(vol, ino, &node);
		}
		return r;
	}

	i32 stat(Volume* vol, const u8* path, Inode* out, u32* out_ino)
	{
		u32 ino = 0;
		i32 r = resolve(vol, path, &ino);
		if (r < 0)
			return r;
		if (out_ino != nullptr)
			*out_ino = ino;
		return read_inode(vol, ino, out);
	}

	i32 chmod(Volume* vol, const u8* path, u16 perm)
	{
		if (vol->read_only)
			return ReadOnly;
		u32 ino = 0;
		Inode node;
		i32 r = stat(vol, path, &node, &ino);
		if (r < 0)
			return r;
		node.mode = (u16)((node.mode & ModeTypeMask) | (perm & ModePermMask));
		node.ctime = now(vol);
		return write_inode(vol, ino, &node);
	}

	i32 chown(Volume* vol, const u8* path, u16 uid, u16 gid)
	{
		if (vol->read_only)
			return ReadOnly;
		u32 ino = 0;
		Inode node;
		i32 r = stat(vol, path, &node, &ino);
		if (r < 0)
			return r;
		node.uid = uid;
		node.gid = gid;
		node.ctime = now(vol);
		return write_inode(vol, ino, &node);
	}
}
