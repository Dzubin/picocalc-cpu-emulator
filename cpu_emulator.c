/*
 *  PicoCalc CPU Emulator
 *  =====================
 *
 *  An old-computer emulator for the ClockworkPi PicoCalc: eventually able
 *  to emulate different real or invented 8-bit microprocessors, each with
 *  a 16-bit address space (8-bit bytes, instructions 1 to 3 bytes long).
 *  Author: Thomas Dzubin.
 *
 *  First step: a raw hexadecimal keyboard entry mechanism for loading code
 *  and data straight into memory, before any CPU definition, assembler, or
 *  disassembler exists. Later steps will add a microprocessor definition
 *  (registers, instruction decode, execution) and other entry methods
 *  (assembly language) that depend on it; none of that exists yet.
 *
 *  MEM[] is the emulated machine's whole address space, MEM_SIZE (64KB)
 *  bytes, plain 8-bit values. The screen is a three line window into it,
 *  each line ROW_BYTES (8) bytes in hex followed by the same bytes as
 *  ASCII (a period for anything not printable). cur_addr is the cursor:
 *  the byte it points at is always on the middle line, outlined in
 *  white, with the line before it above and the line after it below.
 *  Above and to the right of the window, a larger square shows that same
 *  byte in big digits, with an underline under the nibble that the next
 *  hex digit typed will fill in.
 *
 *  The left arrow moves the cursor back one byte, down and up move it
 *  one row (ROW_BYTES bytes). The right arrow moves forward by the length
 *  of the instruction at the cursor: INSTR[] holds, for each of the 256
 *  possible opcode bytes, an assembly mnemonic (shown in large letters to
 *  the left of the big square) and that instruction's length in bytes,
 *  so right-arrow steps from one instruction to the next. Addresses wrap around the 64KB
 *  space, so moving up from 0000 lands in FFF8-FFFF's row.
 *
 *  Typing a hex digit fills in the high nibble of the current byte (held
 *  in pending_nibble, and shown in the big square, but not yet written).
 *  The second digit completes the byte: it is written to MEM[cur_addr],
 *  shown for COMMIT_PAUSE_MS so it can be checked, and then the cursor
 *  moves on to the next byte. Backspace drops a pending
 *  nibble. Moving the cursor with an arrow key also drops it, leaving the
 *  byte as it was.
 *
 *  ESC switches to ADDRESS mode: type up to four hex digits (backspace
 *  corrects), then ENTER moves the cursor there, or ESC again to cancel.
 *
 *  The display is always redrawn straight from MEM[] and cur_addr, with
 *  no separately tracked text, so it is exactly right no matter how the
 *  cursor got where it is.
 *
 *  Any key that does not apply right now is rejected with a short beep
 *  and otherwise ignored. ~ (SHIFT + backtick) always reboots into
 *  BOOTSEL mode. Q asks "EXIT AND LOSE MEMORY? Y/N" (the memory only lives in
 *  RAM) and, on Y, leaves for the PicoCalc UF2 Loader menu.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"      /* reset_usb_boot() - BOOTSEL reboot ('~') */
#include "hardware/watchdog.h" /* watchdog_hw, watchdog_reboot() - exit to the UF2 Loader */

#include "lcd.h"          /* WIDTH, HEIGHT, GLYPH_HEIGHT, MAX_ROW, RGB(), lcd_* */
#include "font.h"         /* font_8x10 glyph bitmaps, scaled up for the big square */
#include "southbridge.h"  /* sb_init(), sb_read_keyboard()               */
#include "keyboard.h"     /* KEY_* codes, KEY_STATE_* values             */
#include "audio.h"        /* LOW_BEEP, audio_init(), audio_play_sound_blocking() */
#include "cpu_emulator_config.h"

/* ===================================================================== */
/*  Function prototypes                                                  */
/* ===================================================================== */

/* small helpers */
static int  hex_digit_value(uint8_t c);
static bool is_printable_ascii(uint8_t b);
static void beep_invalid(void);
static void fill(uint16_t colour, int x, int y, int w, int h);
static void put_row_left(int row, const char *str, uint16_t fg);

/* display */
static void draw_startup_screen(void);
static void redraw_view(void);
static void redraw_entry_row(void);
static void redraw_window(void);
static void draw_dump_row(int screen_row, uint16_t start, uint16_t fg);
static void draw_outline(void);
static void redraw_big_square(void);
static void redraw_mnemonic(void);
static uint8_t displayed_byte(void);
static void buf_fill(uint16_t *buf, int stride, int x, int y, int w, int h, uint16_t colour);
static void buf_glyph(uint16_t *buf, int stride, char ch, int x, int y, int scale, uint16_t colour);

/* startup data */
static void init_memory(void);
static void init_instruction_table(void);

/* key handling */
static void move_cursor(int delta);
static void handle_edit_key(uint8_t c);
static void handle_address_key(uint8_t c);

/* keyboard */
static inline uint16_t key_event(void);
static inline uint8_t  ev_state(uint16_t e);
static inline uint8_t  ev_code(uint16_t e);
static bool is_mod_key(uint8_t c);
static void go_bootsel(void);
static bool confirm_exit(void);
static void exit_to_loader(void);
static void entry_loop(void);

/* ===================================================================== */
/*  main                                                                 */
/* ===================================================================== */
int main(void)
{
    stdio_init_all();

    sb_init();                 /* keyboard / south-bridge I2C (no bg poll) */
    lcd_init();                 /* ST7365P LCD */
    lcd_enable_cursor(false);   /* we don't want the blinking text cursor */
    audio_init();               /* stereo PWM tones; see beep_invalid()   */

    init_memory();              /* seeds rand(); must come before the table */
    init_instruction_table();
    draw_startup_screen();
    entry_loop();
    return 0;
}

/* ===================================================================== */
/*  Memory and entry state                                               */
/* ===================================================================== */

static uint8_t MEM[MEM_SIZE];

typedef enum { MODE_EDIT, MODE_ADDRESS } entry_mode_t;

static entry_mode_t mode = MODE_EDIT;

/* The cursor: the byte the window is centred on and the next completed
 * byte will be written to. pending_nibble is -1 with nothing typed for
 * it yet, else the high nibble already typed.                           */
static uint16_t cur_addr;
static int      pending_nibble = -1;

/* ADDRESS mode: value builds up as digits are typed (each digit shifts
 * the value left 4 bits and ORs in the new nibble); digit_count tracks
 * how many have been typed so far, purely so the display can show
 * leading zeros the same way they were typed.                           */
static uint16_t addr_value;
static int      addr_digit_count;

/* One entry per possible instruction byte: the assembly mnemonic to show
 * for it and how many bytes the whole instruction takes (opcode included),
 * 1 to 3. Filled in by init_instruction_table(); a CPU definition will
 * eventually load real values.                                          */
typedef struct {
    char    mnemonic[MNEM_CHARS + 1];
    uint8_t length;
} instr_t;

static instr_t INSTR[256];

/* Pixels for the big square, built up here and sent to the LCD in one
 * go so it never flickers through a half-drawn state.                   */
static uint16_t big_buf[BIG_W * BIG_H];

/* Same idea for the mnemonic and its arguments to the left of it. */
static uint16_t mnem_buf[MNEM_LINE_W * MNEM_H];

/* ===================================================================== */
/*  Small helpers                                                        */
/* ===================================================================== */

/* Hex digit value of an already-lowercased character, or -1 if it is not
 * one.                                                                   */
static int hex_digit_value(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool is_printable_ascii(uint8_t b)
{
    return b >= 0x20 && b <= 0x7E;
}

/* Fills all of memory with random bytes (0 to 255), fresh each power-up,
 * so there is varied data to browse with the arrow keys. Seeds the
 * random number generator from the clock, for this and the instruction
 * table's lengths too.                                                  */
static void init_memory(void)
{
    srand(time_us_32());
    for (int i = 0; i < MEM_SIZE; i++)
        MEM[i] = (uint8_t)(rand() % 256);
}

/* Until a CPU is defined every opcode is an unknown instruction, and the
 * lengths are random (1 to 3, chosen fresh each power-up from the clock)
 * just so the right arrow has varied steps to test with. Opcode 0 is the
 * exception: NOP, one byte.                                             */
static void init_instruction_table(void)
{
    for (int i = 0; i < 256; i++) {
        strcpy(INSTR[i].mnemonic, "???");
        INSTR[i].length = (uint8_t)(1 + rand() % 3);
    }

    strcpy(INSTR[0].mnemonic, "NOP");   /* the one real entry, for testing */
    INSTR[0].length = 1;
}

static void beep_invalid(void)
{
    audio_play_sound_blocking(BEEP_FREQ, BEEP_FREQ, BEEP_MS);
}

static void fill(uint16_t colour, int x, int y, int w, int h)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > WIDTH)  w = WIDTH  - x;
    if (y + h > HEIGHT) h = HEIGHT - y;
    if (w > 0 && h > 0)
        lcd_solid_rectangle(colour, (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h);
}

/* Left-aligned rather than centred: these lines grow from the left
 * margin as digits are typed, so centring would make them shift position
 * with every keystroke instead of staying anchored.                     */
static void put_row_left(int row, const char *str, uint16_t fg)
{
    fill(COL_BG, 0, row * GLYPH_HEIGHT, WIDTH, GLYPH_HEIGHT);
    lcd_set_foreground(fg);
    lcd_putstr(0, (uint8_t)row, str);
}

/* ===================================================================== */
/*  Display                                                              */
/* ===================================================================== */

static void draw_startup_screen(void)
{
    lcd_clear_screen();
    put_row_left(TITLE_ROW1, "PICOCALC CPU EMULATOR",                  COL_TITLE);
    put_row_left(TITLE_ROW2, VERSION " - HEX MEMORY ENTRY TEST",       COL_TITLE);
    put_row_left(HELP_ROW1,  "ARROWS MOVE THE CURSOR (1 OR 8 BYTES)",  COL_LABEL);
    put_row_left(HELP_ROW2,  "TYPE HEX DIGITS TO FILL IN THE BYTE",    COL_LABEL);
    put_row_left(HELP_ROW3,  "ESC=ADDRESS  Q=EXIT  ~=BOOTSEL",         COL_LABEL);
    redraw_view();
    redraw_entry_row();
}

/* The window and the big square: everything that depends on cur_addr and
 * pending_nibble.                                                        */
static void redraw_view(void)
{
    redraw_window();
    redraw_big_square();
    redraw_mnemonic();
}

/* The bottom line: the cursor address, or the ADDR> prompt in ADDRESS
 * mode.                                                                  */
static void redraw_entry_row(void)
{
    char line[40];

    if (mode == MODE_ADDRESS) {
        if (addr_digit_count == 0)
            snprintf(line, sizeof line, "ADDR> ");
        else
            snprintf(line, sizeof line, "ADDR> %0*X", addr_digit_count, (unsigned)addr_value);
    } else {
        snprintf(line, sizeof line, "CURSOR AT %04X", (unsigned)cur_addr);
    }

    put_row_left(ENTRY_ROW, line, COL_WHITE);
}

/* Three dump lines centred on the cursor's row (the row of ROW_BYTES
 * bytes that contains cur_addr), with a blank text row between each. The
 * whole region is cleared first so the outline of the previous cursor
 * position, which reaches into those blank rows, is wiped too.           */
static void redraw_window(void)
{
    uint16_t mid = (uint16_t)(cur_addr - cur_addr % ROW_BYTES);

    fill(COL_BG, 0, (WIN_ROW1 - 1) * GLYPH_HEIGHT, WIDTH,
         (WIN_ROW3 - WIN_ROW1 + 3) * GLYPH_HEIGHT);

    draw_dump_row(WIN_ROW1, (uint16_t)(mid - ROW_BYTES), COL_LABEL);
    draw_dump_row(WIN_ROW2, mid,                         COL_WHITE);
    draw_dump_row(WIN_ROW3, (uint16_t)(mid + ROW_BYTES), COL_LABEL);
    draw_outline();
}

/* "XXXX: XX XX XX XX XX XX XX XX ABCDEFGH", read straight from MEM[]. */
static void draw_dump_row(int screen_row, uint16_t start, uint16_t fg)
{
    char line[48];   /* "XXXX: " (6) + ROW_BYTES "XX " groups (24) + a
                       * space + ROW_BYTES ASCII chars + a null           */

    int pos = snprintf(line, sizeof line, "%04X: ", (unsigned)start);
    for (int i = 0; i < ROW_BYTES; i++) {
        uint16_t a = (uint16_t)(start + i);
        pos += snprintf(line + pos, sizeof line - (size_t)pos, "%02X ", (unsigned)MEM[a]);
    }

    line[pos++] = ' ';
    for (int i = 0; i < ROW_BYTES; i++) {
        uint8_t b = MEM[(uint16_t)(start + i)];
        line[pos++] = is_printable_ascii(b) ? (char)b : '.';
    }
    line[pos] = '\0';

    lcd_set_foreground(fg);
    lcd_putstr(0, (uint8_t)screen_row, line);
}

/* A one pixel white rectangle around the cursor byte's two characters on
 * the middle line.                                                       */
static void draw_outline(void)
{
    int col = (int)(cur_addr % ROW_BYTES);
    int x = (ADDR_COLS + BYTE_COLS * col) * 8 - OUTLINE_PAD;   /* font is 8 wide */
    int y = WIN_ROW2 * GLYPH_HEIGHT - OUTLINE_PAD;
    int w = 2 * 8 + 2 * OUTLINE_PAD;
    int h = GLYPH_HEIGHT + 2 * OUTLINE_PAD;

    fill(COL_WHITE, x,         y,         w, 1);
    fill(COL_WHITE, x,         y + h - 1, w, 1);
    fill(COL_WHITE, x,         y,         1, h);
    fill(COL_WHITE, x + w - 1, y,         1, h);
}

/* The byte the big square shows: the current byte, with the high nibble
 * replaced by the pending one once it has been typed.                    */
static uint8_t displayed_byte(void)
{
    uint8_t b = MEM[cur_addr];
    if (pending_nibble >= 0)
        b = (uint8_t)((pending_nibble << 4) | (b & 0xF));
    return b;
}

/* The byte being edited, big. The underline marks the nibble the next
 * hex digit will fill in.                                                */
static void redraw_big_square(void)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    uint8_t b = displayed_byte();
    int digit_w = 8 * BIG_SCALE;
    int x_hi = BIG_MARGIN_X;
    int x_lo = BIG_MARGIN_X + digit_w + BIG_DIGIT_GAP;
    int x_ul = (pending_nibble >= 0) ? x_lo : x_hi;

    buf_fill(big_buf, BIG_W, 0, 0, BIG_W, BIG_H, COL_WHITE);
    buf_fill(big_buf, BIG_W, BIG_BORDER, BIG_BORDER,
             BIG_W - 2 * BIG_BORDER, BIG_H - 2 * BIG_BORDER, COL_BG);

    buf_glyph(big_buf, BIG_W, hex_chars[b >> 4],  x_hi, BIG_DIGIT_Y, BIG_SCALE, COL_TITLE);
    buf_glyph(big_buf, BIG_W, hex_chars[b & 0xF], x_lo, BIG_DIGIT_Y, BIG_SCALE, COL_TITLE);
    buf_fill(big_buf, BIG_W, x_ul, BIG_UL_Y, digit_w, BIG_UL_H, COL_LABEL);

    lcd_blit(big_buf, BIG_X, BIG_Y, BIG_W, BIG_H);
}

/* The assembly mnemonic for the byte in the big square, in large letters
 * at the same height, on the left of the screen, followed by one argument
 * for a 2 byte instruction or two for a 3 byte one (smaller, and centred
 * vertically on the mnemonic).                                           */
static void redraw_mnemonic(void)
{
    const instr_t *ins = &INSTR[displayed_byte()];
    int arg_y = (MNEM_H - GLYPH_HEIGHT * ARG_SCALE) / 2;

    buf_fill(mnem_buf, MNEM_LINE_W, 0, 0, MNEM_LINE_W, MNEM_H, COL_BG);

    for (int i = 0; i < MNEM_CHARS && ins->mnemonic[i] != '\0'; i++)
        buf_glyph(mnem_buf, MNEM_LINE_W, ins->mnemonic[i],
                  i * 8 * MNEM_SCALE, 0, MNEM_SCALE, COL_LABEL);

    for (int a = 0; a < ins->length - 1; a++) {
        int x = MNEM_W + ARG_GAP + a * (ARG_W + ARG_SEP);
        for (int i = 0; i < ARG_CHARS; i++)
            buf_glyph(mnem_buf, MNEM_LINE_W, ARG_TEXT[i],
                      x + i * 8 * ARG_SCALE, arg_y, ARG_SCALE, COL_WHITE);
    }

    lcd_blit(mnem_buf, MNEM_X, MNEM_Y, MNEM_LINE_W, MNEM_H);
}

static void buf_fill(uint16_t *buf, int stride, int x, int y, int w, int h, uint16_t colour)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            buf[j * stride + i] = colour;
}

/* One character of the 8x10 font drawn scale times larger at x, y. */
static void buf_glyph(uint16_t *buf, int stride, char ch, int x, int y, int scale, uint16_t colour)
{
    const uint8_t *glyph = &font_8x10.glyphs[(uint8_t)ch * GLYPH_HEIGHT];

    for (int gy = 0; gy < GLYPH_HEIGHT; gy++)
        for (int gx = 0; gx < 8; gx++)
            if (glyph[gy] & (0x80 >> gx))
                buf_fill(buf, stride, x + gx * scale, y + gy * scale, scale, scale, colour);
}

/* ===================================================================== */
/*  Key handling                                                         */
/* ===================================================================== */

static void move_cursor(int delta)
{
    cur_addr = (uint16_t)(cur_addr + delta);
    pending_nibble = -1;
    redraw_view();
    redraw_entry_row();
}

static void handle_edit_key(uint8_t c)
{
    int digit = hex_digit_value(c);
    if (digit >= 0) {
        if (pending_nibble < 0) {
            pending_nibble = digit;
        } else {
            MEM[cur_addr] = (uint8_t)((pending_nibble << 4) | digit);
            pending_nibble = -1;
            redraw_view();
            sleep_ms(COMMIT_PAUSE_MS);
            cur_addr++;
        }
        redraw_view();
        redraw_entry_row();
        return;
    }

    switch (c) {
    case KEY_RIGHT: move_cursor(INSTR[MEM[cur_addr]].length); return;
    case KEY_LEFT:  move_cursor(-1);         return;
    case KEY_DOWN:  move_cursor(ROW_BYTES);  return;
    case KEY_UP:    move_cursor(-ROW_BYTES); return;

    case KEY_BACKSPACE:
        if (pending_nibble < 0) {
            beep_invalid();
            return;
        }
        pending_nibble = -1;
        redraw_view();
        return;

    case KEY_ESC:
        mode = MODE_ADDRESS;
        addr_value = 0;
        addr_digit_count = 0;
        pending_nibble = -1;
        redraw_view();
        redraw_entry_row();
        return;
    }

    beep_invalid();
}

static void handle_address_key(uint8_t c)
{
    int digit = hex_digit_value(c);
    if (digit >= 0) {
        if (addr_digit_count >= 4) {
            beep_invalid();
            return;
        }
        addr_value = (uint16_t)((addr_value << 4) | digit);
        addr_digit_count++;
        redraw_entry_row();
        return;
    }

    if (c == KEY_BACKSPACE) {
        if (addr_digit_count == 0) {
            beep_invalid();
            return;
        }
        addr_value >>= 4;
        addr_digit_count--;
        redraw_entry_row();
        return;
    }

    if (c == KEY_ENTER || c == KEY_RETURN) {
        cur_addr = addr_value;
        mode = MODE_EDIT;
        redraw_view();
        redraw_entry_row();
        return;
    }

    if (c == KEY_ESC) {
        mode = MODE_EDIT;
        redraw_entry_row();
        return;
    }

    beep_invalid();
}

/* ===================================================================== */
/*  Keyboard (raw south-bridge FIFO polling)                             */
/* ===================================================================== */

static inline uint16_t key_event(void)      { return sb_read_keyboard(); }
static inline uint8_t  ev_state(uint16_t e) { return (e >> 8) & 0xFF; }
static inline uint8_t  ev_code(uint16_t e)  { return e & 0xFF; }

static bool is_mod_key(uint8_t c)
{
    return c == KEY_MOD_SHL || c == KEY_MOD_SHR || c == KEY_MOD_CTRL ||
           c == KEY_MOD_ALT || c == KEY_MOD_SYM;
}

static void go_bootsel(void)
{
    put_row_left(ENTRY_ROW, "REBOOTING TO BOOTSEL...", COL_LABEL);
    sleep_ms(300);
    reset_usb_boot(0, 0);
    while (1)
        tight_loop_contents();
}

/* Asks "EXIT AND LOSE MEMORY? Y/N" on the status line (the emulated memory
 * only lives in RAM) and returns true only for Y / y. '~' still goes to
 * BOOTSEL. The caller redraws the status line if the answer is no.      */
static bool confirm_exit(void)
{
    put_row_left(ENTRY_ROW, "EXIT AND LOSE MEMORY? Y/N", COL_LABEL);
    for (;;) {
        uint16_t e = key_event();
        if (e && ev_state(e) == KEY_STATE_PRESSED) {
            uint8_t code = ev_code(e);

            if (is_mod_key(code))
                continue;
            if (code == '~')
                go_bootsel();   /* does not return */
            return code == 'y' || code == 'Y';
        }
        sleep_ms(3);
    }
}

/* Leaves the program for the PicoCalc UF2 Loader's menu: shows a short
 * message, asks the loader for its menu (see LOADER_COMMAND_MAGIC in
 * cpu_emulator_config.h) and reboots with the watchdog. If the program was
 * flashed straight to the chip with no loader, nothing reads the request
 * and it simply restarts.                                                */
static void exit_to_loader(void)
{
    put_row_left(ENTRY_ROW, "BACK TO THE LOADER...", COL_LABEL);
    sleep_ms(LOADER_EXIT_MSG_MS);

    watchdog_hw->scratch[LOADER_SCRATCH_MODE] = LOADER_BOOT_MODE_SD;
    watchdog_hw->scratch[LOADER_SCRATCH_ARGUMENT] = 0;
    watchdog_hw->scratch[LOADER_SCRATCH_MAGIC] = LOADER_COMMAND_MAGIC;
    watchdog_reboot(0, 0, LOADER_REBOOT_DELAY_MS);

    while (1)
        tight_loop_contents();      /* the reboot comes in a few milliseconds */
}

static void entry_loop(void)
{
    for (;;) {
        uint16_t e = key_event();
        if (!e || ev_state(e) != KEY_STATE_PRESSED) {
            sleep_ms(3);
            continue;
        }

        uint8_t code = ev_code(e);
        if (is_mod_key(code))
            continue;

        uint8_t c = (code >= 'A' && code <= 'Z') ? (uint8_t)(code + 0x20) : code;

        if (c == '~')
            go_bootsel();       /* does not return */

        if (c == 'q') {         /* Q or q: back to the UF2 Loader, after a Y/N */
            if (confirm_exit())
                exit_to_loader();   /* does not return */
            redraw_entry_row();     /* answered no: put the status line back */
            continue;
        }

        if (mode == MODE_EDIT)
            handle_edit_key(c);
        else
            handle_address_key(c);
    }
}
