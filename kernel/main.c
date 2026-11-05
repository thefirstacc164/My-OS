/*
 * MyOS - kernel main.
 *
 * Brings the machine up in graphics mode, prints a short boot log, then
 * hands over to the desktop.
 */
#include "kernel.h"
#include "gfx.h"

static void ok_line(const char *text)
{
    console_set_color(C_ACCENT);
    kprintf("  [ ok ] ");
    console_set_color(C_TEXT_DIM);
    kprintf("%s\n", text);
}

void kmain(void)
{
    serial_init();

    if (!gfx_init()) {
        serial_putc('X');       /* no usable framebuffer */
        for (;;)
            cpu_halt();
    }

    console_init();
    console_clear();
    console_set_default_color(C_TEXT);

    console_set_color(C_ACCENT);
    kprintf("\n  MyOS 0.2 \"Desktop\"\n");
    console_set_color(C_TEXT_MUTED);
    kprintf("  a hand-written operating system\n\n");

    ok_line("VESA framebuffer 640x480, 8-bit colour");
    ok_line("VGA font renderer online");

    u32 megabytes = (u32)(memory_total_bytes() >> 20);
    char line[64];
    ksnprintf(line, sizeof(line), "%u MB of RAM detected", megabytes);
    ok_line(line);

    idt_init();
    ok_line("interrupt descriptor table installed");

    timer_init(100);
    ok_line("programmable interval timer at 100 Hz");

    keyboard_init();
    ok_line("PS/2 keyboard driver");

    mouse_init();
    ok_line("PS/2 mouse driver");

    fs_init();
    ok_line("in-memory file system");

    interrupts_enable();
    ok_line("interrupts enabled");

    console_set_color(C_TEXT_MUTED);
    kprintf("\n  starting desktop...\n");
    timer_sleep_ms(500);

    wm_run();
}
