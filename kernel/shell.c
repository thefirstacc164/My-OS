/*
 * MyOS - the shell.
 *
 * The commands themselves are plain C functions that print through
 * kprintf(), so the same code works in the terminal window and over the
 * serial port.
 */
#include "kernel.h"
#include "gfx.h"

#define LINE_MAX 120

void shell_banner(void)
{
    console_set_color(C_ACCENT);
    kprintf("  __  __        ___  ____  \n");
    kprintf(" |  \\/  |_   _ / _ \\/ ___| \n");
    kprintf(" | |\\/| | | | | | | \\___ \\ \n");
    kprintf(" | |  | | |_| | |_| |___) |\n");
    kprintf(" |_|  |_|\\__, |\\___/|____/ \n");
    kprintf("         |___/             \n");
    console_set_color(C_TEXT);
}

/* ------------------------------------------------------------------ */
/* Commands                                                           */
/* ------------------------------------------------------------------ */
static void cmd_help(char *args)
{
    (void)args;
    console_set_color(C_ACCENT);
    kprintf("Commands\n");
    console_set_color(C_TEXT);
    kprintf("  help              this list\n");
    kprintf("  about             what MyOS is\n");
    kprintf("  clear             wipe the terminal\n");
    kprintf("  echo <text>       print text back\n");
    kprintf("  uptime            time since boot\n");
    kprintf("  date              read the hardware clock\n");
    kprintf("  mem               physical memory map (BIOS E820)\n");
    kprintf("  cpu               processor vendor\n");
    kprintf("  ls [path]         list a directory\n");
    kprintf("  cat <file>        print a file\n");
    kprintf("  write <f> <text>  create or overwrite a file\n");
    kprintf("  rm <file>         delete a file\n");
    kprintf("  open <file>       open a file in the editor\n");
    kprintf("  logo              draw the MyOS logo\n");
    kprintf("  reboot            restart the machine\n");
    kprintf("  halt              stop the system\n");
}

static void cmd_about(char *args)
{
    (void)args;
    kprintf("MyOS is a small operating system written from scratch:\n");
    kprintf("its own boot sector, its own 32-bit kernel, its own drivers,\n");
    kprintf("its own window manager. No Linux underneath.\n\n");
    kprintf("  version   0.3 \"Full desktop\"\n");
    kprintf("  arch      i386 (32-bit x86, protected mode)\n");
    kprintf("  display   640x480, 8-bit colour, VESA framebuffer\n");
    kprintf("  input     PS/2 keyboard and mouse\n");
}

static void cmd_clear(char *args)
{
    (void)args;
    console_clear();
}

static void cmd_echo(char *args)
{
    while (*args == ' ')
        args++;
    kprintf("%s\n", args);
}

static void cmd_uptime(char *args)
{
    (void)args;
    u32 total = timer_uptime_seconds();
    kprintf("up %u minutes and %u seconds (%u ticks at 100 Hz)\n",
            total / 60, total % 60, timer_ticks());
}

static void cmd_date(char *args)
{
    (void)args;
    u32 year, month, day, hour, minute, second;
    rtc_read(&year, &month, &day, &hour, &minute, &second);
    kprintf("%u-%02u-%02u %02u:%02u:%02u\n", year, month, day, hour, minute, second);
}

static void cmd_mem(char *args)
{
    (void)args;
    u16 count = e820_entry_count();
    struct e820_entry *map = e820_entries();

    console_set_color(C_ACCENT);
    kprintf("  start        end          size      type\n");
    console_set_color(C_TEXT);

    for (u16 i = 0; i < count && i < E820_MAX; i++) {
        u64 start = map[i].base;
        u64 end = map[i].base + map[i].length;
        u32 kb = (u32)(map[i].length >> 10);
        const char *type = "unknown";

        if (map[i].type == 1) type = "usable";
        else if (map[i].type == 2) type = "reserved";
        else if (map[i].type == 3) type = "ACPI reclaim";
        else if (map[i].type == 4) type = "ACPI NVS";

        kprintf("  %08x%08x %08x%08x %8uK  %s\n",
                (u32)(start >> 32), (u32)start,
                (u32)(end >> 32), (u32)end, kb, type);
    }
    kprintf("\n  usable memory: %u MB\n", (u32)(memory_total_bytes() >> 20));
}

static void cmd_cpu(char *args)
{
    (void)args;
    u32 a, b, c, d;
    char vendor[13];

    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0u), "c"(0u));
    memcpy(vendor + 0, &b, 4);
    memcpy(vendor + 4, &d, 4);
    memcpy(vendor + 8, &c, 4);
    vendor[12] = '\0';

    kprintf("vendor: %s\n", vendor);
    kprintf("mode:   32-bit protected mode, flat 4 GB segments\n");
}

static void cmd_ls(char *args)
{
    while (*args == ' ')
        args++;

    int dir = (*args == '\0') ? fs_resolve("/home") : fs_resolve(args);
    if (dir < 0 || !fs_get(dir) || !fs_get(dir)->is_dir) {
        kprintf("not a directory\n");
        return;
    }

    char path[96];
    fs_path(dir, path, sizeof(path));
    console_set_color(C_ACCENT);
    kprintf("%s\n", path);
    console_set_color(C_TEXT);

    int count = fs_child_count(dir);
    for (int i = 0; i < count; i++) {
        struct fs_node *n = fs_get(fs_child(dir, i));
        if (!n)
            continue;
        if (n->is_dir)
            kprintf("  %s/\n", n->name);
        else
            kprintf("  %-20s %u bytes\n", n->name, (u32)n->size);
    }
    if (count == 0)
        kprintf("  (empty)\n");
}

static void cmd_cat(char *args)
{
    while (*args == ' ')
        args++;

    int node = fs_resolve(args);
    struct fs_node *n = fs_get(node);
    if (!n) {
        kprintf("no such file: %s\n", args);
        return;
    }
    if (n->is_dir) {
        kprintf("%s is a directory\n", args);
        return;
    }
    kprintf("%s", n->data);
    if (n->size > 0 && n->data[n->size - 1] != '\n')
        kprintf("\n");
}

static void cmd_write(char *args)
{
    while (*args == ' ')
        args++;

    char path[96];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < 95) {
        path[i] = args[i];
        i++;
    }
    path[i] = '\0';

    if (path[0] == '\0') {
        kprintf("usage: write <file> <text>\n");
        return;
    }

    while (args[i] == ' ')
        i++;
    const char *text = args + i;

    int node = fs_resolve(path);
    if (node < 0) {
        char dirpath[96];
        int j = 0;
        int last_slash = -1;
        while (path[j]) {
            dirpath[j] = path[j];
            if (path[j] == '/')
                last_slash = j;
            j++;
        }
        dirpath[j] = '\0';

        char name[40];
        int k = 0;
        for (int t = last_slash + 1; path[t] && k < 39; t++)
            name[k++] = path[t];
        name[k] = '\0';

        /* keep only the directory part of the path */
        if (last_slash <= 0)
            strcpy_s(dirpath, sizeof(dirpath), "/");
        else
            dirpath[last_slash] = '\0';

        int dir = fs_resolve(dirpath);
        if (dir < 0) {
            kprintf("no such directory: %s\n", dirpath);
            return;
        }
        node = fs_create(dir, name, 0);
        if (node < 0) {
            kprintf("could not create %s\n", path);
            return;
        }
    }

    fs_write(node, text);
    kprintf("wrote %u bytes to %s\n", (u32)strlen(text), path);
}

static void cmd_rm(char *args)
{
    while (*args == ' ')
        args++;
    int node = fs_resolve(args);
    if (node <= 0) {
        kprintf("no such file: %s\n", args);
        return;
    }
    fs_remove(node);
    kprintf("removed %s\n", args);
}

static void cmd_open(char *args)
{
    while (*args == ' ')
        args++;
    if (*args == '\0') {
        kprintf("usage: open <file>\n");
        return;
    }
    if (fs_resolve(args) < 0) {
        kprintf("no such file: %s\n", args);
        return;
    }
    app_open_file(args);
    kprintf("opening %s\n", args);
}

static void cmd_reboot(char *args)
{
    (void)args;
    kprintf("rebooting...\n");
    timer_sleep_ms(300);

    while (inb(0x64) & 0x02)
        ;
    outb(0x64, 0xFE);

    for (;;)
        cpu_halt();
}

static void cmd_halt(char *args)
{
    (void)args;
    kprintf("System halted. It is safe to close this tab.\n");
    interrupts_disable();
    for (;;)
        cpu_halt();
}

static void cmd_disk(char *args)
{
    (void)args;
    if (!ata_present()) {
        console_set_color(C_WARN);
        kprintf("No ATA disk attached. Files live in RAM only.\n");
        console_set_color(C_TEXT);
        return;
    }
    kprintf("ATA disk    %u KB (%u sectors)\n",
            (u32)((ata_sectors() * 512) / 1024), ata_sectors());
    kprintf("saved image %u bytes per save\n", (u32)65536);
    int count = fs_sync();
    if (count >= 0)
        kprintf("synced      %d file system nodes written\n", count);
    else
        kprintf("sync        failed\n");
}

static void cmd_format(char *args)
{
    (void)args;
    if (!ata_present()) {
        console_set_color(C_WARN);
        kprintf("No disk to format.\n");
        console_set_color(C_TEXT);
        return;
    }
    console_set_color(C_WARN);
    kprintf("Wiping the disk and restoring the built-in files...\n");
    fs_format();
    console_set_color(C_ACCENT);
    kprintf("done.\n");
    console_set_color(C_TEXT);
}

static void cmd_logo(char *args)
{
    (void)args;
    shell_banner();
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                           */
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
    { "ls",     cmd_ls     },
    { "cat",    cmd_cat    },
    { "write",  cmd_write  },
    { "rm",     cmd_rm     },
    { "open",   cmd_open   },
    { "logo",   cmd_logo   },
    { "disk",   cmd_disk   },
    { "sync",   cmd_disk   },
    { "format", cmd_format },
    { "reboot", cmd_reboot },
    { "halt",   cmd_halt   },
};

#define COMMAND_COUNT ((int)(sizeof(commands) / sizeof(commands[0])))

void shell_prompt(void)
{
    console_set_color(C_ACCENT);
    kprintf("myos");
    console_set_color(C_TEXT);
    kprintf("> ");
}

void shell_execute_line(char *line)
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

    for (int i = 0; i < COMMAND_COUNT; i++) {
        if (strcmp(line, commands[i].name) == 0) {
            commands[i].run(args);
            return;
        }
    }

    console_set_color(C_RED);
    kprintf("Unknown command: %s\n", line);
    console_set_color(C_TEXT);
    kprintf("Type 'help' for the command list.\n");
}
