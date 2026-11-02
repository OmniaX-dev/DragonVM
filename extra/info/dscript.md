# DScript

A C-like language for the Dragon machine. `dsc` compiles it to dasm and assembles it (the
assembler is linked in), producing a relocatable **DRX** program (the default) or a bare metal
binary (`--raw`).

```
dsc program.ds -I extra/ds/sdk                    -> program.drx (relocatable)
dsc kernel.ds --raw --load 0x01012000 --fixed 1024,0xFF --header KERNEL0_BOOT
dsc program.ds --emit-dasm program.dasm           (also writes the generated dasm)
drun config/testMachine.dvm program.drx --boot    (runs it without a window)
```

Pipeline: lexer -> preprocessor (on tokens) -> parser (AST) -> checking + code generation
(dasm text) -> assembler -> DRX / raw binary. Sources: `src/compiler/`.


## Language

### Types
| Type | Size | Notes |
|---|---|---|
| `i8` `u8` `i16` `u16` `i32` `u32` | 1, 2, 4 | `char` = `u8`, `int` = `i32`, `uint` = `u32` |
| `bool` | 1 | `true` / `false` |
| `T*` | 4 | pointers; `void*` converts to and from any pointer |
| `string` | 4 | the same as `u8*` (null-terminated) |
| `T[N]` | N * size | arrays; they decay to `T*` |
| `struct Name` | sum of the fields | packed (no padding) |
| `sub(T1, T2) -> R` | 4 | sub pointers; `(sub(i32) -> i32)[4]` for an array of them |
| enums | 4 | their values are `i32` constants: `Color.Red` |

There is no floating point (the machine has no FPU).

### Declarations (top level)
```
var name: type = value;                 // global variable (the type can be inferred: var n = 5;)
const NAME = 5;  const MASK: u32 = 0xFF;    // integer constants
enum Color { Red, Green = 5, Blue }     // Color.Blue == 6
alias Handler = sub(i32) -> void;       // a name for a type
struct Point { var x: i32; var y: i32 = 7; }    // fields can have constant default values
sub name(var a: i32, b: u8 = 0) -> i32 { ... }  // 'var' is optional; default values are constants
sub name(var a: string) -> void { ... } // overloads: same name, other parameter types
sub later(var x: i32) -> i32;           // a declaration (the definition can come later)
interrupt sub handler() { ... }         // ends with rti; no parameters, returns void
low { ... }                             // raw dasm (e.g. helper routines with labels)
```
Everything can be used before it is declared. `main` (no parameters) is the program's entry.

### Statements
`var` declarations (locals: scalars are not initialized unless given a value; structs are zeroed
and get their default values), `if` / `else`, `while`, `do ... while (...);`,
`for (init; condition; step)`, `break`, `continue`, `return`, blocks, expression statements,
`low { ... }`.

### Expressions
C operators and precedence: `= += -= *= /= %= &= |= ^= <<= >>=`, `?:`, `|| &&` (also `or`
`and`, short-circuit), `| ^ &`, `== != < <= > >=`, `<< >>`, `+ -`, `* / %`, unary `- ! ~ * &`
(`not` = `!`), `++ --` (prefix and postfix), calls, `a[i]`, `s.field`, `p->field`.

- `cast<T>(value)`: conversions between integers, pointers and sub pointers.
- `sizeof(type or value)`, `sizeof<type>`: a `u32` constant.
- Literals: `42`, `0x2A`, `0b101010`, `1_000` (underscores), `'a'` (a `u8`), `"text"`
  (escapes `\n \t \r \0 \\ \' \" \xNN`, adjacent strings are joined), `true`, `false`, `null`.
- Integer arithmetic is 32-bit: smaller types are widened (`i8`, `i16` sign-extended), the result
  is `u32` if an operand is `u32`, `i32` otherwise. Values are truncated when stored.
  Division, `%`, `>>` and comparisons are unsigned on `u32` (and pointers), signed otherwise.
- Pointer arithmetic: `p + n` / `p - n` move by `n` elements, `p - q` counts elements, `p[i]` is
  `*(p + i)`.
- Implicit conversions: between integer types, `null` / `0` to pointers, arrays to pointers,
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
Globals with constant values are stored in the program; the others (string pointers,
addresses, non-constant expressions) are set by `__ds_init` before `main` runs.

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
need); `ret` (or `rti`) returns from the sub, with the value in RV.
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
#include(file.ds)          relative to the file, then the -I directories
#include(<file.ds>)        the -I directories only
#define(NAME = tokens)     #define(NAME)     #undef(NAME)
#if(expr) #ifdef(NAME) #ifndef(NAME) #elif(expr) #else #endif
#once                      this file is included once
#error(message)  #warning(message)
```
Defined names are replaced (as tokens) everywhere. `#if` expressions use integers, the C
operators, `and` / `or` / `not` and `defined(NAME)`. Predefined: `__DSCRIPT__`, and `__RAW__`
with `--raw`; more with `-D NAME=value`.


## Generated code

- **Calling convention** (the machine's own `call`): the arguments are pushed left to right as
  32-bit slots, then their count; `call` saves R1..R20, ACC, PP, IP and FP (the CPU's stack
  frame) and `ret` restores them. Parameter `i` is at `PP - 4 * i` (values smaller than 4 bytes
  are in the slot's last bytes). The return value is in RV.
- **Locals** are at `FP + offset` (offset <= 0): the sub moves SP below them first.
- **Registers**: R1..R16 hold the temporaries of an expression (as a stack; an expression can
  need 16 at most), R17..R20 are scratch, S1 / S2 hold the `$var`s of low blocks.
- Needs the `extmov` and `extalu` CPU extensions (`cpuext = extmov, extalu` in the machine config).
- Layout: `jmp __ds_start`, the data (globals, string literals), `__ds_start` (calls `__ds_init`,
  then `main`; then `ret` in a DRX, `hlt` in a raw binary), the top-level low blocks, the subs,
  and the runtime helpers used (`__ds_shru`: the `>>` of a `u32` by a variable).


## DRX: relocatable programs

The default output. The program is assembled at address 0; the relocation table lists the
offsets of every 32-bit address in it (dsc finds them by assembling twice, at two addresses).

```
0x00  magic 'DRX1'   0x04  header size (32)   0x08  image size    0x0C  entry offset
0x10  reloc count    0x14  reloc table offset 0x18  flags (0)     0x1C  reserved
image (the program at address 0), then the relocation table (u32 offsets in the image)
```
To load it at `base`: copy the image, add `base` to the 32-bit word at every relocation offset,
then `push 0; call base + entry`. The program returns when `main` returns (its value in RV).
`src/tools/DRX.hpp` reads, writes and relocates DRX files; `dvm` / `ddb --force-load file.drx
<ram offset>` and `drun` load them.

`--raw` makes a bare metal binary instead (like dasm): fixed address (`--load`, default
0x01010000), optional `--fixed` and `--header`, `hlt` after `main`.


## Tests

`tests/dscript/run_tests.sh` compiles every test (as a DRX and as a raw binary), runs it with
`drun` and checks its return value (`// expect-rv:`), its output (`// expect-output:`, the BIOS
runs first) or the compilation error (`// expect-error:`).


## Limitations (for now)

- One source file (plus includes) per program: no separate compilation / linking.
- `#define` has no parameters.
- No `switch`, `goto`, unions, floating point, variadic subs.
- Structs are passed and returned through pointers.
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
   (the instruction encodings are in `src/tools/GlobalData.*` and `src/hardware/CPUExtensions.*`).
5. **Language features that would help**: `switch`, unions, and a linker / object format
   (DRX could gain a symbol table) to compile big programs in pieces.

Porting order: lexer -> preprocessor -> parser -> code generator; each piece can be tested
against the C++ compiler's output (same dasm for the same input).
