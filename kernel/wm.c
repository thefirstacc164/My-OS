/*
 * MyOS - the desktop: wallpaper, top bar, windows, mouse and keyboard.
 *
 * All drawing happens in the back buffer; the whole frame is copied to
 * video memory once it is finished, so nothing flickers.
 */
#include "kernel.h"
#include "gfx.h"

#define MAX_WINDOWS  6
#define TOPBAR_H     30
#define TITLE_H      24
#define WALLPAPER_BYTES (640 * 480)

extern const struct app app_files;
extern const struct app app_terminal;
extern const struct app app_editor;
extern const struct app app_browser;
extern const struct app app_about;

static const struct app *const launcher[] = {
    &app_files, &app_terminal, &app_editor, &app_browser, &app_about
};
#define LAUNCHER_COUNT ((int)(sizeof(launcher) / sizeof(launcher[0])))

static struct window windows[MAX_WINDOWS];
static int window_count;
static int zorder[MAX_WINDOWS];      /* bottom .. top */
static int zcount;
static int focused = -1;
static int dragging = -1;
static int drag_offset_x, drag_offset_y;
static int dirty = 1;

static u8 wallpaper[WALLPAPER_BYTES];

/* mouse cursor bitmap: 12x18 arrow, 1 = white, 2 = black outline */
static const char cursor_shape[18][13] = {
    "1           ",
    "11          ",
    "121         ",
    "1221        ",
    "12221       ",
    "122221      ",
    "1222221     ",
    "12222221    ",
    "122222221   ",
    "1222222221  ",
    "12222222221 ",
    "12222211111 ",
    "1221221     ",
    "121 1221    ",
    "11  1221    ",
    "1    1221   ",
    "     1221   ",
    "      11    ",
};

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */
int ui_hit(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

void ui_text(int x, int y, int max_px, const char *s, u8 color)
{
    int limit = max_px / 8;
    while (*s && limit-- > 0) {
        gfx_char(x, y, (unsigned char)*s++, color);
        x += 8;
    }
}

void ui_button(int x, int y, int w, int h, const char *label, u8 bg, u8 fg)
{
    gfx_fill(x, y, w, h, bg);
    gfx_frame(x, y, w, h, C_BORDER);
    int tw = gfx_text_width(label);
    gfx_text(x + (w - tw) / 2, y + (h - 16) / 2, label, fg);
}

static void draw_close_button(int x, int y, int hover)
{
    u8 color = hover ? C_RED : C_TEXT_DIM;
    gfx_fill(x, y, 16, 16, hover ? C_WIN_HI : C_TITLE);
    gfx_hline(x + 4, y + 4, 8, color);
    for (int i = 0; i < 8; i++) {
        gfx_pixel(x + 4 + i, y + 4 + i, color);
        gfx_pixel(x + 11 - i, y + 4 + i, color);
    }
}

/* ------------------------------------------------------------------ */
/* Wallpaper                                                          */
/* ------------------------------------------------------------------ */
/* 4x4 ordered dither: an 8-bit palette only has 32 shades for the
 * background, and dithering hides the banding completely. */
static const u8 bayer4[4][4] = {
    {  0,  8,  2, 10 },
    { 12,  4, 14,  6 },
    {  3, 11,  1,  9 },
    { 15,  7, 13,  5 },
};

static void wallpaper_text(int x, int y, const char *s, u8 color)
{
    while (*s) {
        const unsigned char *glyph = (const unsigned char *)0; /* replaced below */
        (void)glyph;
        const unsigned char *g = font8x16 + (unsigned char)(*s) * 16;
        for (int row = 0; row < 16; row++) {
            u8 bits = g[row];
            if (!bits)
                continue;
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    int px = x + col, py = y + row;
                    if (px >= 0 && px < 640 && py >= 0 && py < 480)
                        wallpaper[py * 640 + px] = color;
                }
            }
        }
        x += 8;
        s++;
    }
}

static void build_wallpaper(void)
{
    int levels = C_GRADIENT_LAST - C_GRADIENT_FIRST;

    for (int y = 0; y < 480; y++) {
        int pos = y * levels * 16 / 479;
        int base = pos >> 4;
        int frac = pos & 15;

        for (int x = 0; x < 640; x++) {
            int color = C_GRADIENT_FIRST + base;
            if (bayer4[y & 3][x & 3] < frac)
                color++;
            wallpaper[y * 640 + x] = (u8)color;
        }
    }

    /* a quiet wordmark in the corner */
    wallpaper_text(640 - 16 - 7 * 8, 480 - 34, "MyOS 0.2", C_GRADIENT_LAST);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */
static void draw_topbar(void)
{
    int w = gfx_width();

    gfx_fill(0, 0, w, TOPBAR_H, C_WIN_ALT);
    gfx_hline(0, TOPBAR_H - 1, w, C_BORDER);

    gfx_text(12, 7, "MyOS", C_ACCENT);

    int x = 84;
    for (int i = 0; i < LAUNCHER_COUNT; i++) {
        int open = 0;
        for (int j = 0; j < zcount; j++)
            if (windows[zorder[j]].app == launcher[i])
                open = 1;

        int bw = gfx_text_width(launcher[i]->label) + 20;
        int hover = ui_hit(mouse_x(), mouse_y(), x, 0, bw, TOPBAR_H);

        u8 bg = hover ? C_HOVER : (open ? C_WIN : C_WIN_ALT);
        u8 fg = hover ? C_WHITE : (open ? C_ACCENT2 : C_TEXT_DIM);

        gfx_fill(x, 4, bw, TOPBAR_H - 9, bg);
        gfx_text(x + 10, 7, launcher[i]->label, fg);
        x += bw + 4;
    }

    /* clock */
    u32 year, month, day, hour, minute, second;
    rtc_read(&year, &month, &day, &hour, &minute, &second);
    char clock[8];
    clock[0] = (char)('0' + (hour / 10) % 10);
    clock[1] = (char)('0' + hour % 10);
    clock[2] = ':';
    clock[3] = (char)('0' + (minute / 10) % 10);
    clock[4] = (char)('0' + minute % 10);
    clock[5] = '\0';
    gfx_text(w - 12 - gfx_text_width(clock), 7, clock, C_TEXT_DIM);

    (void)year; (void)month; (void)day; (void)second;
}

static void draw_window(int index)
{
    struct window *win = &windows[index];
    if (!win->open)
        return;

    int active = (index == focused);

    /* drop shadow */
    gfx_fill(win->x + 4, win->y + 5, win->w, win->h, C_SHADOW);
    gfx_fill(win->x + 2, win->y + 3, win->w, win->h, C_SHADOW);

    /* body and border */
    gfx_fill(win->x, win->y, win->w, win->h, C_WIN);
    gfx_frame(win->x, win->y, win->w, win->h, C_BORDER);

    /* title bar */
    gfx_fill(win->x + 1, win->y + 1, win->w - 2, TITLE_H, active ? C_TITLE : C_TITLE_OFF);
    gfx_hline(win->x + 1, win->y + TITLE_H, win->w - 2, C_BORDER);

    gfx_fill(win->x + 9, win->y + 9, 6, 6, active ? C_ACCENT : C_GREY_2);
    ui_text(win->x + 22, win->y + 4, win->w - 60, win->app->name,
                 active ? C_TEXT : C_TEXT_DIM);

    int hover = ui_hit(mouse_x(), mouse_y(), win->x + win->w - 22, win->y + 5, 16, 16);
    draw_close_button(win->x + win->w - 22, win->y + 5, hover);

    /* contents */
    if (win->app->draw) {
        int saved_x = win->x, saved_y = win->y, saved_w = win->w, saved_h = win->h;
        win->x += 1;
        win->y += TITLE_H + 1;
        win->w -= 2;
        win->h -= TITLE_H + 2;
        win->app->draw(win);
        win->x = saved_x;
        win->y = saved_y;
        win->w = saved_w;
        win->h = saved_h;
    }
}

static void draw_cursor(void)
{
    int mx = mouse_x();
    int my = mouse_y();

    for (int row = 0; row < 18; row++) {
        for (int col = 0; col < 12; col++) {
            char c = cursor_shape[row][col];
            if (c == '1')
                gfx_pixel(mx + col, my + row, C_WHITE);
            else if (c == '2')
                gfx_pixel(mx + col, my + row, C_BLACK);
        }
    }
}

static void render(void)
{
    u8 *bb = gfx_buffer();
    int w = gfx_width();
    int h = gfx_height();

    memcpy(bb, wallpaper, (u32)(w * h));

    for (int i = 0; i < zcount; i++)
        draw_window(zorder[i]);

    draw_topbar();
    draw_cursor();

    gfx_present();
}

void wm_redraw(void)
{
    dirty = 1;
}

/* ------------------------------------------------------------------ */
/* Window management                                                  */
/* ------------------------------------------------------------------ */
static void raise_window(int index)
{
    int pos = -1;
    for (int i = 0; i < zcount; i++)
        if (zorder[i] == index)
            pos = i;

    if (pos == zcount - 1)
        return;

    if (pos >= 0)
        for (int i = pos; i < zcount - 1; i++)
            zorder[i] = zorder[i + 1];

    zorder[zcount - 1] = index;
}

static int find_window(const struct app *app)
{
    for (int i = 0; i < window_count; i++)
        if (windows[i].open && windows[i].app == app)
            return i;
    return -1;
}

struct window *wm_open(const struct app *app)
{
    int existing = find_window(app);
    if (existing >= 0) {
        focused = existing;
        raise_window(existing);
        dirty = 1;
        return &windows[existing];
    }

    if (window_count >= MAX_WINDOWS)
        return NULL;

    int index = window_count++;
    struct window *win = &windows[index];
    win->app = app;
    win->open = 1;
    win->w = app->width;
    win->h = app->height;
    win->state = NULL;

    /* cascade new windows so they do not stack exactly */
    int offset = (index % 5) * 28;
    win->x = 40 + offset;
    win->y = TOPBAR_H + 20 + offset;
    if (win->x + win->w > gfx_width() - 8)
        win->x = gfx_width() - win->w - 8;
    if (win->y + win->h > gfx_height() - 8)
        win->y = gfx_height() - win->h - 8;

    zorder[zcount++] = index;
    focused = index;

    serial_printf("OPEN %s\n", app->name);

    if (app->open)
        app->open(win);

    dirty = 1;
    return win;
}

void wm_close_focused(void)
{
    if (focused < 0)
        return;
    windows[focused].open = 0;
    windows[focused].state = NULL;

    int pos = -1;
    for (int i = 0; i < zcount; i++)
        if (zorder[i] == focused)
            pos = i;
    if (pos >= 0) {
        for (int i = pos; i < zcount - 1; i++)
            zorder[i] = zorder[i + 1];
        zcount--;
    }
    focused = zcount > 0 ? zorder[zcount - 1] : -1;
    dirty = 1;
}

static int top_window_at(int x, int y)
{
    for (int i = zcount - 1; i >= 0; i--) {
        struct window *win = &windows[zorder[i]];
        if (win->open && ui_hit(x, y, win->x, win->y, win->w, win->h))
            return zorder[i];
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Input                                                              */
/* ------------------------------------------------------------------ */
static void handle_press(int x, int y)
{
    serial_printf("CLICK %d %d\n", x, y);
    if (y < TOPBAR_H) {
        int bx = 84;
        for (int i = 0; i < LAUNCHER_COUNT; i++) {
            int bw = gfx_text_width(launcher[i]->label) + 20;
            if (x >= bx && x < bx + bw) {
                wm_open(launcher[i]);
                return;
            }
            bx += bw + 4;
        }
        return;
    }

    int index = top_window_at(x, y);
    if (index < 0) {
        focused = -1;
        dirty = 1;
        return;
    }

    struct window *win = &windows[index];

    if (focused != index) {
        focused = index;
        raise_window(index);
        dirty = 1;
    }

    /* close button */
    if (ui_hit(x, y, win->x + win->w - 22, win->y + 5, 16, 16)) {
        focused = index;
        wm_close_focused();
        return;
    }

    /* title bar: start dragging */
    if (ui_hit(x, y, win->x, win->y, win->w, TITLE_H + 1)) {
        dragging = index;
        drag_offset_x = x - win->x;
        drag_offset_y = y - win->y;
        return;
    }

    if (win->app->click)
        win->app->click(win, x - win->x - 1, y - win->y - TITLE_H - 1);
}

static void handle_move(int x, int y)
{
    if (dragging < 0)
        return;

    struct window *win = &windows[dragging];
    win->x = x - drag_offset_x;
    win->y = y - drag_offset_y;

    if (win->x < -win->w + 60)     win->x = -win->w + 60;
    if (win->y < TOPBAR_H)         win->y = TOPBAR_H;
    if (win->x > gfx_width() - 60) win->x = gfx_width() - 60;
    if (win->y > gfx_height() - 30) win->y = gfx_height() - 30;

    dirty = 1;
}

/* ------------------------------------------------------------------ */
/* Main loop                                                          */
/* ------------------------------------------------------------------ */
static void update_hover(void)
{
    static int last_x = -1, last_y = -1;
    if (mouse_x() != last_x || mouse_y() != last_y) {
        last_x = mouse_x();
        last_y = mouse_y();
        dirty = 1;
    }
}

void wm_run(void)
{
    struct mouse_event ev;

    build_wallpaper();
    mouse_set_bounds(gfx_width(), gfx_height());
    render();

    for (;;) {
        int changed = 0;

        while (mouse_pop(&ev)) {
            changed = 1;
            if (ev.type == 1) {
                handle_move(ev.x, ev.y);
                update_hover();
            } else if (ev.type == 2) {
                handle_press(ev.x, ev.y);
            } else if (ev.type == 3) {
                dragging = -1;
                dirty = 1;
            }
        }

        int key;
        while ((key = keyboard_getkey()) >= 0) {
            changed = 1;
            if (focused >= 0 && windows[focused].open && windows[focused].app->key)
                windows[focused].app->key(&windows[focused], key);
        }

        if (changed)
            dirty = 1;

        if (dirty) {
            dirty = 0;
            render();
        }

        cpu_wait_interrupt();
    }
}
