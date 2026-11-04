/*
 * MyOS - the interactive shell.
 *
 * This is what you actually talk to once the system is up: a tiny
 * read-eval-print loop with a handful of built-in commands.
 */
#include "kernel.h"

#define LINE_MAX 120

static void cmd_help(char *args);

/* ------------------------------------------------------------------ */
/* Boot banner                                                        */
/* ------------------------------------------------------------------ */
void shell_banner(void)
{
    u8 old = console_get_color();

    console_set_color(VGA_COLOR(COLOR_LIGHT_CYAN, COLOR_BLACK));
    kprintf("  __  __        ___  ____  \n");
    kprintf(" |  \\/  |_   _ / _ \\/ ___| \n");
    kprintf(" | |\\/| | | | | | | \\___ \\ \n");
    kprintf(" | |  | | |_| | |_| |___) |\n");
    kprintf(" |_|  |_|\\__, |\\___/|____/ \n");
    kprintf("         |___/             \n");
    console_set_color(old);
    kprintf("\n");
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                      */
/* ------------------------------------------------------------------ */
static void ok_line(const char *text)
{
    console_set_color(VGA_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK));
    kprintf("  [ OK ] ");
    console_set_color(VGA_COLOR(COLOR_LIGHT_GREY, COLOR_BLACK));
    kprintf("%s\n", text);
}

static void print_prompt(void)
{
    console_set_color(VGA_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK));
    kprintf("myos");
    console_set_color(VGA_COLOR(COLOR_LIGHT_GREY, COLOR_BLACK));
    kprintf("> ");
}

static int read_line(char *buffer, int max)
{
    int length = 0;

    for (;;) {
        int c = keyboard_getchar_blocking();

        if (c == '\n' || c == '\r') {
            console_putc('\n');
            buffer[length] = '\0';
            return length;
        }
        if (c == '\b') {
            if (length > 0) {
                length--;
                console_putc('\b');
            }
            continue;
        }
        if (c >= 32 && c < 127 && length < max - 1) {
            buffer[length++] = (char)c;
            console_putc((char)c);
        }
    }
}

static char *skip_spaces(char *s)
{
    while (*s == ' ')
        s++;
    return s;
}

/* ------------------------------------------------------------------ */
/* Commands                                                           */
/* ------------------------------------------------------------------ */
static void cmd_help(char *args)
{
    (void)args;
    console_set_color(VGA_COLOR(COLOR_LIGHT_CYAN, COLOR_BLACK));
    kprintf("Commands\n");
    console_set_color(VGA_COLOR(COLOR_LIGHT_GREY, COLOR_BLACK));
    kprintf("  help            this list\n");
    kprintf("  about           what MyOS is\n");
    kprintf("  clear           wipe the screen\n");
    kprintf("  echo <text>     print text back\n");
    kprintf("  uptime          time since boot\n");
    kprintf("  date            read the hardware clock\n");
    kprintf("  mem             physical memory map (BIOS E820)\n");
    kprintf("  cpu             processor vendor and model\n");
    kprintf("  theme <0-15>    change the text colour\n");
    kprintf("  beep            beep the PC speaker\n");
    kprintf("  snake           play Snake (arrows + q)\n");
    kprintf("  logo            draw the MyOS logo\n");
    kprintf("  reboot          restart the machine\n");
    kprintf("  halt            stop the system\n");
}

static void cmd_about(char *args)
{
    (void)args;
    kprintf("MyOS is a small operating system written from scratch:\n");
    kprintf("its own boot sector, its own 32-bit kernel, its own drivers.\n");
    kprintf("It talks straight to the hardware - no Linux underneath.\n\n");
    kprintf("  version   0.1 \"First Light\"\n");
    kprintf("  arch      i386 (32-bit x86, protected mode)\n");
    kprintf("  display   VGA text mode 80x25\n");
    kprintf("  built     with gcc, GNU as and ld\n");
}

static void cmd_clear(char *args)
{
    (void)args;
    console_clear();
}

static void cmd_echo(char *args)
{
    kprintf("%s\n", skip_spaces(args));
}

static void cmd_uptime(char *args)
{
    (void)args;
    u32 total = timer_uptime_seconds();
    kprintf("up %u minutes and %u seconds (%u timer ticks)\n",
            total / 60, total % 60, timer_ticks());
}

static void cmd_date(char *args)
{
    (void)args;
    u32 year, month, day, hour, minute, second;
    rtc_read(&year, &month, &day, &hour, &minute, &second);
    kprintf("%u-%02u-%02u %02u:%02u:%02u (hardware clock)\n",
            year, month, day, hour, minute, second);
}

static void cmd_mem(char *args)
{
    (void)args;
    u16 count = e820_entry_count();
    struct e820_entry *map = e820_entries();

    console_set_color(VGA_COLOR(COLOR_LIGHT_CYAN, COLOR_BLACK));
    kprintf("  start        end          size      type\n");
    console_set_color(VGA_COLOR(COLOR_LIGHT_GREY, COLOR_BLACK));

    for (u16 i = 0; i < count && i < E820_MAX; i++) {
        u64 start = map[i].base;
        u64 end   = map[i].base + map[i].length;
        u32 kb    = (u32)(map[i].length >> 10);
        const char *type;

        switch (map[i].type) {
        case 1:  type = "usable";        break;
        case 2:  type = "reserved";      break;
        case 3:  type = "ACPI reclaim";  break;
        case 4:  type = "ACPI NVS";      break;
        default: type = "unknown";       break;
        }

        kprintf("  %08x%08x %08x%08x %8uK  %s\n",
                (u32)(start >> 32), (u32)start,
                (u32)(end >> 32), (u32)end, kb, type);
    }
    kprintf("\n  usable memory: %u MB\n", (u32)(memory_total_bytes() >> 20));
}

static void cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(0));
}

static void cmd_cpu(char *args)
{
    (void)args;
    u32 a, b, c, d;
    char vendor[13];
    char brand[49];

    cpuid(0, &a, &b, &c, &d);
    memcpy(vendor + 0, &b, 4);
    memcpy(vendor + 4, &d, 4);
    memcpy(vendor + 8, &c, 4);
    vendor[12] = '\0';

    kprintf("vendor: %s\n", vendor);

    cpuid(0x80000000, &a, &b, &c, &d);
    if (a >= 0x80000004) {
        for (u32 i = 0; i < 3; i++) {
            cpuid(0x80000002 + i, &a, &b, &c, &d);
            memcpy(brand + i * 16 + 0, &a, 4);
            memcpy(brand + i * 16 + 4, &b, 4);
            memcpy(brand + i * 16 + 8, &c, 4);
            memcpy(brand + i * 16 + 12, &d, 4);
        }
        brand[48] = '\0';
        kprintf("model:  %s\n", skip_spaces(brand));
    }
    kprintf("mode:   32-bit protected mode, flat 4 GB segments\n");
}

static void cmd_theme(char *args)
{
    char *value = skip_spaces(args);
    if (*value == '\0') {
        kprintf("current colour: %u (try: theme 10)\n", console_get_color() & 0x0F);
        return;
    }
    int n = atoi(value);
    if (n < 0 || n > 15) {
        kprintf("colour must be between 0 and 15\n");
        return;
    }
    console_set_default_color(VGA_COLOR((u8)n, COLOR_BLACK));
    kprintf("colour changed to %d\n", n);
}

static void cmd_beep(char *args)
{
    (void)args;
    u32 divisor = 1193182 / 880;

    outb(0x43, 0xB6);
    outb(0x42, (u8)(divisor & 0xFF));
    outb(0x42, (u8)((divisor >> 8) & 0xFF));
    outb(0x61, (u8)(inb(0x61) | 0x03));

    timer_sleep_ms(180);

    outb(0x61, (u8)(inb(0x61) & 0xFC));
    kprintf("beep\n");
}

static void cmd_snake(char *args)
{
    (void)args;
    game_snake();
}

static void cmd_logo(char *args)
{
    (void)args;
    shell_banner();
}

static void cmd_reboot(char *args)
{
    (void)args;
    kprintf("rebooting...\n");
    timer_sleep_ms(200);

    while (inb(0x64) & 0x02)
        ;
    outb(0x64, 0xFE);          /* ask the keyboard controller to reset */

    for (;;)
        cpu_halt();
}

static void cmd_halt(char *args)
{
    (void)args;
    kprintf("System halted. It is now safe to close this tab.\n");
    interrupts_disable();
    for (;;)
        cpu_halt();
}

/* ------------------------------------------------------------------ */
/* Command table and main loop                                        */
/* ------------------------------------------------------------------ */
struct command {
    const char *name;
    void (*run)(char *args);
};

static const struct command commands[] = {
    { "help",   cmd_help   },
    { "about",  cmd_about  },
    { "clear",  cmd_clear  },
    { "echo",   cmd_echo   },
    { "uptime", cmd_uptime },
    { "date",   cmd_date   },
    { "mem",    cmd_mem    },
    { "cpu",    cmd_cpu    },
    { "theme",  cmd_theme  },
    { "beep",   cmd_beep   },
    { "snake",  cmd_snake  },
    { "logo",   cmd_logo   },
    { "reboot", cmd_reboot },
    { "halt",   cmd_halt   },
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(commands[0]))

static void execute(char *line)
{
    while (*line == ' ')
        line++;
    if (*line == '\0')
        return;

    char *args = line;
    while (*args && *args != ' ')
        args++;
    if (*args) {
        *args = '\0';
        args++;
    }

    for (u32 i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(line, commands[i].name) == 0) {
            commands[i].run(args);
            return;
        }
    }

    console_set_color(VGA_COLOR(COLOR_LIGHT_RED, COLOR_BLACK));
    kprintf("Unknown command: %s\n", line);
    console_set_color(VGA_COLOR(COLOR_LIGHT_GREY, COLOR_BLACK));
    kprintf("Type 'help' to see what MyOS understands.\n");
}

void shell_run(void)
{
    char line[LINE_MAX];

    for (;;) {
        print_prompt();
        read_line(line, LINE_MAX);
        execute(line);
    }
}
