/*
 *  PicoCalc CPU Emulator: compile-time configuration
 *  ==================================================
 *  All #define constants live here (see cpu_emulator.c for the design
 *  notes and the program itself). Author: Thomas Dzubin.
 */
#pragma once

/* Shown on the startup screen. */
#define VERSION "V0.02"

/* 16-bit address space, 8-bit bytes: the largest any emulated CPU in
 * this project will need, so memory is sized for it once here rather
 * than per architecture.                                               */
#define MEM_SIZE 0x10000

/* The memory window shows rows of this many bytes, hex-dump style, and
 * the up and down arrows move the cursor by one row. The row text
 * ("XXXX: " + 8 bytes + a space + 8 ASCII characters) fills 39 of the
 * screen's 40 columns, so changing this needs the layout rethought.     */
#define ROW_BYTES    8

/* Screen rows. WIDTH/HEIGHT/GLYPH_HEIGHT and MAX_ROW come from lcd.h,
 * included before this header. TITLE/HELP rows are a one-time static
 * reference at the top. ENTRY_ROW is the status line at the bottom (the
 * cursor address, or the ADDR> prompt). The three memory window lines
 * sit just above it, one blank text row between each so the outline
 * around the current byte has room to be drawn.                         */
#define TITLE_ROW1   0
#define TITLE_ROW2   1
#define HELP_ROW1    3
#define HELP_ROW2    4
#define HELP_ROW3    5
#define ENTRY_ROW    (MAX_ROW - 1)
#define WIN_ROW1     (ENTRY_ROW - 6)
#define WIN_ROW2     (WIN_ROW1 + 2)
#define WIN_ROW3     (WIN_ROW2 + 2)

/* Where the window text starts within a line: past the 6-character
 * "XXXX: " address, each byte is two characters plus a space.           */
#define ADDR_COLS    6
#define BYTE_COLS    3

/* Pixels of clearance between the current byte's two characters and
 * the white outline drawn around them.                                  */
#define OUTLINE_PAD  2

/* The big square showing the byte being edited, above and to the right
 * of the window. Its two hex digits are the 8x10 font scaled up by
 * BIG_SCALE, with an underline under whichever nibble is typed next.    */
#define BIG_SCALE    3
#define BIG_W        72
#define BIG_H        48
#define BIG_X        (WIDTH - BIG_W - 8)
#define BIG_Y        ((WIN_ROW1 - 1) * GLYPH_HEIGHT - BIG_H - 6)
#define BIG_BORDER   2
#define BIG_MARGIN_X 8
#define BIG_DIGIT_Y  5
#define BIG_DIGIT_GAP 8
#define BIG_UL_Y     39
#define BIG_UL_H     3

/* The instruction mnemonic shown in large letters to the left of the big
 * square, level with its digits. MNEM_CHARS is the longest mnemonic.    */
#define MNEM_CHARS   5
#define MNEM_SCALE   3
#define MNEM_X       8
#define MNEM_Y       (BIG_Y + BIG_DIGIT_Y)
#define MNEM_W       (MNEM_CHARS * 8 * MNEM_SCALE)
#define MNEM_H       (GLYPH_HEIGHT * MNEM_SCALE)

/* Instruction arguments (one for a 2 byte instruction, two for a 3 byte
 * one) follow the mnemonic's MNEM_CHARS wide field on the same line, in a
 * smaller size so all of it still fits to the left of the big square.
 * ARG_TEXT stands in until real operands are decoded.                   */
#define ARG_TEXT     "??"
#define ARG_CHARS    2
#define ARG_SCALE    2
#define ARG_GAP      8
#define ARG_W        (ARG_CHARS * 8 * ARG_SCALE)
#define ARG_SEP      (ARG_GAP + 8 * ARG_SCALE)   /* between two arguments: one extra character */
#define MNEM_LINE_W  (MNEM_W + ARG_GAP + ARG_W + ARG_SEP + ARG_W)

/* After the second nibble completes a byte, the finished byte stays
 * on screen this long before the cursor moves on, so it can be checked. */
#define COMMIT_PAUSE_MS 250

/* A short, low beep for any key that does not apply right now. */
#define BEEP_FREQ    LOW_BEEP
#define BEEP_MS      80

#define COL_BG       RGB(  0,   0,   0)
#define COL_WHITE    RGB(235, 235, 235)
#define COL_LABEL    RGB(255, 160,  40)
#define COL_TITLE    RGB(120, 225, 255)

/* Leaving for the PicoCalc UF2 Loader (pelrun/uf2loader). The loader has no
 * call for an app to use, but its own menu hands commands to its start-up code
 * through the chip's watchdog scratch registers, which survive a watchdog
 * reboot: scratch 0 holds a magic number, 1 the boot mode, 2 an argument.
 * Asking for boot mode "SD" and then rebooting makes the loader show its menu
 * again. With no loader installed the program just restarts.               */
#define LOADER_COMMAND_MAGIC      0xE98CC638u /* PICOCALC_BL_MAGIC in the loader's proginfo.h */
#define LOADER_BOOT_MODE_SD       1           /* BOOT_SD: load the menu from the SD card */
#define LOADER_SCRATCH_MAGIC      0
#define LOADER_SCRATCH_MODE       1
#define LOADER_SCRATCH_ARGUMENT   2
#define LOADER_EXIT_MSG_MS        300         /* how long "BACK TO THE LOADER..." shows */
#define LOADER_REBOOT_DELAY_MS    10          /* the reboot happens this long after it is asked for */
