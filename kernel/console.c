/*
 * MyOS - text console.
 *
 * Output can go to two places:
 *   - the whole screen (boot messages), rendered with the 8x16 font
 *   - a text buffer owned by a window (the terminal app)
 *
 * Every character is mirrored to COM1, which is how the automated tests
 * read what the kernel is doing.
 */
#include <stdarg.h>
#include "kernel.h"
#include "gfx.h"

#define SERIAL_PORT 0x3F8
#define FONT_W 8
#define FONT_H 16

static int cols, rows;
static int cx, cy;
static u8  color = C_TEXT;
static u8  default_color = C_TEXT;

struct textbuf *console_target;

/* ------------------------------------------------------------------ */
/* Serial port (COM1)                                                 */
/* ------------------------------------------------------------------ */
void serial_init(void)
{
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x80);
    outb(SERIAL_PORT + 0, 0x01);
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);
    outb(SERIAL_PORT + 2, 0xC7);
    outb(SERIAL_PORT + 4, 0x0B);
}

static int serial_transmit_empty(void)
{
    return inb(SERIAL_PORT + 5) & 0x20;
}

void serial_putc(char c)
{
    if (c == '\n') {
        while (!serial_transmit_empty()) { }
        outb(SERIAL_PORT, '\r');
    }
    while (!serial_transmit_empty()) { }
    outb(SERIAL_PORT, (u8)c);
}

/* ------------------------------------------------------------------ */
/* Text buffers (used by windows such as the terminal)                */
/* ------------------------------------------------------------------ */
void textbuf_init(struct textbuf *tb, int c, int r, char *cells, u8 *attrs)
{
    tb->cols = c;
    tb->rows = r;
    tb->cells = cells;
    tb->attrs = attrs;
    tb->cx = 0;
    tb->cy = 0;
    tb->color = C_TEXT;
    tb->dirty = 1;
    textbuf_clear(tb);
}

void textbuf_clear(struct textbuf *tb)
{
    for (int i = 0; i < tb->cols * tb->rows; i++) {
        tb->cells[i] = ' ';
        tb->attrs[i] = C_TEXT;
    }
    tb->cx = 0;
    tb->cy = 0;
    tb->dirty = 1;
}

static void textbuf_scroll(struct textbuf *tb)
{
    for (int y = 1; y < tb->rows; y++) {
        for (int x = 0; x < tb->cols; x++) {
            tb->cells[(y - 1) * tb->cols + x] = tb->cells[y * tb->cols + x];
            tb->attrs[(y - 1) * tb->cols + x] = tb->attrs[y * tb->cols + x];
        }
    }
    for (int x = 0; x < tb->cols; x++) {
        tb->cells[(tb->rows - 1) * tb->cols + x] = ' ';
        tb->attrs[(tb->rows - 1) * tb->cols + x] = C_TEXT;
    }
    tb->cy = tb->rows - 1;
}

static void textbuf_putc(struct textbuf *tb, char c)
{
    switch (c) {
    case '\n':
        tb->cx = 0;
        tb->cy++;
        break;
    case '\r':
        tb->cx = 0;
        break;
    case '\b':
        if (tb->cx > 0) {
            tb->cx--;
            tb->cells[tb->cy * tb->cols + tb->cx] = ' ';
        }
        break;
    case '\t':
        do {
            textbuf_putc(tb, ' ');
        } while (tb->cx % 8 != 0);
        break;
    default:
        tb->cells[tb->cy * tb->cols + tb->cx] = c;
        tb->attrs[tb->cy * tb->cols + tb->cx] = tb->color;
        tb->cx++;
        if (tb->cx >= tb->cols) {
            tb->cx = 0;
            tb->cy++;
        }
        break;
    }
    if (tb->cy >= tb->rows)
        textbuf_scroll(tb);
    tb->dirty = 1;
}

/* ------------------------------------------------------------------ */
/* Screen console                                                     */
/* ------------------------------------------------------------------ */
static void screen_scroll(void)
{
    u8 *bb = gfx_buffer();
    int w = gfx_width();
    int h = gfx_height();
    u32 lines = (u32)FONT_H * (u32)w;

    for (int y = FONT_H; y < h; y++)
        for (int x = 0; x < w; x++)
            bb[(y - FONT_H) * w + x] = bb[y * w + x];

    for (int y = h - FONT_H; y < h; y++)
        for (int x = 0; x < w; x++)
            bb[y * w + x] = C_BLACK;

    (void)lines;
    cy = rows - 1;
}

static void screen_putc(char c)
{
    switch (c) {
    case '\n':
        cx = 0;
        cy++;
        break;
    case '\r':
        cx = 0;
        break;
    case '\b':
        if (cx > 0) {
            cx--;
            gfx_fill(cx * FONT_W, cy * FONT_H, FONT_W, FONT_H, C_BLACK);
        }
        break;
    case '\t':
        do {
            screen_putc(' ');
        } while (cx % 8 != 0);
        return;
    default: {
        int px = cx * FONT_W;
        int py = cy * FONT_H;
        gfx_char(px, py, (unsigned char)c, color);
        cx++;
        if (cx >= cols) {
            cx = 0;
            cy++;
        }
        break;
    }
    }

    if (cy >= rows)
        screen_scroll();
}

/* ------------------------------------------------------------------ */
/* Public console API                                                 */
/* ------------------------------------------------------------------ */
void console_init(void)
{
    serial_init();
    cols = gfx_width() / FONT_W;
    rows = gfx_height() / FONT_H;
    cx = cy = 0;
    color = default_color;
    console_target = NULL;
}

void console_set_target(struct textbuf *tb)
{
    console_target = tb;
}

void console_clear(void)
{
    if (console_target) {
        textbuf_clear(console_target);
        return;
    }
    gfx_clear(C_BLACK);
    cx = cy = 0;
}

void console_putc(char c)
{
    serial_putc(c);

    if (console_target) {
        textbuf_putc(console_target, c);
        return;
    }
    if (!gfx_available())
        return;
    screen_putc(c);
}

void console_write(const char *s)
{
    while (*s)
        console_putc(*s++);
}

void console_set_color(u8 c)
{
    color = c;
    if (console_target)
        console_target->color = c;
}

u8 console_get_color(void)
{
    return color;
}

void console_set_default_color(u8 c)
{
    default_color = c;
    color = c;
}

void console_cursor_hide(int hide)
{
    (void)hide;
}

/* ------------------------------------------------------------------ */
/* Tiny printf: %s %c %d %i %u %x %X %p %%  (with 0-padded width)      */
/* ------------------------------------------------------------------ */
static int format(char *out, u32 size, const char *fmt, va_list ap)
{
    u32 written = 0;

    #define PUT(ch) do { if (written + 1 < size) out[written] = (ch); written++; } while (0)

    while (*fmt) {
        if (*fmt != '%') {
            PUT(*fmt++);
            continue;
        }
        fmt++;

        int pad_zero = 0, width = 0, left = 0;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { pad_zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        char tmp[192];
        int len = 0;

        switch (*fmt) {
        case 'd':
        case 'i': {
            i32 v = va_arg(ap, i32);
            u32 u = (v < 0) ? (u32)(-(i32)v) : (u32)v;
            if (v < 0) PUT('-');
            char rev[16];
            int n = 0;
            do { rev[n++] = (char)('0' + (u % 10)); u /= 10; } while (u);
            while (n) tmp[len++] = rev[--n];
            break;
        }
        case 'u': {
            u32 u = va_arg(ap, u32);
            char rev[16];
            int n = 0;
            do { rev[n++] = (char)('0' + (u % 10)); u /= 10; } while (u);
            while (n) tmp[len++] = rev[--n];
            break;
        }
        case 'x':
        case 'X': {
            u32 u = va_arg(ap, u32);
            const char *digits = (*fmt == 'x') ? "0123456789abcdef"
                                               : "0123456789ABCDEF";
            char rev[16];
            int n = 0;
            do { rev[n++] = digits[u & 0xF]; u >>= 4; } while (u);
            while (n) tmp[len++] = rev[--n];
            break;
        }
        case 'c':
            tmp[len++] = (char)va_arg(ap, int);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && len < 191) tmp[len++] = *s++;
            break;
        }
        case '%':
            tmp[len++] = '%';
            break;
        case '\0':
            continue;
        default:
            tmp[len++] = '%';
            tmp[len++] = *fmt;
            break;
        }
        fmt++;

        if (!left)
            for (int i = len; i < width; i++) PUT(pad_zero ? '0' : ' ');
        for (int i = 0; i < len; i++) PUT(tmp[i]);
        if (left)
            for (int i = len; i < width; i++) PUT(' ');
    }
    if (size) out[written < size ? written : size - 1] = '\0';
    #undef PUT
    return (int)written;
}

int kprintf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = format(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    console_write(buf);
    return n;
}

void serial_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    format(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (const char *p = buf; *p; p++)
        serial_putc(*p);
}

int ksnprintf(char *buf, u32 size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = format(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
