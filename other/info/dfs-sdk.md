# DFS from DScript: a guide

This guide explains how DFS works and how to use it from DScript, then follows Drake's `load_kernel0_dfs` line by
line, down to the disk reads.

The SDK has two levels:
- **`dr::fs`** (`file.ds`): the easy one, with paths, open files, true/false and `last_error()`. Most programs only
  need this. It is described in [section 8](#8-drfs-the-high-level-api).
- **`dr::dfs`** (`dfs.ds`): the file system itself, with inodes, block buffers and error codes. `dr::fs` is built on
  it, and Drake uses it directly.

Sections 1 to 7 explain `dr::dfs`, because knowing it makes `dr::fs` obvious. The exact format (every field and offset) is in `dfs.md`. This
guide explains how the pieces fit together.

- [1. The ideas](#1-the-ideas)
- [2. A worked layout: the 4 MiB boot partition](#2-a-worked-layout-the-4-mib-boot-partition)
- [3. The SDK: how the code is organised](#3-the-sdk-how-the-code-is-organised)
- [4. `load_kernel0_dfs`, line by line](#4-load_kernel0_dfs-line-by-line)
- [5. Writing: what changes on disk](#5-writing-what-changes-on-disk)
- [6. More examples](#6-more-examples)
- [7. Rules of thumb](#7-rules-of-thumb)
- [8. `dr::fs`: the high level API](#8-drfs-the-high-level-api)


## 1. The ideas

A partition is just a range of bytes on a disk. A file system decides how to store named files in it. DFS is a
simplified ext2, which you can understand with five ideas.

### Blocks

DFS cuts the partition into **blocks** of the same size: 4 KiB by default (512 B, 1 KiB or 2 KiB are possible too).
Block `n` starts at byte `n * block_size` of the partition. Everything is stored in whole blocks, so "block 37" is
an address.

### The superblock

Something must say how the partition is organised: the block size, how many blocks and files there are, and where
each area starts. That is the **superblock**, 256 bytes at byte **0x20** of the partition, whatever the block size.
It is at 0x20 rather than 0 because the first 32 bytes belong to the DPT boot header (`F1 C4 FF FF ...`), which marks
a partition as bootable. DFS never writes those 32 bytes.

The first thing any DFS code does is read the superblock (`mount`). If byte 0x20 doesn't start with the magic number
`DFS1`, the partition isn't DFS.

### Inodes: a file is a number

A file's name is not the file. The file itself is an **inode**: a 128-byte record that holds:
- its type (file or directory) and permissions
- its size in bytes
- its link count
- its timestamps
- the numbers of the blocks that hold its content

All the inodes sit in one array, the **inode table**, and a file *is* its index in that array: "inode 9". Inode 1
is always the root directory `/`. Inodes 0 and 2-7 are reserved, so the first real file is inode 8.

An inode has no name. Names live in directories (see below). That is why one file can have two names (a hard link:
two directory entries with the same inode number).

### How an inode finds its content: the block pointers

An inode has room for 15 block numbers:

```
direct[0..11]   the file's blocks 0..11, directly
indirect        a block that is itself a list of block numbers: the file's blocks 12, 13, ...
dindirect       a block listing blocks that list block numbers: the blocks after those
```

With 4 KiB blocks, a pointer block holds 4096 / 4 = 1024 block numbers. So a file can have 12 direct blocks (48 KiB),
plus 1024 through `indirect` (4 MiB), plus 1024 × 1024 through `dindirect` (4 GiB).

The function that turns "the file's block number i" into "the disk's block number" is **`bmap`**. Everything that
reads or writes file content goes through it. A pointer of 0 means "no block": that part of the file is a **hole**
and reads as zeros.

### Directories are files of names

A directory is an inode whose content is a list of 64-byte **entries**: an inode number, the name's length, a type
byte and the name (up to 56 bytes). A 4 KiB directory block holds 64 entries. Every directory starts with `.` (itself)
and `..` (its parent). An entry with inode number 0 is an empty slot.

To find `/boot/kernel0`, DFS:
1. starts at inode 1 (the root)
2. reads its entries, looking for `boot`, and finds inode 8
3. reads inode 8's entries, looking for `kernel0`, and finds inode 9

This is **path resolution** (`resolve`).

### Bitmaps: what is free

DFS has two **bitmaps**, one bit per inode and one bit per block: 1 means used, 0 means free. Creating a file finds a
0 bit in the inode bitmap and sets it. Growing a file finds 0 bits in the block bitmap. Only writing touches the
bitmaps: reading never looks at them.

### Putting it together

```
partition:  [block 0      ][inode bitmap][block bitmap][inode table ...][data blocks ..................]
             │ 0x00 boot header (32 B, not DFS's)
             │ 0x20 superblock (256 B)
```

The data blocks hold everything else: file contents, directory entries and pointer blocks.


## 2. A worked layout: the 4 MiB boot partition

These are the numbers `dtools dfs disk.drd 1 mkfs` gave for the 4 MiB partition in the boot test, with the kernel
installed (`dtools dfs disk.drd 1 info` shows them):

| | |
|---|---|
| block size | 4096 B |
| total blocks | 1024 (4 MiB / 4 KiB) |
| inodes | 256 (one per 16 KiB of partition) |
| block 0 | boot header + superblock |
| block 1 | inode bitmap (256 bits: only the start of the block is used) |
| block 2 | block bitmap (1024 bits) |
| blocks 3-10 | inode table: 256 × 128 B = 32 KiB = 8 blocks (32 inodes per block) |
| blocks 11-1023 | data |

| Inode | What |
|---|---|
| 1 | `/`: a directory of 1 block (`.`, `..`, `boot`) |
| 8 | `/boot`: a directory of 1 block (`.`, `..`, `kernel0`) |
| 9 | `/boot/kernel0`: a file of 1 MiB = 256 blocks: 12 direct + 244 through `indirect` |

Where an inode is: inode `n` is at byte `n × 128` of the inode table. So inode 9 is at byte 1152: block 3 (the table's
first block), offset 1152. The root (inode 1) is at offset 128 of the same block.


## 3. The SDK: how the code is organised

`#import(<dragon-sdk>)` gives you `dr::dfs`. The code is in `extra/ds/dragon-sdk/src/dfs.ds`. It is a copy, function
for function, of the C++ core that `dtools` and the debugger use (`src/storage/dfs/DfsCore.cpp`). Both write exactly
the same bytes, and `tests/dfs/vm_test.sh` checks that.

### The layers

Each layer only uses the ones below it:

| Layer | Subs | What they do |
|---|---|---|
| L0 device | `Device`, `disk_device`, `dpt_partition` | read/write bytes of the partition |
| L1 volume | `mkfs`, `mount`, `unmount`, `sync` | the superblock: format, open, close |
| L2 allocators | `alloc_block`, `free_block`, `alloc_inode`, `free_inode` | the bitmaps |
| L3 inodes | `read_inode`, `write_inode`, `bmap`, `truncate` | inode records, block pointers |
| L4 data | `inode_read`, `inode_write` | a file's bytes, at any offset |
| L5 directories | `dir_lookup`, `dir_add`, `dir_remove`, `dir_next`, `dir_is_empty` | entries |
| L6 paths | `resolve`, `resolve_parent` | `"/a/b/c"` → an inode number |
| L7 operations | `create`, `mkdir`, `unlink`, `rmdir`, `rename`, `link`, `stat`, `chmod`, `chown` | what a shell does |
| helpers | `read_file`, `write_file` | a whole file by path |

Most programs only need L1 and L7, plus `inode_read`/`inode_write` or the helpers.

### Errors: every sub returns an `i32`

There are no exceptions in DScript. Every sub that can fail returns an `i32`:
- **`>= 0`** means success, or a useful value (`inode_read` returns the number of bytes read).
- **`< 0`** means an error: `dr::dfs::NotFound` (-1), `Exists`, `NoSpace`, `NotDir`, `IsDir`, `NotEmpty`, `Invalid`,
  `IOError`, `Corrupt`, `TooBig`, `NameTooLong`, `ReadOnly`, `Unsupported` (-13, "not a DFS partition") or
  `TooManyLinks`.

`dr::dfs::error_name(r)` gives a description ("not found"). Results bigger than a number go through out pointers:
`stat(vol, path, &node)` fills `node`. The usual pattern:

```
var r: i32 = dr::dfs::something(...);
if (r < 0)
	return r;		// pass the error up
```

### The Volume: all the memory, in one place

`dr::dfs::Volume` holds everything DFS needs while a partition is mounted:
- the device
- the superblock
- the layout (numbers computed from the superblock)
- 7 block buffers of 4 KiB

That makes about 28 KiB, and DFS allocates nothing else. Each buffer has one job: `buf_meta` for the bitmaps,
`buf_inode` for inode table blocks, `buf_ind` for pointer blocks, `buf_dir` for directory blocks, and so on. That is
how a sub can read a directory block while `bmap` reads a pointer block, without the two overwriting each other.

28 KiB is too big for the stack, so put the Volume on the heap (`dr::malloc`) or at a fixed address. One Volume is
one mounted partition. Two partitions at the same time need two Volumes.

Two of the buffers are also **caches**. `buf_ind` keeps the last pointer block that `bmap` read, and `buf_ind2` the
last double-indirect block (`vol->ind_block` and `vol->ind2_block` say which block each holds). When a file is read
in order, each pointer block is read from the disk once instead of once per data block. The caches can't go stale:
these blocks are only changed in those buffers, and any other write of the same block number (`write_block`) drops
it from the cache.

The order of the Volume's fields matters in DScript, where structs are packed. The buffers come right after fields
whose sizes are multiples of 4 bytes (the `bool`s and `u16`s are at the end), so they start at 4-byte aligned
addresses. `mem_copy` copies words only between aligned addresses. Misaligned buffers made every block copy about
9× slower, and that was the case before this was fixed.

### The device: how DFS reaches the disk

DFS doesn't know about disks, the BIOS or the DPT. It only calls the sub pointers in a `Device`:

```
struct Device {
	var user: void*;                                // anything: passed back to the subs
	var read: sub(void*, u32, u8*, u32) -> i32;     // (user, offset, buffer, size)
	var write: sub(void*, u32, u8*, u32) -> i32;
	var now: sub(void*) -> u32;                     // the time, for timestamps (may be null)
};
```

The offsets are **partition relative**: offset 0 is the partition's first byte. The SDK gives you a device for a
partition of a machine disk:
- `DiskPartition { disk, start, size }` says where the partition is.
- `dpt_partition(&part, entry)` fills it from a DPT entry (on the boot disk).
- `disk_device(&dev, &part)` makes the device. Its `read`/`write` add `start` to the offset, check that the access
  stays inside `size`, and ask the BIOS to do the transfer (`int 0x20`, `INT20_LOAD_FROM_DDD_BLOCKING`: the sub returns
  when the bytes are there). So the BIOS must have run, which is always true for Drake and the kernel.

Because of this indirection, the same code runs on a RAM disk in `tests/dscript/sdk_dfs.ds`: its device's
`read`/`write` just copy memory.

### Big endian, for free

DFS stores numbers big endian on disk, which is also the machine's order. So the DScript structs `Superblock` and
`Inode` have exactly the on-disk layout (DScript structs are packed): reading an inode is a plain 128-byte copy from
the block buffer into an `Inode` variable, and `get32(p)` is just `*cast<u32*>(p)`. The C++ side, on a little endian
PC, has to swap the bytes. That is why it has `encode_inode`/`decode_inode` and DScript doesn't.


## 4. `load_kernel0_dfs`, line by line

This is Drake's code (`extra/ds/drake/entry.ds`). Its job: if the selected boot partition holds DFS, copy the file
`/boot/kernel0` to address 0x01410000, where Drake then jumps. It returns `Ok`, or a negative error. Its caller,
`load_kernel0`, treats `Unsupported` ("not DFS") as "use the old raw kernel block". It shows any other error on the
screen and doesn't boot.

```
sub load_kernel0_dfs(var part: dr::DPTPartitionBlock*) -> i32 {
```
`part` is the partition's entry in the DPT, the table at disk byte 0x400000 that Drake read at startup. Among other
things it holds `startAddress` (where the partition begins on the disk) and `size`.

---

```
	var vol: dr::dfs::Volume* = cast<dr::dfs::Volume*>(dr::malloc(sizeof(dr::dfs::Volume)));
	if (not vol)
		return dr::dfs::NoSpace;
```
These lines allocate the Volume (about 28 KiB) on the BIOS heap. It doesn't need to be cleared: `mount` sets
everything it uses. If the heap is full, they return an error instead of crashing.

---

```
	defer dr::free(vol);
```
`defer` runs `dr::free(vol)` whenever the sub returns, on every `return` below, so there is no leak whichever way the
sub exits. If several defers apply, they run in reverse order: here, `unmount` (below) runs first, then `free`.

---

```
	var disk: dr::dfs::DiskPartition;
	var dev: dr::dfs::Device;
	dr::dfs::dpt_partition(&disk, part);
	dr::dfs::disk_device(&dev, &disk);
```
These lines build the device (section 3):
- `dpt_partition` copies the boot disk number, `part->startAddress` and `part->size` into `disk`.
- `disk_device` fills `dev`: `dev.user = &disk`, `dev.read = disk_read`, `dev.write = disk_write`, `dev.now = null`.

From now on, "read 256 bytes at offset 0x20" becomes "read 256 bytes at disk address `startAddress + 0x20`" through
the BIOS. `disk` and `dev` are locals, which is fine because the volume is only used inside this sub. A device must
not outlive its `DiskPartition`.

---

```
	var r: i32 = dr::dfs::mount(vol, &dev, true);
	if (r < 0)
		return r;
```
`mount` opens the file system. The `true` means **read only**: Drake must never change the disk, and with
`read_only`, every write inside DFS fails with `ReadOnly` before reaching the device. What `mount` does:

1. It copies `dev` into `vol->dev` and clears `vol->buf_zero`.
2. **It reads the superblock**: 256 bytes at partition offset 0x20, straight into `vol->sb`. This is one disk read.
3. It checks:
   - `sb.magic == 'DFS1'`, otherwise it returns **`Unsupported`**. That is the "not a DFS partition" case: on a
     partition with the old raw kernel, offset 0x20 is kernel code, so the magic doesn't match.
   - `sb.version == 1` and no unknown "incompatible" feature bits.
   - The layout: `layout_from_superblock` recomputes every position from the block size and the counts (as `mkfs`
     did), and compares the result with what the superblock says. A damaged superblock gives `Corrupt`. The
     computed numbers go in `vol->layout`, and all the other subs use them: `block_size`, `inode_table_block`,
     `pointers_per_block`, and so on.
   - The root is inode 1 and the first file is inode 8.
4. Read only: done. (A read-write mount would also mark the superblock "dirty", count the mount and write it back.)

So a failed `mount` is not necessarily a problem: `Unsupported` is how Drake learns that it should use the old boot
method.

---

```
	defer dr::dfs::unmount(vol);
```
This closes the volume on the way out. On a read-only volume, `unmount` only sets `vol->mounted = false`. On a
read-write volume it would write the superblock back (state "clean", and the free block and inode counters), which
matters. It is a good habit to always pair `mount` with `defer unmount`.

---

```
	var node: dr::dfs::Inode;
	r = dr::dfs::stat(vol, KERNEL0_PATH, &node);
	if (r < 0)
		return r;
```
`KERNEL0_PATH` is `"/boot/kernel0"`. `stat` finds the path's inode and copies its 128-byte record into `node`. This
is where most of the work happens. With the layout from section 2:

1. `stat` calls `resolve`, which calls **`walk`** with `parent = false`. `walk` starts at inode 1 (the root) and
   reads the path one name at a time, skipping `/`s:
2. **Name `boot`** (4 bytes):
   - `read_inode(1)`: inode 1 is at byte 128 of the inode table, so DFS reads block 3 into `buf_inode` (a disk
     read) and copies 128 bytes from offset 128 into a local `Inode`. That is the root's record.
   - It is a directory (`mode & 0xF000 == 0x4000`). Otherwise the result would be `NotDir`: a file in the middle of
     a path.
   - `dir_lookup(root, "boot")` calls **`dir_find`**, the one loop over a directory's entries:
     - The directory's size is one block (4096 bytes), so it has 4096 / 64 = 64 entries.
     - For entry 0 it needs the directory's block 0: `bmap(root, 0)` gives `root.direct[0]` (say block 11). It reads
       block 11 into `buf_dir` (a disk read).
     - It compares each used entry's name with `boot` (the same length, the same bytes). Entry 0 is `.`, entry 1 is
       `..`, entry 2 is `boot`, which matches. Its inode number is **8**.
3. **Name `kernel0`**: the same steps with inode 8. `read_inode(8)` reads block 3 again (inode 8 is at byte 1024).
   `/boot`'s block 0 is read, the entry `kernel0` matches, and its inode number is **9**.
4. The path is finished, so `walk` returns 9. `stat` does `read_inode(9)` (block 3, byte 1152) into `node`.

The possible errors:
- `NotFound`: no `/boot` or no `kernel0`. This is what Drake showed in the test where the kernel was removed.
- `NotDir`: `/boot` is a file.
- `Corrupt`: a damaged directory.
- `IOError`: the disk failed.

---

```
	if ((node.mode & dr::dfs::ModeTypeMask) != dr::dfs::ModeFile)
		return KernelNotAFile;
```
`node.mode` holds the type in its top 4 bits (`ModeTypeMask` = 0xF000) and the permissions in the other 12.
`/boot/kernel0` must be a regular file (`ModeFile`, 0x8000), not a directory.

`KernelNotAFile` (like `KernelEmpty` and `KernelTooBig` below) is one of Drake's own error codes. They are -100 and
below, so they can't clash with `dr::dfs`'s (-1 to -14). Drake's `kernel_error_name` turns them into "not a file",
"empty file" and "bigger than 1 MiB", and passes any other code to `dr::dfs::error_name`.

---

```
	if (node.size == 0)
		return KernelEmpty;
	if (node.size > dr::DPTBootPartStructure.CodeSize)
		return KernelTooBig;
```
`node.size` is the file's size in bytes. An empty file is refused, because there would be nothing to jump to. The
kernel area at 0x01410000 is 1 MiB (`CodeSize`), and a bigger file would overwrite whatever comes after it.

---

```
	r = dr::dfs::inode_read(vol, &node, 0, cast<u8*>(dr::DPTBootPartStructure.CodeLoadAddr), node.size);
	if (r < 0)
		return r;
```
`inode_read(vol, node, offset, buffer, size)` copies `size` bytes of the file, starting at `offset`, to `buffer`. It
returns the number of bytes copied. Here it copies the whole file (offset 0, `node.size` bytes) directly to address
0x01410000. `inode_read` takes an `Inode*`, not a path: `stat` already found the record.

It works one block at a time. For each position `pos` (0, 4096, 8192, ...):
1. The file block is `pos / 4096`. **`bmap`** turns it into a disk block:
   - File blocks 0-11: `node.direct[i]`, no disk read.
   - File blocks 12-255: through `node.indirect`. `bmap` takes entry `i - 12` (4 bytes, a block number) of that
     pointer block. It reads the pointer block into `buf_ind` for block 12 only. For blocks 13-255 it is still in
     the cache (section 3), so there is no disk read.
2. Then one of three cases:
   - **A whole block** (the case here: the kernel's size is a multiple of 4096) is read **straight into the
     destination**. There is no copy through a buffer: one disk read of 4096 bytes goes directly to 0x01410000 +
     pos.
   - **A partial block** (a file's last block, when the size isn't a multiple of 4096) is read into `buf_data`, and
     only the needed bytes are copied. So nothing past `node.size` is written to memory.
   - **A hole** (pointer 0) is filled with zeros.

For the 1 MiB kernel that makes 256 data block reads, plus one read of the indirect block.

`inode_read` only fails with `IOError` or `Corrupt` (for example a pointer to a block outside the partition, which
`read_block` checks).

---

```
	return dr::dfs::Ok;
}
```
The kernel is in memory. On the way out the defers run: `unmount(vol)`, then `free(vol)`. Back in
`execute_selected_entry`, Drake removes its interrupt handlers and jumps to 0x01410000.

### Disk reads, all together

For the partition in section 2:

| Step | Reads |
|---|---|
| mount | the superblock (256 B) |
| stat | inode table block 3 (×3: inodes 1, 8, 9), `/`'s block, `/boot`'s block |
| inode_read | 256 data blocks, plus the indirect block once |

The DFS logic itself is only a few hundred instructions per block. The time goes into the transfers.

For comparison, on the RAM disk of `tests/dscript/sdk_dfs.ds` (1 KiB blocks): a `read_inode` takes about 3,100
cycles, of which 2,600 are the 1 KiB copy. Mounting takes about 10,000 cycles, and creating a small file about
68,000.


## 5. Writing: what changes on disk

Reading never changes the disk. Writing does, and the order of the writes is chosen so that a crash (the power
cut, the machine reset) can only *waste* space, never corrupt files. For example, a block's bit in the bitmap is
set before anything points to the block. `dtools dfs <img> <n> fsck` finds such waste afterwards.

What `dr::dfs::write_file(vol, "/etc/motd", text, 6)` does (the volume mounted read-write):

1. `stat("/etc/motd")`. If the file doesn't exist (`NotFound`), `create`:
   - `resolve_parent` finds `/etc`'s inode and the last name, `motd`.
   - `alloc_inode` finds a free bit in the inode bitmap and sets it.
   - `write_inode` writes a fresh record (a file, 1 link, size 0).
   - `dir_add` writes a `motd` entry into `/etc`'s first free slot. If the directory is full, it gets a new block.
2. If the file exists, `truncate` it to 0 first, which frees its blocks.
3. `inode_write(..., 0, text, 6)`: for block 0, `bmap(allocate = true)` finds a free block in the block bitmap
   (`alloc_block`). The 6 bytes are written into it (the rest of the block is zeros). Then the inode is written with
   `direct[0]` and `size = 6`.
4. Later, `unmount` writes the superblock: the free counters, and the state "clean".

**Always `unmount` (or `sync`) a read-write volume.** The free block and inode counts are only kept in RAM
(`vol->sb`) until then. Forgetting doesn't lose files, but the superblock stays "dirty" with stale counters, and
`fsck` reports it.


## 6. More examples

### Listing a directory

`dir_next` gives the entries one by one. The cursor is an entry index: start at 0, and `dir_next` moves it past each
entry it returns. It returns `NotFound` at the end.

```
sub list(var vol: dr::dfs::Volume*, var path: string) -> i32 {
	var dir: dr::dfs::Inode;
	var r: i32 = dr::dfs::stat(vol, path, &dir);
	if (r < 0)
		return r;
	var cursor: u32 = 0;
	var entry: dr::dfs::DirEntry;
	while (dr::dfs::dir_next(vol, &dir, &cursor, &entry) == dr::dfs::Ok) {
		print(entry.name);                  // null terminated; entry.inode, entry.type (mode >> 12)
		print("\n");
	}
	return dr::dfs::Ok;
}
```
(`print` stands for whatever text output the program uses: `dr::TextSingle`, a `Text16Console`, ...)

### Reading part of a big file

You don't need to load a whole file:

```
var node: dr::dfs::Inode;
var ino: u32 = 0;
if (dr::dfs::stat(vol, "/data/level3.map", &node, &ino) == dr::dfs::Ok) {
	var header: u8[64];
	var n: i32 = dr::dfs::inode_read(vol, &node, 0, header, 64);     // n < 64 if the file is shorter
	...
	n = dr::dfs::inode_read(vol, &node, 4096 * 10, chunk, 4096);     // any offset
}
```

### A full session, read-write

```
var dpt: dr::DPTBlock;
if (not dr::dpt_load_block(&dpt))
	return -1;
var part: dr::dfs::DiskPartition;
var dev: dr::dfs::Device;
dr::dfs::dpt_partition(&part, &dpt.entries[2]);         // the third partition
dr::dfs::disk_device(&dev, &part);
var vol: dr::dfs::Volume* = cast<dr::dfs::Volume*>(dr::malloc(sizeof(dr::dfs::Volume)));
defer dr::free(vol);
var r: i32 = dr::dfs::mount(vol, &dev, false);          // false: read-write
if (r < 0)
	return r;
defer dr::dfs::unmount(vol);                            // writes the superblock back
dr::dfs::mkdir(vol, "/saves", 0x1ED);                   // 0755 (DScript has no octal literals); Exists is fine
r = dr::dfs::write_file(vol, "/saves/slot1", save, sizeof(SaveData));
```

### Formatting

```
var params: dr::dfs::MkfsParams = { part.size, 0, 0, "games", null };   // size, block size log2 (0: 4 KiB),
r = dr::dfs::mkfs(vol, &dev, &params);                                   // bytes per inode (0: 16 KiB), label, uuid
```
`mkfs` erases the file system's areas (not the 32-byte boot header) and leaves the volume unmounted.


## 7. Rules of thumb

- Check every `i32`: `< 0` is an error. `error_name(r)` tells you which.
- One `Volume` per mounted partition, on the heap. Pair `mount` with `defer unmount`.
- Paths are absolute (`/a/b`). Repeated `/` are fine. Names are 1-56 bytes, with no `/`.
- `stat` gives an `Inode` (the record). `inode_read`/`inode_write` work on the record, so you can keep it to read a
  file in pieces without resolving the path again. After writing with `inode_write`, the record you passed is up
  to date (size, pointers).
- Mount read only when you only read: it's safe even if something goes wrong.
- Permissions and owners are stored, not enforced: that is the OS's job, later.
- Open files, file handles and `/dev` aren't part of DFS either: an OS builds them on top of inode numbers and
  `inode_read`/`inode_write`.


## 8. `dr::fs`: the high level API

`extra/ds/dragon-sdk/src/file.ds`. Everything in it is commented: this section is the overview.

### The three types

| Type | What it is |
|---|---|
| `dr::fs::FileSystem` | one mounted partition. It owns its `dr::dfs::Volume` (on the heap while mounted). |
| `dr::fs::File` | an open file with a position, made by `FileSystem.open`. |
| `dr::fs::Dir` | a directory being read entry by entry, made by `FileSystem.open_dir`. |

### Errors

Subs return `true`/`false`, or a count with -1 for errors. The reason is in `last_error()` (on the FileSystem, the
File or the Dir), and `error_text()` describes it. The codes are `dr::dfs`'s, plus a few of `dr::fs`'s own:
`NotMounted`, `AlreadyMounted`, `NoPartition`, `NoMemory`, `NotOpen`, `NotReadable`, `NotWritable`, `PathTooLong`
and `BadMode`. `dr::fs::error_name(code)` describes any of them.

### A tour

```
var disk: dr::fs::FileSystem;
if (not disk.mount(2)) {                         // partition 2 of the boot disk (1 = the first, as dtools)
	print(disk.error_text());
	return;
}
defer disk.unmount();                            // always: it writes the superblock back

// Questions
disk.exists("/etc/motd");  disk.is_dir("/etc");  disk.size_of("/etc/motd");
var info: dr::fs::Info;
disk.info("/etc/motd", &info);                   // type, size, links, permissions, owner, times

// Whole files
disk.write_text("/etc/motd", "Welcome\n");       // creates or replaces
disk.append_text("/var/log", "booted\n");        // adds at the end (creates it)
var size: u32 = 0;
var text: u8* = disk.load("/etc/motd", &size);   // dr::malloc'd, 0 terminated: dr::free it
disk.read_all("/etc/motd", buffer, sizeof(buffer));     // into your buffer: the bytes read

// Directories and names
disk.make_dirs("/home/player/saves");            // mkdir -p
disk.copy("/etc/motd", "/home/player/motd");
disk.rename("/home/player/motd", "/home/player/welcome");
disk.link("/home/player/welcome", "/welcome");   // a second name for the same file
disk.remove("/welcome");                         // a file, or an empty directory
disk.remove_all("/home/player");                 // rm -r
disk.resize("/var/log", 0);
disk.set_permissions("/etc/motd", 0x1A4);        // 0644 (stored, not enforced)

// Listing
var dir: dr::fs::Dir;
var entry: dr::fs::Entry;
if (disk.open_dir("/", &dir)) {
	while (dir.next(&entry))                     // never "." or ".."
		... entry.name, entry.type (dr::fs::Type.File / Directory), entry.size ...
	dir.close();
}
// or with a visitor (a lambda, and a context pointer for your data)
disk.list("/", sub(var e: dr::fs::Entry*, var ctx: void*) -> bool { ...; return true; }, null);

// Open files
var file: dr::fs::File;
if (disk.open("/data/scores", dr::fs::OpenMode.ReadWrite, &file)) {
	file.seek(16);                               // from the start (SeekFrom.Current / SeekFrom.End too)
	file.read(&score, 4);                        // the bytes read, 0 at the end
	file.seek(-4, dr::fs::SeekFrom.Current);
	file.write(&new_score, 4);
	file.close();
}
if (disk.open("/etc/config", dr::fs::OpenMode.Read, &file)) {
	var line: u8[128];
	while (file.read_line(line, 128) >= 0)       // without the '\n' (and a '\r' before it)
		...
	file.close();
}
```

`OpenMode` flags are OR'd together:
- `Read` and `Write`.
- `Create`: create the file if it's missing.
- `Truncate`: empty it when opening.
- `Append`: every write goes to the end.
- `Exclusive`: with `Create`, fail if the file exists.

There are three shortcuts: `ReadWrite`, `NewFile` (Write | Create | Truncate) and `AppendFile`
(Write | Create | Append).

### How it maps onto `dr::dfs`

- `FileSystem.mount(n)` reads the DPT, then does what section 4 does by hand: `dpt_partition`, `disk_device`,
  `dr::malloc` of a Volume, and `dr::dfs::mount`. `unmount` is `dr::dfs::unmount` + `dr::free`.
- The path subs call the matching `dr::dfs` sub (`make_dir` → `mkdir`, `remove` → `unlink` or `rmdir`, ...) and
  turn the `i32` into a bool + `last_error`.
- A `File` is only an inode number, a position and the open mode. **Every call reads the inode again** (one inode
  table block), then uses `inode_read`/`inode_write`/`truncate` at the position. So two Files on the same file, and
  the path subs, always see the same size and content. A File holds no memory and no buffer.
- A `Dir` is an inode number and a `dir_next` cursor. `next` skips `.` and `..` and reads each entry's inode for
  its size.
- Unmounting closes every File and Dir of that mount. The FileSystem counts its mounts, and a File remembers the
  count it was opened with. So an old File can't write into a partition mounted later.

### Limits to know

- Paths are absolute. There is no current directory yet (that belongs to an OS's process).
- Removing a file that is still open makes the File's calls fail with `NotFound`. If the inode gets reused by a new
  file meanwhile, the old File would reach the new file. Unix keeps a removed file alive until it is closed; DFS
  doesn't, so don't remove open files.
- Don't copy a `FileSystem` (it points into itself), and don't use it from interrupt handlers.
- Timestamps are 0 until the machine has a clock.
- `dr::fs` adds about 23 KiB to a program. With `dr::dfs`, the file system support is about 65 KiB of the SDK.
