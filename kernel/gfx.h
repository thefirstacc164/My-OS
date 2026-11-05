/*
 * MyOS - framebuffer graphics.
 */
#ifndef MYOS_GFX_H
#define MYOS_GFX_H

#include "kernel.h"

/* The desktop's fixed 8-bit palette. Every drawing routine takes one of
 * these indices instead of a raw colour. */
enum {
    C_BLACK = 0,
    C_WHITE,
    C_TEXT,         /* main text                */
    C_TEXT_DIM,     /* secondary text           */
    C_TEXT_MUTED,   /* hints, disabled text     */
    C_WIN,          /* window body              */
    C_WIN_ALT,      /* window body, darker      */
    C_WIN_HI,       /* raised element           */
    C_BORDER,       /* window border            */
    C_TITLE,        /* active title bar         */
    C_TITLE_OFF,    /* inactive title bar       */
    C_DESK,         /* desktop background       */
    C_DESK_DEEP,    /* desktop background, deep */
    C_ACCENT,       /* green                    */
    C_ACCENT2,      /* blue                     */
    C_ACCENT3,      /* amber                    */
    C_RED,
    C_SHADOW,
    C_SEL,          /* selection / hover        */
    C_GREEN_DK,
    C_BLUE_DK,
    C_GREY_1,
    C_GREY_2,
    C_GREY_3,
    C_WARN,
    C_PURPLE,
    C_ICON_BG,
    C_ICON_FG,
    C_HOVER,
    C_DISABLED
};

#define C_GRADIENT_FIRST 32
#define C_GRADIENT_LAST  63

extern const unsigned char font8x16[4096];

int  gfx_init(void);
int  gfx_available(void);
int  gfx_width(void);
int  gfx_height(void);
u8  *gfx_buffer(void);

void gfx_present(void);
void gfx_clear(u8 color);
void gfx_pixel(int x, int y, u8 color);
void gfx_fill(int x, int y, int w, int h, u8 color);
void gfx_hline(int x, int y, int w, u8 color);
void gfx_vline(int x, int y, int h, u8 color);
void gfx_frame(int x, int y, int w, int h, u8 color);
void gfx_vgradient(int x, int y, int w, int h, int first, int last);

void gfx_char(int x, int y, int ch, u8 color);
void gfx_text(int x, int y, const char *s, u8 color);
void gfx_text_n(int x, int y, const char *s, int max_chars, u8 color);
int  gfx_text_width(const char *s);

#endif
