# DScript

A C-like language for the Dragon machine. `dsc` compiles it to dasm and assembles it (the
assembler is linked in), producing a relocatable **DRX** program (the default) or a bare metal
binary (`--raw`).

```
dsc program.ds -L extra/ds/dragon-sdk/lib         -> program.drx (relocatable)
dsc kernel.ds --raw --load 0x01012000 --fixed 1024,0xFF --header KERNEL0_BOOT
dsc program.ds --emit-dasm program.dasm           (also writes the generated dasm)
dsc program.ds -O                                 (optimized: locals in registers)
dsc bios.ds --rom --fixed 16777216,0xFF -Wa,-D    (dasm options: here its debug mode)
drun config/testMachine.dvm program.drx --boot    (runs it without a window)
```

`-Wa,<option>[,<option>...]` hands options to the assembler, as `dasm` takes them: `-D` /
`--debug` (debug mode: keeps the `debug_*` instructions, prints the program info and saves the
disassembly as `disassembly/<md5 of the output path>.dds`, like dasm does),
`--save-disassembly,<file>`, `--save-final-stage,<file>`, `--verbose`, `--verbose-2`,
`--verbose-full`, `-I,<dir>` (for `@include` in low blocks). `-o` and `--disable-ext*` are not
allowed. dsc's own `-D NAME=value` is a define, not dasm's `-D`. For a DRX, the disassembly and
symbols are at offset 0 (add the load address).

### Source-level debugging
The disassembly saved by dsc (and by dasm, for `.dss` files) has the source lines: which line
each instruction comes from, and the text of the source files (disassembly file version 2; ddb
still reads version 1 files). dsc writes markers in the dasm it generates (`.srcfile <index>
<path>` for each file, `.loc <file> <line> <inline depth>` before each statement and each low
block line, `.loc -` before generated code); dasm adds them for its own sources. They are only
for the debugger: the binary is the same.
```
dsc program.ds --raw --save-disassembly disassembly/     (a directory: named for ddb, as dasm does)
dsc program.ds --save-disassembly disassembly/           (a DRX: at offset 0, with the program's id)
dtools print-disassembly -f disassembly/<file>.dds        (the instructions under their lines)
```
A DRX program's disassembly is at offset 0 (a `{ DRX } <id>` entry has the id of its header):
ddb keeps it aside until a loader says where the program is, with `debug_drx_load` (the SDK's
`drx::load` runs it once the program is relocated, whoever calls it: the BIOS loading Drake,
Drake loading a kernel...), then shows it moved there (its sources, breakpoints on its lines,
`where`, stepping). A new program loaded over one replaces it; `drx::notify_unloaded(base)` (and
`drx::free_program`) remove one; a reset removes them all. `ddb --force-load program.drx
<offset>` moves it there too. A program rebuilt without saving its disassembly has a new id:
no disassembly rather than a wrong one (ddb's console says so when it is loaded). A loader of
its own calls `drx::notify_loaded(base, size, id_hi, id_lo)` once the program is in place.
`drun --drx-events` prints the loads and unloads (what ddb gets).
In ddb, each source line (highlighted) is shown above its instructions, which are indented under
it; inlined subs' line numbers are green, each file starts with a header, and the line of the
current instruction is highlighted. A source line's margin and menu act on its
first instruction (breakpoints, run to). Commands (and toolbar buttons): `sl` / `step-line` (F7: to the next line,
into called subs), `nl` / `next-line` (F8: calls, interrupts and inlined subs run whole),
`break <file>:<line>` (at the line, or the next one with code; every copy of an inlined line),
`break <sub>`, `where`, `list [file:line]`. Where the program has no source lines, they step one
instruction.

Breakpoints on source lines and subs (the `break` command, the Breakpoints tab's input):
- `<file>:<line>`: the file is the end of its path, `entry.ds`, `drake/entry.ds` (`.ds` can be left
  out). When several different files end that way, it says which (with the shortest paths that
  tell them apart: `bios/entry.ds, drake/entry.ds`).
- A sub: its full name (`dr::fs::FileSystem::info`) or its last parts (`FileSystem::info`, `info`
  when only one sub has that name; else it lists them). Overloads all get one. dsc writes the
  subs in the disassembly (a `{ SUBS }` part: name, label, parameter types, inline, file, lines);
  a library's are in the program's disassembly with its code, so a library sub gets a breakpoint
  in every program linked with it (the BIOS's copy, Drake's...). An inline sub: at the first line
  of each copy.
- They are kept as written and resolved again whenever the programs in memory change: one in a
  DRX program not loaded yet is pending (the tab and `bl` say in which program), and gets its
  addresses when the program is loaded, wherever it is. Removing one of its addresses (the
  margin) removes it.

Pipeline: lexer -> preprocessor (on tokens) -> parser (AST) -> checking + code generation
(dasm text) -> assembler -> DRX / raw binary. Sources: `src/compiler/`.

Compiled libraries: `dsc lib.ds --lib` makes a `.dlib`, that programs `#import("lib")` (see
"Libraries").

Editors: `dsls` is the language server (errors as you type, go to definition, hover, completion, symbols;
`src/lsp/`, see `editors/README.md`). It uses the same compiler in an analysis mode, which keeps
checking after an error (each declaration and sub on its own) and records what every name in
the source refers to (`src/compiler/Index.hpp`).


## Language

### Types
| Type | Size | Notes |
|---|---|---|
| `i8` `u8` `i16` `u16` `i32` `u32` | 1, 2, 4 | `char` = `u8`, `int` = `i32`, `uint` = `u32` |
| `f32` | 4 | IEEE 754 single precision (`float` = `f32`), see "Floating point" |
| `bool` | 1 | `true` / `false` |
| `T*` | 4 | pointers; `void*` converts to and from any pointer |
| `string` | 4 | the same as `u8*` (null-terminated) |
| `T[N]` | N * size | arrays; they decay to `T*` |
| `T[]` | the value's | in a declaration with a value: `{ 1, 2, 3 }` -> 3, `"text"` -> 5 (with the 0) |
| `struct Name` | sum of the fields | packed (no padding) |
| `struct { fields }` | sum of the fields | a nameless struct, usable wherever a type goes |
| `sub(T1, T2) -> R` | 4 | sub pointers; `(sub(i32) -> i32)[4]` for an array of them |
| enums | 4, or the size of their type | their values are constants of the enum's type (`i32` by default): `Color.Red`; `enum Kind: u8 { ... }` gives them a type (i8, i16, i32, u8, u16, u32) |

Integer literals: `123`, `0xFF`, `0b1010` (`_` between digits: `0x0000_FFFF`), `'a'` (a `u8`).
A literal is an `i32` (a `u32` if it doesn't fit: `0xFFFFFFFF`); with the `u` / `U` suffix it is a
`u32` whatever its value, as `unsigned int` in C: `123u`, `0xFFu`, `0b101U`. So `var h = 2166136261u;`
is a `u32`, `x - 1u` stays unsigned, and C code with `u` constants ports as it is.

### Floating point

`f32` runs on the FPU (the `extfpu` CPU extension). An `f32` lives in the general registers like
a `u32` (its 32 bits), so it is passed, returned, stored and copied like one.

- Literals: `1.5`, `2.`, `1e-3`, `2.5e+4`, `3f`, `0.5f` (an `f` suffix or a `.` / exponent makes
  an `f32`; there is no 64-bit float).
- Integers convert to `f32` implicitly (in arithmetic, assignments, arguments): `f + 1`,
  `var f: f32 = n;`. An `f32` converts to an integer only with `cast`: it truncates toward zero
  and saturates (out of range: the type's nearest limit; NaN: 0). `cast<bool>(f)` is `f != 0.0`.
- Operators: `+ - * /` (`/` by zero gives an infinity, no error), unary `-`, comparisons,
  `! && ||`, `?:`, `++ --` (by 1.0), `+= -= *= /=`. Not `%`, the bitwise ones or the shifts.
- Comparisons follow IEEE: every comparison with NaN is false, except `!=`; `-0.0 == 0.0`.
  As a condition, an `f32` is true unless it is `0.0` / `-0.0` (NaN is true).
- Constants fold at compile time (`const HALF: f32 = 1.0 / 2;`), also in struct constants and
  default values. An overloaded sub called with an integer prefers its integer overload.
- To look at the bits: `*cast<u32*>(&f)` (or `std::math::f32_bits(f)` / `std::math::f32_from_bits(bits)`).
- libcore's `math.ds`, in `std::math`: `sqrt`, `floor`, `ceil`, `round`, `trunc`, `abs`, `min`, `max`,
  `clamp`, `lerp`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `exp`, `log`, `log2`,
  `log10`, `pow`, `fmod`, `is_nan`, `is_inf`, `is_finite`, and `PI`, `TAU`, `E`, `INFINITY`,
  `NAN`, ... `std::ftoa(f, buffer, decimals)` writes one as text; `print(f32)` prints one
  (`TextSingle`, `Text16`, `Text16Console`; 6 decimals by default). `std::atof(text, &f)` reads
  one back (also `inf`, `nan`, `1.5e-3`); `std::atou` / `std::atoi(text, &n, base)` do the same for
  integers (base 0: from a `0x` / `0b` / `0o` prefix). They return false when the text isn't a
  number (or it doesn't fit).

### 64-bit integers (libcore)

There is no 64-bit type in the language: libcore's `int64.ds` has two structs, `std::U64` and
`std::I64` (two's complement), with the words as fields (`hi`, then `lo`: the bytes of a big endian
64-bit value, so a pointer to one in memory can be cast to `U64*` / `I64*`). The operations
change the struct and return it (a pointer), so they chain; each one also has a `new_` form that
leaves the struct as it is and returns the result as a new value:
```
var a: std::U64(5);                     // constructors: (), (u32), (i32), (hi, lo), (U64), (I64)
a.mul(3)->add(b)->shl(4);               // a = ((a * 3) + b) << 4
var c = a.new_add(1).new_shl(2);        // c = (a + 1) << 2, a unchanged
a.band(mask.new_bnot());                // clears mask's bits (mask unchanged)
if (a.gt(1000)) ...                     // eq ne lt le gt ge (bool), cmp (-1 / 0 / 1)
var text: u8[24]; a.str(text);           // str(buffer, base = 10), parse(text, base = 10) -> bool
console.print(&a);                      // TextSingle / Text16 / Text16Console print them
```
- Every operation takes a `u32`, an `i32` or a `U64` (`I64` for `I64`), by value; the smaller
  types use the `u32` / `i32` one (zero / sign extended), and so does an integer literal (`i32`).
- `new_<operation>` (`new_add`, `new_band`, `new_shl`, `new_abs`, ...): every operation that
  changes the struct, except `set`.
- The limits: `std::UINT64_MAX`, `std::INT64_MAX` (struct constants: `var m = std::UINT64_MAX;`,
  `std::INT64_MAX.str(text)`, `std::UINT64_MAX.new_shr(4)`).
- The subs that do not change the struct are `readonly` (callable on constants), and all of
  them are `constexpr`: with constant operands (`std::UINT64_MAX.new_shr(60)`), when compiling.
- Arithmetic: `add`, `subt` (`sub` is a keyword), `mul`, `div`, `mod`, `divmod(d, &rem)` (`rem`
  a pointer: it gets the remainder), `neg`, `inc`, `dec`, `min`, `max`; `I64` also `abs`. Signed
  division truncates toward zero, the remainder has the dividend's sign (as for `i32`); division
  by zero is the CPU's.
- Bits: `band`, `bor`, `bxor`, `bnot` (`and` / `or` / `not` are keywords), `shl`, `shr` (`I64`:
  arithmetic), `rol`, `ror`, `bit(n)`, `setb(n)`, `clrb(n)`, `clz`, `ctz`, `popc`.
- Other: `set(...)` (the constructors' forms), `is_zero`, `fits32`, `to_u32` / `to_i32` (the low
  word), `I64`: `is_neg`, `sign`, `u()` (itself as `U64*`).
- The cost: add / sub / compare / bits are a few instructions on each word, `mul` about 20,
  `div` / `mod` up to several hundred (a divisor under 65536 takes a faster path).

### Time and dates (SDK, libcore, stdlib)

The clock: in supervisor mode, the SDK reads the timer directly: `dr::time_get_millis()` (since
the machine started), `dr::time_get_cycles()`, `dr::time_sleep(ms)`, `dr::time_get_unix()`
(seconds since 1970-01-01 00:00 UTC: the host's clock when the machine started, running on with
it; `drun --unix-time <seconds>` fixes it for tests). A DragonOS program asks the kernel with the
stdlib's `std::time::millis()`, `cycles()`, `unix_time()`, `sleep(ms)` and `std::time::Timer`
(system calls).

The calendar is libcore's (no clock: it works with the times it is given): `std::time::DateTime`,
a moment in time like `ostd::Date` (1970 to 2106-02-07, the timer's `u32`):
```
var now: std::time::DateTime(dr::time_get_unix());   // constructors: () 1970-01-01, (unix_seconds),
var d: std::time::DateTime(1993, 12, 19, 18, 30);    //   (year, month, day, hour, minute, second) UTC
var text: u8[32];
d.setFormat("YYYY-MM-DD HH:Min:SS")->toString(text, 32);    // "1993-12-19 18:30:00"
if (d.parse("01.02.2026", "DD.MM.YYYY")) ...          // strict; false: unchanged, not valid
d.addDays(30)->addHours(-2);                        // addSeconds / Minutes / Hours / Days
```
- Format tokens (as in `ostd::Date`): `YYYY`, `YY` (parsed: 00..69 the 2000s), `MM`, `DD`, `HH` /
  `hh` (24h, padded / not), `Min` / `min`, `SS` / `ss`; anything else is literal text.
- `getYear`, `getMonth`, `getDay`, `getHour`, `getMinute`, `getSecond`, `getWeekDay` (0 Sunday),
  `getDayOfYear`, `getFields(&y, &m, &d, &h, &mi, &s)`, `getUnixTime`, `setUnixTime`,
  `set(y, m, d, h, mi, s) -> bool`, `isValid`, `compare(&other)` (-1 / 0 / 1), `equals`,
  `daysUntil(&other)`.
- `setTimeZone(minutes)`: a fixed UTC offset (no daylight saving) for the fields (the getters,
  `set`, `parse`, `toString`); comparisons and arithmetic use the moment itself.
- The calendar: `is_leap_year`, `days_in_month`, `days_from_civil(y, m, d)` (days from 1970-01-01),
  `civil_from_days(days, &y, &m, &d)`.

### Supervisor and user mode (SDK)

The CPU runs in supervisor mode (the BIOS, the boot loader, the kernel) or user mode (programs:
no `hlt`, `rti`, `umode`, interrupt flag changes, `int` to a fault vector, or writes to the
protected registers: those are faults). `cpu.ds` has what a kernel needs (the hardware side is
in `other/info/info`, "Modes"):
```
dr::set_interrupt_vector_entry(dr::cpu::Faults.AccessViolation, &on_fault);   // 0x00 - 0x05
dr::set_interrupt_vector_entry(0x40, &on_syscall, true);  // user-callable: a system call
dr::cpu::set_kernel_stack(top, size);                 // traps from user mode switch to it
dr::set_interrupt_interval(10);                       // the Timer interrupt (0xC0) every 10 ms
dr::cpu::enter_user_mode(program_main, stack_top, stack_size);   // doesn't return
```
- In a handler (an `interrupt sub`): `dr::cpu::is_supervisor()`, `fault_vector()`, `fault_ip()`,
  `fault_address()`; `trap_frame()` (called directly from the interrupt sub) for
  `trap_from_user(frame)`, `trap_return_address(frame)`, `set_trap_return_address(frame, ip)`.
- A fault's handler returns to the faulting instruction: fix the cause, return elsewhere, or
  don't return (end the program). Faults not handled stop the machine, as before. Supervisor
  mode faults too (the kernel's own bugs: `trap_from_user(frame)` is false), except a stack
  overflow and a fault inside a fault handler (a double fault): those stop the machine.
- `enter_user_mode`: entry runs as a call on the user stack (`set_user_stack`, for the stack
  check), called with FP 0 (a backtrace ends there); if it returns, a `hlt` follows (a
  privileged instruction fault). `drop_to_user()`
  switches mode in place.
- Memory protection: a permission map (16 KiB in RAM, 2 bits a 4 KiB page: user read / execute,
  user write) set with `set_permission_map(map)`; `set_page_permissions(map, address, size,
  dr::cpu::Permissions.ReadWrite)` (`None`, `Read`, `Write`, `ReadWrite`), `page_permissions(map,
  address)`. Denied accesses are access violation faults (`fault_address()`); a handler can also
  grant the page and return (the instruction runs again). No map: user mode accesses everything.
  The devices (and the interrupt vector) share the page at 0x01000000: give it `Read` (user code
  then still reads the timer) or `None`.
- The boot stack: the BIOS, the boot loader and the kernel's `main` run on the stack the CPU
  starts with (CMOS StackSize bytes at the end of the RAM: `boot_stack_start()`,
  `boot_stack_end()`). `switch_stack(top, size, kernel_main)` moves to a stack of the kernel's
  own for good (it becomes the kernel stack; it never returns), after which
  `release_boot_stack()` sets StackSize to 0 (SP there is a stack overflow from then on; a reset
  gives it back) and the memory can go to a heap. It returns false while SP is still on it.

### Memory: dr::Heap (SDK)

`heap.ds`: an allocator over one or more regions of memory, its state in a header at the start of
the first one (no globals: it works from ROM, and can be handed from program to program).
```
var heap: dr::Heap* = dr::Heap::create(start, size);   // null: too small
heap->add_region(other_start, other_size);             // up to dr::HeapMaxRegions (16), no overlaps
var p: u8* = heap->alloc(100);                         // 8 byte aligned; null: no room
var page: u8* = heap->alloc_aligned(4096, 4096);
p = heap->realloc(p, 200);                             // in place when it can, else moved
heap->free(p);                                         // false: not the heap's, or freed already
```
- Also `alloc_zeroed`, `block_size(p)`, `owns(p)`, `total_bytes()`, `free_bytes()`,
  `largest_free()`, `check()` (walks every block: false after a header was overwritten),
  `region_count()`, `region(i)`, `dr::Heap::valid(h)`.
- In supervisor mode each operation runs with interrupts disabled (handlers may allocate); in user
  mode they don't (the interrupt flag is privileged there).
- `dr::bios_alloc` / `dr::bios_free` use the system heap through the BIOS (int 0x20):
  `dr::system_heap()`, its address in CMOS HeapStart. The BIOS makes it (4 MiB below the boot
  stack); a kernel can make its own and `dr::set_system_heap(heap)` (supervisor mode): from then
  on `dr::bios_alloc` everywhere serves the kernel's heap. A kernel calls its heap directly where it
  can: no interrupt (and `int` does nothing in supervisor mode while interrupts are disabled: in
  an atomic block, or before the kernel enables them).

### Text formatting (libcore)

`va.ds`: `std::VaList` (the extra arguments of a variadic sub: `count`, `get_i32(i)`, `get_u32`,
`get_f32`, `get_bool`, `get_ptr`, `get_string`, `word(i)`; `typed()`, `type_of(i)`), and
formatting with it:
```
var n: u32 = std::format(buffer, capacity, "x = {}, hex {:08x}, %-6s|%.2f", x, x, name, f);
std::vformat(buffer, capacity, fmt, &args);       // another variadic sub's arguments
std::debug_printf("ticks: {}\n", ticks);          // through debug_print (up to 255 characters)
```
- `{}` formats the next argument as its type says (integers in decimal, `f32` with 6 decimals,
  strings, `bool` as true / false, pointers as `0x0000abcd`); `{:spec}` and the C style `%spec`
  take `[-][0][width][.precision][conversion]` (`{:>8}` too): `d i u x X b o c s f p B`.
  `{{`, `}}`, `%%`: the characters themselves.
- With types (`format`, `debug_printf`), a value is converted to what the spec asks (`%f` of an
  integer, `%d` of an `f32`: truncated), `%s` of a non-string writes `<not a string>`; a missing
  argument writes `<missing>`.
- The text is cut to capacity - 1 and always ends with a 0; the result is its whole length (as
  `snprintf`).

### Declarations (top level)
```
var name: type = value;                 // global variable (the type can be inferred: var n = 5;)
auto name = value;                      // the type from the value, and saying so: an error with ': type'
                                        // or without a value (variables only: locals, globals, for,
                                        // static; not parameters or fields)
const NAME = 5;  const MASK: u32 = 0xFF;    // integer constants
const Origin: Point = { 3, 4 };         // struct constants (see below)
const VERSION = "DragonBIOS 0.5";       // string constants
const Primes: u16[] = { 2, 3, 5, 7 };   // constant arrays: read-only data in the program
const START = read_clock();             // not a constant value: a variable that never changes
constexpr SIZE = 4 * 1024;              // must be a compile-time constant (see "const, constexpr...")
constinit g_count: u32 = SIZE;          // a variable whose value is known when compiling
                                        // (all constants can also be declared in a sub, see below)
enum Color { Red, Green = 5, Blue }     // Color.Blue == 6
enum Kind: u8 { Cmd, Data = 0x80 }      // a typed enum: 1 byte (in structs, arrays, sizeof); the
                                        // values must fit the type (an error otherwise)
alias Handler = sub(i32) -> void;       // a name for a type
struct Point { var x: i32; var y: i32 = 7; }    // fields can have constant default values
struct Box { var w: u32; sub area() -> u32 { return this->w * this->w; } }   // methods (see below)
struct Snake { readonly var length: u32 = 3; }  // readonly: only its subs change it (see below)
struct Id { const value: u32; readonly sub get() -> u32 { ... } }  // const fields, readonly subs
class Dog ex Animal impl Pet { override sub speak() { ... } }   // classes, interfaces (see below)
sub name(var a: i32, b: u8 = 0) -> i32 { ... }  // 'var' is optional; default values are constants
sub name(var a: string) -> void { ... } // overloads: same name, other parameter types
sub later(var x: i32) -> i32;           // a declaration (the definition can come later)
interrupt sub handler() { ... }         // ends with rti; no parameters, returns void
inline sub sq(var x: i32) -> i32 { ... }    // the body is generated at every call (see below)
constexpr sub f(var n: u32) -> u32 { ... }  // evaluated when compiling when it can (see below)
consteval sub g(var n: u32) -> u32 { ... }  // always evaluated when compiling
low { ... }                             // raw dasm (e.g. helper routines with labels)
namespace Bios { ... }                  // groups declarations under a name (see below)
```
Everything can be used before it is declared. `main` is the program's entry: `sub main() -> i32`
(or `-> void`), or `sub main(var argc: u32, var argv: string*) -> i32` for the program's
arguments (see "Starting a program").

### Namespaces
```
namespace Bios {
    const ROWS = 25;
    enum Color: u8 { Black, White = 15 }
    namespace Text16 {                          // nested: Bios::Text16
        sub print(var s: string) -> void { ... }
        sub clear(var c: Color = Color.Black) -> void { ... }   // Bios::Color
    }
}
namespace Bios::Text16 {                        // reopened (in any file), named with A::B
    sub lines() -> i32 { return ROWS; }         // Bios::ROWS
}

Bios::Text16::print("hi");
var c: Bios::Color = Bios::Color.White;
```
- A namespace holds any top-level declaration (subs, globals, constants, enums, aliases,
  structs, low blocks, other namespaces). Its names are used from outside as `Namespace::name`
  (from any depth: `Bios::Text16::print`).
- A name is looked up from the innermost namespace outwards, then globally: in
  `Bios::Text16`, `print` finds `Bios::Text16::print`, then `Bios::print`, then `print`.
  `::name` is the global one (`::print` inside a namespace that has its own `print`).
- `.` is for values (enum members, the fields of struct constants: `Bios::Color.White`), `::`
  for scopes (namespaces, a struct's static subs and nested types).
- An alias of a struct or enum works as that struct or enum, scopes included: with
  `alias Con = Gfx::Console;`, `Con::Errors.Bad`, `Con::Cell`, `Con::make()`, `Con.make()`;
  with `alias C = Bios::Color;`, `C.White` (also `$C.White` in low blocks).
- `$name` in low blocks follows the same lookup (from the namespace of the sub, or of the low
  block); `$Bios::ROWS` works too.
- The labels and data symbols are made from the full name (`__ns_Bios__Text16__print`): names
  in different namespaces never clash. A namespace and another declaration cannot have the
  same full name. No `using namespace` (yet).

### Local constants and static variables
In a sub (or any block), `const` declares any kind of constant (integer, struct, string,
array) visible only there (one whose value is not a compile-time constant is a variable that
never changes: see "const, constexpr, constinit"); `static var` is a variable that lives for
the whole program, like a global, but is only visible there:
```
sub size_text(var bytes: u32, var buff: u8*) -> string {
    const units: string[] = { "B", "KiB", "MiB", "GiB" };     // stored once, in the data
    static var calls: u32 = 0;                                // keeps its value between calls
    calls++;
    ...
}
```
- Compile-time ones are stored exactly like the top-level ones (a hidden declaration named
  `<sub>::<name>`, which is also what error messages show), so they are safe in ROM. The others
  are computed where they are declared, every time (`const square = i * i;` in a loop).
- A static variable is set once, before `main` (like a global, in the order of the source),
  not at its first use; its value can be any expression that does not use the sub's locals or
  parameters (it can use constants, globals and other subs).
- `static const` is a `const` set once (a value that is not a compile-time constant: before
  `main`, like a static variable). `static` is an error at the top level.
- Each sub (and each lambda) has its own; an inline sub's static variable is shared by all the
  places it is expanded. Lambdas can use the constants and statics of the sub around them.
- Inner declarations hide outer ones (a local `var` hides a constant of the same name, and the
  other way around), `$name` in low blocks included. `--rom`: static variables are errors.

### Methods
Subs written in a struct are its methods: they are called on a struct (`x.m()`) or a pointer to
one (`p->m()`), and get it as `this` (a `Struct*`). They are not stored in the struct (its size
and layout stay the same).
```
struct Counter {
    var value: i32;
    var step: i32 = 1;
    sub add(var n: i32) -> i32 { this->value += n; return this->value; }
    sub add() -> i32 { return this->add(this->step); }     // overloads, default values, inline sub
};

var c: Counter;
c.add(5);                                        // Counter::add(&c, 5)
var p: Counter* = &c;
p->add();
var f: sub(Counter*, i32) -> i32 = Counter::add; // the method as a sub: this comes first
```
- The fields are used through `this->` (`this->value`); the struct's fields and methods cannot
  have the same name. A method is defined in the struct (no declaration without a body), and
  only named structs have methods.
- For dynamic dispatch, use a class's virtual subs (see "Classes"), or sub pointer fields (their
  default value can be a sub: `var area: sub(Shape*) -> u32 = square_area;`, called as `s->area(s)`).
- `priv` before a field or a sub (`priv var balance: i32;`, `priv sub add(...)`, `priv static
  sub ...`) makes it private: only the struct's own subs (methods and static subs, the lambdas
  written in them) can use it, on any instance of the struct. Everything else is public. Outside
  the struct, `{ ... }` can only set the fields before the first private one (the private ones
  get their default values), and the private fields of struct constants are read only inside.
- `readonly` before a field (`readonly var length: u32 = 3;`) lets everyone read it, but only the
  struct's own subs (as for `priv`) change it: outside, assigning it, `++` / `--` and changing a
  field or element in it (`s.head.x = 1`, `s.cells[0] = 1`) are errors; its address is a const
  pointer (`&s.length` is a `const u32*`), and only readonly subs can be called on a struct
  field. Initializing it (its default value, `{ ... }`) and copying a whole struct (`a = b`) are
  allowed. It protects the field itself: a pointer field's target can still be changed, and an
  array field still converts to a pointer (`print(s.name)`). Only named structs have readonly
  fields; a field is either `priv` or `readonly`.
- `const` fields (`const id: u32 = 1;`) never change once the struct is built: their value is
  their default, `{ ... }`, or what the constructor gives them (`this->id = id;` in `new`, the
  only place they can be assigned). A whole struct with a const field (also in a struct field or
  an array of them) cannot be assigned (`a = b`): initialize a new variable instead.
- `readonly sub` (a method that does not change the struct): its `this` is a `const T*`, so it
  cannot change the fields or call subs that are not readonly. Only readonly subs can be called
  on a constant (or through a `const T*`). A readonly and a normal sub can have the same name and
  parameters (the readonly one for constants, the other one for variables).
- `constexpr NAME = value;` in a struct is a constant of the struct (no memory): `Struct.NAME`,
  `Struct::NAME`, `x.NAME`, or just `NAME` in its subs (`priv constexpr`: only there).
- `static sub` in a struct has no `this`: it is a sub grouped under the struct's name, called as
  `Struct.name(...)` or `Struct::name(...)` (also from the struct's other subs: a struct's subs
  are not found by their name alone). A struct can have only static subs (and no fields).
- `static var` / `static const` in a struct is one variable for the struct (a global under its
  name), not one per instance: `Struct.count`, `Struct::count`, `x.count` / `p->count` (`x` is only
  its type), or `count` in its subs. It is not part of `sizeof`, `{ ... }` or a copy. `priv`
  (only its subs use it) and `readonly` (`readonly static var`: only its subs change it) work as
  for fields; `static const` follows `const` (a compile-time constant, or computed when the
  program starts); `static var p: Point(1, 2)` calls the constructor (before `main`). In a struct
  template, each instance has its own (`Pool<u8>.made`, `Pool<u32>.made`); in a library they are
  its data (programs use them through its interface).
```
struct Counter {
    static var count: u32 = 0;
    readonly static var created: u32;
    var id: u32;
    sub new() { count++; created++; this->id = created; }
};
var a: Counter();
Counter.count += 1;      // a.count is the same variable
```
```
struct Print {
    static sub str(var s: string) { ... }
    static sub line(var s: string) { Print.str(s); Print.str("\n"); }
};
Print.line("hello");
```

### Constructors
`sub new(...)` in a struct is its constructor: a method (on `this`, returning nothing) that
`var p: Struct(args);` calls on the new variable, after its fields got their zeroes and
default values.
```
struct Point {
    var x: i32;
    var y: i32 = 7;
    sub new(var x: i32, var y: i32) { this->x = x; this->y = y; }
    sub new(var v: i32 = 0) { this->x = v; }        // overloads and default values
}
var origin: Point(0, 0);            // a global: built before main, in the order of the source
sub main() -> i32 {
    var p: Point(1, 2);             // Point::new(&p, 1, 2)
    var q: Point();                 // new(): q.x == 0, q.y == 7
    static var s: Point(5);         // once, before main (like a global)
    ...
}
```
- A constructor only runs where it is written: `var p: Point;`, `{ ... }` lists, arrays and
  struct fields work as before (no hidden calls), and a variable with a constructor call
  cannot also have a value. (A destructor, if the struct has one, still runs for them: it must
  accept a value that was only zeroed. A copy constructor is called for copies: see
  "Destructors, copies and moves".)
- The arguments see the names around the declaration (in `var x: Point(x);`, `x` is the outer
  one). `priv sub new` makes a struct that only its own subs can build; `new` is a method like
  the others (`p.new(3)` runs it again). It cannot be `static` or return a value.
- `--rom`: globals (and so global constructors) are errors, as before.

### Nested structs and enums
A struct can declare structs and enums: they are named `Outer::Inner`, and inside the struct
(its fields, subs and the types nested in it) also just `Inner`.
```
struct List {
    struct Node { var value: i32; var next: Node*; }   // List::Node
    enum Kind: u8 { Empty, Some }                       // List::Kind (List::Kind.Some)
    priv struct Cursor { var at: Node*; }               // only usable in List
    var head: Node*;
    sub kind() -> Kind { return this->head == null ? Kind.Empty : Kind.Some; }
}
var n: List::Node;
```
- A nested struct is a struct of its own (not a field: `List` has only `head`); it can have
  fields, methods and nested types.
- `priv struct` / `priv enum`: only the outer struct (and what is nested in it) can use the
  type. The subs of a nested struct can use the private fields and subs of the structs around it.

### Nameless structs
`struct { fields }` is a type: it can be used for variables (global and local), fields, arrays,
pointers, `sizeof`, aliases, and so on. The fields are declared as in a named struct (`var`, and
default values).
```
var cursor: struct { var x: i32; var y: i32; } = { 3, 4 };
struct Window {
    var pos: struct { var x: i32; var y: i32 = 7; };    // window.pos.x
    var title: string;
}
var table: struct { var key: u8; var value: i32; }[8];
```
Every nameless struct is its own type (as in C): two variables declared with the same fields
cannot be assigned to each other. To share one, give it a name (`struct`, or
`alias Pair = struct { ... };`). In messages they are shown as `struct { ... } (file:line)`.

### Lambdas
`sub(params) -> type { body }` is an expression: a sub without a name, whose value is a sub
pointer (`sub(params) -> type`). It is compiled as a normal (hidden) sub, so it costs nothing
more than one.
```
var twice = sub(var x: u32) -> u32 { return x * 2; };      // inferred: sub(u32) -> u32
map(arr, 3, sub(var x: u32) -> u32 { return x * x; });     // as an argument
var f: sub(u32) -> u32 = sub(var x: u32) -> u32 { return x - 1; };
var n = sub(var a: i32, var b: i32 = 10) -> i32 { return a * b; }(4);   // called right away: 40
install(0x41, interrupt sub() { ticks++; });               // an interrupt sub (ends with rti)
```
Parameters, default values and the return type work as in a named sub (no `-> type`: void).
Lambdas capture nothing: the body can use globals, constants, subs and its own parameters,
not the local variables or parameters of the sub around it (an error; pass them as parameters).

### Structs by value
Structs can be parameters and results like any other value: the sub gets its own copy (changing
it doesn't change the caller's), and a result is a new value.
```
struct Point { var x: i32; var y: i32; };
sub add(var a: Point, var b: Point) -> Point { var r: Point = { a.x + b.x, a.y + b.y }; return r; }
sub scale(var p: Point, var k: i32) -> Point { p.x *= k; p.y *= k; return p; }    // its own copy

var c = add(a, b);                  // a Point
var d = scale(a, 3).x;              // a result's field
g_origin = add(mk(1, 2), mk(3, 4)); // also in a global's value (computed before main)
```
- Also for inline subs, methods, sub pointers (`sub(Point, Point) -> Point`), variadic subs (the
  fixed parameters; the extra arguments (`...`) still take structs only by pointer), templates
  (`T` a struct) and libraries.
- How: the caller copies a struct argument into its own frame and passes the copy's address (one
  32-bit slot, as for any parameter); a sub returning a struct gets the address of the caller's
  room for the result as a hidden first argument (`return s;` copies `s` there, before the defers
  run). So each struct argument and result is a copy: a big struct is cheaper by pointer (`T*`),
  as in C.

### Classes
A class is a struct that can extend another class (or a struct) and have virtual subs. A struct
stays exactly its fields (no hidden data); a class adds a table pointer only when it needs one.
```
interface Shape {                           // only subs (all pure: no body), no fields
    sub area() -> i32;
}
abstract class Figure impl Shape {          // abstract: has pure subs, only classes derived from it are built
    prot var s: i32;                        // prot: its subs and those of the classes derived from it
    sub new(var s: i32) { this->s = s; }
    pure sub sides() -> i32;                // a slot in its table, no body
    virt sub name() -> string { return "figure"; }  // virtual: a derived class can override it
}
final class Square ex Figure {              // final: no class extends it
    sub new(var s: i32) { Figure::new(s); } // the base's part first
    override sub area() -> i32 { return this->s * this->s; }
    override sub sides() -> i32 { return 4; }
}
var sq: Square(3);
var f: Figure* = &sq;                       // a class pointer converts to a pointer to a class it extends
var sh: Shape* = &sq;                       // ... or to an interface it implements
f->name();  sh->area();                     // through the object's table: Figure::name, Square::area
var back = static_cast<Square*>(sh);        // back to the class (unchecked: it must point at one)
```
- `class Name ex Base impl I1, I2 friend A, f { ... }`: one base class (no multiple inheritance),
  any number of interfaces. A class can extend a class or a struct; `interface Name ex Other`
  extends one interface. `abstract class` (it may have pure subs, and is only pointed at: no
  variables, fields, parameters or arrays of it) or `final class` (nothing extends it).
- A class has its base's fields (first, at the same offsets) and subs: `d.x`, `d.baseSub()`,
  inherited static subs, constants and static fields (`Derived.make()`, `Derived::KIND`, `KIND`
  in its subs). Nothing it inherits can be hidden: a field, or a sub with the same parameters as
  one of a class it extends, is an error, unless that sub is virtual and the new one is marked
  `override`. A sub with the same name and other parameters is one more overload.
- Virtual subs: `virt sub` adds a slot to the class's table, `override sub` replaces one (a
  class's virtual, or an interface's sub: the parameters and the result the same, both readonly
  or neither), `pure sub ...;` is a slot without a body (abstract classes), `final` (with `virt`
  or `override`) stops the classes derived from it from overriding it. A call through a pointer
  (`p->m()`, `this->m()`) uses the object's table, so it calls the object's own class's sub; a call
  on a variable (`x.m()`: its type is exact), on a final class or a final sub is direct (and an
  inline sub stays inline there). Virtual subs are not static, constructors and consteval subs are
  not virtual, method templates neither.
- `Base::m(args)` (or `Base.m(args)`, the form for templates: `Base<T>.m(args)`) in a class's sub
  calls Base's `m` on `this`, directly (not through the table): what an override uses to extend
  its base's sub. A pure sub has no body to call.
- Constructors are not inherited. A class's `new` builds its base's part first: `Base::new(...)`
  (or `Base.new(...)`), as one of its statements (only there, only for the class it extends);
  without one, the base's constructor without arguments is called first (an error if the base has
  constructors, but none without arguments). A class without a constructor gets an implicit
  `new()` when it has a table or its base has a constructor without arguments. While the base's
  constructor runs the object is of the base's class (its virtual calls go to the base's subs);
  the class's own table is set right after it.
- An object gets its tables where its fields get their defaults (`var x: C;`, `var x: C(...)`,
  globals, fields, arrays) and in its constructor: memory from elsewhere (a heap) is built with
  `p->new(...)` (the implicit `new()` for a class without one). A copy into a new value (a value
  given or returned, `var b: B = *p;`) has its type's table, and an assignment (`*p = b;`) keeps
  the object's own: a derived object never becomes a base value by accident. A derived class's
  value does not convert to its base's value at all (`var a: A = b;` is an error): use pointers.
- Conversions: a class pointer converts to a pointer to a class it extends or an interface it
  implements (moving to that part of the object; `null` stays `null`); the nearest one is the
  better overload, `void*` the worst. The other way needs `static_cast<T*>(p)` (unchecked).
  `static_cast` does what converts anyway, numbers to numbers, and class pointers up or down;
  `cast<T*>` between pointers to classes derived from one another is an error (it would not find
  the part of the object). Pointers to related classes can be compared (`f == &sq`).
- `dynamic_cast<T*>(p)` asks the object's table: a pointer to its `T` part (`T` a class up the
  object's chain, or an interface it implements: also from one interface to another), or `null`.
  `p` points at a class with a table (virtual subs or interfaces) or an interface; an upcast is
  decided when compiling. While a base's constructor runs, the object is of the base's class.
  (Types are the same when their names are: a library's objects work with the program's types.)
- `friend A, B::C, f`: those structs, classes and subs have the access of the class's own subs
  (`priv`, `prot`, changing `readonly` fields). Structs can have friends too. Friendship is not
  inherited.
- `prot` (fields, subs, static fields, constants): the class's subs, those of the classes derived
  from it, and its friends. `priv`: only the class's own (a derived class's subs cannot use its
  base's `priv` members).
- Layout: a class with virtual subs has its table pointer first (when its base has none, the
  base's fields follow it, 4 bytes on), then a table pointer for each interface it adds, then its
  fields. An interface is 4 bytes (its table pointer). A table (read-only data) holds the class's
  type, the offset of its pointer in the object, the destructor and its `this` adjustment, then a
  sub and a `this` adjustment per slot. A type (for `dynamic_cast`): its name, its base's type and offset,
  and each interface's type and table pointer offset.
- Templates can declare classes and interfaces (`class Box ex Base<T> impl Getter<T>`); classes
  work in libraries and shared libraries (a program can extend a library's class); constant
  values (`constexpr`, struct constants) and consteval subs cannot hold objects with tables.
  Condition built-ins: `is_class(T)`, `is_interface(T)`, `is_abstract(T)`, `is_base_of(B, D)`;
  `has_method` and `is_range` see inherited subs.

### Destructors, copies and moves
`sub del()` in a struct (or a class) is its destructor: it runs when a value's life ends. A
struct that owns something (memory from a heap, a handle) frees it there.
```
struct Buffer {
    var data: u8*;
    var size: u32;
    sub new(var size: u32) { this->data = alloc(size); this->size = size; }
    sub new(var o: const Buffer*) { ... a copy of o's data ... }   // the copy constructor
    sub del() { free(this->data); }
}
sub load() -> Buffer { var b: Buffer(64); ...; return b; }   // moved out: no copy
sub main() {
    var b = load();             // moved in: no copy
    var c: Buffer = b;          // copied: new(&b)
}                               // c then b destroyed
```
- When: a local when its scope ends (the end of its block, `return`, `break`, `continue`; the
  last declared first); a temporary (a call's result, a value passed to a sub) when its statement
  ends (in `&&`, `||`, `?:`: only if it was made); a global or a static local after `main` returns
  (the last built first; not a library's); the fields of a struct after its destructor's body
  (the last first), then a class's base's destructor; an array's elements (the last first).
  Memory from elsewhere: `p->del()` (with `p->new(...)`).
- A struct whose fields (or base) have destructors gets an implicit `del()` for them. Destructors
  are not inherited (each struct has its own), are never static, and take nothing.
- In a class with a table, `del` is virtual by itself (the table's destructor slot): `p->del()`
  through a base pointer runs the object's own. While a base's destructor runs, the object is of
  the base's class again.
- Copies (a variable from another, a value passed, a value returned that is not a local of the
  sub): the copy constructor `sub new(var o: const T*)` (or `T*`) if the struct has one; member by
  member if its fields (or base) have one (each field by its own); its bytes otherwise. A struct
  with its own destructor and no copy constructor cannot be copied (an error): moves still work.
- Moves: a call's result (into a variable, passed by value, assigned), and a local returned by
  its sub, are moved: their bytes taken, their destructor not run. No `&&`, no `move()`.
- Assignment `a = b`: `operator=` if the struct has one (it can also take other types:
  `sub operator=(var s: string)`); otherwise a struct with its own destructor or copy constructor
  has its old value destroyed, then gets a copy (nothing when it is assigned itself; a call's
  result is moved in); a struct holding such fields is assigned member by member. A class object
  keeps its own table.
- Constant values and compile-time evaluation have no destructors (an error).

### Operators
A struct (or a class) can give meaning to operators on its values: `sub operator<op>(...)` in
it is a method whose first operand is `this`; a top-level `sub operator<op>(...)` takes all its
operands (one of them a struct, or a pointer to one), for `2 * v`.
```
struct Vec {
    var x: i32; var y: i32;
    sub operator+(var o: Vec) -> Vec { ... }             // a + b
    sub operator-(var o: const Vec*) -> Vec { ... }      // a - b (b by its address: no copy)
    sub operator-() -> Vec { ... }                       // -a
    readonly sub operator==(var o: Vec) -> bool { ... }  // a == b (and a != b)
    readonly sub operator<(var o: Vec) -> bool { ... }   // a < b (and >, <=, >=)
    sub operator+=(var o: Vec) { ... }                   // a += b
    sub operator++() -> Vec* { ... }                     // ++a
    sub operator post++() -> Vec { ... }                 // a++
    sub operator[](var i: u32) -> i32* { ... }           // a[i]: a pointer result, so a[i] = 5 works
    sub operator()(var t: i32) -> i32 { ... }            // a(t)
}
sub operator*(var k: i32, var v: Vec) -> Vec { ... }     // 2 * v
```
- Operators: `+ - * / % & | ^ << >> == != < <= > >= ! ~ ++ --`, the compound assignments,
  `[]` and `()` (those two only as methods); `-` and `+` with no operand are unary. `&&` / `||`,
  `&` (address), `.`, `->`, `?:` and `sizeof` keep their meaning.
- `a != b` is `!(a == b)` when there is no `operator!=`; with only `operator<`, `a > b` is
  `b < a`, `a <= b` is `!(b < a)`, `a >= b` is `!(a < b)`. `a += b` is only `operator+=` (not made
  from `+` and `=`).
- An operator is chosen as an overloaded sub is: the struct's (with those it inherits) and the
  top-level ones visible from the code or from an operand's namespace. A parameter `T*` /
  `const T*` takes a struct operand by its address (a variable, or a call's result).
- Operators are subs like the others: `readonly`, `inline`, `constexpr`, `virt` / `override`
  (`(*shape)()` calls the object's own), in templates and libraries; not `static`.

### Variadic subs
A sub whose parameters end with `...` takes more arguments, of any scalar type; with
`...typed`, each one's type is passed too. Three built-ins read them, in the sub itself:
```
sub sum(var count: u32, ...) -> i32 {
    var args: std::VaList(va_start(), va_count());    // (libcore's va.ds)
    var total: i32 = 0;
    for (var i: u32 = 0; i < args.count; i++)
        total += args.get_i32(i);
    return total;
}
sum(3, 10, 20, 30);
std::format(buffer, 64, "{} is {} ({:08x})", name, value, value);  // a ...typed sub
```
- `va_count()`: how many extra arguments the call gave; `va_start()`: the address of the first
  (the next ones are each 4 bytes lower); `va_types()` (`...typed` only): the address of their
  types (the same way down; the values are libcore's `std::VaTypes`: Bool 1, I8 2, U8 3, I16 4,
  U16 5, I32 6, U32 7, F32 8, Pointer 9, String 10 (u8* / i8*), Sub 11). All three are `u32`.
- Each extra argument is one 32-bit slot: integers smaller than 32 bits (and `bool`) are
  widened (to `i32` if signed, else `u32`), an `f32` is passed as its bits, arrays as pointers,
  structs only by pointer (an error otherwise). An overloaded sub's name needs a cast.
- Overloads: a sub that takes the arguments without `...` wins (`f(i32)` over `f(i32, ...)` for
  `f(1)`). Sub pointer types can have `...` too: `sub(string, ...typed) -> u32`.
- Not with default values, not on inline subs, interrupt subs or `main`. Methods and lambdas can
  have `...`.
- Without types (`...`), the sub has to know what it got (a count, a format string), as in C;
  `...typed` costs one more push per argument.

### Inline subs
`inline sub` works like a sub, but every direct call generates the body in place: no `call`,
no frame, the arguments are stored in hidden locals of the caller.
```
inline sub square(var x: i32) -> i32 { return x * x; }
var y = square(a + 1);      // the parameter is a copy: a is not changed
```
- Useful for small, often called subs (register accessors, tiny math); every call adds the
  body's code, so big inline subs make the program bigger.
- An inline sub used as a sub pointer (`var f: sub(i32) -> i32 = square;`) also gets real code;
  otherwise it has none (but it is still checked).
- Not allowed: recursion (also through other inline subs), `low` blocks (the code needs a real
  call frame), `inline interrupt sub`, `inline sub main`.
- `dsc --no-inline` compiles inline subs as normal subs: every call is a real `call` to the sub's
  label, which is easier to follow in the disassembly and the debugger. The rules above still
  apply (the program also compiles without the option).

### assert
```
assert(p != null);
assert(count < MAX, "too many entries");    // an optional message (a string)
```
When the condition is false, the program calls the global `__assert_fail(file, line, condition,
message)` (the condition as written, the message or ""); without one, an assert is an error.
The program decides what a failed assert does, not its libraries: libcore and the SDK don't
define it.
- A DragonOS program gets it from the stdlib (`#import("stdlib")`): it prints the failure with
  the Print system call and ends the process (the Exit system call, code 134).
- A program on the SDK (a BIOS, a boot loader, a kernel) writes its own; the SDK's
  `dr::assert_fail_halt` prints to the host's terminal with `debug_print` (no VGA needed), then
  stops the machine (`hlt`; with `dsc -g`, ddb stops first at a `debug_break`):
  ```
  sub __assert_fail(var file: string, var line: u32, var condition: string, var message: string) {
      dr::assert_fail_halt(file, line, condition, message);
  }
  ```
  DragonOS's kernel shows its panic screen instead.
- A library can't use assert: it would need the handler when it is compiled (and it would then
  be the library's, for every program).

With `NDEBUG` defined (`dsc -D NDEBUG` or `#define(NDEBUG)`), asserts are only checked by the
compiler: no code, the conditions are not run (so they should not have side effects that
matter), and no `__assert_fail` is needed.

### debug_print
```
debug_print("got here\n");
debug_print(name);                      // any string / u8* (zero-terminated)
```
Prints a string to the host's terminal (drun's or ddb's stdout), not to the VM's screen: for
tracing without the VGA stack. It is the `debug_print` instruction (`debug_print $label` /
`debug_print R1` in dasm), which is always assembled and run (not only with `-g`, unlike
`debug_break`). At most 4096 bytes are printed per call.

### const, constexpr, constinit
`const` declares something that never changes once it has its value, anywhere a `var` can be
(globals, locals, parameters, fields; `const p: Point(1, 2)` too). If its value is a
compile-time constant, it is one (usable in array sizes, enum values, `$NAME` in low blocks...,
as always); otherwise it is a variable that cannot change:
```
const LIMIT = 10;                       // a compile-time constant
const START = read_clock();             // computed when the program starts (globals: before main,
                                        // in the order of the source, like var)
sub f(const n: u32) {                   // the sub cannot change n
    const half = n / 2;                 // computed at every call
    half++;                             // error: 'half' is a constant (const): it cannot change
}
```
- `constexpr` is a `const` that must be a compile-time constant (an error otherwise): for a
  value that must not cost anything when the program runs.
- `constinit` (globals only) is a `var` whose value must be known when compiling: no code sets it
  before `main` (it is in the program's data), but it can change.
- A constant's address is a const pointer (`&START` is a `const u32*`): it cannot be changed
  through it.
- `--rom`: a `const` computed when the program runs needs RAM: an error (use constexpr values).
- Libraries: a constant computed when a library starts is a read-only global of the library
  (`readonly var` in its interface).

### Const pointers and readonly subs
`const T*` is a pointer that cannot change what it points at (it can point elsewhere). The
`const` in front of a declaration is about the variable, in a type about the target:
```
var p: const u8* = text;                // *p = 'x' is an error; p = other is fine
const q: u8* = buffer;                  // *q = 'x' is fine; q = other is an error
const r: const u8* = text;              // neither
var s: const u8** = &p;                 // a pointer to a const u8* (const (u8*)* is the other way)
```
- `T*` converts to `const T*` (also as a sub's argument); `const T*` to `T*` needs a cast
  (`cast<u8*>(p)`: when it really may be changed). `const void*` takes any pointer.
- Through a `const T*`, `*p`, `p[i]` and `p->field` cannot be changed (a field that is itself a
  pointer can still point elsewhere: as in C, it protects one level).
- `&` of something that cannot change (a constant, a const or readonly field, through a const
  pointer) is a const pointer; constant arrays still give their elements' plain pointers (as
  before), and string literals are `string` (`u8*`).
- Methods: see `readonly sub` (under Methods); `this` of a readonly sub is a `const T*`.
- Templates: `T*` matched with a `const u8*` gives `T = u8` (then the const is an error: use
  `const T*` for parameters that only read).

### constexpr and consteval subs
A `constexpr sub` is evaluated when compiling when all its arguments are compile-time constants:
its value replaces the call (no code runs). Otherwise (or if it cannot be evaluated) it is a
normal call. A `consteval sub` is always evaluated when compiling (an error if it cannot be), and
has no code in the program:
```
constexpr sub fact(var n: u32) -> u32 { return n <= 1 ? 1 : n * fact(n - 1); }
consteval sub mask(var bits: u32) -> u32 { return (1u << bits) - 1; }

var table: u8[fact(4)];                 // an array size (24)
constexpr ORIGIN: Point = make(3, 4);   // a struct constant from a constexpr sub
var x = fact(n);                        // n is a variable: a normal call
const m = mask(4);                      // 15, when compiling
```
- When compiling, they can use locals, parameters, loops, `if`, recursion (up to 256 deep),
  structs (by value, methods, `this`), arrays, pointers to their own variables, strings, f32,
  constants, constant arrays and struct constants, and other constexpr / consteval subs.
- They cannot use (when compiling) globals that are variables, memory addresses, other subs,
  sub pointers, `...`, low blocks, `debug_print`, `defer`; and they run at most 4 million steps.
  A constexpr call that does any of that is a normal call; for consteval, it is an error that
  says why and where.
- The value: a number, `bool`, f32, `null`, or a struct (read-only data in the program; its
  pointer fields must be null). A call that returns a pointer is not replaced.
- Methods can be constexpr / consteval too (`readonly constexpr sub len() -> u32`), and called
  on struct constants: `std::UINT64_MAX.new_shr(60)` is computed when compiling.
- A consteval sub cannot be used as a value (it has no address); its calls with arguments that
  are not constants are only allowed in another consteval sub.
- Libraries: their bodies are in the interface (like inline subs), so programs evaluate them.

### Struct constants
A `const` with a struct type (named or nameless) is a group of compile-time values: reading a
field gives its value, like an enum member (no memory is read).
```
const MemoryLayout: struct {            // no value: the fields' defaults
    var CMOS: u32 = 0x01000000;
    var VGA: u32  = 0x01000900;
    var Video: u8* = cast<u8*>(0x01000900);
};
const Origin: Point = { 3 };            // { ... }: the fields in order (the rest: defaults, then 0)

var p: u8* = MemoryLayout.Video + 4;
const VGA_END = MemoryLayout.VGA + 0xFF;    // usable in constants, array sizes, enum values...
low { mov R1, $MemoryLayout.VGA }           // and in low blocks ($Box.br.x for nested fields)
```
- The fields can be integers, pointers (`null`, `cast<T*>(constant)`) and structs (nested
  `{ ... }`); arrays and strings are not allowed (they would need memory).
- Every value must be a constant (or the value of a constexpr sub that returns the struct: then
  the constant is that value). With values that are not constants, a `const` struct is a
  variable that never changes (see "const, constexpr, constinit").
- Used as a value (`var c = Origin;`, `&Origin`, passing it to a sub, calling one of its readonly
  subs, a struct field of it), it is a read-only copy in the program's data (only when something
  uses it). Assigning to its fields is an error.

### String constants
`const NAME = "text";` (or `const NAME: string = ...`) is used exactly like the literal: a
`string` pointing at the text, stored once in the program (no code runs to set it up). It works
wherever a literal does: values, default values (of fields and parameters), `u8[]` / `u8[N]`
initializers, other constants (`const ALSO = VERSION;`), and `$NAME` in low blocks (the text's
address). Its address is only known when the program is assembled, so it cannot be used in
integer constant expressions, nor as a field of a struct constant.

### Constant arrays
`const Name: T[N] = value;` (or `T[]`) is an array stored in the program and never written:
```
const Logo: u8[54][] = {                // 19 rows of u8[54] (sizes are read left to right)
    "|===================================================|",
    ...
};
const Names: string[] = { "red", "green", "blue" };     // pointers to the literals
const Table: Entry[] = { { 'a', "alpha" }, { 'b', "beta" } };

for (var i: u32 = 0; i < sizeof(Logo) / sizeof(Logo[0]); i++) { print(Logo[i]); new_line(); }
```
- The values are constants, strings, `null` or `cast<T*>(constant)` (the elements can be
  integers, pointers, arrays and structs of them); everything is in the program's data, so it
  is safe in ROM. With values computed when the program runs, a `const` array is a constant
  variable (set before `main`; a local one at every call); `constexpr` arrays must be constant.
- Assigning to an element (or `++` / `--`) is an error. Its address can be taken (`&Primes[2]`,
  or passing it to a sub): it is a plain pointer (as before const pointers existed), so writes
  through it are not stopped (in ROM they fail at run time): take a `const T*` where only
  reading is meant.
- In low blocks `$Name` is its address.

### ROM programs (`--rom`)
`dsc --rom` compiles a program that runs from ROM (the BIOS): it is a `--raw` binary (loaded at
0x00000000 unless `--load` is given; use `--fixed 16777216,0xFF` for a BIOS image) and defines
`__ROM__`. Everything stored in the program is read-only there, so whatever would be written
into it is an error: every global `var`. What is left is ROM-safe: constants, string
constants, constant arrays, string literals, code. What changes goes into RAM: locals (on the
stack), or memory at a RAM address (`var state: State* = cast<State*>(0x01010000);`).
Not checked: low blocks (raw dasm), and writes through pointers to the program's data.

### Statements
`var` declarations (locals: scalars are not initialized unless given a value; structs are zeroed
and get their default values), `if` / `else`, `while`, `do ... while (...);`,
`for (init; condition; step)`, `for (auto x in range)`, `if constexpr`, `break`, `continue`, `return`, blocks,
expression statements, `low { ... }`, `assert(...)`, `debug_print(...)`, `defer ...`,
`atomic { ... }`.

### Templates
`template(T) { ... }` holds structs and subs written for any type `T`; each use with types makes
an instance of them, compiled like the code you would have written for those types.
```
template(T) {
    struct Array {
        var data: T[16];
        var count: u32;
        sub push(var v: T) { this->data[this->count] = v; this->count++; }
        inline sub begin() -> u32 { return 0; }        // a range (for-in): see below
        inline sub end() -> u32 { return this->count; }
        inline sub next(var i: u32) -> u32 { return i + 1; }
        inline sub get(var i: u32) -> T* { return &this->data[i]; }
    };
    sub max(var a: T, var b: T) -> T { if (a > b) return a; return b; }
}

var a: Array<u32>;                  // the instance Array<u32>
a.push(5);
for (auto v in a) { }               // v: u32
var m = max(3, 7);                  // T deduced from the arguments: max<i32>
var n = max<u32>(10, 4);            // or given
var nested: Array<Array<u32>*>;
```
- An instance is made the first time it is used, once for each set of types (`Array<u32>` is one
  struct wherever it appears). Its subs and methods are compiled only if something calls them (or
  takes their address): a method that doesn't work for some type is fine as long as nothing calls
  it (its error appears where it is called).
- A template's body is checked for each instance (as in C++): an error in it says where the
  instance was made (`... (in Array<Point>, instantiated at main.ds:12:5)`).
- The types of a sub template's call are deduced from its arguments (`T`, `T*`, `T[N]`,
  `Array<T>*`, sub types); a typed argument decides over a literal (`max(x_u32, 5)`: `T = u32`).
  Types it can't tell are given: `make<u32>()`.
- One `template` block shares its parameters with everything in it. It can be in a namespace, and
  in a struct: method templates (`template(T) { sub add(var v: T) { ... } }`), called as
  `s.add(1)` / `s.add<u8>(1)` (also `static`: `Struct.make<T>()`), also in a struct template.
- `Name<...>` is a template's types wherever the name is a template's (the compiler collects the
  names of every template first); `>>` closes two of them.
- In a library, the templates are in its interface (as source, like inline subs): a program makes
  its own instances. The instances are never exported (each user has its own copy).

**Optional parameters**: `template(K, optional V)`: `optional` once, the parameters after it are
optional too. An instance gives the types it needs:
```
template(K, optional V) {
    struct Entry { var key: K; var value: V; };     // Entry<u32, i8>
    struct Key { var key: K; };                     // Key<u32>
}
var e: Entry<u32>;      // error where V is used: "V is not given here"
var k: Key<u32, i8>;    // error: Key doesn't use V
```
`is_given(V)` tells if it was given (in a `where` or an `if constexpr`).

**Defaults**: a parameter with a default is optional, and an instance without it gets the default
(`template(T, U = T*, N: u32 = 16)`: `Box<u8>` is `Box<u8, u8*, 16>`, the same instance as when
they are written). A default can use the parameters before it (`N: u32 = sizeof(T) * 4`).

**Value parameters**: `name: type` is a value known when compiling (a number, a `bool`, an `f32`,
or a struct value), given like a type (`Ring<u8, 16>`, `Grid<SMALL>`, `Grid<config(3, 3)>`):
```
struct Config { var width: u32; var height: u32; };
constexpr SMALL: Config = { 4, 2 };

template(T, N: u32 = 8) {
    struct Ring { var items: T[N]; readonly sub capacity() -> u32 { return N; } };
}
template(C: Config) {
    struct Grid { var cells: u8[C.width * C.height]; };
}
template(T, N: u32) { sub count(var a: T[N]*) -> u32 { return N; } }

var r: Ring<u16, 4>;        // Ring<u16, 4>
var g: Grid<SMALL>;         // Grid<Config{4, 2}>
var arr: i32[5];
count(&arr);                // N deduced from the array: 5
```
- In the template, the parameter is a compile-time constant (array sizes, conditions, `C.field`,
  `C` as a value...). The argument is any constant expression: a literal, a constant, `N + 1`,
  `-1`, a struct constant, a constexpr sub's value (a comparison needs parentheses: `B<(A > 2)>`).
- An instance is made once per value: `Ring<u16, 4>`, `Ring<u16, 4u>` and `Ring<u16>` with a
  default of 4 are one (the value is converted to the parameter's type). A struct value's
  instance is named by its fields (`Grid<Config{4, 2}>`).
- A value parameter deduced from a sub's arguments: from an array's size (`T[N]`), or from an
  instance's (`Ring<T, N>*`). The others are given (or have a default).
- In a condition, a value parameter is its value: `where N > 0`, `if constexpr (C.width == 4)`.
- A parameter without a type (`template(T)`) takes a type or a value (`describe<u8>()`,
  `describe<5>()`, `describe<ORIGIN>()`): `is_value(T)` tells which, and `value_type(T)` is the
  value's type (an error for a type). A value keeps its own type (`5` is an `i32`, `5u` a `u32`).
  `value_type(expression)` is a type anywhere (`var v: value_type(T) = T;`, a sub's return type,
  `is_type(value_type(T), u32)`, `sizeof(value_type(T))`):
```
template(T) {
    sub twice() -> value_type(T) { return T + T; }
    struct Holder { if constexpr (is_value(T)) { var v: value_type(T); } else { var v: T; } };
}
twice<21>();            // 42 (an i32)
var h: Holder<7u>;      // v: u32
var k: Holder<u8>;      // v: u8
```
- `base_type(T)` is the class `T` extends (an error if it extends none: `has_base(T)` tells), and
  `absolute_base_type(T)` the first class of its chain (`T` itself if it extends none); for a
  pointer to a class, a pointer to it. Types anywhere, like `value_type`
  (`sizeof(base_type(T))`, `-> absolute_base_type(T*)`, `is_type(base_type(T), Shape)`).

**if constexpr around declarations**: at the top level, in a namespace, in a struct's body, and in
a template's body, `if constexpr (condition) { declarations } else { ... }` keeps the declarations
of the branch the condition chooses (the others don't exist: no names, no code). In a template,
the condition is about its parameters, for each instance:
```
template(T) {
    if constexpr (is_float(T)) { sub kind() -> u32 { return 1; } }
    else if constexpr (is_signed(T)) { sub kind() -> u32 { return 2; } }
    else { sub kind() -> u32 { return 3; } }
    struct Holder {
        var value: T;
        if constexpr (sizeof(T) > 2) { var extra: u32; }    // fields and methods too
    };
}
if constexpr (WIDE) { alias Word = u32; } else { alias Word = u16; }
```
- Outside templates, the conditions use constants and types (as `#if` uses `#define`s, but with
  the language's constants and `sizeof`). They are decided in the order of the source: a
  condition can use what an earlier one declared.
- A template's sub or struct that its if constexpr leaves out for an instance is not there for
  it (`kind<...>: not there for these arguments`).

**static_assert**: `static_assert(condition, "message")` (the message is optional) stops the
compilation if the condition is false: `static_assert failed: message`. It is checked when
compiling (no code, nothing to provide, unlike `assert`): at the top level and in namespaces
(after the declarations), in a struct (with its layout: `static_assert(sizeof(Packet) == 6)`), in
a sub (where it is), and in a template's body (for each instance, with its parameters:
`static_assert(N > 0, "a ring holds at least one item")`, the error says which instance).

**Conditions**: `template(T) where <condition> { ... }`: an instance is made only if the condition
holds for its types; otherwise the error names the part that is false
(`four<u8>: its where is false: sizeof(T) == 4`). Several templates can declare the same struct /
sub with different conditions: the one whose condition holds is used (more than one: an error).
```
template(T) where is_integer(T) { sub describe(var v: T) -> u32 { return 1; } }
template(T) where is_pointer(T) { sub describe(var v: T) -> u32 { return 2; } }
template(T1, T2) where (is_numeric(T1) and not is_integer(T2)) or sizeof(T1) == 4 or is_one_of(T2, u8, u32, std::I64) { ... }
```
A condition is an expression computed while compiling: `and` / `or` / `not` (`&&` / `||` / `!`),
comparisons, `+ - * / % & | ^ << >>`, constants, `sizeof(T)`, and these built-ins (their type
arguments are types: `T*`, `Array<u32>`, `u8`):

| built-in | true when |
|---|---|
| `is_given(T)` | an optional parameter was given |
| `is_value(T)` | a parameter given a value (not a type), or a constant's name |
| `is_integer(T)`, `is_signed(T)`, `is_unsigned(T)` | an integer (not `bool`), signed, unsigned |
| `is_float(T)` / `is_decimal(T)`, `is_numeric(T)`, `is_bool(T)` | `f32`; an integer or `f32`; `bool` |
| `is_pointer(T)`, `is_array(T)`, `is_struct(T)`, `is_sub(T)` | the kind of type (`is_sub`: a sub pointer type; `is_struct`: also classes, interfaces) |
| `is_class(T)`, `is_interface(T)`, `is_abstract(T)` | a class; an interface; an abstract class or an interface (only pointed at) |
| `is_base_of(B, D)` | `B` is `D`, a class `D` extends, or an interface it implements |
| `has_base(T)`, `has_parent(T, P)` | a class that extends a class (or a struct); `P` is up `T`'s chain of classes (not `T` itself; interfaces: `is_base_of`) |
| `has_common_base(T, U, P)`, `has_friend(T, F)` | `P` is up the chains of both `T` and `U`; `T` names the struct / class `F` as a friend |
| `is_scalar(T)`, `is_builtin(T)` | fits a register; `i8` ... `u32`, `f32`, `bool`, `void` |
| `is_type(T, U)`, `is_one_of(T, A, B, ...)` | the same type; one of them |
| `is_pointer_to(T, U)`, `is_convertible(T, U)` | `T` is a `U*`; a `T` converts to a `U` |
| `has_method(T, name)`, `has_field(T, name)` | a struct (or a pointer to one) with that method (or method template; a class: also inherited) / field |
| `has_method(T, name, sub(u32, f32) -> i32*)` | a method with that sub type (`this` not counted; one of its overloads; not a method template) |
| `has_field(T, name, u32)` | a field of that type |
| `has_priv_method`, `has_prot_method`, `has_pub_method` (and `_field`), `has_readonly_field` | as `has_method` / `has_field` (same arguments), for a `priv`, `prot`, public (neither: `readonly` fields too) or `readonly` member |
| `is_range(T)` | an array, or a struct with `begin()` and `end()` (a for-in works on it) |
| `size_of(T)` (= `sizeof(T)`), `array_count(T)` | numbers: the size; an array's element count (0 otherwise) |

(Adding a built-in is one entry in the table of `CodeGen.cpp`: `ConditionEval`.)

**if constexpr**: `if constexpr (condition) { ... } else { ... }` decides while compiling: only the
branch it chooses is compiled (the other one is not even checked: in a template it can use what
this instance doesn't have). The same conditions as `where`; it works outside templates too
(`if constexpr (sizeof(Point) == 8)`).
```
template(K, optional V) {
    sub kind() -> u32 {
        if constexpr (is_given(V)) {
            if constexpr (is_type(V, u8)) return 30;
            return 20;
        }
        return 10;
    }
}
```

### for-in loops
`for (auto x in range) body` runs the body for every element of the range, `x` being a copy of
it; `for (auto& x in range)` makes `x` a pointer to the element (to change it: `*x = ...`,
`x->field`). `for (var x: T in range)` / `for (var x: T* in range)` say the type.
```
for (auto i in 0..10) { }            // numbers: 0 to 9 (the end is excluded); i: i32
for (auto i in 0..count) { }         // i: count's type (u32 here); a..b: both evaluated once
for (auto v in values) { }           // an array (T[N]): v: T
for (auto& v in values) *v *= 2;     // v: T*
for (auto item in list) { }          // a struct that is a range (below), or a pointer to one
```
A struct is a range when it has these methods (a "cursor" `C` is any integer or pointer: an
index, a node pointer, ...; `T` is the element's type, which gives `auto` its type):
```
sub begin() -> C            // the cursor of the first element
sub end() -> C              // the cursor one past the last (compared with !=)
sub next(var c: C) -> C     // the cursor after c
sub get(var c: C) -> T*     // the element at c
```
```
struct IntList {
    var items: i32[16];
    var count: u32;
    inline sub begin() -> u32 { return 0; }
    inline sub end() -> u32 { return this->count; }
    inline sub next(var i: u32) -> u32 { return i + 1; }
    inline sub get(var i: u32) -> i32* { return &this->items[i]; }
};
struct Chain {                       // a linked list: node pointers, null at the end
    var head: Node*;
    inline sub begin() -> Node* { return this->head; }
    inline sub end() -> Node* { return null; }
    inline sub next(var n: Node*) -> Node* { return n->next; }
    inline sub get(var n: Node*) -> Item* { return &n->item; }
};
```
- A cursor that points at the elements (`begin()` / `end()` return `T*`) needs no `next` / `get`:
  they are `c + 1` and `c`.
- The loop is a `for` loop over the cursor (`break` / `continue` work as usual): the range is
  evaluated once, and so is `end()`. With `inline` methods it costs about what a hand written
  loop does.
- The range is a variable (or a field, or a pointer to the struct); `in` is not a keyword (it
  means something only there).
- `begin` / `end` / `next` / `get` can be `priv`: a for-in may still use them, nothing else can.
  Its cursors only come from `begin()` and `next()`, so a `get()` without bound checks is safe
  there without being callable with any index.

### defer
`defer <statement or block>` runs that code when the block it is in is left, on every way out:
the end of the block, `return`, `break` and `continue` (each runs the defers of the blocks it
leaves). Several defers run in the reverse order. As in Zig:
```
sub copy(var path: string) -> i32 {
    var f: File* = open(path);
    defer close(f);                 // runs after the return value is computed
    if (f->size == 0) {
        return 0;                   // close(f) runs here
    }
    for (var i: u32 = 0; i < f->size; i++) {
        var b: u8* = alloc(16);
        defer release(b);           // at the end of every iteration (continue / break too)
        ...
    }
    return 1;                       // and here
}
```
- `return x` computes `x` first, then the defers run, then the sub returns.
- A defer sees the variables declared before it (as they are when it runs). `return`,
  `break` and `continue` cannot jump out of a defer, and a defer cannot be declared alone as
  the body of `if` / `while` / ...: it needs a block (`if (c) { defer ...; }`).
- The code is generated at every exit (like an inline sub), so a long defer in a sub with many
  `return`s makes it bigger. Jumping out of a low block skips the defers.

### atomic
`atomic { ... }` runs the block with interrupts disabled, and puts them back as they were on
every way out of it (the end, `return`, `break`, `continue`): it saves the `InterruptsEnabled`
flag, clears it, and restores the saved value (not just enables it), so atomic blocks can be
nested and used where interrupts are already disabled. `atomic sub` (also `atomic interrupt
sub`, and `atomic` methods) makes the whole body an atomic block.
```
var g_ticks: u32;                   // also written by an interrupt sub
atomic sub take_ticks() -> u32 {
    var t: u32 = g_ticks;           // no interrupt between the read and the write
    g_ticks = 0;
    return t;
}
```
- The CPU ignores every interrupt while they are disabled, `int` too: a `raise_interrupt`
  (`print`, `malloc`, ...) does nothing in an atomic block. An `int` written in a low block of
  an atomic block is an error; the compiler cannot see the ones in the subs it calls.
- Defers written inside the block run before interrupts are put back.
- Not in inline subs (`atomic` uses a low block).

### Expressions
C operators and precedence: `= += -= *= /= %= &= |= ^= <<= >>=`, `?:`, `|| &&` (also `or`
`and`, short-circuit), `| ^ &`, `== != < <= > >=`, `<< >>`, `+ -`, `* / %`, unary `- ! ~ * &`
(`not` = `!`), `++ --` (prefix and postfix), calls, `a[i]`, `s.field`, `p->field`.

- `cast<T>(value)`: conversions between integers, `f32`, pointers and sub pointers (not between
  `f32` and pointers). `cast<T*>(constant)`
  is a constant address (it can be a field's default value).
- `bitcast<T>(value)`: the same bits read as a `T` (nothing is converted): `bitcast<f32>(0x3F800000)`
  is `1.0`, `bitcast<u32>(1.0)` is `0x3F800000` (what `*cast<f32*>(&bits)` does, for any value, not
  only a variable). Any two types of the same size (an error otherwise): integers (`bitcast<u16>(-2)`
  is `0xFFFE`), `f32`, pointers, sub pointers, and structs and arrays as values (`bitcast<u32>(pixel)`,
  `bitcast<Pixel>(0xAABBCCDDu).r`; big endian: the first byte is the top one); not to an array (not
  a value) or a `bool`. Constants give constants, and it works in constexpr subs.
- `static_cast<T>(value)`: a conversion that happens anyway, numbers to numbers, and a class
  pointer to a pointer to a class derived from it (see "Classes").
- `sizeof(type or value)`, `sizeof<type>`: a `u32` constant.
- Literals: `42`, `0x2A`, `0b101010`, `1_000` (underscores), `1.5` / `1e-3` / `2f` (`f32`), `'a'` (a `u8`), `"text"`
  (escapes `\n \t \r \0 \\ \' \" \xNN`, adjacent strings are joined), `true`, `false`, `null`.
- Integer arithmetic is 32-bit: smaller types are widened (`i8`, `i16` sign-extended), the result
  is `u32` if an operand is `u32`, `i32` otherwise. Values are truncated when stored.
  Division, `%`, `>>` and comparisons are unsigned on `u32` (and pointers), signed otherwise.
- Pointer arithmetic: `p + n` / `p - n` move by `n` elements, `p - q` counts elements, `p[i]` is
  `*(p + i)`.
- Implicit conversions: between integer types, integers to `f32`, `null` / `0` to pointers, arrays to pointers,
  `void*` to and from pointers, a class pointer to a pointer to a class it extends (or an interface it
  implements). Everything else needs `cast` (or `static_cast`).
- Structs can be assigned (copied) and initialized from another struct; they are passed to and
  returned from subs through pointers.

### Initializers
```
var a: i32[5] = { 1, 2, 3 };            // the rest is 0
var p: Point = { 10, 20 };  var q: Point = { 5 };     // q.y is 7 (its default)
var buf: u8[16] = "text";
var r: Rect = { { 1, 2 }, { 5, 6 } };
```
Globals with constant values are stored in the program, string literal pointers too (as the
literal's address); the others (addresses of variables and subs, non-constant expressions) are
set by `__ds_init` before `main` runs.

### Overloading
The sub whose parameters fit the arguments best is called: an exact type is best, then
widening with the same signedness (`u16` -> `u32`), then other integer conversions; a tie is an
error ("ambiguous call", use a cast). A sub name used as a sub pointer picks the overload by
the pointer's type (`var f: sub(u8) -> i32 = twice;`).

### low blocks
Raw dasm, copied line by line, except the `$name`s of DScript things:

| In a low block | Means |
|---|---|
| `$var` | the variable's value, loaded into S1 or S2 before the line; if it is the first operand (the destination), it is stored back after the line |
| `&$var` | the variable's address |
| `[$var]` or `*$var` | the memory at the variable's value (a pointer) |
| `$sub` | the sub's address (its label); overloaded subs: take a sub pointer in DScript first |
| `$CONST`, `$Enum.Member` | the value |
| other `$name`s | left alone (labels and data of the low code) |

At most two DScript variables per line. Arrays and structs give their address. The low code can
use every register (but S1 / S2 hold the `$var`s, and `arg` changes PP, which the parameters
need); `ret` (or `rti`) returns from the sub, with the value in RV. With `-O`, the registers the
low code names (`R1`..`R20`) are never DScript variables in that sub; name every register it
changes (a register changed by code it jumps to, without `call`, must be named too).
```
sub print(var text: string) -> void {
    low {
        mov R9, $text
        mov R10, 0x0C
        int 0x30
    }
}
```

### Preprocessor
```
#include("file.ds")        next to the file, then in the -I directories (the quotes are optional)
#import("name")            a compiled library (name.dlib): next to the file, then in the -L
                           directories (see "Libraries")
#import("name", dynamic)   the same, linked dynamically (or static; see "Shared libraries")
#include(<file.ds>)        the old forms, with <>: still accepted, they search only the -I / -L
#import(<name>)            directories
#define(NAME = tokens)     #define(NAME)     #undef(NAME)
#define(NAME(a, b) = tokens)                 with parameters
#if(expr) #ifdef(NAME) #ifndef(NAME) #elif(expr) #else #endif
#multiple                  this file can be included many times (the first thing in the file)
#once                      accepted, does nothing: files are included once by default
#error(message)  #warning(message)
```
Defined names are replaced (as tokens) everywhere. `#if` expressions use integers, the C
operators, `and` / `or` / `not` and `defined(NAME)`. Predefined: `__DSCRIPT__`, and `__RAW__`
with `--raw`; more with `-D NAME=value`.

A define with parameters is replaced only where its name is followed by `(`; elsewhere (`var
f = NAME;`) the name stays as it is:
```
#define(MAX(a, b) = ((a) > (b) ? (a) : (b)))
#define(ZERO() = 0)
var m: i32 = MAX(x, y + 1);
```
- The arguments are split on the commas outside `()`, `[]` and `{}`; a call can span lines,
  but not a directive. The count must match (`MAX expects 2 arguments, got 3`).
- The arguments are expanded first, then put in place of the parameters; the result is
  scanned again for more defines. A define is never replaced inside its own expansion, so
  `#define(log(x) = log(x, 0))` calls the sub `log`.
- Parenthesize the parameters in the tokens, as in C: they are put in as tokens, not values.
- Errors in a define's tokens point at the use; dsls' hover on a use shows the define and what
  that use expands to. No stringizing (`#a`) or pasting (`a ## b`).


## Generated code

- **Calling convention** (the machine's own `call`): the arguments are pushed left to right as
  32-bit slots, then their count; `call` saves R1..R20, ACC, PP, IP and FP (the CPU's stack
  frame) and `ret` restores them. Parameter `i` is at `PP - 4 * i` (values smaller than 4 bytes
  are in the slot's last bytes). The return value is in RV. A variadic sub's extra arguments
  follow (`...typed`: then their type codes, in the same order), and the count is all the slots
  pushed; the callee reads it in the CPU's frame (`FP + 104`, the word after the 26 of the frame).
- **Locals** are at `FP + offset` (offset <= 0): the sub moves SP below them first.
- **Registers**: R1..R16 hold the temporaries of an expression (as a stack; an expression can
  need 16 at most), R17..R20 are scratch, S1 / S2 hold the `$var`s of low blocks.
- **`-O`** (off by default, `-O0`): each sub is generated twice. The first pass finds the scalar
  locals and parameters (integers, `bool`, `f32`, pointers, enums) whose address is never taken
  (`&v`, also `&$v` in a low block), how often each is used (a use in a loop counts 8 times
  more, per loop depth) and where each is live (its block); the second keeps the most used in
  the registers above the temporaries the sub needs (up to R16), variables whose blocks don't
  overlap sharing one. Parameters are loaded into theirs at the start (only those used more
  than once). Registers a low block in the sub names (`R5`, also through an inline sub) are
  never used for variables, and a low block using `reg(...)` keeps them all in memory. Then a
  peephole pass removes copies through registers: a value computed straight into the variable
  that receives it, a register read instead of its copy, copies nothing reads (it follows the
  sub's jumps; low code is never changed). `call` keeps R1..R20, so the variables survive
  calls; hardware interrupts keep them too. In the debugger, a register variable is in its
  register (its memory slot isn't used).
- Needs the `extmov` and `extalu` CPU extensions, and `extfpu` for programs using `f32`
  (`cpuext = extmov, extalu, extfpu` in the machine config).
- Layout: `jmp __ds_start`, the data (globals, string literals), `__ds_start` (calls `__ds_init`,
  then `main`; then `ret` in a DRX, `hlt` in a raw binary), the top-level low blocks, the subs,
  and the runtime helpers used (`__ds_shru`: the `>>` of a `u32` by a variable).


## DRX: relocatable programs

The default output. The program is assembled at address 0; the relocation table lists the
offsets of every 32-bit address in it (dsc finds them by assembling twice, at two addresses).

```
0x00  magic 'DRX1'   0x04  header size (32)   0x08  image size    0x0C  entry offset
0x10  reloc count    0x14  reloc table offset 0x18  id (64 bits, high word first; 0: none)
image (the program at address 0), then the relocation table (u32 offsets in the image)
```
The id identifies the build (FNV-1a of the entry, the image and the relocations): the
disassembly saved with it (`--save-disassembly`) has it too, so the debugger finds the right one
for a program wherever it is loaded (see "Source-level debugging").
To load it at `base`: copy the image, add `base` to the 32-bit word at every relocation offset,
then call `base + entry` (see "Starting a program"). The program returns when `main` returns
(its value in RV).
`src/tools/DRX.hpp` reads, writes and relocates DRX files; `dvm` / `ddb --force-load file.drx
<ram offset>` and `drun` load them.

`--raw` makes a bare metal binary instead (like dasm): fixed address (`--load`, default
0x01010000), optional `--fixed` and `--header`, `hlt` after `main`. `--rom`: the same, for a
program in ROM (see "ROM programs").


## Libraries

A library is compiled on its own, once, into a `.dlib`; programs (and other libraries) import
it and dsc links it into them. There are no header files: the compiler writes the library's
declarations into the `.dlib` itself.
```
dsc gfx.ds --lib -o build/gfx.dlib           // a library: no main
dsc src/*.ds --lib -o build/gfx.dlib         // several files, compiled as one, in order
dsc game.ds -L build -o game.drx             // #import("gfx") in game.ds
```
```
#import("gfx")          // gfx.dlib: next to this file first, then the -L directories
#import("libs/gfx")     // a path works too (.dlib is added)

sub main() -> i32 { return Gfx::width(); }
```
- `#import` makes the library's declarations visible there (once, however many times it is
  imported), as if they were written in the program: names, namespaces, structs, methods,
  overloads, default values, `priv`, constants and enums all work the same. The program's own
  `#define`s don't apply to them. The library's code is linked into the program after its own
  (the output is still one DRX or raw binary: nothing changes for loaders, the BIOS or drun).
- The library's globals are its own (in its data); the ones that need code to be initialized
  (and their constructors) are set up before the program's: every imported library's
  initialization runs first, the libraries a library imports before it.
- `inline` subs are generated at every call, so their bodies are in the `.dlib` (like templates
  in a C++ header) and compiled into the program. A `static var` in an inline sub of a library
  is the program's own copy, not the library's.
- A library can import other libraries: the `.dlib` remembers its `#import`s, as written, and
  finds them again from where the `.dlib` is, then in the -L directories.
- The `.dlib` holds the library's code and data (assembled at 0), its relocations, the
  addresses of other libraries' symbols it uses (imports), its symbols (exports) and its
  declarations (DScript source, with where each one is in the library's sources: errors,
  hover and go to definition in dsls point there). `src/tools/DLIB.hpp` has the format.
- Linking: the program is assembled with the library symbols it uses as made-up addresses
  (assembled again with them moved: the words that change are where they are used), then
  the libraries are put after it (16-byte aligned) and those words are set (and become
  relocations in a DRX).
- dsls finds the libraries with `-L` in `dsls.json` (`"args": ["-L", "build"]`).
- The debugger sees the libraries' code like the program's: a `.dlib` keeps its disassembly
  (with its source lines and files), and the program's (`--save-disassembly`,
  `-Wa,--save-disassembly`) gets it at the address the library is linked at, with the
  addresses in its instructions as linked (and the symbols of other libraries by name).
- Only what the program uses is linked: a `.dlib` records where its image can be cut (each sub,
  global, string and constant array is a piece), and dsc keeps the pieces reachable from what the
  program uses (its calls and addresses of the library's things, every library's initialization,
  then what those pieces call or point to, in the same library or another one). Every address in
  a library is a relocation, so the kept pieces are moved together and their addresses fixed.
  wing-shell: 1.5 KB instead of 32 KB (145 bytes of libcore's 26 KB). `--whole-libraries` links
  them whole, as before. A sub right after a top-level low block stays with the code before it
  (the low code could run into it). A raw program with libraries is padded (`--fixed`) after
  linking.

### Shared libraries (dynamic linking)
A library can also be compiled as a **shared library**: `dsc --lib --shared` writes both the
`.dlib` and a `.drl`. A program then chooses, for each library, to link it statically (copied in,
as above) or dynamically (only recorded: its loader loads the `.drl` once for every process that
uses it; the library's writable globals stay per process). drun loads them
(`drun ... prog.drx -L <dir>`); everything else (choosing static / dynamic, the formats, what a
loader does, the SDK readers `dr::dyn`, the plan for the DragonOS kernel's loader) is in
[dynamic-linking.md](dynamic-linking.md).
```
dsc --lib --shared mylib.ds -o lib/mylib.dlib    // lib/mylib.dlib and lib/mylib.drl
dsc prog.ds -L lib --dynamic -o prog.drx         // every library dynamic: a DRX2 (659 bytes instead of 32 KB for wing-shell)
#import("mylib", dynamic)                        // or one by one, in the source (also --dynamic=mylib, --static=...)
```

DragonVM's own libraries (`extra/ds/<name>/src`, built by `ds_env` into `<name>.dlib`, copied to
`extra/ds/<name>/lib`):
- `libcore`: freestanding utilities, in `std`: `itoa` / `utoa` / `ftoa`, `atou` / `atoi` / `atof`,
  `format_size`, `BitField32` / `16`, `memcpy` / `memset` / `memcmp`, strings, `VaList` and
  `format`, `U64` / `I64`, `std::math`, `RingBuffer`, the calendar (`std::time::DateTime`),
  `std::data::ASCII` and `KeyCodes`. No hardware, no system calls, no `__assert_fail`: any program
  can link it.
- `dragon-sdk`: the hardware and the BIOS services, in `dr`, for programs in supervisor mode (the
  BIOS, Drake, the kernels). Imports libcore.
- `stdlib`: the runtime of DragonOS programs (user mode): `std::syscall`, `std::rand`, the clock
  (`std::time::millis`, ...), `__assert_fail`. Imports libcore.


## Starting a program
The loader calls the entry with the program's arguments, or with none:
```
push argc        // the number of arguments (argv[0] is the program itself, by convention)
push argv        // string*: argc strings, then null
push 2
call base + entry
```
or `push 0; call base + entry` (no arguments). `__ds_start` runs the global initializers and
constructors (`__ds_init`), then calls `main`; with `main(argc, argv)`, it passes what it was
called with, or `0, null` without arguments. A raw binary (`--raw`, `--rom`) is started by a
jump: its `main(argc, argv)` always gets `0, null`. `main`'s value is returned in RV.
- `drun` passes its program's path as `argv[0]`, then the arguments after `--`
  (`drun machine.dvm prog.drx -- a b`: argc = 3); the strings and the argv array are below
  the stack. The BIOS starts its boot code (raw) without arguments.
- This is the convention a DragonOS loader will follow.
- The program runs in supervisor mode, as the BIOS, the boot loader and a kernel do; `drun
  --user` runs it in user mode (its `hlt`, privileged there, then ends the run).

## Tests

`tests/dscript/run_tests.sh` compiles every test (as a DRX and as a raw binary), runs it with
`drun` and checks its return value (`// expect-rv:`), its output (`// expect-output:`, the BIOS
runs first) or the compilation error (`// expect-error:`). `// expect-rom-rv:` compiles the
test with `--rom` as the machine's BIOS, boots it and checks `main`'s return value;
`// dsc-flags:` adds dsc options. The runner builds libcore, the SDK and the stdlib first. `DSCRIPT_TEST_CONFIG=<file.dvm>` runs the tests on another
machine config (e.g. one with a known-good BIOS); `DSCRIPT_TEST_DSC_FLAGS=<options>` adds dsc
options to every compilation, the libraries' too (`DSCRIPT_TEST_DSC_FLAGS=-O`: the whole suite
optimized).


## Limitations (for now)

- One source file (plus includes) per program (a library: several, compiled as one).
- `#define` has no stringizing or token pasting.
- No `switch`, `goto`, unions, 64-bit floats.
- Arrays are passed by pointer (structs by value).
- Lambdas do not capture the variables of the sub around them.
- Uninitialized global arrays are stored in the program (zeroes).
- The assembler replaces data symbols line by line: very big programs assemble slowly.


## Towards self-hosting

The compiler is written in a deliberately plain style (structs, vectors, strings, recursive
descent, no templates or other C++ magic of its own), so it maps onto DScript. What is missing
before it can run on the machine:

1. **A heap**: `alloc` / `free` (in the SDK, or the OS) for the AST, the token lists, the strings.
2. **Containers in the SDK**: a growable array, a string builder and a map (the compiler uses
   `std::vector`, `std::string`, `std::map`).
3. **Files**: reading the sources and writing the output through the OS (the disk driver and a
   file system on a partition).
4. **The assembler**: port it too, or let the DScript compiler emit machine code directly
   (the instruction encodings are in `src/tools/GlobalData.*` and `src/hardware/CPUExtensions.*`). The FPU: `other/info/fpu.md`.
5. **Language features that would help**: `switch`, unions, and a linker / object format
   (DRX could gain a symbol table) to compile big programs in pieces.

Porting order: lexer -> preprocessor -> parser -> code generator; each piece can be tested
against the C++ compiler's output (same dasm for the same input).
