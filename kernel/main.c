/*
 * MyOS - kernel main.
 *
 * Brings the system up: console, interrupt controller, timer, keyboard,
 * and then hands control to the shell.
 */
#include "kernel.h"

/* The boot sector stored the BIOS memory map at 0x5000. */
#define E820_COUNT_ADDR 0x5000
#define E820_MAP_ADDR   0x5004

u16 e820_entry_count(void)
{
    return *(volatile u16 *)E820_COUNT_ADDR;
}

struct e820_entry *e820_entries(void)
{
    return (struct e820_entry *)E820_MAP_ADDR;
}

u64 memory_total_bytes(void)
{
    u64 total = 0;
    u16 count = e820_entry_count();
    struct e820_entry *map = e820_entries();

    for (u16 i = 0; i < count && i < E820_MAX; i++) {
        if (map[i].type == 1)
            total += map[i].length;
    }
    return total;
}

static void ok_line(const char *text)
{
    console_set_color(VGA_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK));
    kprintf("  [ OK ] ");
    console_set_color(VGA_COLOR(COLOR_LIGHT_GREY, COLOR_BLACK));
    kprintf("%s\n", text);
}

void kmain(void)
{
    console_init();
    console_clear();
    console_set_default_color(VGA_COLOR(COLOR_LIGHT_GREY, COLOR_BLACK));

    kprintf("\n");
    shell_banner();

    console_set_color(VGA_COLOR(COLOR_YELLOW, COLOR_BLACK));
    kprintf("  MyOS 0.1 \"First Light\"");
    console_set_color(VGA_COLOR(COLOR_DARK_GREY, COLOR_BLACK));
    kprintf("  -  a hand-written operating system\n");
    console_set_color(VGA_COLOR(COLOR_LIGHT_GREY, COLOR_BLACK));
    kprintf("\n");

    ok_line("VGA text console at 80x25");
    ok_line("serial port COM1 ready");

    u32 megabytes = (u32)(memory_total_bytes() >> 20);
    console_set_color(VGA_COLOR(COLOR_LIGHT_GREEN, COLOR_BLACK));
    kprintf("  [ OK ] ");
    console_set_color(VGA_COLOR(COLOR_LIGHT_GREY, COLOR_BLACK));
    kprintf("%u MB of RAM detected\n", megabytes);

    idt_init();
    ok_line("interrupt descriptor table installed");

    timer_init(100);
    ok_line("programmable interval timer at 100 Hz");

    keyboard_init();
    ok_line("PS/2 keyboard driver loaded");

    interrupts_enable();
    ok_line("interrupts enabled");

    kprintf("\n  Kernel is running in 32-bit protected mode.\n");
    kprintf("  Type 'help' and press Enter to begin.\n\n");

    shell_run();
}
