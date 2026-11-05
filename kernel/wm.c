/*
 * MyOS - the desktop: wallpaper, top bar, start menu, windows, mouse, keyboard.
 *
 * All drawing happens in the back buffer; the whole frame is copied to
 * video memory once it is finished, so nothing flickers.
 */
#include "kernel.h"
#include "gfx.h"

#define MAX_WINDOWS  8
#define TOPBAR_H     30
#define TITLE_H      24
#define MIN_W        220
#define MIN_H        150
#define GRIP         14
#define WALLPAPER_BYTES (640 * 480)

extern const struct app app_files;
extern const struct app app_terminal;
extern const struct app app_editor;
extern const struct app app_browser;
extern const struct app app_clock;
extern const struct app app_calc;
extern const struct app app_snake;
extern const struct app app_about;

#define ACTION_NONE     0
#define ACTION_RESTART  1
#define ACTION_SHUTDOWN 2

struct menu_item {
    const struct app *app;
    const char *label;
    const char *hint;
    int action;
};

static const struct menu_item menu_items[] = {
    { &app_files,    "Files",      "files",      ACTION_NONE },
    { &app_terminal, "Terminal",   "shell",      ACTION_NONE },
    { &app_editor,   "Editor",     "text",   ACTION_NONE },
    { &app_browser,  "Browser",    "pages",  ACTION_NONE },
    { &app_clock,    "Clock",      "time",     ACTION_NONE },
    { &app_calc,     "Calculator", "maths",     ACTION_NONE },
    { &app_snake,    "Snake",      "game",      ACTION_NONE },
    { &app_about,    "About",      "info", ACTION_NONE },
    { NULL, "Restart",    "", ACTION_RESTART },
    { NULL, "Shut down",  "", ACTION_SHUTDOWN },
};
#define MENU_COUNT   ((int)(sizeof(menu_items) / sizeof(menu_items[0])))
#define MENU_SEP_AFTER 8
#define MENU_ITEM_H  28
#define MENU_W       212
#define MENU_X       6
#define MENU_Y       (TOPBAR_H + 2)

static struct window windows[MAX_WINDOWS];
static int window_count;
static int zorder[MAX_WINDOWS];      /* bottom .. top */
static int zcount;
static int focused = -1;
static int dragging = -1;
static int resizing = -1;
static int drag_offset_x, drag_offset_y;
static int menu_open;
static int dirty = 1;

/* the wallpaper is a full frame; it lives above the back buffer */
static u8 *const wallpaper = (u8 *)WALLPAPER_ADDR;

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

int wm_is_focused(struct window *w)
{
    return focused >= 0 && &windows[focused] == w;
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
    wallpaper_text(640 - 16 - 7 * 8, 480 - 34, "MyOS 0.3", C_DESK_DEEP);
}

/* ------------------------------------------------------------------ */
/* Top bar                                                            */
/* ------------------------------------------------------------------ */
#define START_BTN_W 74

static void draw_start_button(void)
{
    int hover = ui_hit(mouse_x(), mouse_y(), 4, 3, START_BTN_W, TOPBAR_H - 7) || menu_open;

    gfx_fill(4, 3, START_BTN_W, TOPBAR_H - 7, hover ? C_HOVER : C_WIN);
    gfx_frame(4, 3, START_BTN_W, TOPBAR_H - 7, hover ? C_ACCENT2 : C_BORDER);

    /* little "layers" logo */
    gfx_fill(11, 10, 10, 3, C_ACCENT);
    gfx_fill(11, 15, 10, 3, C_ACCENT2);
    gfx_fill(11, 20, 10, 3, C_ACCENT3);

    gfx_text(28, 7, "MyOS", hover ? C_WHITE : C_TEXT);
}

static void draw_topbar(void)
{
    int w = gfx_width();

    gfx_fill(0, 0, w, TOPBAR_H, C_WIN_ALT);
    gfx_hline(0, TOPBAR_H - 1, w, C_BORDER);

    draw_start_button();

    /* taskbar: one button per open window, in stacking order */
    int x = START_BTN_W + 12;
    for (int i = 0; i < zcount; i++) {
        struct window *win = &windows[zorder[i]];
        int bw = gfx_text_width(win->app->label) + 22;
        if (x + bw > w - 80)
            break;

        int hover = ui_hit(mouse_x(), mouse_y(), x, 4, bw, TOPBAR_H - 9);
        int active = (zorder[i] == focused);

        u8 bg = hover ? C_HOVER : (active ? C_TITLE : C_WIN);
        u8 fg = active ? C_WHITE : (hover ? C_WHITE : C_TEXT_DIM);

        gfx_fill(x, 4, bw, TOPBAR_H - 9, bg);
        if (active)
            gfx_hline(x, 4, bw, C_ACCENT);
        gfx_text(x + 11, 7, win->app->label, fg);
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

/* ------------------------------------------------------------------ */
/* Start menu                                                         */
/* ------------------------------------------------------------------ */
static int menu_height(void)
{
    return MENU_COUNT * MENU_ITEM_H + 12 + 9;
}

static int menu_item_at(int x, int y)
{
    if (!menu_open)
        return -1;
    if (x < MENU_X || x >= MENU_X + MENU_W)
        return -1;

    int top = MENU_Y + 6;
    if (y < top || y >= top + MENU_COUNT * MENU_ITEM_H + 9)
        return -1;

    /* the separator sits after MENU_SEP_AFTER items */
    int offset = y - top;
    int sep_at = MENU_SEP_AFTER * MENU_ITEM_H;
    if (offset >= sep_at && offset < sep_at + 9)
        return -1;
    if (offset >= sep_at + 9)
        offset -= 9;

    int index = offset / MENU_ITEM_H;
    if (index < 0 || index >= MENU_COUNT)
        return -1;
    return index;
}

static void draw_menu(void)
{
    if (!menu_open)
        return;

    int h = menu_height();
    int x = MENU_X, y = MENU_Y;

    gfx_fill(x + 3, y + 4, MENU_W, h, C_SHADOW);
    gfx_fill(x, y, MENU_W, h, C_WIN_ALT);
    gfx_frame(x, y, MENU_W, h, C_BORDER);

    int mx = mouse_x(), my = mouse_y();
    int iy = y + 6;

    for (int i = 0; i < MENU_COUNT; i++) {
        if (i == MENU_SEP_AFTER) {
            gfx_hline(x + 8, iy + 4, MENU_W - 16, C_BORDER);
            iy += 9;
        }

        const struct menu_item *item = &menu_items[i];
        int hover = ui_hit(mx, my, x + 4, iy, MENU_W - 8, MENU_ITEM_H - 2);

        if (hover)
            gfx_fill(x + 4, iy, MENU_W - 8, MENU_ITEM_H - 2, C_SEL);
        /* icon */
        if (item->app) {
            u8 icon = C_ACCENT2;
            if (item->app == &app_files) icon = C_ACCENT3;
            else if (item->app == &app_terminal) icon = C_ACCENT;
            else if (item->app == &app_editor) icon = C_TEXT_DIM;
            else if (item->app == &app_browser) icon = C_ACCENT2;
            else if (item->app == &app_clock) icon = C_PURPLE;
            else if (item->app == &app_calc) icon = C_WARN;
            else if (item->app == &app_snake) icon = C_GREEN_DK;
            else if (item->app == &app_about) icon = C_GREY_2;

            gfx_fill(x + 12, iy + 7, 14, 14, icon);
            gfx_fill(x + 12, iy + 7, 14, 2, C_WIN_HI);
        } else {
            gfx_fill(x + 12, iy + 9, 14, 10, C_WIN_HI);
            gfx_hline(x + 12, iy + 9, 14, C_BORDER);
        }

        gfx_text(x + 36, iy + 5, item->label, hover ? C_WHITE : C_TEXT);
        if (item->hint && item->hint[0])
            ui_text(x + 128, iy + 5, MENU_W - 136, item->hint,
                    hover ? C_TEXT : C_TEXT_MUTED);

        iy += MENU_ITEM_H;
    }
}

/* ------------------------------------------------------------------ */
/* Windows                                                            */
/* ------------------------------------------------------------------ */
static void draw_window_buttons(struct window *win, int active)
{
    int mx = mouse_x(), my = mouse_y();

    /* close */
    {
        int bx = win->x + win->w - 22, by = win->y + 5;
        int hover = ui_hit(mx, my, bx, by, 16, 16);
        u8 color = hover ? C_WHITE : C_TEXT_DIM;
        gfx_fill(bx, by, 16, 16, hover ? C_RED : C_TITLE);
        for (int i = 0; i < 8; i++) {
            gfx_pixel(bx + 4 + i, by + 4 + i, color);
            gfx_pixel(bx + 11 - i, by + 4 + i, color);
        }
    }

    /* maximize / restore */
    {
        int bx = win->x + win->w - 44, by = win->y + 5;
        int hover = ui_hit(mx, my, bx, by, 16, 16);
        u8 color = hover ? C_WHITE : (active ? C_TEXT : C_TEXT_MUTED);
        gfx_fill(bx, by, 16, 16, hover ? C_WIN_HI : C_TITLE);
        if (win->maximized) {
            gfx_frame(bx + 4, by + 6, 7, 6, color);
            gfx_frame(bx + 6, by + 4, 7, 6, color);
        } else {
            gfx_frame(bx + 4, by + 5, 8, 7, color);
        }
    }
}

static void draw_grip(struct window *win)
{
    int gx = win->x + win->w - GRIP;
    int gy = win->y + win->h - GRIP;

    for (int i = 0; i < 3; i++) {
        gfx_hline(gx + 2 + i * 4, gy + 11, 10 - i * 4, C_TEXT_MUTED);
        gfx_vline(gx + 11, gy + 2 + i * 4, 10 - i * 4, C_TEXT_MUTED);
    }
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
    ui_text(win->x + 22, win->y + 4, win->w - 70, win->app->name,
            active ? C_TEXT : C_TEXT_DIM);

    draw_window_buttons(win, active);

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

    draw_grip(win);
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
    draw_menu();
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
    win->maximized = 0;
    win->restore_x = win->restore_y = win->restore_w = win->restore_h = 0;

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
    menu_open = 0;

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

static void close_window(int index)
{
    focused = index;
    wm_close_focused();
}

static void toggle_maximize(struct window *win)
{
    if (!win->maximized) {
        win->restore_x = win->x;
        win->restore_y = win->y;
        win->restore_w = win->w;
        win->restore_h = win->h;
        win->x = 0;
        win->y = TOPBAR_H;
        win->w = gfx_width();
        win->h = gfx_height() - TOPBAR_H;
        win->maximized = 1;
        serial_printf("MAXIMIZE %s\n", win->app->name);
    } else {
        win->x = win->restore_x;
        win->y = win->restore_y;
        win->w = win->restore_w;
        win->h = win->restore_h;
        win->maximized = 0;
        serial_printf("RESTORE %s\n", win->app->name);
    }
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
/* Power actions                                                      */
/* ------------------------------------------------------------------ */
static void system_restart(void)
{
    serial_printf("ACTION restart\n");
    for (int i = 0; i < 50; i++)
        io_wait();
    outb(0x64, 0xFE);               /* keyboard controller: pulse reset */
    for (int i = 0; i < 1000000; i++)
        __asm__ volatile("nop");
    outb(0xCF9, 0x06);              /* ACPI reset, if the first one failed */
    for (;;)
        cpu_halt();
}

static void system_shutdown(void)
{
    serial_printf("ACTION shutdown\n");

    gfx_clear(C_BLACK);
    int w = gfx_width(), h = gfx_height();
    gfx_text((w - gfx_text_width("MyOS has shut down.")) / 2, h / 2 - 20,
             "MyOS has shut down.", C_TEXT_DIM);
    gfx_text((w - gfx_text_width("It is now safe to switch off the computer.")) / 2,
             h / 2 + 4, "It is now safe to switch off the computer.", C_TEXT_MUTED);
    gfx_present();

    interrupts_disable();
    for (;;)
        cpu_halt();
}

/* ------------------------------------------------------------------ */
/* Input                                                              */
/* ------------------------------------------------------------------ */
static void run_menu_action(int action)
{
    menu_open = 0;
    dirty = 1;

    if (action == ACTION_RESTART)
        system_restart();
    else if (action == ACTION_SHUTDOWN)
        system_shutdown();
}

static void handle_press(int x, int y)
{
    serial_printf("CLICK %d %d\n", x, y);

    /* the start menu swallows the click that dismisses it */
    if (menu_open) {
        int item = menu_item_at(x, y);
        if (item >= 0) {
            const struct menu_item *mi = &menu_items[item];
            menu_open = 0;
            if (mi->app) {
                serial_printf("MENU %s\n", mi->label);
                wm_open(mi->app);
            } else {
                run_menu_action(mi->action);
            }
        } else {
            menu_open = 0;
            dirty = 1;
        }
        return;
    }

    if (y < TOPBAR_H) {
        if (x < START_BTN_W + 8) {
            menu_open = 1;
            serial_printf("MENU\n");
            dirty = 1;
            return;
        }

        /* taskbar buttons */
        int bx = START_BTN_W + 12;
        for (int i = 0; i < zcount; i++) {
            struct window *win = &windows[zorder[i]];
            int bw = gfx_text_width(win->app->label) + 22;
            if (x >= bx && x < bx + bw) {
                int index = zorder[i];
                if (index == focused && !win->maximized) {
                    /* clicking the focused window's button hides it behind the rest */
                    if (zcount > 1) {
                        int bottom = zorder[0];
                        raise_window(bottom);
                        focused = zorder[zcount - 1];
                    }
                } else {
                    focused = index;
                    raise_window(index);
                }
                dirty = 1;
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
        close_window(index);
        return;
    }

    /* maximize button */
    if (ui_hit(x, y, win->x + win->w - 44, win->y + 5, 16, 16)) {
        toggle_maximize(win);
        return;
    }

    /* resize grip */
    if (!win->maximized &&
        ui_hit(x, y, win->x + win->w - GRIP, win->y + win->h - GRIP, GRIP, GRIP)) {
        resizing = index;
        return;
    }

    /* title bar: start dragging */
    if (ui_hit(x, y, win->x, win->y, win->w, TITLE_H + 1)) {
        if (!win->maximized) {
            dragging = index;
            drag_offset_x = x - win->x;
            drag_offset_y = y - win->y;
        }
        return;
    }

    if (win->app->click)
        win->app->click(win, x - win->x - 1, y - win->y - TITLE_H - 1);
}

static void handle_move(int x, int y)
{
    if (resizing >= 0) {
        struct window *win = &windows[resizing];

        int w = x - win->x + 1;
        int h = y - win->y + 1;
        if (w < MIN_W) w = MIN_W;
        if (h < MIN_H) h = MIN_H;
        if (win->x + w > gfx_width()) w = gfx_width() - win->x;
        if (win->y + h > gfx_height()) h = gfx_height() - win->y;

        win->w = w;
        win->h = h;
        dirty = 1;
        return;
    }

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

    u32 sync_counter = 0;

    for (;;) {
        int changed = 0;

        /* every second or so, push changed files to the disk */
        if (++sync_counter >= 100) {
            sync_counter = 0;
            if (fs_is_dirty())
                fs_sync();
        }

        while (mouse_pop(&ev)) {
            changed = 1;
            if (ev.type == 1) {
                handle_move(ev.x, ev.y);
                update_hover();
            } else if (ev.type == 2) {
                handle_press(ev.x, ev.y);
            } else if (ev.type == 3) {
                dragging = -1;
                resizing = -1;
                dirty = 1;
            }
        }

        int key;
        while ((key = keyboard_getkey()) >= 0) {
            changed = 1;
            if (key == 27 && menu_open) {
                menu_open = 0;
                dirty = 1;
                continue;
            }
            if (focused >= 0 && windows[focused].open && windows[focused].app->key)
                windows[focused].app->key(&windows[focused], key);
        }

        /* apps that animate (clock, snake) get a tick about 100 times a second */
        for (int i = 0; i < window_count; i++) {
            if (!windows[i].open || !windows[i].app->tick)
                continue;
            if (windows[i].app->tick(&windows[i]))
                changed = 1;
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
