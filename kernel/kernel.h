/*
 * MyOS - shared types, port I/O helpers and prototypes.
 */
#ifndef MYOS_KERNEL_H
#define MYOS_KERNEL_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed   int       i32;

#define NULL ((void *)0)

/* ---- port I/O ---------------------------------------------------- */
static inline void outb(u16 port, u8 value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline u8 inb(u16 port)
{
    u8 ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void)
{
    outb(0x80, 0);
}

static inline void interrupts_enable(void)
{
    __asm__ volatile("sti");
}

static inline void interrupts_disable(void)
{
    __asm__ volatile("cli");
}

static inline void cpu_halt(void)
{
    __asm__ volatile("hlt");
}

static inline void cpu_wait_interrupt(void)
{
    __asm__ volatile("sti; hlt");
}

/* ---- string.c ---------------------------------------------------- */
void *memset(void *dst, int c, u32 n);
void *memcpy(void *dst, const void *src, u32 n);
u32   strlen(const char *s);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, u32 n);
int   atoi(const char *s);

/* ---- console.c --------------------------------------------------- */
void console_init(void);
void console_clear(void);
void console_putc(char c);
void console_write(const char *s);
void console_set_color(u8 color);
u8   console_get_color(void);
void console_set_default_color(u8 color);
void console_cursor_hide(int hide);
int  kprintf(const char *fmt, ...);
void serial_init(void);
void serial_putc(char c);

/* ---- idt.c ------------------------------------------------------- */
void idt_init(void);
void irq_install(u8 irq, void (*handler)(void));

/* ---- pit.c ------------------------------------------------------- */
void timer_init(u32 hz);
u32  timer_ticks(void);
u32  timer_uptime_seconds(void);
void timer_sleep_ms(u32 ms);

/* ---- keyboard.c -------------------------------------------------- */
void keyboard_init(void);
int  keyboard_getchar(void);
int  keyboard_getchar_blocking(void);
int  keyboard_getkey(void);            /* returns special key codes too */

/* ---- rtc.c ------------------------------------------------------- */
void rtc_read(u32 *year, u32 *month, u32 *day,
              u32 *hour, u32 *minute, u32 *second);

/* ---- memory map (filled in by the boot sector at 0x5000) ---------- */
struct e820_entry {
    u64 base;
    u64 length;
    u32 type;
    u32 acpi;
} __attribute__((packed));

#define E820_MAX 64
u16 e820_entry_count(void);
struct e820_entry *e820_entries(void);
u64 memory_total_bytes(void);

/* ---- special keys ------------------------------------------------ */
#define KEY_UP      0x100
#define KEY_DOWN    0x101
#define KEY_LEFT    0x102
#define KEY_RIGHT   0x103
#define KEY_PGUP    0x104
#define KEY_PGDN    0x105
#define KEY_DELETE  0x106

/* ---- game.c ------------------------------------------------------ */
void game_snake(void);

/* ---- shell.c ----------------------------------------------------- */
void shell_run(void);
void shell_banner(void);

#endif

/* ---- VGA text colours -------------------------------------------- */
#define COLOR_BLACK        0
#define COLOR_BLUE         1
#define COLOR_GREEN        2
#define COLOR_CYAN         3
#define COLOR_RED          4
#define COLOR_MAGENTA      5
#define COLOR_BROWN        6
#define COLOR_LIGHT_GREY   7
#define COLOR_DARK_GREY    8
#define COLOR_LIGHT_BLUE   9
#define COLOR_LIGHT_GREEN  10
#define COLOR_LIGHT_CYAN   11
#define COLOR_LIGHT_RED    12
#define COLOR_LIGHT_MAGENTA 13
#define COLOR_YELLOW       14
#define COLOR_WHITE        15

#define VGA_COLOR(fg, bg)  ((u8)(((bg) << 4) | ((fg) & 0x0F)))
