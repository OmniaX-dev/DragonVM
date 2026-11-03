# The Dragon video system

How text gets from a program to the screen: the BIOS side (`int 0x30`, in the dasm BIOS under
`extra/dss/bios/`) and the machine side (the C++ video card: `src/hardware/VirtualDisplay.*` and
`hw::interface::Graphics` in `src/hardware/VirtualIODevices.*`).

Short version: the BIOS never draws anything. It writes a few bytes into the video card's
registers and then writes a **signal** byte. The card notices the signal between two CPU
instructions, does the work in C++, and clears the signal. What you see on the window only changes
when the card **redraws**, which mostly happens on a timer.

---

## 1. The pieces

```
 program ──int 0x30──> BIOS handler (_int_30_handler, drivers.dss)
                           │  writes registers + a signal byte
                           ▼
            VGA registers, 0x01000900 - 0x010009FF  (CPU-visible, 256 bytes)
                           │  read once per CPU cycle
                           ▼
            VirtualDisplay::processSignal()          (C++, VirtualDisplay.cpp)
              ├─ single color mode: a list of text lines (std::deque), no VRAM
              └─ 16 color mode: hw::interface::Graphics VRAM (not CPU-visible)
                           │  on redraw
                           ▼
            VirtualDisplay::__redraw_screen() -> to_cells() -> TextConsoleCanvas (ogfx)
                                              -> hardware interrupt 0xE0
```

| Part | Where | What it is |
|---|---|---|
| `hw::interface::Graphics` | `VirtualIODevices.{hpp,cpp}` | The video card's **memory**: 64 KiB (`m_videoMemory`). The first 256 bytes are the registers, which are mapped at 0x01000900. The rest is VRAM for the 16 color mode. Also has the helpers that read, write, clear, swap and scroll VRAM. |
| `hw::VirtualDisplay` | `VirtualDisplay.{hpp,cpp}` | The video card's **logic** and the screen widget (an ogfx `TextConsoleCanvas`, 90 x 21 cells of 11 x 26 pixels). It handles signals, keeps the single color text, and turns everything into cells on a redraw. |
| `_int_30_handler` | `extra/dss/bios/drivers.dss` | The BIOS video interrupt: one sub-function per `R10` value. |
| `display.dss` | `extra/dss/bios/display.dss` | BIOS helpers: printing integers (both modes), strings (16 color) and the boot logo / machine info. |
| `RuntimeWindow`, `Debugger` | `src/runtime`, `src/debugger` | Call `processSignal()` every cycle and request redraws on a timer. |
| `drun` | `src/runtime/drun_main.cpp` | Headless: its own copy of the single color protocol that prints to stdout. The 16 color mode is ignored. |

---

## 2. The register block (0x01000900)

The `Graphics` device is mapped with *remap* on, so CPU address `0x01000900 + n` is byte `n` of the
card's memory. Multi-byte registers are big endian, like everything else on the machine.

| Offset | Size | Register | Used by |
|---|---|---|---|
| 0x00 | 1 | `VideoMode`: 0 = single color text, 1 = 16 color text | both modes |
| 0x01 | 1 | `ClearColor` | nothing (defined, never read) |
| 0x02 | 1 | `Palette` | nothing (the palette comes from the machine config) |
| 0x03 | 1 | `Signal`: what the card should do now (section 3) | both modes |
| 0x04 | 1 | `TextSingleCharacter`: the character for PRINT_CHAR / STORE_CHAR | single color |
| 0x05 | 1 | `TextSingleInvertColors`: not 0 = characters are written with swapped colors | single color |
| 0x08 | 4 | `TextSingleString`: address of a 0-terminated string for PRINT_STRING | single color |
| 0x7C | 4 | `Flags`: bit 0 = double buffering, bit 1 = screen redraw disabled | 16 color (bit 1: both) |
| 0x80 | 4 | `MemControllerX` (the BIOS writes only the low byte, at +3) | 16 color |
| 0x84 | 4 | `MemControllerY` (same, at +3) | 16 color |
| 0x88 | 1 | `MemControllerChar` | 16 color |
| 0x89 | 1 | `MemControllerBGCol` | 16 color |
| 0x8A | 1 | `MemControllerFGCol` | 16 color |
| 0xE0 | | `BUFF_START` in the BIOS data | nothing |

**VRAM** starts at card offset 0x100, right after the registers. That's past the end of the mapped
range (0x010009FF), so **the CPU can't read or write VRAM**. Everything goes through signals.

---

## 3. Signals: how the card is told to do something

`VirtualDisplay::processSignal()`:
1. reads `Signal`. If it's 0 (`Continue`), there's nothing to do;
2. reads `VideoMode` and runs the signal's action for that mode;
3. writes `Signal` back to 0, but only if the mode was 0 or 1. With another mode the signal stays
   pending.

It runs **once per CPU cycle**:
- **Runtime:** `RuntimeWindow::runMachine()` calls `mainLoop()` (which calls `processSignal()`) just
  before each `cpu.execute()`.
- **Debugger:** `Debugger::step_machine()` calls it right after each `cpu.execute()`.
- **`drun`:** it has its own `processVideoSignal()`, also once per cycle.

So a signal is always handled before the next instruction runs. That's why the BIOS can write
`Signal` in back-to-back instructions without ever waiting for the card. One instruction = at most
one signal.

### Signal values

| Value | Single color mode (`VideoMode` 0) | 16 color mode (`VideoMode` 1) |
|---|---|---|
| 0x00 | Continue (nothing) | Continue (nothing) |
| 0x02 | PRINT_CHAR: add `TextSingleCharacter` to the screen text | – |
| 0x03 | STORE_CHAR: add `TextSingleCharacter` to the line buffer | – |
| 0x04 | PRINT_FLUSH_BUFF: print the line buffer, then empty it | – |
| 0x05 | FLUSH_BUFF: empty the line buffer (nothing printed) | – |
| 0x06 | PRINT_BUFF: print the line buffer, keep it | – |
| 0x07 | PRINT_STRING: print the string at `TextSingleString` (the whole string, in one signal) | – |
| 0x10 | – | SWAP_BUFFERS (section 6.4), then redraw now |
| 0x11 | – | WRITE_VRAM: write one cell from the `MemController*` registers |
| 0x12 | – | SCROLL: move VRAM up one row, then redraw now |
| 0xE0 | REFRESH_SCREEN: nothing (the comment says the text is converted on every redraw anyway) | not handled: nothing |
| 0xE1 | CLEAR_SCREEN: delete all lines | FORCE_CLEAR: fill VRAM with the `MemController*` char/colors, request a redraw |
| 0xE2 | not handled: nothing | FORCE_REDRAW: redraw now |

"Redraw now" calls `__redraw_screen()` directly. "Request a redraw" only sets `m_redrawScreen`,
which is handled later (section 5).

---

## 4. Single color mode: the teletype

This mode has **no VRAM at all**. The card keeps the screen as text in C++:

- `m_singleTextLines`: a `std::deque` of lines. Each line is a vector of cells (character +
  foreground/background palette index).
- `m_singleTextBuffer`: a "line buffer" string, used by the STORE / PRINT_BUFF signals.

### 4.1 Adding a character: `single_text_add_char_to_line(c)`

This one function is the whole teletype:

```
if there are no lines yet: add an empty line
if c == '\n':             start a new line
else if c is printable:
    if the last line already has 90 characters: start a new line     <- automatic wrap
    append c to the last line, with fg/bg swapped if TextSingleInvertColors != 0
else:                     ignore c (other control characters, 0, ...)
while there are more than 21 lines: remove the first (oldest) line  <- automatic scroll
```

**This is the answer to "how does the screen know to scroll?"** It doesn't scroll anything. The
card keeps a list of at most 21 lines, and when a new line pushes the count past 21, the oldest
line is dropped from the front of the deque. On the next redraw, `single_text_to_cells()` copies
lines 0..20 into the 21 screen rows (and pads each with empty cells), so everything appears to have
moved up by one. Wrapping is the same idea: a line never grows past 90 characters, and the 91st
starts a new line.

So for `int 0x30` with `R10 = 0x0C`:
1. the BIOS writes the string's address (`R9`) to `TextSingleString` and signal 0x07
   (`_int_30_print_string_buffered`);
2. before the next instruction, `processSignal()` reads the string from memory, one byte at a time
   until the 0, and calls `single_text_add_char_to_line()` for each character;
3. wrapping and scrolling happen as part of that;
4. the screen shows it at the next redraw.

**`\n` works in strings** printed with 0x0C (and 0x01 / 0x03), because `add_char_to_line` handles
`'\n'` itself. That was the reason you gave for splitting the BIOS logo into rows, so it may be
worth a try.

The invert register is read **per character**, at the moment it's added. That's why the BIOS can
toggle it (`int 0x30` / 0x0B) around "DragonBIOS 0.5" and only those characters come out inverted.

### 4.2 The line buffer

STORE_CHAR (`single_text_add_char_to_buffer`) appends to `m_singleTextBuffer` instead of the screen,
with two exceptions:
- **`'\n'`** prints and empties the buffer, then adds the `'\n'` (a new line).
- **A full buffer (90 characters)** prints and empties the buffer, then adds this character
  **directly to the screen** (not to the buffer).

The buffer is a way to build a line out of pieces and decide later whether to print it (0x04 / 0x06)
or throw it away (0x05). `int 0x30` / 0x02 ("new line") is implemented as STORE_CHAR `'\n'`, so it
also prints whatever is in the buffer.

### 4.3 The int 0x30 functions (single color)

`R10` selects the function, `R9` is the argument.

| R10 | BIOS label | What it does |
|---|---|---|
| 0x01 | `_int_30_direct_print_char_text_single` | `R9` -> `TextSingleCharacter`, signal PRINT_CHAR |
| 0x02 | `_int_30_direct_new_line_text_single` | `'\n'` -> STORE_CHAR (prints the buffer, new line) |
| 0x03 | `_int_30_direct_print_string_text_single` | The BIOS loops over the string at `R9`: one PRINT_CHAR signal per character. Same result as 0x0C, but much slower (about 6 instructions per character). |
| 0x04 | `_int_30_direct_print_integer_text_single` | Prints `R9` as an **unsigned** decimal (PRINT_CHAR per digit) |
| 0x05 | `_int_30_store_integer_text_single` | Same, but STORE_CHAR (into the buffer) |
| 0x06 | `_int_30_store_char_text_single` | `R9` -> STORE_CHAR |
| 0x07 | `_int_30_print_buffer_no_flush_text_single` | Should print the buffer and keep it. Prints it twice and empties it instead: see section 8. |
| 0x08 | `_int_30_print_buffer_and_flush_text_single` | Signal PRINT_FLUSH_BUFF |
| 0x09 | `_int_30_flush_buffer_text_single` | Signal FLUSH_BUFF |
| 0x0A | `_int_30_store_string_text_single` | Loops over the string at `R9`, one STORE_CHAR per character |
| 0x0B | `_int_30_invert_colors_text_single` | Toggles `TextSingleInvertColors` (0 <-> 1) |
| 0x0C | `_int_30_print_string_buffered` | `R9` -> `TextSingleString`, signal PRINT_STRING. The card prints the whole string at once. "Buffered" in the name is misleading: it doesn't touch the line buffer. |
| 0xE0 | `_int_30_clear_screen` | Signal 0xE1 (CLEAR_SCREEN). In 16 color mode this is FORCE_CLEAR, with whatever is in the `MemController*` registers. |
| 0xE1 | `_int_30_refresh_screen` | Signal 0xE0 (REFRESH), which **does nothing** in either mode |

Watch out for the swapped numbers: the BIOS function 0xE0 sends signal 0xE1, and function 0xE1
sends signal 0xE0.

The integer printers (`_print_integer_signle_color` in `display.dss`):
- **Digits:** they divide by 10 with `divipu` (unsigned; the remainder ends up in `RV`) and push
  each digit (`'0'` + remainder) on the stack. Then they pop the digits, so they come out in the
  right order.
- **Signal:** the signal to send (PRINT_CHAR or STORE_CHAR) is passed in the special register
  `reg(S_REG_1)` = `reg(0x07)`.
- **Negative numbers** come out as large unsigned values. The DScript SDK's `print(i32)` in
  `extra/ds/sdk/bios.ds` handles the sign before calling the BIOS.

The colors are not in the registers. They come from the machine config (`SingleColor_foreground`,
`SingleColor_background`), as palette entries 1 and 2 of `m_singlePalette`.

---

## 5. When the screen actually changes: redraws

Handling a signal only changes the card's **state**: the line deque, the buffer, or VRAM. The
window only shows the new state after a **redraw**:

```
__redraw_screen():
    apply_video_mode(VideoMode)      // switch palette/colors if the mode register changed
    to_cells()                       // single color: lines -> cells; 16 color: VRAM -> cells
    cpu.handleInterrupt(0xE0, true)  // the "screen refreshed" hardware interrupt
    m_redrawScreen = false
```

### 5.1 Who asks for a redraw

| Caller | How often |
|---|---|
| `RuntimeWindow::runMachine()` | a `StepTimer` at `ScreenRedrawRate` per second (CMOS 0x18, from `screen_redraw_rate_per_second` in the `.dvm`; 30 in the test machine). It only **requests** one (`redrawScreen()` sets `m_redrawScreen`). |
| the widget's `onUpdate()` | at the window's frame rate (60 fps). With auto redraw on (the runtime's default), it calls `update()`, which redraws if one was requested. |
| `Debugger::step_machine()` | every `clock_rate_sec / screen_redraw_rate_per_second` **cycles** (machine time, not wall time, so stepping is reproducible). Auto redraw is off; it calls `update()` itself after every cycle. |
| signals 0x10, 0x12, 0xE2 (16 color mode) | immediately |
| signal 0xE1 (16 color mode), `apply_video_mode()` | request |

So in single color mode, text printed with 0x0C shows up at the next timer tick, at most about 1/30
of a second later.

### 5.2 The ScreenRedrawDisabled flag (bit 1 of `Flags`)

`update()` does `m_redrawScreen = m_redrawScreen && !ScreenRedrawDisabled`. While the flag is set,
timer redraws are **thrown away**, not postponed: the request is lost. The immediate redraws (signals
0x10, 0x12, 0xE2) call `__redraw_screen()` directly and **ignore the flag**. Together that gives a
program full control: set the flag, write all the cells, then send 0xE2 once. The screen never shows
a half-drawn frame.

### 5.3 The 0xE0 "screen refreshed" interrupt

**Every** redraw raises hardware interrupt 0xE0 (`Text16ModeScreenRefreshed`), in both modes, despite
the name. The handler runs only if entry 0xE0 of the interrupt vector is enabled (0xFF); the BIOS
doesn't install one.

Hardware interrupts don't check the CPU's `InterruptsEnabled` flag: `handleInterrupt()` only looks at
the vector entry. Only the `int` instruction checks the flag. So `zflg Flags.InterruptsEnabled` does
**not** block this one (or the disk's 0x80).

---

## 6. 16 color mode: VRAM and cells

Set `VideoMode` to 1. Nothing in the BIOS does that; the program does it itself.

### 6.1 VRAM layout (inside the card, not CPU-visible)

```
card offset 0x0000 - 0x00FF   registers (the CPU sees these at 0x01000900)
card offset 0x0100            frame A   90 x 21 cells x 4 bytes = 7560 bytes
card offset 0x1E88            frame B   (7560 bytes)
```

`m_16Color_currentFrameAddr` is the frame that's **shown** and starts as A;
`m_16Color_secondFrameAddr` is the other one. One VRAM cell is 4 bytes:

| +0 | +1 | +2 | +3 |
|---|---|---|---|
| character | foreground | background | reserved |

Cell (x, y) is at `frame + (y * 90 + x) * 4`.

### 6.2 Writing a cell

The program's side of a cell is a 5-byte struct in RAM: `Text16VModeCell` in `bios_api.dss`, and
`VGAText16Cell` in the DScript SDK:

| +0 | +1 | +2 | +3 | +4 |
|---|---|---|---|---|
| Character | Foreground | Background | CoordX | CoordY |

`int 0x30` / 0x21 with `R9` = the cell's address (`_int_30_write_vram_text16`):
1. The BIOS copies the 5 bytes into `MemControllerChar` / `FG` / `BG`, and into the low bytes of
   `MemControllerX` / `Y`.
2. It sends signal 0x11, and the card calls
   `Graphics::writeVRAM_16Colors(x, y, char, bg, fg)`.
3. That writes into the shown frame, or into the second frame if double buffering is on
   (section 6.4).

**This does not request a redraw.** The cell appears at the next timer redraw.

Coordinates aren't checked. Since the offset is `y * 90 + x`:
- an `x` of 90 or more lands on the following rows;
- a `y` of 21 or more lands in the other frame;
- a write past the 64 KiB of card memory is silently dropped.

### 6.3 The other int 0x30 functions (16 color)

| R10 | What it does |
|---|---|
| 0x21 | Write one cell (above). |
| 0x22 | Print a string: `R8` = string, `R9` = cell (colors and starting position). Calls `_print_string_text16`, which does `int 0x30` / 0x21 per character, **from inside the int 0x30 handler**. Interrupts nest fine: each one pushes a CPU frame. |
| 0x23 | Print an integer: `R8` = number (unsigned), `R9` = cell. Same pattern (`_print_integer_text16`). |
| 0x24 | Clear: `R9` = cell (only char, fg and bg are used). Signal FORCE_CLEAR fills the **shown** frame with that cell, then requests a redraw. DragonOS clears with char 0x20 and fg = bg = the color. |
| 0x25 | Scroll: signal SCROLL (below), redrawn immediately. |

There is **no automatic scrolling or wrapping** in this mode. The cell's X only moves while a string
or integer is printed, and nothing ever moves Y. Starting a new line is up to the program: increment
`CoordY`, and send 0x25 when it reaches the bottom.

**Scroll** (`Graphics::scroll_16Colors`):
1. copies rows 1..20 of the shown frame to rows 0..19;
2. writes character 0x20 (space) into the bottom row. Only the character byte, so the bottom row
   keeps the colors it had, which are the same as row 19 now.

### 6.4 Double buffering (bit 0 of `Flags`)

With bit 0 set, WRITE_VRAM goes to the **second** frame, so the program draws off screen. Signal
0x10 (SWAP_BUFFERS), which no int 0x30 function sends (the program writes `Signal` itself):

1. copies the second frame into the shown frame, cell by cell;
2. swaps the two frame addresses;
3. redraws.

After that, the frame you just drew is shown, and the frame you'll draw into next holds a copy of
it, so you only need to redraw what changes. As written, though, step 1 mixes up the colors (see
section 8). Without bit 0, SWAP_BUFFERS does nothing but redraw. CLEAR and SCROLL always work on
the **shown** frame, even with double buffering on.

### 6.5 Colors

The 16 color palette comes from the machine config (`16Color_Palette` = palette number; only 0, the
default, exists). It's `BiosVideoDefaultPalette` in `GlobalData.hpp`:

| | | | |
|---|---|---|---|
| 0 Black | 1 Gray | 2 White | 3 Red |
| 4 Pink | 5 Brown | 6 Dark orange | 7 Orange |
| 8 Yellow | 9 Dark green | 10 Green | 11 Lime |
| 12 Dark blue | 13 Blue | 14 Light blue | 15 Sky |

Indexes 16 and up are black. VM color *i* is entry *i* of the canvas palette. Entry 0 can't hold a
color in ogfx's `TextConsoleCanvas` (it means "the default"), so `apply_video_mode` sets the
console's default colors to VM color 0 instead.

---

## 7. Switching modes and the headless runner

- **Switching modes:** `VideoMode` is read on every `update()` (every frame) and on every redraw.
  `apply_video_mode()` only acts when the mode changed. It swaps the canvas palette and default
  colors, then requests a redraw. The single color text and VRAM both stay as they were, so
  switching back shows the old content again.
- **Booting:** `_bios_entry_point` sets mode 0 first thing. The logo and machine info are printed
  by `_print_machine_info_signle_color`, using functions 0x0C, 0x02, 0x01, 0x0B and 0x04. Your
  current `entry.dss` jumps to the disk-loaded program before that call.
- **`drun`** has no screen. Its `processVideoSignal()` copies the single color logic onto stdout:
  - PRINT_CHAR and PRINT_STRING print right away;
  - the line buffer works like the card's;
  - the 16 color mode is ignored, apart from clearing `Signal`.

  That's what the DScript tests' `expect-output` compares against.
- **Debugger:** the display's state (lines, buffer, pending redraw) is saved in each history
  snapshot (`saveState` / `loadState`). The VRAM frame addresses are saved separately in
  `Graphics::tState`.

---

## 8. Quirks and bugs found while writing this

Only the first is verified by running it. The others come from reading the code.

1. **Verified: int 0x30 / 0x07 (print buffer, keep it) prints the buffer twice and then empties
   it.** In `drivers.dss`, `_int_30_print_buffer_no_flush_text_single` has no `jmp $_int_30_end`,
   so it falls through into 0x08. With "ab" stored, `0x07` then `0x01 'X'` prints `ababX` instead of
   `abX`. Fix: add `jmp $_int_30_end` after its `movb`.
2. **SWAP_BUFFERS mixes up foreground and background.** `swapBuffers_16Colors()` copies source byte
   +1 (foreground) into `+ background` (+2), and +2 into `+ foreground` (+1). The shown frame is
   right, because it's the old second frame after the pointer swap. But the new drawing frame holds
   a copy with the colors swapped, so cells you don't redraw come back with swapped colors after
   the next swap. The copy also isn't needed for a plain page flip.
3. **`_print_string_text16` (int 0x30 / 0x22) seems to start one column to the right.** It
   increments the cell's X **before** each `int 0x30` / 0x21, so the first character lands at
   X + 1. X is left after the last character, while `_print_integer_text16` restores it. An empty
   string still writes one cell (the 0), because the end test comes after the write.
4. **Function 0xE1 (refresh) does nothing**: its signal 0xE0 isn't handled in either mode. Use the
   timer, or signal 0xE2 in 16 color mode.
5. **REDRAW (0xE2) isn't handled in single color mode**, so there's no way to force an immediate
   redraw there.
6. **Function 0xE0 (clear) in 16 color mode** uses whatever char and colors the last 0x21 / 0x24
   left in the `MemController` registers.
7. **The integer printers are unsigned** (`divipu`), and the old comment says `int16`. Both are
   from before the 32-bit switch; DScript's `print(i32)` works around the sign.
8. **`ClearColor`, `Palette` and `BUFF_START`** are defined but unused.
