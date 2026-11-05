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

static inline u8 inb_status(u16 port)
{
    return inb(port);
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
void  strcpy_s(char *dst, u32 size, const char *src);
void  strcat_s(char *dst, u32 size, const char *src);

/* ---- console.c --------------------------------------------------- */
struct textbuf {
    int cols, rows;
    int cx, cy;
    char *cells;
    u8   *attrs;
    u8    color;
    int   dirty;
};

extern struct textbuf *console_target;

void console_init(void);
void console_clear(void);
void console_putc(char c);
void console_write(const char *s);
void console_set_color(u8 color);
u8   console_get_color(void);
void console_set_default_color(u8 color);
void console_cursor_hide(int hide);
void console_set_target(struct textbuf *tb);
int  kprintf(const char *fmt, ...);
int  ksnprintf(char *buf, u32 size, const char *fmt, ...);
void serial_init(void);
void serial_putc(char c);
void serial_printf(const char *fmt, ...);

void textbuf_init(struct textbuf *tb, int cols, int rows, char *cells, u8 *attrs);
void textbuf_clear(struct textbuf *tb);

/* ---- idt.c ------------------------------------------------------- */
void idt_init(void);
void irq_install(u8 irq, void (*handler)(void));

/* ---- pit.c ------------------------------------------------------- */
void timer_init(u32 hz);
u32  timer_ticks(void);
u32  timer_uptime_seconds(void);
void timer_sleep_ms(u32 ms);

/* ---- keyboard.c -------------------------------------------------- */
#define KEY_UP      0x100
#define KEY_DOWN    0x101
#define KEY_LEFT    0x102
#define KEY_RIGHT   0x103
#define KEY_PGUP    0x104
#define KEY_PGDN    0x105
#define KEY_DELETE  0x106

void keyboard_init(void);
int  keyboard_getchar(void);
int  keyboard_getchar_blocking(void);
int  keyboard_getkey(void);

/* ---- mouse.c ----------------------------------------------------- */
struct mouse_event {
    u8 type;        /* 1 = move, 2 = button down, 3 = button up */
    u8 button;
    int x, y;
};

void mouse_init(void);
int  mouse_x(void);
int  mouse_y(void);
int  mouse_buttons(void);
int  mouse_pop(struct mouse_event *out);
void mouse_set_bounds(int w, int h);

/* ---- rtc.c ------------------------------------------------------- */
void rtc_read(u32 *year, u32 *month, u32 *day,
              u32 *hour, u32 *minute, u32 *second);

/* ---- memory.c ---------------------------------------------------- */
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

/* ---- fs.c -------------------------------------------------------- */
#define FS_MAX_NODES 48
#define FS_NAME_MAX  24
#define FS_FILE_MAX  3072

struct fs_node {
    char name[FS_NAME_MAX];
    int  parent;
    int  is_dir;
    int  size;
    int  used;
    char data[FS_FILE_MAX];
};

void fs_init(void);
int  fs_root(void);
int  fs_node_count(void);
struct fs_node *fs_get(int index);
int  fs_find(int dir, const char *name);
int  fs_child_count(int dir);
int  fs_child(int dir, int nth);
int  fs_create(int dir, const char *name, int is_dir);
int  fs_remove(int index);
int  fs_write(int index, const char *data);
int  fs_path(int index, char *out, u32 size);
int  fs_resolve(const char *path);

/* ---- apps / window manager --------------------------------------- */
struct window;
struct app {
    const char *name;
    const char *label;      /* short name shown in the top bar */
    int width, height;
    void (*open)(struct window *w);
    void (*draw)(struct window *w);
    void (*key)(struct window *w, int key);
    void (*click)(struct window *w, int x, int y);
    void (*scroll)(struct window *w, int amount);
};

struct window {
    const struct app *app;
    int open;
    int x, y, w, h;
    void *state;
};

int  ui_hit(int x, int y, int rx, int ry, int rw, int rh);
void ui_button(int x, int y, int w, int h, const char *label, u8 bg, u8 fg);
void ui_text(int x, int y, int max_px, const char *s, u8 color);

void wm_run(void);
struct window *wm_open(const struct app *app);
void wm_redraw(void);
void wm_close_focused(void);

/* ---- apps.c ------------------------------------------------------ */
void app_open_file(const char *path);

/* ---- shell.c ----------------------------------------------------- */
void shell_banner(void);
void shell_prompt(void);
void shell_execute_line(char *line);

#endif
