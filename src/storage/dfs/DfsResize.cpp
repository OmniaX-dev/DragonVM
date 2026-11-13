#include "DfsResize.hpp"

#include <algorithm>
#include <vector>

namespace dfs
{
	namespace
	{
		u32 div_up(u32 a, u32 b) { return a / b + (a % b != 0 ? 1 : 0); }

		// The layout for another block count (same block size and inodes): what mkfs would
		// have computed. Invalid when the metadata doesn't fit.
		i32 layout_for(const Layout& l, u32 total_blocks, Layout& out)
		{
			out = l;
			out.total_blocks = total_blocks;
			out.block_bitmap_len = div_up(total_blocks, l.bits_per_block);
			out.inode_table_block = out.block_bitmap_block + out.block_bitmap_len;
			out.first_data_block = out.inode_table_block + out.inode_table_len;
			if (total_blocks == 0 || out.first_data_block >= total_blocks)
				return Invalid;
			return Ok;
		}

		u32 used_data_blocks(const Volume* vol)
		{
			const Layout& l = vol->layout;
			return l.total_blocks - l.first_data_block - vol->sb.free_blocks;
		}

		// Raw block I/O (no layout checks: the blocks past the old end are used when growing).
		i32 read_raw(Volume* vol, u32 block, u8* out)
		{
			return vol->dev.read(vol->dev.user, block << vol->layout.block_size_log2, out, vol->layout.block_size) < 0 ? IOError : Ok;
		}

		i32 write_raw(Volume* vol, u32 block, const u8* data)
		{
			return vol->dev.write(vol->dev.user, block << vol->layout.block_size_log2, data, vol->layout.block_size) < 0 ? IOError : Ok;
		}

		// The bitmap's bits (one byte per bit, for the first count ones).
		i32 read_bitmap(Volume* vol, u32 first, u32 len, u32 count, std::vector<u8>& out)
		{
			const Layout& l = vol->layout;
			std::vector<u8> block(l.block_size);
			out.assign(count, 0);
			for (u32 b = 0; b < len; b++)
			{
				if (read_raw(vol, first + b, block.data()) < 0)
					return IOError;
				for (u32 i = 0; i < l.bits_per_block; i++)
				{
					const u32 n = b * l.bits_per_block + i;
					if (n >= count)
						return Ok;
					out[n] = (block[i / 8] >> (i % 8)) & 1;
				}
			}
			return Ok;
		}

		struct Resizer
		{
			Volume* vol;
			Layout old;
			Layout nl;
			std::vector<u8> used;		// per block, up to max(old, new) blocks
			u32 cursor { 0 };			// where the free block search continues
			u32 moved { 0 };

			bool bad(u32 block) const { return block < nl.first_data_block || block >= nl.total_blocks; }

			i32 alloc(u32* out)
			{
				for (; cursor < nl.total_blocks; cursor++)
				{
					if (!used[cursor])
					{
						used[cursor] = 1;
						*out = cursor++;
						return Ok;
					}
				}
				return NoSpace;
			}

			// Moves a block to a free one below the new end, and points *pointer at it.
			i32 relocate(u32* pointer)
			{
				u32 to = 0;
				i32 r = alloc(&to);
				if (r < 0)
					return r;
				std::vector<u8> data(old.block_size);
				if ((r = read_raw(vol, *pointer, data.data())) < 0 || (r = write_raw(vol, to, data.data())) < 0)
					return r;
				if (*pointer < used.size())
					used[*pointer] = 0;
				*pointer = to;
				moved++;
				return Ok;
			}

			// The pointers in a pointer block (depth 2: a double indirect one).
			i32 walk_pointer_block(u32 block, u32 depth)
			{
				std::vector<u8> data(old.block_size);
				i32 r = read_raw(vol, block, data.data());
				if (r < 0)
					return r;
				bool dirty = false;
				for (u32 i = 0; i < old.pointers_per_block; i++)
				{
					u32 p = get32(data.data() + i * 4);
					if (p == 0)
						continue;
					if (bad(p))
					{
						if ((r = relocate(&p)) < 0)
							return r;
						put32(data.data() + i * 4, p);
						dirty = true;
					}
					if (depth == 2 && (r = walk_pointer_block(p, 1)) < 0)
						return r;
				}
				return dirty ? write_raw(vol, block, data.data()) : Ok;
			}

			i32 relocate_blocks(void)
			{
				std::vector<u8> inodes;
				i32 r = read_bitmap(vol, old.inode_bitmap_block, old.inode_bitmap_len, old.inode_count, inodes);
				if (r < 0)
					return r;
				for (u32 ino = RootInode; ino < old.inode_count; ino++)
				{
					if (!inodes[ino])
						continue;
					Inode node;
					if ((r = read_inode(vol, ino, &node)) < 0)
						return r;
					if (node.mode == 0)
						continue;
					bool changed = false;
					for (u32 i = 0; i < DirectPointers; i++)
					{
						if (node.direct[i] != 0 && bad(node.direct[i]))
						{
							if ((r = relocate(&node.direct[i])) < 0)
								return r;
							changed = true;
						}
					}
					u32* pointers[2] = { &node.indirect, &node.dindirect };
					for (u32 depth = 1; depth <= 2; depth++)
					{
						u32* p = pointers[depth - 1];
						if (*p == 0)
							continue;
						if (bad(*p))
						{
							if ((r = relocate(p)) < 0)
								return r;
							changed = true;
						}
						if ((r = walk_pointer_block(*p, depth)) < 0)
							return r;
					}
					if (changed && (r = write_inode(vol, ino, &node)) < 0)
						return r;
				}
				return Ok;
			}

			// Copies the inode table to its new place (the copy order handles the overlap).
			i32 move_inode_table(void)
			{
				std::vector<u8> data(old.block_size);
				const u32 len = old.inode_table_len;
				for (u32 k = 0; k < len; k++)
				{
					const u32 i = (nl.inode_table_block > old.inode_table_block ? len - 1 - k : k);
					i32 r = read_raw(vol, old.inode_table_block + i, data.data());
					if (r < 0 || (r = write_raw(vol, nl.inode_table_block + i, data.data())) < 0)
						return r;
				}
				vol->layout.inode_table_block = nl.inode_table_block;
				return Ok;
			}

			i32 write_block_bitmap(u32* out_free)
			{
				std::vector<u8> data(old.block_size);
				u32 free = 0;
				for (u32 b = 0; b < nl.block_bitmap_len; b++)
				{
					std::fill(data.begin(), data.end(), 0);
					for (u32 i = 0; i < nl.bits_per_block; i++)
					{
						const u32 n = b * nl.bits_per_block + i;
						bool set = true;
						if (n >= nl.first_data_block && n < nl.total_blocks)
						{
							set = used[n] != 0;
							free += set ? 0 : 1;
						}
						if (set)
							data[i / 8] = (u8)(data[i / 8] | (1u << (i % 8)));
					}
					i32 r = write_raw(vol, nl.block_bitmap_block + b, data.data());
					if (r < 0)
						return r;
				}
				*out_free = free;
				return Ok;
			}

			i32 run(u32 total_blocks)
			{
				old = vol->layout;
				i32 r = layout_for(old, total_blocks, nl);
				if (r < 0)
					return NoSpace;
				// The device must reach the new end before anything is written.
				if (nl.total_blocks > old.total_blocks)
				{
					std::vector<u8> probe(old.block_size);
					if (read_raw(vol, nl.total_blocks - 1, probe.data()) < 0)
						return IOError;
				}
				std::vector<u8> bits;
				if ((r = read_bitmap(vol, old.block_bitmap_block, old.block_bitmap_len, old.total_blocks, bits)) < 0)
					return r;
				used.assign(std::max(old.total_blocks, nl.total_blocks), 0);
				std::copy(bits.begin(), bits.end(), used.begin());
				// Shrinking bitmap: the table moves back first, its old tail becomes free data.
				if (nl.inode_table_block < old.inode_table_block)
				{
					if ((r = move_inode_table()) < 0)
						return r;
					for (u32 b = nl.first_data_block; b < old.first_data_block; b++)
						used[b] = 0;
				}
				cursor = nl.first_data_block;
				if ((r = relocate_blocks()) < 0)
					return r;
				// Growing bitmap: the blocks in the way were just moved.
				if (nl.inode_table_block > old.inode_table_block && (r = move_inode_table()) < 0)
					return r;
				u32 free = 0;
				if ((r = write_block_bitmap(&free)) < 0)
					return r;
				Superblock* sb = &vol->sb;
				sb->total_blocks = nl.total_blocks;
				sb->block_bitmap_len = nl.block_bitmap_len;
				sb->inode_table_block = nl.inode_table_block;
				sb->first_data_block = nl.first_data_block;
				sb->free_blocks = free;
				vol->layout = nl;
				vol->block_hint = nl.first_data_block;
				vol->ind_block = 0;
				vol->ind2_block = 0;
				return Ok;
			}
		};
	}

	i32 resize_plan(Volume* vol, u32 new_partition_size, ResizeInfo& info)
	{
		const Layout& l = vol->layout;
		info = ResizeInfo();
		info.block_size = l.block_size;
		info.old_blocks = l.total_blocks;
		info.new_blocks = new_partition_size >> l.block_size_log2;
		info.used_blocks = used_data_blocks(vol);
		info.max_size = (0xFFFFFFFFu >> l.block_size_log2) << l.block_size_log2;
		// The smallest block count with room for the used blocks after the metadata.
		const u32 need = std::max(info.used_blocks, 1u);
		u32 t = need + l.block_bitmap_block + 1 + l.inode_table_len;
		Layout probe;
		while (layout_for(l, t, probe) < 0 || t - probe.first_data_block < need)
			t++;
		info.min_size = t << l.block_size_log2;

		Layout nl;
		if (layout_for(l, info.new_blocks, nl) < 0 || info.new_blocks - nl.first_data_block < info.used_blocks)
			return NoSpace;
		info.table_moves = nl.block_bitmap_len != l.block_bitmap_len;
		info.new_free_blocks = info.new_blocks - nl.first_data_block - info.used_blocks;
		std::vector<u8> bits;
		i32 r = read_bitmap(vol, l.block_bitmap_block, l.block_bitmap_len, l.total_blocks, bits);
		if (r < 0)
			return r;
		for (u32 b = l.first_data_block; b < l.total_blocks; b++)
		{
			if (bits[b] && (b < nl.first_data_block || b >= nl.total_blocks))
				info.moved_blocks++;
		}
		return Ok;
	}

	i32 resize(Volume* vol, const Device* dev, u32 new_partition_size, ResizeInfo* info, FsckResult* check)
	{
		FsckResult result;
		i32 r = mount(vol, dev, false);
		if (r < 0)
			return r;
		auto l_fail = [&](i32 error) {
			unmount(vol);
			if (check != nullptr)
				*check = result;
			return error;
		};
		if (vol->read_only)
			return l_fail(ReadOnly);
		if ((r = fsck(vol, result)) < 0)
			return l_fail(r);
		if (!result.clean())
			return l_fail(Corrupt);
		ResizeInfo plan;
		r = resize_plan(vol, new_partition_size, plan);
		if (info != nullptr)
			*info = plan;
		if (r < 0)
			return l_fail(r);
		if (plan.new_blocks != plan.old_blocks)
		{
			Resizer resizer { vol, {}, {}, {} };
			if ((r = resizer.run(plan.new_blocks)) < 0)
				return l_fail(r);
		}
		if ((r = unmount(vol)) < 0)
			return l_fail(r);
		// Checked again.
		if ((r = mount(vol, dev, true)) < 0)
			return r;
		r = fsck(vol, result);
		unmount(vol);
		if (check != nullptr)
			*check = result;
		if (r < 0)
			return r;
		return result.clean() ? Ok : Corrupt;
	}
}
