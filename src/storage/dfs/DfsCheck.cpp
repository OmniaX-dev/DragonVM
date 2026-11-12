#include "DfsCheck.hpp"

#include <deque>

namespace dfs
{
	namespace
	{
		struct Checker
		{
			Volume* vol;
			FsckResult& res;
			const Layout& l;
			std::vector<u8> block_bitmap, inode_bitmap;
			std::vector<u8> seen_block, seen_inode;
			std::vector<u32> refs, subdirs, parent;
			std::deque<u32> dirs;

			Checker(Volume* v, FsckResult& r) : vol(v), res(r), l(v->layout) {  }

			void problem(u32& counter, const std::string& message)
			{
				counter++;
				if (res.messages.size() < 200)
					res.messages.push_back(message);
			}
			void error(const std::string& message) { problem(res.errors, message); }

			static bool bit(const std::vector<u8>& map, u32 n) { return (map[n / 8] >> (n % 8)) & 1; }

			i32 read_raw(u32 block, u8* out)
			{
				return vol->dev.read(vol->dev.user, block << l.block_size_log2, out, l.block_size) < 0 ? IOError : Ok;
			}

			i32 read_bitmap(u32 first, u32 len, std::vector<u8>& out)
			{
				out.assign(cast_size(len) * l.block_size, 0);
				for (u32 i = 0; i < len; i++)
				{
					if (read_raw(first + i, out.data() + cast_size(i) * l.block_size) < 0)
						return IOError;
				}
				return Ok;
			}
			static size_t cast_size(u32 v) { return static_cast<size_t>(v); }

			// Claims a block for an inode; false if the block can't be read through.
			bool claim(u32 block, u32 ino)
			{
				if (block < l.first_data_block || block >= l.total_blocks)
				{
					error("inode " + std::to_string(ino) + ": block " + std::to_string(block) + " is out of the data area");
					return false;
				}
				if (seen_block[block])
				{
					error("block " + std::to_string(block) + " has more than one owner (inode " + std::to_string(ino) + ")");
					return false;
				}
				seen_block[block] = 1;
				if (!bit(block_bitmap, block))
					error("block " + std::to_string(block) + " is used by inode " + std::to_string(ino) + " but marked free");
				return true;
			}

			// Claims all the inode's blocks; data_blocks gets the data block of each file block (0: hole).
			i32 walk_blocks(u32 ino, const Inode& node, std::vector<u32>& data_blocks)
			{
				const u32 P = l.pointers_per_block;
				const u32 end = (node.size >> l.block_size_log2) + ((node.size & l.block_mask) != 0 ? 1 : 0);
				data_blocks.assign(end, 0);
				auto data = [&](u32 index, u32 block) {
					if (block == 0)
						return;
					if (!claim(block, ino))
						return;
					if (index >= end)
						error("inode " + std::to_string(ino) + ": block mapped past the end of the file (index " + std::to_string(index) + ")");
					else
						data_blocks[index] = block;
				};
				for (u32 i = 0; i < DirectPointers; i++)
					data(i, node.direct[i]);
				std::vector<u8> ind(l.block_size), ind2(l.block_size);
				if (node.indirect != 0 && claim(node.indirect, ino))
				{
					if (read_raw(node.indirect, ind.data()) < 0)
						return IOError;
					for (u32 i = 0; i < P; i++)
						data(DirectPointers + i, get32(ind.data() + i * 4));
				}
				if (node.dindirect != 0 && claim(node.dindirect, ino))
				{
					if (read_raw(node.dindirect, ind2.data()) < 0)
						return IOError;
					for (u32 j = 0; j < P; j++)
					{
						const u32 child = get32(ind2.data() + j * 4);
						if (child == 0 || !claim(child, ino))
							continue;
						if (read_raw(child, ind.data()) < 0)
							return IOError;
						for (u32 i = 0; i < P; i++)
							data(DirectPointers + P + j * P + i, get32(ind.data() + i * 4));
					}
				}
				return Ok;
			}

			// First time an inode is reached: its own checks and its blocks.
			i32 visit(u32 ino, Inode& node, std::vector<u32>& data_blocks)
			{
				seen_inode[ino] = 1;
				if (!bit(inode_bitmap, ino))
					error("inode " + std::to_string(ino) + " is used but marked free");
				const u16 type = node.mode & ModeTypeMask;
				if (type != ModeFile && type != ModeDir && type != ModeCharDevice)
					error("inode " + std::to_string(ino) + " has an unknown type (mode " + std::to_string(node.mode) + ")");
				if (type == ModeDir)
					res.directories++;
				else
					res.files++;
				if (type == ModeCharDevice)
				{
					data_blocks.clear();
					return Ok;		// direct[0] is the device number
				}
				return walk_blocks(ino, node, data_blocks);
			}

			i32 check_dir(u32 ino, const Inode& node, const std::vector<u32>& blocks)
			{
				const std::string where = "directory " + std::to_string(ino);
				if ((node.size & l.block_mask) != 0 || node.size == 0)
					error(where + ": size " + std::to_string(node.size) + " is not a whole number of blocks");
				bool dot = false, dotdot = false;
				std::vector<u8> buf(l.block_size);
				for (size_t b = 0; b < blocks.size(); b++)
				{
					if (blocks[b] == 0)
					{
						error(where + ": hole at block " + std::to_string(b));
						continue;
					}
					if (read_raw(blocks[b], buf.data()) < 0)
						return IOError;
					for (u32 e = 0; e < l.dirents_per_block; e++)
					{
						const u8* entry = buf.data() + e * l.dirent_size;
						const u32 child = get32(entry + DirEntInode);
						if (child == 0)
							continue;
						const u32 len = entry[DirEntNameLen];
						std::string name(reinterpret_cast<const char*>(entry + DirEntName), len <= MaxNameLength ? len : MaxNameLength);
						const std::string what = where + ", entry '" + name + "'";
						if (len == 0 || len > MaxNameLength || name.find('/') != std::string::npos || name.find('\0') != std::string::npos)
						{
							error(what + ": invalid name");
							continue;
						}
						if (child >= l.inode_count)
						{
							error(what + ": inode " + std::to_string(child) + " out of range");
							continue;
						}
						if (name == "." || name == "..")
						{
							const u32 expected = (name == "." ? ino : parent[ino]);
							bool& found = (name == "." ? dot : dotdot);
							if (found)
								error(what + ": duplicate");
							found = true;
							if (child != expected)
								error(what + ": points to " + std::to_string(child) + " instead of " + std::to_string(expected));
							continue;
						}
						Inode child_node;
						i32 r = read_inode(vol, child, &child_node);
						if (r < 0)
							return r;
						refs[child]++;
						if (entry[DirEntType] != (child_node.mode >> 12))
							error(what + ": type " + std::to_string(entry[DirEntType]) + " doesn't match the inode's");
						if ((child_node.mode & ModeTypeMask) == ModeDir)
						{
							if (seen_inode[child])
							{
								error(what + ": directory " + std::to_string(child) + " has more than one parent");
								continue;
							}
							parent[child] = ino;
							subdirs[ino]++;
							seen_inode[child] = 1;		// visited when dequeued
							dirs.push_back(child);
						}
						else if (!seen_inode[child])
						{
							std::vector<u32> unused;
							if ((r = visit(child, child_node, unused)) < 0)
								return r;
						}
					}
				}
				if (!dot)
					error(where + ": no '.' entry");
				if (!dotdot)
					error(where + ": no '..' entry");
				return Ok;
			}

			i32 run(void)
			{
				i32 r = Ok;
				if ((r = read_bitmap(l.block_bitmap_block, l.block_bitmap_len, block_bitmap)) < 0)
					return r;
				if ((r = read_bitmap(l.inode_bitmap_block, l.inode_bitmap_len, inode_bitmap)) < 0)
					return r;
				seen_block.assign(l.total_blocks, 0);
				seen_inode.assign(l.inode_count, 0);
				refs.assign(l.inode_count, 0);
				subdirs.assign(l.inode_count, 0);
				parent.assign(l.inode_count, 0);
				for (u32 b = 0; b < l.first_data_block; b++)
				{
					seen_block[b] = 1;
					if (!bit(block_bitmap, b))
						error("metadata block " + std::to_string(b) + " is marked free");
				}
				for (u32 i = 0; i < FirstInode; i++)
				{
					if (!bit(inode_bitmap, i))
						error("reserved inode " + std::to_string(i) + " is marked free");
				}

				// The tree, a directory at a time.
				parent[RootInode] = RootInode;
				seen_inode[RootInode] = 1;
				dirs.push_back(RootInode);
				while (!dirs.empty())
				{
					const u32 ino = dirs.front();
					dirs.pop_front();
					Inode node;
					if ((r = read_inode(vol, ino, &node)) < 0)
						return r;
					if ((node.mode & ModeTypeMask) != ModeDir)
					{
						error("inode " + std::to_string(ino) + " should be a directory");
						continue;
					}
					std::vector<u32> blocks;
					if ((r = visit(ino, node, blocks)) < 0)
						return r;
					if ((r = check_dir(ino, node, blocks)) < 0)
						return r;
				}

				// Link counts and leaked inodes.
				for (u32 ino = 1; ino < l.inode_count; ino++)
				{
					if (seen_inode[ino])
					{
						Inode node;
						if ((r = read_inode(vol, ino, &node)) < 0)
							return r;
						const u32 expected = ((node.mode & ModeTypeMask) == ModeDir ? 2 + subdirs[ino] : refs[ino]);
						if (node.links != expected)
							error("inode " + std::to_string(ino) + ": link count " + std::to_string(node.links) + ", should be " + std::to_string(expected));
					}
					else if (ino >= FirstInode && bit(inode_bitmap, ino))
						problem(res.leaked_inodes, "inode " + std::to_string(ino) + " is marked used but unreachable");
				}
				// Leaked blocks and the free counters.
				u32 free_blocks = 0, free_inodes = 0;
				for (u32 b = l.first_data_block; b < l.total_blocks; b++)
				{
					const bool used = bit(block_bitmap, b);
					if (!used)
						free_blocks++;
					else if (!seen_block[b])
						problem(res.leaked_blocks, "block " + std::to_string(b) + " is marked used but unreachable");
				}
				for (u32 i = FirstInode; i < l.inode_count; i++)
				{
					if (!bit(inode_bitmap, i))
						free_inodes++;
				}
				if (free_blocks != vol->sb.free_blocks)
					error("free block count is " + std::to_string(vol->sb.free_blocks) + ", the bitmap says " + std::to_string(free_blocks));
				if (free_inodes != vol->sb.free_inodes)
					error("free inode count is " + std::to_string(vol->sb.free_inodes) + ", the bitmap says " + std::to_string(free_inodes));
				return Ok;
			}
		};
	}

	i32 fsck(Volume* vol, FsckResult& result)
	{
		result = FsckResult();
		if (!vol->mounted)
			return Invalid;
		Checker checker(vol, result);
		return checker.run();
	}
}
