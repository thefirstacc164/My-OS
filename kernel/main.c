/*
 * MyOS - kernel main.
 *
 * Brings the machine up in graphics mode, prints a short boot log, then
 * hands over to the desktop.
 */
#include "kernel.h"
#include "gfx.h"

/* draw text with the 8x16 font magnified by an integer scale */
static void big_text(int x, int y, const char *s, int scale, u8 color)
{
    while (*s) {
        const unsigned char *g = font8x16 + (unsigned char)(*s) * 16;
        for (int row = 0; row < 16; row++) {
            u8 bits = g[row];
            if (!bits)
                continue;
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col))
                    gfx_fill(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
        x += 8 * scale;
        s++;
    }
}

static int big_width(const char *s, int scale)
{
    int n = 0;
    while (s[n])
        n++;
    return n * 8 * scale;
}

/* the boot splash: logo, tagline and a progress bar */
static void splash(void)
{
    int w = gfx_width(), h = gfx_height();

    gfx_vgradient(0, 0, w, h, C_GRADIENT_LAST, C_GRADIENT_FIRST);
    gfx_fill(0, 0, w, h, C_DESK_DEEP);
    gfx_vgradient(0, 0, w, h, C_GRADIENT_FIRST, C_GRADIENT_LAST);

    big_text((w - big_width("MyOS", 4)) / 2, h / 2 - 90, "MyOS", 4, C_ACCENT);
    gfx_hline(w / 2 - 90, h / 2 - 14, 180, C_BORDER);

    const char *tag = "0.3  -  full desktop";
    gfx_text((w - gfx_text_width(tag)) / 2, h / 2 + 2, tag, C_TEXT_DIM);

    int bar_w = 300, bar_x = (w - bar_w) / 2, bar_y = h / 2 + 60;
    gfx_frame(bar_x - 1, bar_y - 1, bar_w + 2, 8, C_BORDER);

    const char *status = "starting desktop";
    gfx_text((w - gfx_text_width(status)) / 2, bar_y + 20, status, C_TEXT_MUTED);

    for (int step = 0; step <= 30; step++) {
        gfx_fill(bar_x, bar_y, bar_w * step / 30, 6, C_ACCENT);
        gfx_present();
        timer_sleep_ms(18);
    }

    timer_sleep_ms(220);
}

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
    kprintf("\n  MyOS 0.3 \"Full desktop\"\n");
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

    if (ata_init()) {
        ksnprintf(line, sizeof(line), "ATA disk, %u KB",
                  (u32)((ata_sectors() * 512) / 1024));
        ok_line(line);
    } else {
        ok_line("no ATA disk, files stay in RAM");
    }

    fs_init();
    if (ata_present() && fs_load() > 0) {
        ok_line("file system restored from disk");
    } else if (ata_present()) {
        ok_line("new file system written to disk");
        fs_save();
    } else {
        ok_line("in-memory file system");
    }

    interrupts_enable();
    ok_line("interrupts enabled");

    console_set_color(C_TEXT_MUTED);
    kprintf("\n  starting desktop...\n");
    timer_sleep_ms(150);

    splash();

    wm_run();
}
