# The FPU: the `extfpu` CPU extension

Floating point for the Dragon CPU: IEEE 754 single precision (`f32`). The machine needs it in its
config (`cpuext = extmov, extalu, extfpu`); a program using it on a machine without it stops
with "Unsupported Extension". dasm has its mnemonics unless `--disable-extfpu` is given.

There are no float registers: a general register holds an `f32` as its 32 bits, so loads,
stores, moves, the stack and calls are the usual instructions. Results go in the first register
(in place), as with `extalu`; comparisons write `1` / `0` to `ACC`. Nothing traps: dividing by
zero gives an infinity, an invalid operation a NaN. There are no FPU flags (no extra CPU state).

Encoding: `Ext03` (opcode 0xE2), the sub-opcode, then the operands (a
register: 1 byte; an immediate: the f32's 4 bytes, big-endian). See `src/hardware/CPUExtensions.*`.

## Instructions

| Mnemonic | Operands | Result |
|---|---|---|
| `fadd` `fsub` `fmul` `fdiv` | `rd, rs` / `rd, imm` | `rd = rd op x` |
| `fmin` `fmax` | `rd, rs` / `rd, imm` | the smaller / larger (of NaN and a number: the number) |
| `fpow` | `rd, rs` / `rd, imm` | `rd = rd ^ x` |
| `fatan2` | `rd, rs` / `rd, imm` | `rd = atan2(rd, x)` (the angle of (x, rd)) |
| `fneg` `fabs` `fsqrt` | `rd` | `-rd`, `|rd|`, square root |
| `ffloor` `fceil` `ftrunc` `fround` | `rd` | rounded down / up / toward zero / to the nearest (halves away from zero) |
| `fsin` `fcos` `ftan` `fexp` `flog` | `rd` | radians; `flog` is the natural logarithm |
| `itof` `utof` | `rd` | `i32` / `u32` -> `f32` |
| `ftoi` `ftou` | `rd` | `f32` -> `i32` / `u32`: truncated toward zero, saturated (out of range: the nearest limit), NaN -> 0 |
| `feq` `flt` `fle` | `ra, rb` / `ra, imm` | `ACC = (ra == / < / <= x) ? 1 : 0`; 0 when an operand is NaN |

`>` and `>=` are `flt` / `fle` with the operands swapped; `!=` is `feq` with the result inverted.

## Immediates (dasm)

An immediate is written as a number and stored as the f32 it rounds to: `1.5`, `-2`, `10`,
`1e-3`, `2.5f`, `inf`, `-inf`, `nan`. A hex immediate is the f32's bits themselves:
`0x3FC00000` (= 1.5); dsc writes its constants that way.

```
mov R1, 0x40000000      ; 2.0 (mov takes the bits)
fsqrt R1                ; 1.41421354
fmul R1, 10             ; 14.1421356
ftoi R1                 ; 14
```

## Determinism

`fadd` `fsub` `fmul` `fdiv` `fsqrt` `fmin` `fmax` `fneg` `fabs`, the rounding instructions and the
conversions give the same bits on any host (IEEE 754, computed as C++ `float`). `fsin` `fcos`
`ftan` `fexp` `flog` `fpow` `fatan2` use the host's math library and can differ in the last bit
between hosts.
