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
change the struct and return it, so they chain:
```
var a: std::U64(5);                     // constructors: (), (u32), (i32), (hi, lo), (U64*), (I64*)
a.mul(3)->add(&b)->shl(4);              // a = ((a * 3) + b) << 4
if (a.gt(1000)) ...                     // eq ne lt le gt ge (bool), cmp (-1 / 0 / 1)
var text: u8[24]; a.str(text);           // str(buffer, base = 10), parse(text, base = 10) -> bool
console.print(&a);                      // TextSingle / Text16 / Text16Console print them
```
- Every operation takes a `u32`, an `i32` or a `U64*` (`I64*` for `I64`); the smaller types use
  the `u32` / `i32` one (zero / sign extended), and so does an integer literal (`i32`).
- Arithmetic: `add`, `subt` (`sub` is a keyword), `mul`, `div`, `mod`, `divmod(d, &rem)`, `neg`,
  `inc`, `dec`, `min`, `max`; `I64` also `abs`. Signed division truncates toward zero, the
  remainder has the dividend's sign (as for `i32`); division by zero is the CPU's.
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
                                        // (all constants can also be declared in a sub, see below)
enum Color { Red, Green = 5, Blue }     // Color.Blue == 6
enum Kind: u8 { Cmd, Data = 0x80 }      // a typed enum: 1 byte (in structs, arrays, sizeof); the
                                        // values must fit the type (an error otherwise)
alias Handler = sub(i32) -> void;       // a name for a type
struct Point { var x: i32; var y: i32 = 7; }    // fields can have constant default values
struct Box { var w: u32; sub area() -> u32 { return this->w * this->w; } }   // methods (see below)
struct Snake { readonly var length: u32 = 3; }  // readonly: only its subs change it (see below)
sub name(var a: i32, b: u8 = 0) -> i32 { ... }  // 'var' is optional; default values are constants
sub name(var a: string) -> void { ... } // overloads: same name, other parameter types
sub later(var x: i32) -> i32;           // a declaration (the definition can come later)
interrupt sub handler() { ... }         // ends with rti; no parameters, returns void
inline sub sq(var x: i32) -> i32 { ... }    // the body is generated at every call (see below)
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
array) visible only there; `static var` is a variable that lives for the whole program, like a
global, but is only visible there:
```
sub size_text(var bytes: u32, var buff: u8*) -> string {
    const units: string[] = { "B", "KiB", "MiB", "GiB" };     // stored once, in the data
    static var calls: u32 = 0;                                // keeps its value between calls
    calls++;
    ...
}
```
- They are stored exactly like the top-level ones (a hidden declaration named `<sub>::<name>`,
  which is also what error messages show), so local constants are safe in ROM.
- A static variable is set once, before `main` (like a global, in the order of the source),
  not at its first use; its value can be any expression that does not use the sub's locals or
  parameters (it can use constants, globals and other subs).
- `static const` is the same as `const`. `static` is an error at the top level.
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
- For dynamic dispatch, use sub pointer fields; their default value can be a sub:
  `var area: sub(Shape*) -> u32 = square_area;` (called as `s->area(s)`).
- `priv` before a field or a sub (`priv var balance: i32;`, `priv sub add(...)`, `priv static
  sub ...`) makes it private: only the struct's own subs (methods and static subs, the lambdas
  written in them) can use it, on any instance of the struct. Everything else is public. Outside
  the struct, `{ ... }` can only set the fields before the first private one (the private ones
  get their default values), and the private fields of struct constants are read only inside.
- `readonly` before a field (`readonly var length: u32 = 3;`) lets everyone read it, but only the
  struct's own subs (as for `priv`) change it: outside, assigning it, `++` / `--`, changing a
  field or element in it (`s.head.x = 1`, `s.cells[0] = 1`) and taking its address (`&s.length`)
  are errors. Initializing it (its default value, `{ ... }`) and copying a whole struct
  (`a = b`) are allowed. It protects the field itself: a pointer field's target can still be
  changed, a method called on a struct field can change it, and an array field still converts
  to a pointer (`print(s.name)`). Only named structs have readonly fields; a field is either
  `priv` or `readonly`.
- `static sub` in a struct has no `this`: it is a sub grouped under the struct's name, called as
  `Struct.name(...)` or `Struct::name(...)` (also from the struct's other subs: a struct's subs
  are not found by their name alone). A struct can have only static subs (and no fields).
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
  cannot also have a value.
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
- Every value must be a constant: no variables, no sub calls.
- A struct constant has no address: only its integer and pointer fields can be used.
  `var c = MemoryLayout;`, `&MemoryLayout` or passing it to a sub is an error, and so is
  assigning to its fields. `sizeof(MemoryLayout)` works.

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
- The values must be constants, strings, `null` or `cast<T*>(constant)` (the elements can be
  integers, pointers, arrays and structs of them); everything is in the program's data, so it
  is safe in ROM.
- Assigning to an element (or `++` / `--`) is an error. Its address can be taken (`&Primes[2]`,
  or passing it to a sub): the compiler cannot stop writes through such a pointer (there are
  no read-only pointer types), so in ROM they fail at run time.
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
`for (init; condition; step)`, `for (auto x in range)`, `break`, `continue`, `return`, blocks,
expression statements, `low { ... }`, `assert(...)`, `debug_print(...)`, `defer ...`,
`atomic { ... }`.

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
- `sizeof(type or value)`, `sizeof<type>`: a `u32` constant.
- Literals: `42`, `0x2A`, `0b101010`, `1_000` (underscores), `1.5` / `1e-3` / `2f` (`f32`), `'a'` (a `u8`), `"text"`
  (escapes `\n \t \r \0 \\ \' \" \xNN`, adjacent strings are joined), `true`, `false`, `null`.
- Integer arithmetic is 32-bit: smaller types are widened (`i8`, `i16` sign-extended), the result
  is `u32` if an operand is `u32`, `i32` otherwise. Values are truncated when stored.
  Division, `%`, `>>` and comparisons are unsigned on `u32` (and pointers), signed otherwise.
- Pointer arithmetic: `p + n` / `p - n` move by `n` elements, `p - q` counts elements, `p[i]` is
  `*(p + i)`.
- Implicit conversions: between integer types, integers to `f32`, `null` / `0` to pointers, arrays to pointers,
  `void*` to and from pointers. Everything else needs `cast`.
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

- One source file (plus includes) per program or library; libraries are linked statically
  (into the program, by dsc), not loaded at run time.
- `#define` has no stringizing or token pasting.
- No `switch`, `goto`, unions, 64-bit floats, variadic subs.
- Structs are passed and returned through pointers.
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
