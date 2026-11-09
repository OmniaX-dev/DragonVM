# DScript

A C-like language for the Dragon machine. `dsc` compiles it to dasm and assembles it (the
assembler is linked in), producing a relocatable **DRX** program (the default) or a bare metal
binary (`--raw`).

```
dsc program.ds -L extra/ds/dragon-sdk/lib         -> program.drx (relocatable)
dsc kernel.ds --raw --load 0x01012000 --fixed 1024,0xFF --header KERNEL0_BOOT
dsc program.ds --emit-dasm program.dasm           (also writes the generated dasm)
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
dtools print-disassembly -f disassembly/<file>.dds        (the instructions under their lines)
```
In ddb, each source line (highlighted) is shown above its instructions, which are indented under
it; inlined subs' line numbers are green, each file starts with a header, and the line of the
current instruction is highlighted. A source line's margin and menu act on its
first instruction (breakpoints, run to). Commands (and toolbar buttons): `sl` / `step-line` (F7: to the next line,
into called subs), `nl` / `next-line` (F8: calls, interrupts and inlined subs run whole),
`break <file>:<line>` (at the line, or the next one with code; every copy of an inlined line),
`where`, `list [file:line]`. Where the program has no source lines, they step one instruction.

Pipeline: lexer -> preprocessor (on tokens) -> parser (AST) -> checking + code generation
(dasm text) -> assembler -> DRX / raw binary. Sources: `src/compiler/`.

Compiled libraries: `dsc lib.ds --lib` makes a `.dlib`, that programs `#import(<lib>)` (see
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
| `bool` | 1 | `true` / `false` |
| `T*` | 4 | pointers; `void*` converts to and from any pointer |
| `string` | 4 | the same as `u8*` (null-terminated) |
| `T[N]` | N * size | arrays; they decay to `T*` |
| `T[]` | the value's | in a declaration with a value: `{ 1, 2, 3 }` -> 3, `"text"` -> 5 (with the 0) |
| `struct Name` | sum of the fields | packed (no padding) |
| `struct { fields }` | sum of the fields | a nameless struct, usable wherever a type goes |
| `sub(T1, T2) -> R` | 4 | sub pointers; `(sub(i32) -> i32)[4]` for an array of them |
| enums | 4, or the size of their type | their values are constants of the enum's type (`i32` by default): `Color.Red`; `enum Kind: u8 { ... }` gives them a type (i8, i16, i32, u8, u16, u32) |

There is no floating point (the machine has no FPU).

### Declarations (top level)
```
var name: type = value;                 // global variable (the type can be inferred: var n = 5;)
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
#include(<assert.ds>)
assert(p != null);
assert(count < MAX, "too many entries");    // an optional message (a string)
```
When the condition is false, the program calls `__assert_fail(file, line, condition, message)`
(the condition as written, the message or ""). The SDK's (`assert.ds`) prints them to the
host's terminal with `debug_print` (no VGA needed), then stops the machine (`hlt`; with `dsc -g`,
ddb stops first at a `debug_break`). A program (an OS, for example) can write its own
`__assert_fail` instead, to show it on the screen. With `NDEBUG` defined
(`dsc -D NDEBUG` or `#define(NDEBUG)`), asserts are only checked by the compiler: no code, the
conditions are not run (so they should not have side effects that matter).

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
`for (init; condition; step)`, `break`, `continue`, `return`, blocks, expression statements,
`low { ... }`, `assert(...)`, `debug_print(...)`, `defer ...`.

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

### Expressions
C operators and precedence: `= += -= *= /= %= &= |= ^= <<= >>=`, `?:`, `|| &&` (also `or`
`and`, short-circuit), `| ^ &`, `== != < <= > >=`, `<< >>`, `+ -`, `* / %`, unary `- ! ~ * &`
(`not` = `!`), `++ --` (prefix and postfix), calls, `a[i]`, `s.field`, `p->field`.

- `cast<T>(value)`: conversions between integers, pointers and sub pointers. `cast<T*>(constant)`
  is a constant address (it can be a field's default value).
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
#import(<name>)            a compiled library (name.dlib) from the -L directories (see "Libraries")
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
dsc game.ds -L build -o game.drx             // #import(<gfx>) in game.ds
```
```
#import(<gfx>)          // gfx.dlib, in the -L directories
#import(libs/gfx)       // next to this file first, then the -L directories (.dlib is added)

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
- For now: the whole library is linked (also what the program doesn't use); a raw program with
  libraries is padded (`--fixed`) after linking.


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

## Tests

`tests/dscript/run_tests.sh` compiles every test (as a DRX and as a raw binary), runs it with
`drun` and checks its return value (`// expect-rv:`), its output (`// expect-output:`, the BIOS
runs first) or the compilation error (`// expect-error:`). `// expect-rom-rv:` compiles the
test with `--rom` as the machine's BIOS, boots it and checks `main`'s return value;
`// dsc-flags:` adds dsc options. `DSCRIPT_TEST_CONFIG=<file.dvm>` runs the tests on another
machine config (e.g. one with a known-good BIOS).


## Limitations (for now)

- One source file (plus includes) per program or library; libraries are linked statically
  (into the program, by dsc), not loaded at run time.
- `#define` has no stringizing or token pasting.
- No `switch`, `goto`, unions, floating point, variadic subs.
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
   (the instruction encodings are in `src/tools/GlobalData.*` and `src/hardware/CPUExtensions.*`).
5. **Language features that would help**: `switch`, unions, and a linker / object format
   (DRX could gain a symbol table) to compile big programs in pieces.

Porting order: lexer -> preprocessor -> parser -> code generator; each piece can be tested
against the C++ compiler's output (same dasm for the same input).
