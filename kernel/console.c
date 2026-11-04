/*
 * MyOS - VGA text console (80x25) + serial mirror for debugging.
 *
 * Everything printed on screen is also sent out of the first serial port,
 * which is how the automated test suite reads the kernel's output.
 */
#include <stdarg.h>
#include "kernel.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define SERIAL_PORT 0x3F8

static volatile u16 *const vga = (volatile u16 *)0xB8000;
static int cx, cy;
static u8 attr = VGA_COLOR(COLOR_LIGHT_GREY, COLOR_BLACK);
static u8 default_attr = VGA_COLOR(COLOR_LIGHT_GREY, COLOR_BLACK);

/* ------------------------------------------------------------------ */
/* Serial port (COM1)                                                 */
/* ------------------------------------------------------------------ */
void serial_init(void)
{
    outb(SERIAL_PORT + 1, 0x00);   /* disable interrupts            */
    outb(SERIAL_PORT + 3, 0x80);   /* enable DLAB (set baud rate)   */
    outb(SERIAL_PORT + 0, 0x01);   /* divisor 1 => 115200 baud      */
    outb(SERIAL_PORT + 1, 0x00);
    outb(SERIAL_PORT + 3, 0x03);   /* 8 bits, no parity, one stop   */
    outb(SERIAL_PORT + 2, 0xC7);   /* enable + clear FIFO           */
    outb(SERIAL_PORT + 4, 0x0B);   /* IRQs off, RTS/DSR set         */
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
/* Hardware cursor                                                    */
/* ------------------------------------------------------------------ */
static void update_cursor(void)
{
    u16 pos = (u16)(cy * VGA_WIDTH + cx);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)((pos >> 8) & 0xFF));
}

void console_cursor_hide(int hide)
{
    outb(0x3D4, 0x0A);
    outb(0x3D5, hide ? 0x20 : 0x0E);
}

/* ------------------------------------------------------------------ */
/* Screen primitives                                                  */
/* ------------------------------------------------------------------ */
void console_clear(void)
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        vga[i] = (u16)((attr << 8) | ' ');
    cx = cy = 0;
    update_cursor();
}

static void scroll(void)
{
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
        vga[i] = vga[i + VGA_WIDTH];
    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
        vga[i] = (u16)((attr << 8) | ' ');
    cy = VGA_HEIGHT - 1;
}

void console_putc(char c)
{
    serial_putc(c);

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
            vga[cy * VGA_WIDTH + cx] = (u16)((attr << 8) | ' ');
        }
        break;
    case '\t':
        do {
            console_putc(' ');
        } while (cx % 8 != 0);
        return;
    default:
        vga[cy * VGA_WIDTH + cx] = (u16)((attr << 8) | (u8)c);
        cx++;
        if (cx >= VGA_WIDTH) {
            cx = 0;
            cy++;
        }
        break;
    }

    if (cy >= VGA_HEIGHT)
        scroll();
    update_cursor();
}

void console_write(const char *s)
{
    while (*s)
        console_putc(*s++);
}

void console_set_color(u8 color)
{
    attr = color;
}

u8 console_get_color(void)
{
    return attr;
}

void console_set_default_color(u8 color)
{
    default_attr = color;
    attr = color;
}

void console_init(void)
{
    serial_init();
    attr = default_attr;
    console_clear();
    console_cursor_hide(0);
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

        int pad_zero = 0, width = 0;
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

        for (int i = len; i < width; i++) PUT(pad_zero ? '0' : ' ');
        for (int i = 0; i < len; i++) PUT(tmp[i]);
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
