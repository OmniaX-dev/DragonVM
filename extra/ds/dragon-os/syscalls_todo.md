# DragonOS system calls: the plan

## Conventions (all of them)
- `int 0x40`: R9 is the function, R8, R7, R6, R5 the arguments (in that order), RV the result.
  In the stdlib: `std::syscall4(function, a, b, c, d)` (unused arguments are 0, pointers go as
  `cast<u32>(p)`); each typed wrapper is one line on top of it.
- The result: RV >= 0 success (an fd, a byte count, a pid, ...), RV < 0 an error code.
- Error codes: libcore's `errors.ds`, shared by everything (`std::error_name` describes any of
  them). Every code is unique, so the file system calls pass `std::FsErrors` / `std::FileErrors`
  through unchanged. The argument checks use `std::SystemErrors` (`BadAddress`: a pointer check
  failed, `BadHandle`, `InvalidArgument`, `NoMemory`, `TooMany`, `TooLong`, `WouldBlock`,
  `NoSuchCall`), processes `std::ProcessErrors` (`NoChild`, `NoProcess`), Spawn
  `std::ExecErrors`. A new kind of error: a new group there (see the comment at its top).
- Pointers from the process are checked, never trusted:
  - a `string`: copied into a kernel buffer (at most `MaxPath`, 256), checking each new page with
    `proc::user_readable`;
  - an input buffer: the whole range with `user_readable`;
  - an output buffer or struct (`*`): `user_writable`;
  - a failed check returns `BadAddress` (never a kernel fault).
- Structs that cross the boundary (`Info`, `Entry`, `FsInfo`, `SysInfo`, `ProcInfo`): defined
  once in libcore, so both sides have the same layout.

## Process (0x00xx, 0x01xx)

| # | Call | Parameters → result | What it does |
|---|---|---|---|
| 0x0010 | **Exit** | `code: i32` → doesn't return | Ends the calling process: `process_exit(code)`. *(in the stdlib, not handled by the kernel yet)* |
| 0x0011 | **Print** | `text: string, newline: bool` → 0 | Prints to the console. *(exists: needs the `user_readable` check)* |
| 0x0100 | **GetPid** | → `pid` | The caller's pid. |
| 0x0101 | **GetParentPid** | → `pid` | The pid that spawned it (0 for the first process). |
| 0x0102 | **Yield** | → 0 | Gives up the rest of its turn: `g_current->frame = frame; Ready; schedule()`. |
| 0x0103 | **Spawn** | `path: string, argc: u32, argv: string*` → `pid` | Starts a DRX program as a new process (`process_start`); the caller keeps running. |
| 0x0104 | **Wait** | `pid: i32` (-1: any child), `out_code: i32*` → `pid` | Blocks until that child ends, writes its exit code, frees its zombie slot. No children: an error. |
| 0x0105 | **Sleep** | `ms: u32` → 0 | Blocks that long (`Blocked` with a wake time, checked in `on_timer`). Replaces the busy loop of `std::time::sleep`. |
| 0x0106 | **Kill** | `pid: u32, code: i32` → 0 | Ends another process (later: only its own children, or with permissions). |

Why `Spawn` and not `fork` + `exec`: `fork` copies a process *at the same addresses*, which
needs address translation; with one shared address space, the copy would land on the original.
Starting a new program directly (like `posix_spawn`) is the natural fit.

## Memory (0x02xx)

| # | Call | Parameters → result | What it does |
|---|---|---|---|
| 0x0200 | **AllocPages** | `count: u32` → `address` | `count` zeroed pages from `g_pages`, mapped `ReadWrite` as a Heap region of the caller. The stdlib's heap (user `malloc`) is built on it. |
| 0x0201 | **FreePages** | `address: u32, count: u32` → 0 | Gives a region back (one the caller owns: `as_unmap`). |

`AS_MAX_REGIONS` is 16: the user heap should ask for big chunks (e.g. 64 KiB), not single pages.

## Console and input (0x03xx)

| # | Call | Parameters → result | What it does |
|---|---|---|---|
| 0x0300 | **ReadKey** | `blocking: bool` → `key` | The next key from the kernel's keyboard buffer (filled by the keyboard interrupt). Blocking: the process waits (`Blocked`); otherwise `WouldBlock` when there is none. |
| 0x0301 | **ConsoleClear** | → 0 | Clears the screen. *(optional)* |
| 0x0302 | **ConsoleSetColor** | `fg: u8, bg: u8` → 0 | The colors of the output that follows. *(optional)* |
| 0x0303 | **ConsoleSetCursor** | `x: u32, y: u32` → 0 | Moves the cursor (full screen programs). *(optional)* |

Later, `Print` could become `Write` on a console fd.

## Random and time (exist)

| # | Call | Parameters → result |
|---|---|---|
| 0x1000 | **Random** | → `i32` |
| 0x1001 | **RandomF32** | → the bits of an `f32` |
| 0x1002 | **SeedRandom** | `seed: u32` → 0 |
| 0x1100 | **TimeMillis** | → `u32` |
| 0x1101 | **TimeCycles** | → `u32` |
| 0x1102 | **TimeUnix** | → `u32` |

## Files (0x04xx)

The kernel keeps a table per process, e.g. `files: OpenFile[16]` with `{ used, ino, mode, pos }`
(`dr::fs::File` is close to one entry); an fd is the index. `process_exit` closes them all.

| # | Call | Parameters → result | What it does |
|---|---|---|---|
| 0x0400 | **Open** | `path: string, mode: u32` (`OpenMode`) → `fd` | Opens (or creates) a file: the flags of `dr::fs::OpenMode`. |
| 0x0401 | **Close** | `fd: u32` → 0 | Frees the fd (files and directories). |
| 0x0402 | **Read** | `fd: u32, buffer: u8*, size: u32` → `bytes` | Reads at the position, moves it (0 at the end). |
| 0x0403 | **Write** | `fd: u32, data: u8*, size: u32` → `bytes` | Writes at the position (the end with `Append`). |
| 0x0404 | **Seek** | `fd: u32, offset: i32, from: u32` (`SeekFrom`) → `position` | Moves the position; `Seek(fd, 0, Current)` is `tell`. |
| 0x0405 | **Truncate** | `fd: u32, size: u32` → 0 | Resizes the file (needs Write). |
| 0x0406 | **FStat** | `fd: u32, out: Info*` → 0 | An open file's `Info` (size, type, times). |
| 0x0407 | **Stat** | `path: string, out: Info*` → 0 | The same by path: `exists`, `is_file`, `is_dir`, `size_of` in user space. |
| 0x0408 | **Ioctl** | `fd: u32, command: u32, arg: u32` → the command's | A device's own commands (`std::video::Commands` for `/dev/video`); `NotSupported` on a disk file. |

*(done: `fs/vfs.ds`, the fd table `Process.files` in `fs/file.ds`, `std::fs::*` in the stdlib; the
structs `std::fs::FileInfo` / `OpenMode` / `SeekFrom` in libcore's `system.ds`.)*

### Devices: /dev/<name>
Not on the disk: `vfs::device_of` maps the path to a kernel driver (`dev/video_dev.ds`).
- **/dev/video**: opened with `Write`, the fd *controls* the card, one at a time (`Busy` for the
  others until it is closed or its process ends; fds aren't inherited). dinit opens it first.
  Read-only fds can only `GetInfo` (`NotPermitted` otherwise). `write()` on the controlling fd
  prints, like `Print`. The commands: `SetMode`, `Clear`, `SetColors`, `SetCursor`, `PutCell`,
  `PresentFrame`, `SetPalette`, `SelectPalette`, `GetVSyncCount` (`std::video` in libcore; the
  kernel counts the card's VSync interrupts, so an uncapped program can draw once per redraw).
- Processes never get the card's registers: they take addresses the card reads *and writes*
  (ConsolePrintString updates a Text16Console struct), so the driver checks every address it is
  given and doesn't offer the signals that write memory.
- **The foreground process** (`SetForeground`, or `Spawn` with `std::SpawnFlags.Foreground`): the
  one on the screen and at the keyboard. The owner names any process; the foreground process may
  pass it to its own descendants; when it ends, its parent gets it back (a shell, after the
  program it ran). It may draw from a read-only fd (not change the mode or palette), and only it
  gets keys from `ReadKey` (others: `NotPermitted`). Use the Spawn flag rather than
  SetForeground after Spawn: the child could run before it.
- `Print` follows the card's mode (the kernel console in `dev/video.ds`: Text16 has a cursor,
  colors and scrolling).

## Directories and file systems (0x05xx)

| # | Call | Parameters → result | What it does |
|---|---|---|---|
| 0x0500 | **OpenDir** | `path: string` → `fd` | Opens a directory to read its entries (the same fd table). |
| 0x0501 | **ReadDir** | `fd: u32, out: Entry*` → 1 / 0 | The next entry ("." and ".." skipped); 0 at the end. |
| 0x0502 | **MakeDir** | `path: string, permissions: u16` → 0 | One directory (`make_dirs`: a user space loop ignoring `Exists`). |
| 0x0503 | **Remove** | `path: string` → 0 | A file or an empty directory (`remove_all`: user space recursion). |
| 0x0504 | **Rename** | `from: string, to: string` → 0 | Renames or moves. |
| 0x0505 | **Link** | `existing: string, new_path: string` → 0 | A second name for a file. *(optional)* |
| 0x0506 | **StatFs** | `path: string, out: FsInfo*` → 0 | The file system the path is on: label, block size, total / free bytes, free inodes. |
| later | **Chmod**, **Chown**, **Sync**, **Mount**, **Unmount**, **ChDir** / **GetCwd** | | With users, a cache, more partitions, relative paths (the cwd could also live in user space only). |

Mounting is the kernel's: at boot it mounts its own partition as "/" (Drake can pass the
partition number as a third argument); other partitions later through a mount table (`/mnt/3`).

## System (0x06xx, optional)

| # | Call | Parameters → result | What it does |
|---|---|---|---|
| 0x0600 | **SysInfo** | `out: SysInfo*` → 0 | Total / free pages, uptime, process count (for `free` / `top`). |
| 0x0601 | **ProcessList** | `out: ProcInfo*, capacity: u32` → `count` | pid, name, state and pages of each process. |

## Suggested order
1. [ ] **Process basics:**
   - [ ] `Exit` in the kernel (the stdlib already calls it, also from `__assert_fail`)
   - [ ] `Print` with its pointer check
   - [ ] `GetPid`, `Yield`

   The first real user program runs and ends properly.
2. [ ] **`AllocPages` / `FreePages`**, then a user space heap in the stdlib.
3. [ ] **`Open`, `Close`, `Read`, `Stat`**: programs can read files (a `cat`).
4. [ ] **`Spawn`, `Wait`, `ReadKey`**: enough for a shell. `Wait` and `ReadKey` are the first
   calls that block: the `Blocked` state and waking processes up.
5. [ ] **`Sleep`**, then **`Write`, `Seek`, `Truncate`, `FStat`**.
6. [ ] **Directories**: `OpenDir`, `ReadDir`, `MakeDir`, `Remove`, `Rename`, `StatFs`.
7. [ ] The optional ones, when they're needed.
