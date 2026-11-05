/*
 * MyOS - 640x480 8-bit framebuffer graphics.
 *
 * The boot sector asks the BIOS (VBE) for a linear framebuffer and leaves
 * the mode information block at 0x4800. Everything is drawn into a
 * back buffer in low memory and copied to the video card in one go, which
 * keeps window dragging free of flicker.
 */
#include "gfx.h"

#define VBE_INFO_ADDR 0x4800

/* offsets inside the VBE mode information block */
#define VBE_BYTES_PER_SCANLINE 0x10
#define VBE_X_RES              0x12
#define VBE_Y_RES              0x14
#define VBE_BITS_PER_PIXEL     0x19
#define VBE_PHYS_BASE          0x28

#define BACKBUFFER_ADDR 0x200000u
#define MAX_WIDTH  1024
#define MAX_HEIGHT 768

static u8 *fb;              /* video memory          */
static u8 *bb;              /* back buffer           */
static u32 pitch;           /* bytes per scan line   */
static int width, height;
static int ready;

/* ------------------------------------------------------------------ */
/* Palette                                                            */
/* ------------------------------------------------------------------ */
static const u8 palette[64][3] = {
    /* 0-15: UI colours */
    { 0x00, 0x00, 0x00 },   /* black          */
    { 0xff, 0xff, 0xff },   /* white          */
    { 0xe6, 0xed, 0xf3 },   /* text           */
    { 0x9f, 0xb0, 0xc3 },   /* text dim       */
    { 0x6b, 0x7c, 0x90 },   /* text muted     */
    { 0x16, 0x20, 0x2d },   /* window         */
    { 0x0f, 0x17, 0x21 },   /* window alt     */
    { 0x1e, 0x2a, 0x38 },   /* window hi      */
    { 0x2b, 0x3d, 0x52 },   /* border         */
    { 0x1b, 0x2b, 0x3d },   /* title          */
    { 0x13, 0x1c, 0x26 },   /* title inactive */
    { 0x0b, 0x0f, 0x14 },   /* desk           */
    { 0x07, 0x0a, 0x0e },   /* desk deep      */
    { 0x3d, 0xdc, 0x97 },   /* accent green   */
    { 0x59, 0xb0, 0xff },   /* accent blue    */
    { 0xff, 0xcc, 0x66 },   /* accent amber   */
    { 0xff, 0x5a, 0x5a },   /* red            */
    { 0x04, 0x06, 0x0a },   /* shadow         */
    { 0x1f, 0x3a, 0x5c },   /* selection      */
    { 0x1f, 0x6f, 0x4f },   /* green dark     */
    { 0x1b, 0x3a, 0x5e },   /* blue dark      */
    { 0x2a, 0x34, 0x42 },   /* grey 1         */
    { 0x3c, 0x4a, 0x5c },   /* grey 2         */
    { 0x55, 0x67, 0x7d },   /* grey 3         */
    { 0xf0, 0xa3, 0x5e },   /* warn           */
    { 0xb4, 0x8e, 0xf2 },   /* purple         */
    { 0x2a, 0x3b, 0x52 },   /* icon bg        */
    { 0xd7, 0xe2, 0xee },   /* icon fg        */
    { 0x24, 0x3a, 0x55 },   /* hover          */
    { 0x4a, 0x56, 0x66 },   /* disabled       */
    /* 30-31 spare */
    { 0x10, 0x18, 0x22 },
    { 0x0d, 0x13, 0x1b },
    /* 32-63: wallpaper gradient */
    { 0x18, 0x27, 0x3f }, { 0x17, 0x26, 0x3d }, { 0x16, 0x24, 0x3b },
    { 0x15, 0x23, 0x39 }, { 0x14, 0x22, 0x37 }, { 0x13, 0x20, 0x35 },
    { 0x12, 0x1f, 0x33 }, { 0x11, 0x1d, 0x31 }, { 0x10, 0x1c, 0x2f },
    { 0x0f, 0x1a, 0x2d }, { 0x0e, 0x19, 0x2b }, { 0x0d, 0x17, 0x29 },
    { 0x0c, 0x16, 0x27 }, { 0x0c, 0x15, 0x25 }, { 0x0b, 0x13, 0x23 },
    { 0x0a, 0x12, 0x21 }, { 0x0a, 0x11, 0x1f }, { 0x09, 0x10, 0x1d },
    { 0x09, 0x0f, 0x1b }, { 0x08, 0x0e, 0x19 }, { 0x08, 0x0d, 0x17 },
    { 0x07, 0x0c, 0x15 }, { 0x07, 0x0b, 0x13 }, { 0x06, 0x0a, 0x11 },
    { 0x06, 0x09, 0x0f }, { 0x05, 0x08, 0x0d }, { 0x05, 0x07, 0x0b },
    { 0x04, 0x06, 0x09 }, { 0x04, 0x05, 0x07 }, { 0x03, 0x04, 0x05 },
    { 0x03, 0x03, 0x03 }, { 0x02, 0x02, 0x02 },
};

static void set_palette(void)
{
    for (int i = 0; i < 64; i++) {
        outb(0x3C8, (u8)i);
        outb(0x3C9, (u8)(palette[i][0] >> 2));
        outb(0x3C9, (u8)(palette[i][1] >> 2));
        outb(0x3C9, (u8)(palette[i][2] >> 2));
    }
}

/* ------------------------------------------------------------------ */
/* Initialisation                                                     */
/* ------------------------------------------------------------------ */
int gfx_init(void)
{
    volatile u16 *info = (volatile u16 *)VBE_INFO_ADDR;
    volatile u32 *base = (volatile u32 *)(VBE_INFO_ADDR + VBE_PHYS_BASE);

    u16 xres  = info[VBE_X_RES / 2];
    u16 yres  = info[VBE_Y_RES / 2];
    u8  bpp   = *(volatile u8 *)(VBE_INFO_ADDR + VBE_BITS_PER_PIXEL);
    u32 addr  = *base;
    u32 line  = info[VBE_BYTES_PER_SCANLINE / 2];

    if (bpp != 8 || xres < 320 || yres < 200 || addr == 0)
        return 0;
    if (xres > MAX_WIDTH || yres > MAX_HEIGHT)
        return 0;

    width  = xres;
    height = yres;
    pitch  = line ? line : (u32)xres;
    fb     = (u8 *)addr;
    bb     = (u8 *)BACKBUFFER_ADDR;

    set_palette();

    /* make sure the card is showing the whole frame */
    outb(0x3C4, 0x01);
    outb(0x3C5, 0x01);

    ready = 1;
    gfx_clear(C_DESK);
    gfx_present();
    return 1;
}

int gfx_available(void) { return ready; }
int gfx_width(void)     { return width; }
int gfx_height(void)    { return height; }
u8 *gfx_buffer(void)    { return bb; }

/* ------------------------------------------------------------------ */
/* Output                                                             */
/* ------------------------------------------------------------------ */
void gfx_present(void)
{
    if (!ready)
        return;

    /* The video card is emulated, so every write has a cost. Copy four
     * pixels at a time: the buffer width is always a multiple of four. */
    u8 *dst = fb;
    u8 *src = bb;
    u32 words = (u32)width / 4;

    for (int y = 0; y < height; y++) {
        volatile u32 *d = (volatile u32 *)dst;
        const u32 *s = (const u32 *)src;
        for (u32 i = 0; i < words; i++)
            d[i] = s[i];
        dst += pitch;
        src += width;
    }
}

void gfx_clear(u8 color)
{
    if (!ready)
        return;
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            bb[y * width + x] = color;
}

void gfx_pixel(int x, int y, u8 color)
{
    if (x < 0 || y < 0 || x >= width || y >= height)
        return;
    bb[y * width + x] = color;
}

void gfx_fill(int x, int y, int w, int h, u8 color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (x1 > width)  x1 = width;
    if (y1 > height) y1 = height;

    for (int yy = y0; yy < y1; yy++) {
        u8 *row = bb + yy * width;
        for (int xx = x0; xx < x1; xx++)
            row[xx] = color;
    }
}

void gfx_hline(int x, int y, int w, u8 color)
{
    gfx_fill(x, y, w, 1, color);
}

void gfx_vline(int x, int y, int h, u8 color)
{
    gfx_fill(x, y, 1, h, color);
}

void gfx_frame(int x, int y, int w, int h, u8 color)
{
    gfx_hline(x, y, w, color);
    gfx_hline(x, y + h - 1, w, color);
    gfx_vline(x, y, h, color);
    gfx_vline(x + w - 1, y, h, color);
}

void gfx_vgradient(int x, int y, int w, int h, int first, int last)
{
    for (int yy = 0; yy < h; yy++) {
        int color = first + (last - first) * yy / (h > 1 ? h - 1 : 1);
        gfx_hline(x, y + yy, w, (u8)color);
    }
}

/* ------------------------------------------------------------------ */
/* Text: 8x16 bitmap font                                             */
/* ------------------------------------------------------------------ */
void gfx_char(int x, int y, int ch, u8 color)
{
    if (ch < 0 || ch > 255)
        ch = '?';

    const unsigned char *glyph = font8x16 + (unsigned)ch * 16;
    for (int row = 0; row < 16; row++) {
        unsigned char bits = glyph[row];
        if (!bits)
            continue;
        int py = y + row;
        if (py < 0 || py >= height)
            continue;
        u8 *line = bb + py * width;
        for (int col = 0; col < 8; col++) {
            if (bits & (0x80 >> col)) {
                int px = x + col;
                if (px >= 0 && px < width)
                    line[px] = color;
            }
        }
    }
}

void gfx_text_n(int x, int y, const char *s, int max_chars, u8 color)
{
    while (*s && max_chars-- > 0) {
        gfx_char(x, y, (unsigned char)*s++, color);
        x += 8;
    }
}

void gfx_text(int x, int y, const char *s, u8 color)
{
    while (*s) {
        gfx_char(x, y, (unsigned char)*s++, color);
        x += 8;
    }
}

int gfx_text_width(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n * 8;
}
