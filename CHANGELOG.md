# Changelog

All notable changes to PicoCalc CPU Emulator.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/);
the version here matches the `VERSION` define in `cpu_emulator_config.h`.

## [Unreleased]

- Updated the vendored `picocalc-text-starter` to Blair Leduc's code of 2026-10-18 (commit
  `85a002e`) and moved to Pico SDK 2.3.1. The search-hint, deleted-entry and busy-card fixes this
  copy carried are in his code now; four small local fixes remain (README, Third-party). The audio
  driver in it now sounds 20 to 20000 Hz, where it used to be silent outside 100 to 2000 Hz.
- Vendored starter: `fat32.c` `fat32_write()` no longer leaks a cluster each time a write starts on a
  cluster boundary. The program itself is unchanged; not rebuilt.

- Vendored starter: `fat32.c` skips deleted directory entries before long-name parts (an
  out-of-bounds stack write) and `sdcard.c` waits for a busy card by the clock and before
  every command (failed reads and writes on a card that was still programming). The
  program itself is unchanged; not rebuilt.

## [0.02] - Unreleased

Replaces the single hex entry line with a cursor-driven memory window.

### Added
- Build outputs are named without the `picocalc-` prefix, since the chip or system
  at the end of the name already says what they are for: `cpu-emulator-RP2040.uf2` and `cpu-emulator-RP2350.uf2`.
- Leaving: `Q` asks `EXIT AND LOSE MEMORY? Y/N` and, on Y, leaves for the
  PicoCalc UF2 Loader menu (it asks the loader for its menu through the
  watchdog scratch registers; with no loader installed the program just
  restarts). The help line lists it; `ESC` keeps its job and `~` still goes to
  BOOTSEL.
- Vendored `drivers/fat32.c`: `get_next_free_cluster()` now advances its search
  hint past the cluster it hands out (a local fix, not in upstream), so a long
  sequential write to the SD card no longer re-scans every cluster already given
  out and looks hung.
- All 64KB of memory is filled with random bytes (0-255) at each
  power-up, so there is varied data to browse.
- A 256-entry instruction table (`INSTR[]`): a mnemonic of up to five
  characters and an instruction length in bytes for each opcode byte,
  pre-filled with `???` and a random length of 1 to 3 (fresh each
  power-up) so the right arrow has varied steps to test with.
- The mnemonic of the byte in the big square, in large letters to its
  left at the same height.
- Instruction arguments on the mnemonic line: a length 2 instruction
  shows one `??` argument and a length 3 one shows two, smaller than the
  mnemonic. Opcode 0 is set to `NOP`, length 1, as the test entry.
- The right arrow now moves forward by the instruction length from the
  table; the left arrow still steps back exactly one byte.

### Changed
- The screen is now a three-line window into memory (8 bytes per line,
  hex then ASCII), centred on a cursor whose byte is outlined in white.
- Arrow keys move the cursor: left/right one byte, up/down one row of 8,
  wrapping around the 64KB space.
- A larger square, above and to the right of the window, shows the cursor
  byte in big digits with an underline under the nibble to be typed next.
- Hex digits fill in the cursor byte a nibble at a time; the byte is
  written to memory and the cursor advances when the second nibble is
  typed, after a quarter-second pause (`COMMIT_PAUSE_MS`) showing the
  finished byte in place. Backspace or an arrow key drops a half-typed
  nibble.
- ESC opens the ADDR> prompt to jump to any address (ENTER jumps, ESC
  again cancels). This replaces ADDRESS/DATA mode switching with ENTER.

### Removed
- The fixed-row entry line, its two preview lines, and the
  session-start backspace floor, all superseded by the window.

## [0.01] - Unreleased

First cut: a 64KB emulated memory space and a raw hexadecimal keyboard
entry mechanism for it. No CPU definition, instruction execution,
assembler, or disassembler yet.

### Added
- `MEM[]`, a full 64KB (`MEM_SIZE`) emulated address space, one byte per
  address.
- ADDRESS entry mode: type up to four hex digits, ENTER sets the current
  address and switches to DATA mode.
- DATA entry mode: type hex digits two at a time to store bytes at the
  current address, which advances automatically after each one. ENTER
  (with no half-typed digit pending) switches back to ADDRESS mode.
- A live entry line near the bottom of the screen, shared by both modes,
  that always re-renders itself directly from `MEM[]` rather than
  tracking its own text buffer.
- Digit-granular backspace: erases one hex digit at a time in either
  mode, reopening a previously committed byte for editing if there is no
  half-typed digit left to erase, down to the address the current entry
  session started at.
- A short beep, via the vendored audio driver, for any key that does not
  apply right now (a non-hex, non-control key; a hex digit past the
  4-digit address limit; backspace with nothing left to erase; ENTER
  mid-byte).
- `~` (SHIFT + backtick) BOOTSEL reboot, wired in from the start.
- Bytes are now entered in fixed rows of 8 (`ROW_BYTES`); a row fills and
  the next one starts automatically 8 bytes on, wrapping around the 64KB
  space like every other address here.
- Two read-only preview lines just above the entry line, always showing
  the 16 bytes immediately before the current row, in hex and then as
  ASCII (a period standing in for anything not printable) — reads
  straight from `MEM[]`, so it is correct whether or not those bytes
  were ever explicitly typed, and updates correctly when backspacing
  crosses back into an earlier row.
- ESC, in DATA mode, drops back to ADDRESS mode immediately, discarding
  any half-typed digit rather than requiring a completed byte first
  (unlike ENTER's version of the same switch).
- RP2350 build setup.

### Not implemented yet
- No CPU/microprocessor definition of any kind — nothing executes.
- No assembler or assembly-language entry (hex only, for now).
- No disassembler or full memory-dump/view mode — only the current row
  and the one immediately before it are visible at once.
- No RP2040 build yet, and no Windows/Linux desktop port either — RP2350
  only while this is under active development, per current practice.
