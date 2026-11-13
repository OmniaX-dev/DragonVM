// dfs_test — tests for the DFS core (src/storage/dfs), on a disk in memory.
// Run: ./bin/dfs_test   (exit code 0: all passed)

#include "../../src/storage/dfs/DfsCore.hpp"
#include "../../src/storage/dfs/DfsCheck.hpp"
#include "../../src/storage/dfs/DfsResize.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace dfs;

static int g_failures = 0;
static int g_checks = 0;
static const char* g_test = "";

#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failures++; std::printf("  FAILED %s:%d [%s]: %s\n", __FILE__, __LINE__, g_test, #cond); } } while (0)
#define CHECK_EQ(a, b) do { g_checks++; long long va_ = (long long)(a), vb_ = (long long)(b); if (va_ != vb_) { g_failures++; std::printf("  FAILED %s:%d [%s]: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, g_test, #a, #b, va_, vb_); } } while (0)

// ---- A disk in memory, with a fake 32 byte boot header to check it's never touched ----
struct MemDisk
{
	std::vector<u8> data;
	u32 clock = 1000;
	u32 reads = 0;		// device reads (the pointer cache test)

	explicit MemDisk(u32 size) : data(size, 0)
	{
		for (u32 i = 0; i < BootHeaderSize; i++)
			data[i] = (u8)(0xA0 + i);
	}
	bool header_intact(void) const
	{
		for (u32 i = 0; i < BootHeaderSize; i++)
		{
			if (data[i] != (u8)(0xA0 + i))
				return false;
		}
		return true;
	}
	Device device(void)
	{
		Device dev;
		dev.user = this;
		dev.read = [](void* user, u32 offset, u8* buffer, u32 size) -> i32 {
			auto* d = (MemDisk*)user;
			if ((size_t)offset + size > d->data.size())
				return IOError;
			d->reads++;
			std::memcpy(buffer, d->data.data() + offset, size);
			return Ok;
		};
		dev.write = [](void* user, u32 offset, const u8* buffer, u32 size) -> i32 {
			auto* d = (MemDisk*)user;
			if ((size_t)offset + size > d->data.size())
				return IOError;
			std::memcpy(d->data.data() + offset, buffer, size);
			return Ok;
		};
		dev.now = [](void* user) -> u32 { return ((MemDisk*)user)->clock++; };
		return dev;
	}
};

static const u8* P(const char* s) { return (const u8*)s; }

// A formatted, mounted volume.
struct Fixture
{
	MemDisk disk;
	Device dev;
	std::unique_ptr<Volume> vol { new Volume() };

	Fixture(u32 size, u32 log2, u32 bytes_per_inode = 16384) : Fixture(size, size, log2, bytes_per_inode) {  }
	// A file system smaller than the disk (room to grow).
	Fixture(u32 disk_size, u32 size, u32 log2, u32 bytes_per_inode) : disk(disk_size)
	{
		dev = disk.device();
		MkfsParams params { size, log2, bytes_per_inode, P("TEST"), nullptr };
		CHECK_EQ(mkfs(vol.get(), &dev, &params), Ok);
		CHECK_EQ(mount(vol.get(), &dev, false), Ok);
	}
	~Fixture()
	{
		check_fsck();
		CHECK_EQ(unmount(vol.get()), Ok);
		CHECK(disk.header_intact());
	}
	void check_fsck(void)
	{
		FsckResult res;
		CHECK_EQ(fsck(vol.get(), res), Ok);
		CHECK(res.clean());
		for (const auto& m : res.messages)
			std::printf("    fsck: %s\n", m.c_str());
	}
	u32 free_blocks(void) const { return vol->sb.free_blocks; }
	u32 free_inodes(void) const { return vol->sb.free_inodes; }

	// Creates (or truncates) a file with size bytes of a pattern.
	u32 write_file(const char* path, u32 size, u32 seed = 1)
	{
		u32 ino = 0;
		i32 r = create(vol.get(), P(path), 0644, &ino);
		if (r == Exists)
		{
			Inode node;
			CHECK_EQ(stat(vol.get(), P(path), &node, &ino), Ok);
			CHECK_EQ(truncate(vol.get(), ino, &node, 0), Ok);
		}
		else
			CHECK_EQ(r, Ok);
		std::vector<u8> data = pattern(size, seed);
		Inode node;
		CHECK_EQ(read_inode(vol.get(), ino, &node), Ok);
		CHECK_EQ(inode_write(vol.get(), ino, &node, 0, data.data(), size), (i32)size);
		return ino;
	}
	bool file_is(const char* path, u32 size, u32 seed = 1)
	{
		Inode node;
		u32 ino = 0;
		if (stat(vol.get(), P(path), &node, &ino) != Ok || node.size != size)
			return false;
		std::vector<u8> got(size + 16, 0xEE);
		if (inode_read(vol.get(), &node, 0, got.data(), size + 16) != (i32)size)
			return false;
		got.resize(size);
		return got == pattern(size, seed);
	}
	static std::vector<u8> pattern(u32 size, u32 seed)
	{
		std::vector<u8> data(size);
		u32 x = seed * 2654435761u + 1;
		for (u32 i = 0; i < size; i++)
		{
			x = x * 1103515245u + 12345u;
			data[i] = (u8)(x >> 16);
		}
		return data;
	}
	u16 links(const char* path)
	{
		Inode node;
		if (stat(vol.get(), P(path), &node, nullptr) != Ok)
			return 0xFFFF;
		return node.links;
	}
	u32 count_entries(const char* path)
	{
		Inode node;
		CHECK_EQ(stat(vol.get(), P(path), &node, nullptr), Ok);
		u32 cursor = 0, count = 0;
		DirEntry entry;
		while (dir_next(vol.get(), &node, &cursor, &entry) == Ok)
			count++;
		return count;
	}
};

// ============================================================================
static void test_layout(void)
{
	g_test = "layout";
	Layout l;
	// 5 MiB, 4 KiB blocks: 320 inodes = 10 inode table blocks (32 per block).
	CHECK_EQ(layout_from_params(&l, 5 * 1024 * 1024, 12, 16384), Ok);
	CHECK_EQ(l.total_blocks, 1280);
	CHECK_EQ(l.inode_count, 320);
	CHECK_EQ(l.inode_bitmap_block, 1);
	CHECK_EQ(l.inode_bitmap_len, 1);
	CHECK_EQ(l.block_bitmap_block, 2);
	CHECK_EQ(l.block_bitmap_len, 1);
	CHECK_EQ(l.inode_table_block, 3);
	CHECK_EQ(l.inode_table_len, 10);
	CHECK_EQ(l.first_data_block, 13);
	// 1 KiB blocks
	CHECK_EQ(layout_from_params(&l, 5 * 1024 * 1024, 10, 16384), Ok);
	CHECK_EQ(l.total_blocks, 5120);
	CHECK_EQ(l.inode_count, 320);
	CHECK_EQ(l.block_bitmap_block, 2);
	CHECK_EQ(l.block_bitmap_len, 1);
	CHECK_EQ(l.inode_table_block, 3);
	CHECK_EQ(l.inode_table_len, 40);
	CHECK_EQ(l.first_data_block, 43);
	// 512 B blocks: the block bitmap takes 3 blocks
	CHECK_EQ(layout_from_params(&l, 5 * 1024 * 1024, 9, 16384), Ok);
	CHECK_EQ(l.total_blocks, 10240);
	CHECK_EQ(l.block_bitmap_block, 2);
	CHECK_EQ(l.block_bitmap_len, 3);
	CHECK_EQ(l.inode_table_block, 5);
	CHECK_EQ(l.inode_table_len, 80);
	CHECK_EQ(l.first_data_block, 85);
	// Tiny partitions still get a few usable inodes; too small ones fail.
	CHECK_EQ(layout_from_params(&l, 64 * 1024, 12, 16384), Ok);
	CHECK(l.inode_count > FirstInode);
	CHECK_EQ(layout_from_params(&l, 8 * 1024, 12, 16384), Invalid);
	CHECK_EQ(layout_from_params(&l, 5 * 1024 * 1024, 13, 16384), Invalid);
	CHECK_EQ(layout_from_params(&l, 5 * 1024 * 1024, 8, 16384), Invalid);
}

static void test_mkfs_mount(void)
{
	g_test = "mkfs/mount";
	{
		Fixture f(5 * 1024 * 1024, 12);
		CHECK_EQ(f.vol->sb.magic, Magic);
		CHECK_EQ(f.free_blocks(), 1280 - 13 - 1);		// the root's block
		CHECK_EQ(f.free_inodes(), 320 - FirstInode);
		CHECK_EQ(f.links("/"), 2);
		CHECK_EQ(f.count_entries("/"), 2);
		CHECK_EQ(std::memcmp(f.vol->sb.label, "TEST", 5), 0);
		// big endian on disk
		CHECK_EQ(f.disk.data[0x20], 0x44);
		CHECK_EQ(f.disk.data[0x23], 0x31);
	}
	// Data survives a remount; a read only mount doesn't write.
	MemDisk disk(1024 * 1024);
	Device dev = disk.device();
	std::unique_ptr<Volume> vol(new Volume());
	MkfsParams params { 1024 * 1024, 10, 0, nullptr, nullptr };
	CHECK_EQ(mkfs(vol.get(), &dev, &params), Ok);
	CHECK_EQ(mount(vol.get(), &dev, false), Ok);
	CHECK_EQ(vol->sb.state, StateDirty);
	u32 ino = 0;
	CHECK_EQ(create(vol.get(), P("/hello.txt"), 0644, &ino), Ok);
	Inode node;
	CHECK_EQ(read_inode(vol.get(), ino, &node), Ok);
	CHECK_EQ(inode_write(vol.get(), ino, &node, 0, P("Hello DragonOS"), 14), 14);
	CHECK_EQ(unmount(vol.get()), Ok);
	std::vector<u8> before = disk.data;
	CHECK_EQ(mount(vol.get(), &dev, true), Ok);
	CHECK_EQ(vol->sb.state, StateClean);
	CHECK_EQ(stat(vol.get(), P("/hello.txt"), &node, &ino), Ok);
	u8 text[32] = {};
	CHECK_EQ(inode_read(vol.get(), &node, 0, text, 32), 14);
	CHECK_EQ(std::memcmp(text, "Hello DragonOS", 14), 0);
	CHECK_EQ(create(vol.get(), P("/x"), 0644, nullptr), ReadOnly);
	CHECK_EQ(inode_write(vol.get(), ino, &node, 0, P("x"), 1), ReadOnly);
	CHECK_EQ(unmount(vol.get()), Ok);
	CHECK(disk.data == before);
	// Not DFS / unknown incompatible features / bad version
	MemDisk blank(1024 * 1024);
	Device blank_dev = blank.device();
	CHECK_EQ(mount(vol.get(), &blank_dev, false), Unsupported);
	disk.data[0x20 + 20 * 4 + 3] = 1;		// feature_incompat
	CHECK_EQ(mount(vol.get(), &dev, false), Unsupported);
	disk.data[0x20 + 20 * 4 + 3] = 0;
	disk.data[0x20 + 19 * 4 + 3] = 1;		// feature_ro_compat: read only
	CHECK_EQ(mount(vol.get(), &dev, false), Ok);
	CHECK(vol->read_only);
	CHECK_EQ(unmount(vol.get()), Ok);
	CHECK(disk.header_intact());
}

static void test_pointer_boundaries(void)
{
	g_test = "pointer boundaries";
	Fixture f(8 * 1024 * 1024, 10);		// 1 KiB blocks: P = 256
	const u32 B = 1024, PP = 256;
	const u32 base = f.free_blocks();
	struct { u32 blocks; u32 meta; } cases[] = {
		{ 12, 0 }, { 13, 1 }, { 12 + PP, 1 }, { 12 + PP + 1, 1 + 2 }, { 12 + PP + PP + 1, 1 + 3 }
	};
	for (auto& c : cases)
	{
		f.write_file("/big", c.blocks * B, c.blocks);
		CHECK(f.file_is("/big", c.blocks * B, c.blocks));
		CHECK_EQ(base - f.free_blocks(), c.blocks + c.meta);
		f.check_fsck();
	}
	// Truncate down through each boundary (and to odd sizes), checking the free count.
	Inode node;
	u32 ino = 0;
	CHECK_EQ(stat(f.vol.get(), P("/big"), &node, &ino), Ok);
	for (int i = 4; i >= 0; i--)
	{
		CHECK_EQ(truncate(f.vol.get(), ino, &node, cases[i].blocks * B), Ok);
		CHECK_EQ(base - f.free_blocks(), cases[i].blocks + cases[i].meta);
		CHECK_EQ(truncate(f.vol.get(), ino, &node, cases[i].blocks * B - 100), Ok);
		CHECK_EQ(base - f.free_blocks(), cases[i].blocks + cases[i].meta);
		f.check_fsck();
	}
	// The truncated tail reads as zeros when the file grows again.
	CHECK_EQ(truncate(f.vol.get(), ino, &node, 10), Ok);
	CHECK_EQ(truncate(f.vol.get(), ino, &node, 2000), Ok);
	std::vector<u8> got(2000);
	CHECK_EQ(inode_read(f.vol.get(), &node, 0, got.data(), 2000), 2000);
	std::vector<u8> expected = Fixture::pattern(525 * B, 525);	// the last content written
	CHECK(std::memcmp(got.data(), expected.data(), 10) == 0);
	bool zeros = true;
	for (u32 i = 10; i < 2000; i++)
		zeros = zeros && got[i] == 0;
	CHECK(zeros);
	CHECK_EQ(truncate(f.vol.get(), ino, &node, 0), Ok);
	CHECK_EQ(f.free_blocks(), base);
	// Past the double indirect range
	u32 dummy = 0;
	CHECK_EQ(bmap(f.vol.get(), &node, 12 + PP + PP * PP, false, &dummy), TooBig);
	CHECK_EQ(unlink(f.vol.get(), P("/big")), Ok);
}

// The pointer block caches: reading in order reads each pointer block once, and they stay
// right while pointer blocks are freed and their blocks reused for other things.
static void test_pointer_cache(void)
{
	g_test = "pointer cache";
	Fixture f(8 * 1024 * 1024, 10);		// 1 KiB blocks: P = 256
	const u32 B = 1024, PP = 256;
	const u32 blocks = 12 + PP + 300;	// into the double indirect block
	f.write_file("/a", blocks * B, 3);
	CHECK_EQ(unmount(f.vol.get()), Ok);		// empty caches: the count below is exact
	CHECK_EQ(mount(f.vol.get(), &f.dev, false), Ok);
	Inode node;
	u32 ino = 0;
	CHECK_EQ(stat(f.vol.get(), P("/a"), &node, &ino), Ok);
	std::vector<u8> got(blocks * B);
	const u32 before = f.disk.reads;
	CHECK_EQ(inode_read(f.vol.get(), &node, 0, got.data(), blocks * B), (i32)(blocks * B));
	// The data blocks, the indirect block, the double indirect one and its 2 children.
	CHECK_EQ(f.disk.reads - before, blocks + 1 + 1 + 2);
	CHECK(got == Fixture::pattern(blocks * B, 3));

	// Free the pointer blocks while cached, let other files take the blocks, and grow /a again.
	for (u32 round = 0; round < 3; round++)
	{
		CHECK_EQ(truncate(f.vol.get(), ino, &node, (12 + 5) * B), Ok);
		f.write_file("/b", (12 + PP + 40) * B, 10 + round);		// reuses the freed blocks
		std::vector<u8> tail = Fixture::pattern((PP + 200) * B, 20 + round);
		CHECK_EQ(inode_write(f.vol.get(), ino, &node, (12 + 5) * B, tail.data(), (u32)tail.size()), (i32)tail.size());
		CHECK(f.file_is("/b", (12 + PP + 40) * B, 10 + round));
		std::vector<u8> back(tail.size());
		CHECK_EQ(inode_read(f.vol.get(), &node, (12 + 5) * B, back.data(), (u32)back.size()), (i32)back.size());
		CHECK(back == tail);
		CHECK_EQ(unlink(f.vol.get(), P("/b")), Ok);
		f.check_fsck();
	}
	// Remounting (another volume could have changed the disk) starts with empty caches.
	CHECK_EQ(unmount(f.vol.get()), Ok);
	CHECK_EQ(mount(f.vol.get(), &f.dev, false), Ok);
	CHECK_EQ(f.vol->ind_block, 0u);
	CHECK_EQ(f.vol->ind2_block, 0u);
	CHECK_EQ(unlink(f.vol.get(), P("/a")), Ok);
}

static void test_sparse(void)
{
	g_test = "sparse";
	Fixture f(4 * 1024 * 1024, 10);
	const u32 base = f.free_blocks();
	u32 ino = 0;
	CHECK_EQ(create(f.vol.get(), P("/sparse"), 0644, &ino), Ok);
	Inode node;
	CHECK_EQ(read_inode(f.vol.get(), ino, &node), Ok);
	const u32 offset = 1024 * 1024 + 7;
	CHECK_EQ(inode_write(f.vol.get(), ino, &node, offset, P("dragon"), 6), 6);
	CHECK_EQ(node.size, offset + 6);
	CHECK_EQ(base - f.free_blocks(), 1 + 1 + 1);		// data + dindirect + its child
	std::vector<u8> got(offset + 6, 0xEE);
	CHECK_EQ(inode_read(f.vol.get(), &node, 0, got.data(), offset + 100), (i32)(offset + 6));
	bool zeros = true;
	for (u32 i = 0; i < offset; i++)
		zeros = zeros && got[i] == 0;
	CHECK(zeros);
	CHECK_EQ(std::memcmp(got.data() + offset, "dragon", 6), 0);
	// Growing with truncate is a hole too.
	CHECK_EQ(truncate(f.vol.get(), ino, &node, offset + 5000), Ok);
	CHECK_EQ(base - f.free_blocks(), 3);
	CHECK_EQ(unlink(f.vol.get(), P("/sparse")), Ok);
	CHECK_EQ(f.free_blocks(), base);
}

static void test_directories(void)
{
	g_test = "directories";
	Fixture f(4 * 1024 * 1024, 10);		// 16 entries per block
	CHECK_EQ(mkdir(f.vol.get(), P("/d"), 0755, nullptr), Ok);
	char path[64];
	for (int i = 0; i < 40; i++)
	{
		std::snprintf(path, sizeof(path), "/d/file%02d", i);
		CHECK_EQ(create(f.vol.get(), P(path), 0644, nullptr), Ok);
	}
	Inode d;
	CHECK_EQ(stat(f.vol.get(), P("/d"), &d, nullptr), Ok);
	CHECK_EQ(d.size, 3 * 1024);		// 42 entries
	CHECK_EQ(f.count_entries("/d"), 42);
	for (int i = 5; i < 11; i++)
	{
		std::snprintf(path, sizeof(path), "/d/file%02d", i);
		CHECK_EQ(unlink(f.vol.get(), P(path)), Ok);
	}
	CHECK_EQ(f.count_entries("/d"), 36);
	for (int i = 0; i < 6; i++)
	{
		std::snprintf(path, sizeof(path), "/d/new%d", i);
		CHECK_EQ(create(f.vol.get(), P(path), 0644, nullptr), Ok);
	}
	CHECK_EQ(stat(f.vol.get(), P("/d"), &d, nullptr), Ok);
	CHECK_EQ(d.size, 3 * 1024);		// the slots were reused
	CHECK_EQ(f.count_entries("/d"), 42);

	// Paths and names
	u32 ino = 0;
	CHECK_EQ(resolve(f.vol.get(), P("//d///new3"), &ino), Ok);
	CHECK_EQ(resolve(f.vol.get(), P("/d/./new3"), &ino), Ok);
	u32 root = 0;
	CHECK_EQ(resolve(f.vol.get(), P("/d/.."), &root), Ok);
	CHECK_EQ(root, RootInode);
	CHECK_EQ(resolve(f.vol.get(), P("/.."), &root), Ok);
	CHECK_EQ(root, RootInode);
	CHECK_EQ(resolve(f.vol.get(), P("d"), &ino), Invalid);
	CHECK_EQ(resolve(f.vol.get(), P("/nope"), &ino), NotFound);
	CHECK_EQ(resolve(f.vol.get(), P("/d/new3/x"), &ino), NotDir);
	CHECK_EQ(create(f.vol.get(), P("/d/new3"), 0644, nullptr), Exists);
	CHECK_EQ(create(f.vol.get(), P("/d/."), 0644, nullptr), Exists);
	CHECK_EQ(create(f.vol.get(), P("/"), 0644, nullptr), Invalid);
	std::string longest = "/" + std::string(56, 'n');
	std::string too_long = "/" + std::string(57, 'n');
	CHECK_EQ(create(f.vol.get(), P(longest.c_str()), 0644, nullptr), Ok);
	CHECK_EQ(resolve(f.vol.get(), P(longest.c_str()), &ino), Ok);
	CHECK_EQ(create(f.vol.get(), P(too_long.c_str()), 0644, nullptr), NameTooLong);
	CHECK_EQ(create(f.vol.get(), P("/trailing/"), 0644, nullptr), Ok);
	CHECK_EQ(resolve(f.vol.get(), P("/trailing"), &ino), Ok);

	// rmdir
	CHECK_EQ(rmdir(f.vol.get(), P("/d")), NotEmpty);
	CHECK_EQ(rmdir(f.vol.get(), P("/d/new3")), NotDir);
	CHECK_EQ(rmdir(f.vol.get(), P("/d/.")), Invalid);
	CHECK_EQ(rmdir(f.vol.get(), P("/")), Invalid);
	CHECK_EQ(unlink(f.vol.get(), P("/d")), IsDir);
	DirEntry entry;
	u32 cursor = 0;
	std::vector<std::string> names;
	while (dir_next(f.vol.get(), &d, &cursor, &entry) == Ok)
		names.push_back((const char*)entry.name);
	for (const auto& name : names)
	{
		if (name != "." && name != "..")
			CHECK_EQ(unlink(f.vol.get(), P(("/d/" + name).c_str())), Ok);
	}
	CHECK_EQ(rmdir(f.vol.get(), P("/d")), Ok);
	CHECK_EQ(f.links("/"), 2);
}

static void test_links(void)
{
	g_test = "links";
	Fixture f(4 * 1024 * 1024, 10);
	const u32 base_inodes = f.free_inodes();
	const u32 base_blocks = f.free_blocks();
	CHECK_EQ(mkdir(f.vol.get(), P("/a"), 0755, nullptr), Ok);
	CHECK_EQ(mkdir(f.vol.get(), P("/a/b"), 0755, nullptr), Ok);
	CHECK_EQ(mkdir(f.vol.get(), P("/c"), 0755, nullptr), Ok);
	CHECK_EQ(f.links("/"), 4);
	CHECK_EQ(f.links("/a"), 3);
	CHECK_EQ(f.links("/a/b"), 2);
	f.write_file("/a/b/data", 5000, 3);

	// Moving a directory across directories
	CHECK_EQ(rename(f.vol.get(), P("/a/b"), P("/c/b")), Ok);
	CHECK_EQ(f.links("/a"), 2);
	CHECK_EQ(f.links("/c"), 3);
	CHECK_EQ(f.links("/c/b"), 2);
	u32 c = 0, dotdot = 0;
	CHECK_EQ(resolve(f.vol.get(), P("/c"), &c), Ok);
	CHECK_EQ(resolve(f.vol.get(), P("/c/b/.."), &dotdot), Ok);
	CHECK_EQ(c, dotdot);
	CHECK(f.file_is("/c/b/data", 5000, 3));
	CHECK_EQ(resolve(f.vol.get(), P("/a/b"), &c), NotFound);
	f.check_fsck();
	// Not into itself, not over an existing name
	CHECK_EQ(rename(f.vol.get(), P("/c"), P("/c/b/x")), Invalid);
	CHECK_EQ(rename(f.vol.get(), P("/c"), P("/c/x")), Invalid);
	CHECK_EQ(rename(f.vol.get(), P("/c/b"), P("/a")), Exists);
	CHECK_EQ(rename(f.vol.get(), P("/c/b/.."), P("/z")), Invalid);
	// Renames in the same directory, files across directories
	CHECK_EQ(rename(f.vol.get(), P("/c/b"), P("/c/bb")), Ok);
	CHECK_EQ(f.links("/c"), 3);
	CHECK_EQ(rename(f.vol.get(), P("/c/bb/data"), P("/a/moved")), Ok);
	CHECK(f.file_is("/a/moved", 5000, 3));
	CHECK_EQ(f.links("/a/moved"), 1);
	f.check_fsck();

	// Hard links
	f.write_file("/f", 100, 9);
	CHECK_EQ(link(f.vol.get(), P("/f"), P("/a/g")), Ok);
	CHECK_EQ(f.links("/f"), 2);
	CHECK_EQ(link(f.vol.get(), P("/a"), P("/a2")), IsDir);
	CHECK_EQ(link(f.vol.get(), P("/f"), P("/a/g")), Exists);
	CHECK_EQ(f.links("/f"), 2);
	f.check_fsck();
	CHECK_EQ(unlink(f.vol.get(), P("/f")), Ok);
	CHECK_EQ(f.links("/a/g"), 1);
	CHECK(f.file_is("/a/g", 100, 9));
	CHECK_EQ(unlink(f.vol.get(), P("/a/g")), Ok);

	// chmod / chown
	Inode node;
	CHECK_EQ(chmod(f.vol.get(), P("/a/moved"), 0600), Ok);
	CHECK_EQ(chown(f.vol.get(), P("/a/moved"), 12, 34), Ok);
	CHECK_EQ(stat(f.vol.get(), P("/a/moved"), &node, nullptr), Ok);
	CHECK_EQ(node.mode, ModeFile | 0600);
	CHECK_EQ(node.uid, 12);
	CHECK_EQ(node.gid, 34);

	// Everything goes away again
	CHECK_EQ(unlink(f.vol.get(), P("/a/moved")), Ok);
	CHECK_EQ(rmdir(f.vol.get(), P("/c/bb")), Ok);
	CHECK_EQ(rmdir(f.vol.get(), P("/c")), Ok);
	CHECK_EQ(rmdir(f.vol.get(), P("/a")), Ok);
	CHECK_EQ(f.links("/"), 2);
	CHECK_EQ(f.free_inodes(), base_inodes);
	CHECK_EQ(f.free_blocks(), base_blocks);
}

static void test_nospace(void)
{
	g_test = "no space";
	Fixture f(256 * 1024, 10, 4096);
	const u32 base = f.free_blocks();
	u32 ino = 0;
	CHECK_EQ(create(f.vol.get(), P("/fill"), 0644, &ino), Ok);
	Inode node;
	CHECK_EQ(read_inode(f.vol.get(), ino, &node), Ok);
	std::vector<u8> data = Fixture::pattern(512 * 1024, 5);
	const i32 written = inode_write(f.vol.get(), ino, &node, 0, data.data(), (u32)data.size());
	CHECK(written > 0 && written < (i32)data.size());
	CHECK_EQ(f.free_blocks(), 0);
	CHECK_EQ(node.size, (u32)written);
	CHECK_EQ(inode_write(f.vol.get(), ino, &node, node.size, data.data(), 1024), NoSpace);
	CHECK_EQ(mkdir(f.vol.get(), P("/dir"), 0755, nullptr), NoSpace);
	CHECK(f.file_is("/fill", (u32)written, 5));
	f.check_fsck();
	// Empty files still fit (no blocks), until the inodes run out.
	char path[32];
	int files = 0;
	while (true)
	{
		std::snprintf(path, sizeof(path), "/e%d", files);
		const i32 r = create(f.vol.get(), P(path), 0644, nullptr);
		if (r != Ok)
		{
			CHECK_EQ(r, NoSpace);
			break;
		}
		files++;
	}
	CHECK(files > 0);
	f.check_fsck();
	CHECK_EQ(unlink(f.vol.get(), P("/fill")), Ok);
	for (int i = 0; i < files; i++)
	{
		std::snprintf(path, sizeof(path), "/e%d", i);
		CHECK_EQ(unlink(f.vol.get(), P(path)), Ok);
	}
	// The root directory may have grown by a block: that's all that stays.
	Inode root;
	CHECK_EQ(read_inode(f.vol.get(), RootInode, &root), Ok);
	CHECK_EQ(f.free_blocks(), base - (root.size / 1024 - 1));
	CHECK_EQ(mkdir(f.vol.get(), P("/dir"), 0755, nullptr), Ok);
}

static void test_fsck_detects(void)
{
	g_test = "fsck detects";
	Fixture f(1024 * 1024, 10);
	f.write_file("/a", 3000, 2);
	u32 ino = 0;
	Inode node;
	CHECK_EQ(stat(f.vol.get(), P("/a"), &node, &ino), Ok);
	// A wrong link count, then a leaked block.
	node.links = 5;
	CHECK_EQ(write_inode(f.vol.get(), ino, &node), Ok);
	FsckResult res;
	CHECK_EQ(fsck(f.vol.get(), res), Ok);
	CHECK_EQ(res.errors, 1);
	node.links = 1;
	CHECK_EQ(write_inode(f.vol.get(), ino, &node), Ok);
	u32 block = 0;
	CHECK_EQ(alloc_block(f.vol.get(), &block), Ok);
	CHECK_EQ(fsck(f.vol.get(), res), Ok);
	CHECK_EQ(res.errors, 0);
	CHECK_EQ(res.leaked_blocks, 1);
	CHECK_EQ(free_block(f.vol.get(), block), Ok);
	CHECK_EQ(free_block(f.vol.get(), block), Corrupt);		// double free
	// Two owners
	Inode other;
	u32 other_ino = f.write_file("/b", 100, 1);
	CHECK_EQ(read_inode(f.vol.get(), other_ino, &other), Ok);
	const u32 saved = other.direct[0];
	other.direct[0] = node.direct[0];
	CHECK_EQ(write_inode(f.vol.get(), other_ino, &other), Ok);
	CHECK_EQ(fsck(f.vol.get(), res), Ok);
	CHECK(res.errors >= 1);
	CHECK_EQ(res.leaked_blocks, 1);
	other.direct[0] = saved;
	CHECK_EQ(write_inode(f.vol.get(), other_ino, &other), Ok);
}

// Growing and shrinking: the bitmap keeps its length or not (the inode table moves),
// data blocks move out of the way / below the new end, the files stay.
static void test_resize(void)
{
	g_test = "resize";
	const u32 KiB = 1024, MiB = 1024 * 1024, B = 512;
	// 512 byte blocks: a bitmap block covers 4096 blocks (2 MiB).
	Fixture f(4 * MiB, 3 * MiB / 2, 9, 8192);
	Volume* vol = f.vol.get();
	CHECK_EQ(vol->layout.block_bitmap_len, 1);
	f.write_file("/big", 200 * KiB, 1);				// double indirect
	CHECK_EQ(mkdir(vol, P("/d"), 0755, nullptr), Ok);
	f.write_file("/d/a", 30 * KiB, 2);
	char name[32];
	for (u32 i = 0; i < 40; i++)
	{
		std::snprintf(name, sizeof(name), "/d/f%u", i);
		f.write_file(name, 3 * KiB, 10 + i);
	}
	for (u32 i = 0; i < 40; i += 2)
	{
		std::snprintf(name, sizeof(name), "/d/f%u", i);
		CHECK_EQ(unlink(vol, P(name)), Ok);
	}
	f.write_file("/tail", 300 * KiB, 3);
	auto l_files_ok = [&](bool big) {
		if (big)
		{
			CHECK(f.file_is("/big", 200 * KiB, 1));
			CHECK(f.file_is("/tail", 300 * KiB, 3));
		}
		CHECK(f.file_is("/d/a", 30 * KiB, 2));
		for (u32 i = 1; i < 40; i += 2)
		{
			std::snprintf(name, sizeof(name), "/d/f%u", i);
			CHECK(f.file_is(name, 3 * KiB, 10 + i));
		}
	};

	// The plan (mounted).
	ResizeInfo plan;
	CHECK_EQ(resize_plan(vol, 3 * MiB, plan), Ok);
	CHECK(plan.table_moves);
	CHECK(plan.moved_blocks >= 1);					// the root directory's block is in the way
	CHECK_EQ(plan.old_blocks, 3072);
	CHECK_EQ(plan.new_blocks, 6144);
	CHECK(plan.min_size > 0 && plan.min_size < 3 * MiB / 2);
	CHECK_EQ(resize_plan(vol, plan.min_size - B, plan), NoSpace);
	const u32 free0 = f.free_blocks();

	// Growing past the bitmap's capacity: one more bitmap block, the table moves.
	CHECK_EQ(unmount(vol), Ok);
	ResizeInfo info;
	FsckResult check;
	CHECK_EQ(resize(vol, &f.dev, 3 * MiB, &info, &check), Ok);
	CHECK(check.clean());
	CHECK(info.table_moves && info.moved_blocks >= 1);
	CHECK_EQ(mount(vol, &f.dev, false), Ok);
	CHECK_EQ(vol->layout.total_blocks, 6144);
	CHECK_EQ(vol->layout.block_bitmap_len, 2);
	CHECK_EQ(f.free_blocks(), free0 + 3072 - 1);
	l_files_ok(true);
	f.check_fsck();

	// The new space is used: a file past the old end (and past 2 MiB).
	f.write_file("/high", 1600 * KiB, 4);
	CHECK(f.file_is("/high", 1600 * KiB, 4));
	CHECK_EQ(unlink(vol, P("/big")), Ok);
	CHECK_EQ(unlink(vol, P("/tail")), Ok);

	// Shrinking to 2 MiB: the bitmap gets shorter (the table moves back), the blocks past
	// the end move down.
	CHECK_EQ(resize_plan(vol, 2 * MiB, plan), Ok);
	CHECK(plan.table_moves);
	CHECK(plan.moved_blocks > 0);
	CHECK_EQ(unmount(vol), Ok);
	CHECK_EQ(resize(vol, &f.dev, 2 * MiB, &info, &check), Ok);
	CHECK(check.clean());
	CHECK_EQ(info.moved_blocks, plan.moved_blocks);
	CHECK_EQ(mount(vol, &f.dev, false), Ok);
	CHECK_EQ(vol->layout.total_blocks, 4096);
	CHECK_EQ(vol->layout.block_bitmap_len, 1);
	CHECK(f.file_is("/high", 1600 * KiB, 4));
	l_files_ok(false);
	f.check_fsck();

	// Too small: refused, nothing changes.
	CHECK_EQ(resize_plan(vol, 2 * MiB, plan), Ok);
	const u32 min = plan.min_size;
	CHECK_EQ(unmount(vol), Ok);
	CHECK_EQ(resize(vol, &f.dev, min - B, &info, &check), NoSpace);
	CHECK_EQ(mount(vol, &f.dev, false), Ok);
	CHECK_EQ(vol->layout.total_blocks, 4096);
	CHECK(f.file_is("/high", 1600 * KiB, 4));

	// Down to the minimum: no free block left.
	CHECK_EQ(unmount(vol), Ok);
	CHECK_EQ(resize(vol, &f.dev, min, &info, &check), Ok);
	CHECK_EQ(info.new_free_blocks, 0);
	CHECK_EQ(mount(vol, &f.dev, false), Ok);
	CHECK_EQ(f.free_blocks(), 0);
	CHECK(f.file_is("/high", 1600 * KiB, 4));
	l_files_ok(false);

	// Growing within the bitmap (a size that isn't a block multiple: the rest is unused).
	CHECK_EQ(unmount(vol), Ok);
	CHECK_EQ(resize(vol, &f.dev, min + 64 * B + 100, &info, &check), Ok);
	CHECK(!info.table_moves);
	CHECK_EQ(info.moved_blocks, 0);
	CHECK_EQ(mount(vol, &f.dev, false), Ok);
	CHECK_EQ(f.free_blocks(), 64);
	f.write_file("/more", 63 * B, 5);			// 63 data blocks + 1 pointer block
	CHECK_EQ(f.free_blocks(), 0);

	// The device must reach the new end.
	CHECK_EQ(unmount(vol), Ok);
	CHECK_EQ(resize(vol, &f.dev, 8 * MiB, &info, &check), IOError);
	CHECK_EQ(mount(vol, &f.dev, false), Ok);
	CHECK(f.file_is("/more", 63 * B, 5));
}

int main(void)
{
	test_layout();
	test_mkfs_mount();
	test_pointer_boundaries();
	test_pointer_cache();
	test_sparse();
	test_directories();
	test_links();
	test_nospace();
	test_fsck_detects();
	test_resize();
	if (g_failures == 0)
		std::printf("dfs_test: all %d checks passed\n", g_checks);
	else
		std::printf("dfs_test: %d of %d checks FAILED\n", g_failures, g_checks);
	return g_failures == 0 ? 0 : 1;
}
