# Dynamic linking: shared libraries (DRL) and the programs that use them (DRX2)

A program compiled normally carries a copy of every library it imports: `wing-shell` is 32 KB
of which ~31 KB is libcore and libstd, and every process has its own copy of that code. With
dynamic linking, the program only says which libraries it needs; a loader (drun's now, the
DragonOS kernel's later) loads each library **once**, and every process that needs it shares
that one copy of its code. `wing-shell` compiled with `--dynamic` is 659 bytes.

This document has everything a loader needs. The compiler side, the file formats, drun's
loader, the SDK readers (`dr::dyn`) and DragonOS's loader
([extra/ds/dragon-os/sched/dynlink.ds](../../extra/ds/dragon-os/sched/dynlink.ds), built as
[Writing the kernel's loader](#writing-the-kernels-loader) describes) exist and are tested.

Contents:
1. [Building](#building)
2. [The memory model](#the-memory-model): shared code, per-process data, the table
3. [What the compiler generates](#what-the-compiler-generates)
4. [The DRL file](#the-drl-file-a-shared-library) and [the DRX2 file](#the-drx2-file-a-program)
5. [The import kinds](#the-import-kinds): what each import word gets
6. [Loading, in general](#loading-in-general)
7. [Writing the kernel's loader](#writing-the-kernels-loader)
8. [The SDK readers](#the-sdk-readers-drdyn), [drun](#drun-and-the-tests), [the debugger](#the-debugger)
9. [Limits and notes](#limits-and-notes)


## Building

```
dsc --lib --shared src/*.ds -o lib/mylib.dlib    // lib/mylib.dlib and lib/mylib.drl
dsc prog.ds -L lib --dynamic -o prog.drx         // a DRX2: #import("mylib") is linked at load time
drun machine.dvm prog.drx -L lib                 // drun loads lib/mylib.drl and links it
```
- `--lib --shared` compiles the library twice, internally, from the same sources: the `.dlib`
  (the interface programs compile against, with the code a program copies when it links the
  library statically) and the `.drl` (the shared code, loaded when a program links it
  dynamically). `-o` names either one (or neither: `-o lib/mylib`); the other is next to it.
  The `.drl` gets `--save-disassembly`, the `.dlib` keeps its disassembly in itself.
- Both record the library's **ABI**, a hash of its interface: a program records the ABI of each
  library it links dynamically, and the loader refuses a `.drl` with another ABI (its structs,
  subs or inline subs may have changed).

### Static and dynamic, together
A program chooses, for each library it imports, whether it is **static** (its `.dlib` code is
copied into the program, as always) or **dynamic** (its `.drl` is loaded with the program):
```
#import("libstd", dynamic)       // in the source: this one dynamic
#import("mylib", static)         // this one static (also when the command line says --dynamic)
#import("other")                 // the command line decides (static by default)
```
```
dsc prog.ds -L lib                          // every library static (a DRX, as before)
dsc prog.ds -L lib --dynamic                // every library dynamic, unless #import(..., static)
dsc prog.ds -L lib --dynamic=libstd,mylib   // just these dynamic
dsc prog.ds -L lib --dynamic --static=mylib // everything dynamic but mylib
```
- What decides, from strongest to weakest: `--static=<names>` / `--dynamic=<names>`, then the
  `#import`'s `static` / `dynamic`, then `--dynamic`, then static.
- **A dynamic library's dependencies are dynamic too.** Its `.drl` uses its dependencies'
  shared copies; a static copy of one in the program would be a second copy, with its own
  globals (two different states of the same library). A dependency nobody chose is made
  dynamic automatically; one chosen static is an error, saying what to change.
- The other way around is fine: a static library can use a dynamic one. Its code is in the
  program, and its imports of the dynamic library become the program's imports.
- With no dynamic library, the output is a DRX1, byte for byte what it always was. With at
  least one, it is a DRX2 (the static libraries inside it, the dynamic ones in its library
  table). A raw program (`--raw`, `--rom`) has no loader: its libraries are all static.
- A shared library's own dependencies are always shared (a `.drl` can't contain a static copy
  of another library: its writable globals would be shared by every process).
- `ds_env` (cmake) builds `libcore.drl` and `libstd.drl` next to their `.dlib`
  (`bin/ds/libs/<name>/lib`, `extra/ds/libs/<name>/lib`), puts them on DragonOS's partition (2) as
  `/lib/libcore.drl` and `/lib/libstd.drl`, and saves their disassembly (`bin/disassembly`).
  DragonOS's user programs are built with `--dynamic` (DRX2s using `/lib/libstd.drl` and
  `/lib/libcore.drl`): the kernel loads each library once and maps its text into every process
  that uses it. A program in `/bin` can still be a DRX1 (static): the kernel loads both.


## The memory model

DragonOS has one address space for every process (permission maps, no address translation):
an address means the same memory for everyone. So a library's code can be loaded once and
mapped into every process, but its **writable globals** can't be shared: two processes using
`std::rand` must not share its state. The compiler solves it like this:

```
 one copy, for everyone (shared)                      one copy per process
 ┌──────────────────────────────────────────┐        ┌──────────────────────────┐
 │ libX text: code, strings, const arrays,  │        │ libX's rw copy           │  process A
 │  and the rw TEMPLATE (initial values)    │ ─copy─▶│ (its writable globals)   │  (ReadWrite)
 │  [0 .. image_size), rw = [rwStart, +size)│        └──────────────────────────┘
 └──────────────────────────────────────────┘        ┌──────────────────────────┐
   mapped Read into every process using it            │ the table: one word per  │  process A
                                                      │ slot: table[slot(libX)]  │  (Read)
 ┌───────────────────┐                                │ = copy - rwStart (bias)  │
 │ LibTable word ────┼──── points at the running ────▶└──────────────────────────┘
 └───────────────────┘     process's table (the kernel switches it)
   one word, Read for everyone, written by the kernel
```
- **Text** (the whole image of the `.drl`): loaded once, relocated once, imports filled in once,
  then never written. Every process maps it Read (execute is read).
- **The rw part**: `[rwStart, rwStart + rwSize)` of the image, the library's writable globals.
  The image keeps it as the **template** (their initial values). Each process gets its own
  **copy** of it (writable), made from the template when the process starts.
- **The slot**: a number the loader gives each loaded library (0, 1, 2, ...), the same for
  every process.
- **The table**: one per process, one 32-bit word per slot. `table[slot] = bias = (this
  process's copy of the rw part) - rwStart`. So the address of a global `g` (at offset `off` in
  the image) in this process is `table[slot] + off`.
- **The LibTable word**: one word for the whole system, at an address the loader chooses. It
  holds the address of the running process's table. The kernel writes it at every context
  switch. (One CPU: only one process runs at a time.)

A library without writable globals (libcore, today's libstd) has `rwSize` 0: no copy, its table
entry is unused (0).


## What the compiler generates

In a shared library (`dsc --lib --shared`), every access to a writable global goes through the
table (`CodeGen::varAddress`):
```
mov R, [$__ds_libtable]          ## R = the running process's table   (import: LibTable)
movo R, *R, dword $__ds_slot      ## R = table[slot] = this library's bias (import: SelfSlot = 4 * slot)
addip R, $__dsrw__counter         ## R = bias + counter's offset in the image (dsc fills it in)
mov R1, *R                        ## the value
```
Another library's writable global: `$__dsslot__<symbol>` (import SlotOf: 4 * that library's
slot) and `$__dsoff__<symbol>` (import OffsetOf: the symbol's offset in that library).
Everything else is as in any DRX: calls, string literals and constant arrays use absolute
addresses (relocations, or Absolute imports into other libraries' text), because the text is at
the same address in every process.

The writable globals are emitted together, between two one-byte markers
(`$__ds_rw_start 0x00` ... `$__ds_rw_end 0x00`); `rwStart` is right after the first marker.
dsc then sorts every relocation by where its word is and what it points to:

| word in | points into | becomes |
|---|---|---|
| text | text | a text relocation: + text base, once |
| text | rw | **an error**: shared code would hold a per-process address |
| rw | text | a data→text relocation: + text base, per process |
| rw | rw | a data→data relocation: + bias, per process |

Programs (`--dynamic`) need no special code: a program is never shared, so its imports are
simply the addresses for its own process (a library's writable global: in this process's copy).


## The DRL file (a shared library)

Big endian. Strings are offsets into the string table (0-terminated; offset 0 is "").

| offset | field |
|---|---|
| 0x00 | magic `'DRL1'` (0x44524C31) |
| 0x04 | header size (0x60) |
| 0x08 | image size |
| 0x0C | init: the library's initialization routine (offset in the image) |
| 0x10 | rw start (offset in the image) |
| 0x14 | rw size |
| 0x18, 0x1C | id (hi, lo): the build's id, for the debugger |
| 0x20 | ABI |
| 0x24 | name (string) |
| 0x28, 0x2C | text relocations: count, offset (words: + text base, once) |
| 0x30, 0x34 | data→text relocations: count, offset (words in rw: + text base, per process) |
| 0x38, 0x3C | data→data relocations: count, offset (words in rw: + bias, per process) |
| 0x40, 0x44 | dependencies: count, offset (8 bytes each: name, ABI) |
| 0x48, 0x4C | imports: count, offset (12 bytes each: offset (u32), dependency (u16), kind (u16), symbol (u32)) |
| 0x50, 0x54 | exports: count, offset (8 bytes each: symbol, offset), **sorted by name** |
| 0x58, 0x5C | string table: offset, size |

Then: the image, the tables (in that order), the strings. A relocation or import "offset" is
the offset in the image of the 32-bit word to change. An import's "dependency" is an index in
the dependency table (`0xFFFF` for SelfSlot and LibTable). The dependencies are every library
this one uses, directly or not, dependencies first. Exports are the library's public symbols
(dasm names: `dc::counter` is `__ns_dc__counter`), code and data; an export inside rw is a
writable global (per process).

## The DRX2 file (a program)

| offset | field |
|---|---|
| 0x00 | magic `'DRX2'` (0x44525832) |
| 0x04 | header size (0x40) |
| 0x08 | image size |
| 0x0C | entry |
| 0x10, 0x14 | relocations: count, offset (words: + the program's base) |
| 0x18, 0x1C | id (hi, lo) |
| 0x20, 0x24 | libraries: count, offset (8 bytes each: name, ABI), in initialization order |
| 0x28, 0x2C | imports: count, offset (12 bytes each: offset, library (u16), kind (u16), symbol) |
| 0x30, 0x34 | string table: offset, size |
| 0x38, 0x3C | 0 |

Then: the image, the relocations, the libraries, the imports, the strings. The program is
started like any DRX (`push argc; push argv; push 2; call base + entry`): its `__ds_start` calls
each library's initialization (Init imports), in the order of the library table, then main.
**The loader never runs library code itself.**

`dr::drx::load` (DRX1) refuses a DRX2 (`InvalidMagic`): the BIOS and Drake keep loading DRX1.


## The import kinds

| kind | value | where | the word gets |
|---|---|---|---|
| Absolute | 0 | program, library text, library rw | the symbol's address: `lib.text + off`, or, if `off` is in the library's rw part, `bias(lib, this process) + off` (not allowed in a library's text: per process) |
| Init | 1 | program | `lib.text + lib.init` |
| SlotOf | 2 | library text | `4 * slot(dependency)` |
| OffsetOf | 3 | library text | the symbol's offset `off` in the dependency |
| SelfSlot | 4 | library text | `4 * slot(this library)` |
| LibTable | 5 | library text | the address of the LibTable word |

**Once or per process**: everything in a library's text (text relocations, imports whose word is
outside rw) is done once, when the library is loaded. Everything in a library's rw part
(data→text, data→data relocations, imports whose word is inside rw) and everything in the
program is done for each process.


## Loading, in general

**Once per library** (the first time a process needs it):
1. Find `<name>.drl` (DragonOS: `/lib/<name>.drl`), read it, check its header and that its ABI
   is the one asked for (`AbiMismatch`).
2. Load its dependencies first (same steps), refusing cycles (`CircularDependency`).
3. Give it a slot (`TooManyLibraries` if none is free).
4. Copy its image to its text pages. Add the text base to every text relocation. Fill in every
   import whose word is outside rw (SelfSlot, LibTable, SlotOf, OffsetOf, Absolute: see the
   table; an Absolute whose symbol is in the dependency's rw part is `NotShareable`; a symbol
   not exported is `SymbolNotFound`).
5. Tell the debugger: `dr::drx::notify_loaded(text, image_size, id_hi, id_lo)`.
6. Keep what each process will need: the file (or at least its header, rw template, data
   relocations, rw imports and exports), its text address, its slot.

**For each process**:
1. The libraries it needs: the program's library table, each loaded (above) with the ABI it
   records, and **all of their dependencies** (the process's code runs their code too).
2. Its table: one word per slot, mapped Read for the process. For each needed library with
   `rwSize > 0`: copy the template into new pages, add the text base to the data→text
   relocations, add the bias to the data→data relocations, fill in the imports whose word is in
   rw (Absolute only), and set `table[slot] = copy - rwStart`. Map the copy ReadWrite.
3. Map each needed library's text Read (shared: not freed with the process).
4. The program: copy, add its base to its relocations, fill in its imports (Absolute, Init).
5. Start it as any DRX. Whenever it runs, the LibTable word must point at its table.

drun's loader does exactly this for one process: `dyn::Linker` in
[src/tools/DynamicLinking.hpp](../../src/tools/DynamicLinking.hpp) (C++, ~200 lines). Use it as
the reference.


## Writing the kernel's loader

What follows was the plan for DragonOS's loader, now `extra/ds/dragon-os/sched/dynlink.ds`
(`lib_load`, `link_process`, `copy_rw`; the LibTable word at the trampoline page's
`PAGE_SIZE - 16`, written by `switch_to`; the table and the copies are `RegionKind.Library`
regions, which `FreePages` can't free). The sketches below are kept as the explanation; the
code follows them closely, with these differences:
- **256 slots** (`proc::MAX_LIBS`), the library table allocated at boot (not in the kernel
  image). A process doesn't keep its copies' addresses (`lib_data`): its copy of a library's
  writable globals is `table[slot] + rw_start`. Which libraries it uses is a bitmap
  (`Process.libs`, 32 bytes).
- **Slots are reused** (see "6. Later" below), so a lower slot isn't always a dependency: the rw
  copies are made by walking the dependencies (`map_library`), not in slot order.
- At most 32 libraries per program or library (`dynlink::MAX_DEPS`, dependencies included), and
  dependency chains at most 8 deep while loading (`MAX_DEPTH`: `lib_load` recurses on the
  8 KiB kernel stack).
- `SysInfo` has `libraries` (loaded) and `libraries_in_use`.

### 1. The LibTable word

One word, at an address every library's text gets once (LibTable imports), so it must exist
before the first library is loaded, and never move. The trampoline page is the natural place:
it is mapped Read into every process, and the kernel can write it (supervisor mode ignores the
permission map). The idle loop is at `PAGE_SIZE - 8`; use, for example, `PAGE_SIZE - 16`:
```
var g_libtable_word: u32* = null;        // in init_trampoline:
g_libtable_word = cast<u32*>(g_trampoline + PAGE_SIZE - 16);
*g_libtable_word = 0;
```
In `switch_to`, before the `low { ... rti }` (interrupts are already off there):
```
*proc::g_libtable_word = p->lib_table;   // 0 for processes without shared libraries (and idle)
```
That one line is the whole "context switch" of dynamic linking.

### 2. The library cache (kernel memory, system-wide)

```
const MAX_LIBS: u32 = 16;                // the slots; each process's table has MAX_LIBS words
struct SharedLib {
    var used: bool;
    var name: u8[32];
    var file: u8*;                       // the .drl, kept (kernel heap or pages): its tables are read per process
    var file_size: u32;
    var h: dr::dyn::LibraryHeader;       // read once (dr::dyn::read_library_header)
    var text: u32;                       // its text pages (g_pages), image_size rounded up
    var text_pages: u32;
    var deps: u8[MAX_LIBS];              // its dependencies, as slots (h.dependencies.count of them)
    var users: u32;                      // processes using it (later: unload when 0)
};
var g_libs: SharedLib[MAX_LIBS];         // the index is the slot
```
`lib_load(name, abi, out_slot) -> i32`, following [Loading, in general](#loading-in-general),
"once per library":
- already in `g_libs` (same name): check the ABI, return its slot;
- read `/lib/<name>.drl` with `g_root_fs` (`info` + `read_all`, as `process_start` does) into
  kernel memory; `dr::dyn::read_library_header`; compare `h.abi`;
- mark it "loading" (a flag) before loading its dependencies, to detect cycles: for each
  `dr::dyn::library_dependency`, `lib_load(dep.name, dep.abi, &slot)` and remember the slot;
- `g_pages.alloc(pages_for(h.image_size))`, `memcpy` the image (at `file + h.header_size`);
- text relocations: `dr::dyn::library_relocation(file, &h, &h.text_relocations, i, &off)`, then
  `word(text + off) += text`;
- imports (`dr::dyn::library_import`), the ones with `not dr::dyn::in_rw(&h, imp.offset)`:
  SelfSlot `4 * slot`, LibTable `cast<u32>(g_libtable_word)`, and for the others the dependency
  `d = g_libs[deps[imp.library]]`, `dr::dyn::find_export(d.file, &d.h, imp.symbol, &off)`:
  SlotOf `4 * deps[imp.library]`, OffsetOf `off`, Absolute `d.text + off` (`NotShareable` if
  `dr::dyn::in_rw(&d.h, off)`);
- `dr::drx::notify_loaded(text, h.image_size, h.id_hi, h.id_lo)`.

A word is written big endian with `dr::drx::__writeU32` / read with `__readU32` (or a
`*cast<u32*>(addr)` if aligned: the machine is big endian, and takes unaligned words too).

### 3. The process

New fields in `Process`:
```
var lib_table: u32;                      // its table (MAX_LIBS words), 0: none
var lib_data: u32[MAX_LIBS];             // its copy of each library's rw part (0: none)
var libs: u8[MAX_LIBS];                  // needed[slot]: it uses that library (for users--)
```
In `process_start`, after `drx::load` is replaced by a DRX2-aware step (a DRX1 still goes the
old way: check the magic):
1. `dr::dyn::read_program_header`; for each `dr::dyn::program_library`: `lib_load` → its slot;
   then mark the slot **and its dependencies' slots** (recursively, from `g_libs[s].deps`) in
   `p->libs`.
2. The table: `g_pages.alloc(1)` (16 slots = 64 bytes: one page is plenty), zeroed, mapped
   `Permissions.Read` and owned (`as_map(&p->as, table, 1, Read, RegionKind.Heap, true)`: the
   process reads it, only the kernel writes it). `p->lib_table` = it.
3. For each slot in `p->libs` with `h.rw_size > 0`: pages for `rw_size`, copy the template
   (`text + h.rw_start`, or from the file: `file + h.header_size + h.rw_start`), then with
   `bias = copy - h.rw_start`:
   - data→text: `word(copy + off - rw_start) += lib.text`;
   - data→data: `word(copy + off - rw_start) += bias`;
   - imports in rw (Absolute): the dependency's text or (its export in its rw) **this
     process's** copy of it: `p->lib_data[dep] - dep.h.rw_start + off` (so make the copies in
     dependency order, a library's dependencies before it: with slots given in load order and
     never reused, increasing slot order is that order);
   - `table[slot] = bias`; map the copy `ReadWrite` (owned).
4. Map each needed library's text: `as_map(&p->as, lib.text, lib.text_pages, Read,
   RegionKind.Shared, false)` (not owned: freed by nobody but the cache). `users++`.
5. The program's image: the same as today's DRX path (relocations + the program's base), then
   its imports (`dr::dyn::program_import`): Init `lib.text + lib.h.init`, Absolute
   `lib.text + off` or `p->lib_data[slot] - lib.h.rw_start + off` (its rw part).
6. Undo on errors as `process_start` already does (`as_destroy` frees the owned copies and the
   table; `users--` for what was counted).

In `process_exit`: `users--` for each slot in `p->libs` (the copies and the table are owned
regions: `as_destroy` frees them).

**Regions**: a process with 2 libraries uses image + stack + trampoline + table + 2 texts + up to
2 copies = 8 of the 16 (`AS_MAX_REGIONS`). Raise it if programs use many libraries.

### 4. Spawn

Nothing changes: `sys_spawn` calls `process_start`, which now accepts both DRX1 and DRX2. Build
a program with `--dynamic` (and put `libcore.drl` / `libstd.drl` in `/lib`: ds_env does) to
test it. Two processes using libstd with a writable global (add one to test, e.g. a call
counter) show the isolation: each sees its own count.

### 5. Errors

`std::ExecErrors` has the loader's codes: `LibraryNotFound`, `SymbolNotFound`, `AbiMismatch`,
`TooManyLibraries`, `CircularDependency`, `BadImport`, `NotShareable` (and the readers return
`FileTooSmall`, `InvalidMagic`, `TruncatedFile`, `RelocationOutOfRange`, `EntryOutOfRange`).
`sys_spawn` passes them to the program unchanged, `std::error_name` describes them.

### 6. Unloading: the cache

Done this way: a library with `users == 0` stays loaded (the next program that needs it starts
without reading it again). It is unloaded only when its room is needed: no free slot for a new
library, or no free pages for anything the kernel allocates for processes (`dynlink::alloc_pages`,
which `AllocPages`, the stacks, the images and the permission maps use). The least recently used
library that can go is unloaded first: `users == 0` and `dependents == 0` (no loaded library
depends on it: their text has its addresses). A library being loaded, or just found for a
process, is pinned until it is counted (its dependencies' `dependents++`, a program's libraries'
`users++`, both as soon as each is found), so nothing about to be used is unloaded.

Note: memory is reclaimed one library at a time until the allocation succeeds. A request for a
long run of contiguous pages that the freed pages can't form empties the cache and still fails.
- Spawn cost: with shared text, a spawn copies only the program and the rw parts; most of
  today's ~87,000 cycles (reading, copying and relocating 32 KB) disappear.


## The SDK readers (`dr::dyn`)

[extra/ds/machine/dragon-sdk/src/dyn.ds](../../extra/ds/machine/dragon-sdk/src/dyn.ds): the files read in place,
nothing allocated. The headers are checked against the file when read (tables inside the file,
the string table ending with a 0); entries are checked when taken.
```
dr::dyn::read_library_header(file, size, &h)      dr::dyn::read_program_header(file, size, &p)
dr::dyn::library_dependency(file, &h, i, &ref)    dr::dyn::program_library(file, &p, i, &ref)
dr::dyn::library_import(file, &h, i, &imp)        dr::dyn::program_import(file, &p, i, &imp)
dr::dyn::library_relocation(file, &h, &h.text_relocations, i, &off)   (and data_text / data_data)
dr::dyn::program_relocation(file, &p, i, &off)
dr::dyn::library_export(file, &h, i, &e)          dr::dyn::find_export(file, &h, "name", &off)
dr::dyn::in_rw(&h, off)                           dr::dyn::ImportKinds.SlotOf, ...
```
Strings (`h.name`, `imp.symbol`, `ref.name`) point into the file: keep the file while they are
used. `tests/dscript/sdk_dyn.ds` reads real files dsc wrote (and broken copies).


## drun and the tests

- `drun machine.dvm prog.drx -L <dir> [--link-log] [--drx-events]`: loads a DRX2 at 0x01020000,
  the libraries after it (page aligned, slots in load order), then the LibTable word and the
  table, then the rw copies. `--link-log` prints the slots and addresses.
- `--file <address>:<path>`: a file in memory (its size as a word, then its bytes), for tests
  that read files.
- Tests (`tests/dscript/run_tests.sh`): `// dsc-shared: libs/a.ds libs/b.ds` builds each with
  `--lib --shared` (a `.dlib` and a `.drl`); the system libraries are built that way too. A test
  using shared libraries (`dsc-shared`, or `dsc-flags` with `--dynamic...`) runs as a DRX only,
  with drun `-L`. `@LIBS@` / `@PROGRAM@` in `drun-flags` are the test's library directory /
  compiled test.
  - `dynamic_linking.ds`: two libraries (one using the other): every kind of global, static
    locals, structs, low blocks, pointers in writable data, the program and both libraries
    seeing the same copies;
  - `dynamic_libcore.ds`: a program using libcore.drl;
  - `dynamic_mixed.ds`: a static library using a dynamic one (and libcore static);
  - `dynamic_promote.ds`: a dynamic library's dependency made dynamic automatically;
  - `dynamic_flags.ds`: `--dynamic --static=libcore`;
  - `error_dynamic_static_dependency.ds`: a dynamic library's dependency chosen static;
  - `sdk_dyn.ds`: the `dr::dyn` readers.
  - `DSCRIPT_TEST_TOOLS=<dir>` runs the tests with dsc and drun from another build directory.


## The debugger

- A `.drl` built with `--save-disassembly` (ds_env does) has its id (`{ DRX } <id>`), like a DRX.
  When the loader calls `dr::drx::notify_loaded(text, image_size, id_hi, id_lo)` for a library,
  ddb places its disassembly (with its source lines) at its text, as it does for programs. Each
  library is mapped on its own (several at once are fine).
- A program's disassembly shows its imports as `$symbol`.
- The data symbols of a library are shown at the **template** (in its text), not at a process's
  copy: to watch a library's global, look at `table[slot] + offset` in memory.


## Limits and notes

- A writable global access in shared code costs 3 more instructions (load the table, the bias,
  add the offset). Read-only data (strings, `const` arrays) and code are as fast as ever.
- A low block in a shared library can use globals by name (`$counter`, `&$counter`: they go
  through the table), but a low block's own `.data` is read-only there (it is part of the shared
  text): writing it is an access violation.
- Every global access has its own LibTable / SelfSlot imports (a loader fills in many small
  entries: fine, they are only filled once per library).
- A shared library is loaded whole (also what a program doesn't use): but only once, for
  everyone. A library linked statically, on the other hand, only brings what the program reaches
  (see "Libraries" in dscript.md; `--whole-libraries` turns that off).
- `dsc` checks that no shared code holds the address of a writable global (an error naming the
  global); DScript itself doesn't allow one in a `const` array, so it is a safety net.
- Programs and kernels loaded by the BIOS, Drake or `dr::drx::load` stay DRX1 (static).
