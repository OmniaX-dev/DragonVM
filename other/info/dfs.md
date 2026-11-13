# DFS — the Dragon File System

A guide (the ideas, the DScript SDK, Drake's kernel loading line by line): `dfs-sdk.md`.

A simplified ext2: superblock, inode bitmap, block bitmap, inode table, data blocks. Inodes have 12 direct pointers,
a single-indirect and a double-indirect one; directories are files made of fixed size entries.

- Core (C++): `src/storage/dfs/DfsCore.hpp/.cpp`
- Core (DScript, in the SDK): `extra/ds/dragon-sdk/src/dfs.ds`, `dr::dfs`. A function for function port of the C++
  core: a change to one goes into the other.
- High level API (DScript, in the SDK): `extra/ds/dragon-sdk/src/file.ds`, `dr::fs` (paths, open files, directories)
- fsck (host only): `src/storage/dfs/DfsCheck.hpp/.cpp`
- Resizing (host only): `src/storage/dfs/DfsResize.hpp/.cpp`
- `storage::DFS` (detection for the Partition Manager, a device on a `Disk`): `src/storage/DFS.hpp/.cpp`
- `storage::DfsVolume` (host tools: a mounted volume on a partition, recursive copies in and out of the host, recursive
  delete): `src/storage/DfsVolume.hpp/.cpp`
- Tool: `dtools dfs <image> <partition#> <command>` (`src/tools/ToolsDfs.cpp`)
- Debugger: the Partition Manager formats, checks, resizes and browses DFS partitions (`src/debugger/PartitionManager`,
  `src/debugger/DfsBrowser`)
- Tests: `bin/dfs_test` (`tests/dfs/dfs_test.cpp`, the C++ core), `tests/dscript/sdk_dfs.ds` (the DScript core on a RAM
  disk, in the DScript suite), `tests/dscript/sdk_fs.ds` (`dr::fs`), `tests/dfs/vm_test.sh` (the two on one disk image: each reads and checks what the other
  wrote, and dtools' fsck checks it all)

## Format

All integers are **big endian** (like the machine and DPT). All offsets are partition relative; block `n` is at byte
`n << block_size_log2`. Block size: 512 B to 4 KiB (log2 9..12), 4 KiB by default. Partitions are at most 4 GiB, files
at most 4 GiB - 1 (u32 sizes, no 64 bit fields anywhere).

### Block 0

| Bytes | Contents |
|---|---|
| 0x000-0x01F | reserved for the DPT boot header (0xF1C4 on bootable partitions), **never written by DFS** |
| 0x020-0x11F | superblock (256 B), at this fixed offset whatever the block size |
| 0x120-end   | zero |

### Layout

```
[block 0][inode bitmap][block bitmap][inode table][data blocks ...]
```

mkfs parameters: the partition size, `block_size_log2` (12), `bytes_per_inode` (16384).

```
total_blocks      = partition_size >> log2              (a tail smaller than a block is unused)
inodes_per_block  = block_size / 128
inode_count       = round_up(max(partition_size / bytes_per_inode, inodes_per_block, 16), inodes_per_block)
bits_per_block    = block_size * 8
inode bitmap      = block 1, ceil(inode_count / bits_per_block) blocks
block bitmap      = next,    ceil(total_blocks / bits_per_block) blocks   (bit n = block n, all blocks)
inode table       = next,    inode_count / inodes_per_block blocks
first data block  = next     (mkfs fails if it's >= total_blocks)
```

Example: 5 MiB with 4 KiB blocks gives 1280 blocks and 320 inodes. The inode bitmap is at block 1, the block bitmap at
block 2, the inode table at blocks 3-12 (320 × 128 B = 10 blocks), and the first data block is 13.

Bitmaps: bit `n` is bit `n % 8` (LSB first) of byte `n / 8`. mkfs marks as used: inodes 0-7, the blocks before the first
data block, and the bits past the end in the last block of each bitmap.

### Superblock (256 B)

| Offset | Field | |
|---|---|---|
| 0x00 | magic | 0x44465331 ('DFS1') |
| 0x04 | version | 1 |
| 0x08 | block_size_log2 | |
| 0x0C | total_blocks | |
| 0x10 | inode_count | |
| 0x14 | inode_size | 128 (readers use this field) |
| 0x18 | dirent_size | 64 (same) |
| 0x1C | inode_bitmap_block, inode_bitmap_len | |
| 0x24 | block_bitmap_block, block_bitmap_len | |
| 0x2C | inode_table_block, inode_table_len | |
| 0x34 | first_data_block | |
| 0x38 | root_inode | 1 |
| 0x3C | first_inode | 8 (0 null, 1 root, 2-7 reserved; 2 = a future journal) |
| 0x40 | free_blocks, free_inodes | written on unmount / sync |
| 0x48 | feature_compat, feature_ro_compat, feature_incompat | unknown bits: ignore / mount read only / refuse. v1 defines none |
| 0x54 | state | 1 clean, 2 dirty (set on a read-write mount, cleared on unmount) |
| 0x58 | mount_count | |
| 0x5C | created, last_mount, last_write | seconds, from the device's clock |
| 0x68 | journal_inode | 0 (reserved) |
| 0x6C | checksum | 0 (reserved) |
| 0x70 | uuid[16] | |
| 0x80 | label[16] | not null terminated when 16 long |
| 0x90 | reserved[112] | |

### Inode (128 B)

| Offset | Field | |
|---|---|---|
| 0x00 | u16 mode | Unix: the type in the high 4 bits (0x8000 file, 0x4000 directory, 0x2000 character device, 0xA000 symlink: reserved), permissions in the low 12 (stored, not enforced) |
| 0x02 | u16 links | |
| 0x04 | u16 uid, u16 gid | |
| 0x08 | u32 flags | |
| 0x0C | u32 size | |
| 0x10 | reserved | |
| 0x14 | atime | set at creation (reads don't write the inode) |
| 0x18 | mtime, ctime, crtime | |
| 0x24 | direct[12] | |
| 0x54 | indirect | |
| 0x58 | dindirect | |
| 0x5C | reserved (×3) | (a triple indirect pointer, xattrs, a checksum) |
| 0x68 | reserved[24] | |

A character device stores its device number in `direct[0]`.

Block number 0 means "no block": unallocated blocks read as zeros (holes). With `P = block_size / 4`, file block `i`
is `direct[i]` (i < 12), entry `i - 12` of the indirect block (i < 12 + P), or, from there, entry `(i - 12 - P) / P` of
the double indirect block, then entry `(i - 12 - P) % P` of that block. Pointer blocks are allocated when needed.

### Directory entry (64 B)

| Offset | Field | |
|---|---|---|
| 0x00 | u32 inode | 0: an empty slot |
| 0x04 | u8 name_len | 1-56 |
| 0x05 | u8 type | the inode's mode >> 12 |
| 0x06 | u16 reserved | |
| 0x08 | name[56] | not null terminated when 56 long; no '/' and no NUL |

Directories are whole blocks, without holes. Each has `.` and `..` (the root's `..` is itself). A new entry takes the
first empty slot, or a new block at the end.

Link counts: a file has one link per entry pointing to it. A directory has 2 + the number of its subdirectories (its
entry in the parent, its own `.`, and each child's `..`). Directories can't have hard links.

## Crash safety (write order)

When creating, the bitmap bit is set before anything points to the block or inode. When deleting, the pointer or the
entry goes first (truncate writes the inode, or clears a pointer block's entries, before freeing the blocks), and the
bit is cleared last. While renaming or linking, the link count is raised before the new entry exists. After a crash
there can be leaked blocks or inodes, a link count that's too high, or stale free counters (fsck reports all of
these). A block can never have two owners.

## Resizing

The layout only depends on the block count (and the inode count, which never changes), so a resized file system is
exactly what mkfs would have made for the new size, plus the data. `dfs::resize()` (host only) does it in three steps:

1. When the block bitmap gets shorter (shrinking past a multiple of `bits_per_block` blocks: 128 MiB with 4 KiB
   blocks, 2 MiB with 512 B blocks), the inode table moves back first. The end of its old place becomes free data.
2. Every block in use that is outside the new data area moves to a free block inside it, and the pointer to it
   (in the inode, a pointer block, or a double indirect block) is updated. These are the blocks past the new end
   when shrinking, and the blocks the longer bitmap needs when growing.
3. When the bitmap gets longer, the inode table moves forward into the space step 2 freed. Then the new bitmap and
   the superblock are written.

The file system has to be clean (fsck) before, and it's checked again after. It can shrink down to the size its
blocks use, plus the metadata (`ResizeInfo::min_size`), and grow up to 4 GiB. A resize isn't crash safe. When
shrinking, resize before the partition gets smaller. When growing, resize after it got bigger: the device must reach
the new end.

```
dtools dfs disk.drd 2 resize          # to the partition's size (after growing the partition)
dtools dfs disk.drd 2 resize 6M       # smaller (before shrinking the partition)
```

## In the debugger

Disk Manager → Partitions... opens the Partition Manager. It shows each partition's content (DFS with its usage, or
empty / raw / unknown), and the selected partition's file system: usage, layout, state, how far it can be resized,
and whether Drake can boot it.

- **Format...** makes a DFS volume (label, block size, bytes per inode; the resulting layout is shown first).
- **Check** runs fsck. The problems go to the console.
- **Browse...** opens the files in a tree. You can import host files and folders (or drop them on the tree), extract
  files and folders, create folders, rename, and delete. Each change is written to the disk at once. On one of the
  machine's disks, every change is an edit in the history (Step back undoes it).
- Table changes wait for **Apply**, which lists them first. With "Move and resize contents" on (the default), a
  partition whose start changed has its data moved, and a DFS partition whose size changed has its file system
  resized with it. A partition that's too small for its files is refused. Everything is done on a copy of the disk,
  then written, so a failure writes nothing.

## Core API (layers)

1. **L0 device**: `Device { user, read(offset, buffer, size), write(...), now() }`, partition relative bytes.
2. **L1 volume**: `mkfs`, `mount` (read only on request or for unknown ro_compat features), `unmount`, `sync`.
3. **L2 allocators**: `alloc_block` / `free_block` / `alloc_inode` / `free_inode`. The bitmap search starts at a
   hint kept in RAM.
4. **L3 inodes**: `read_inode`, `write_inode`, `bmap` (one function for lookups and allocation), and `truncate`
   (which frees the pointer blocks that become empty, and zeros the rest of the last block).
5. **L4 data**: `inode_read`, `inode_write`.
6. **L5 directories**: `dir_lookup`, `dir_add`, `dir_remove`, `dir_next` (with a cursor), `dir_is_empty`. They are
   built on one loop, `dir_find`.
7. **L6 paths**: `resolve`, `resolve_parent`. Paths are absolute, repeated `/` collapse, and `.`/`..` are ordinary
   entries.
8. **L7 operations**: `create`, `mkdir`, `unlink`, `rmdir`, `rename` (the destination must not exist; a directory
   can't move into itself), `link`, `stat`, `chmod`, `chown`.

Errors are negative `dfs::Error` values. All the memory is in `Volume`, about 28 KiB with seven 4 KiB buffers, each
with a fixed role (see `DfsCore.hpp`). Open files and `/dev` aren't in the file system: those belong to the OS.

## DScript (`dr::dfs`)

The same API as the C++ core (the names, the error values, the buffers). The on disk structures are copied as they are
(the machine is big endian). It has no globals (the SDK is also used in ROM), and adds about 41 KiB to a program (the
whole SDK is linked into every program, for now).

```
var part: dr::dfs::DiskPartition;
var dev: dr::dfs::Device;
dr::dfs::dpt_partition(&part, &dpt.entries[0]);     // a DPT entry on the boot disk
dr::dfs::disk_device(&dev, &part);                  // reads and writes through the BIOS (int 0x20)
var vol: dr::dfs::Volume* = cast<dr::dfs::Volume*>(dr::malloc(sizeof(dr::dfs::Volume)));   // ~28 KiB
dr::dfs::mount(vol, &dev, false);
dr::dfs::write_file(vol, "/etc/motd", "hello\n", 6);
var n: i32 = dr::dfs::read_file(vol, "/etc/motd", buffer, size);
dr::dfs::unmount(vol);
```

`read_file` / `write_file` (create or replace) are the only additions to the C++ API. There is no clock yet: the
timestamps a VM writes are 0.

## Booting from DFS

A bootable partition keeps the DPT boot header (0xF1C4, then 0xFF) in its first 32 bytes, which DFS never writes, and
the kernel is the file `/boot/kernel0`. Drake loads it at 0x01410000 (at most 1 MiB) and jumps to it. A partition
without DFS is loaded the old way (the 1 MiB after the header). If the partition has DFS but the kernel can't be
loaded, Drake shows why under its list and doesn't jump.

```
dtools dfs disk.drd 2 mkfs
dtools dfs disk.drd 2 install-kernel dragon/kernel0.bin    # /boot/kernel0 (a KERNEL0_BOOT header is removed),
                                                          # and the partition's boot header
```

The partition needs the DPT boot flag too (`new-dpt ... -f boot`). `dsc -D DRAKE_AUTOBOOT` builds a Drake that boots
its first entry without a key (headless tests with `drun --screen`).
