/*
 * MyOS - the applications that run inside windows.
 *
 *   Files     - browse the in-memory file system
 *   Terminal  - the shell, inside a window
 *   Editor    - edit a text file and save it back
 *   Browser   - a tiny page renderer for .html files in the file system
 *   About     - system information
 */
#include "kernel.h"
#include "gfx.h"

#define ROW_H 26

/* ------------------------------------------------------------------ */
/* Shared helpers                                                     */
/* ------------------------------------------------------------------ */
static void clear_content(struct window *w, u8 color)
{
    gfx_fill(w->x, w->y, w->w, w->h, color);
}

static void put(struct window *w, int x, int y, const char *s, u8 color)
{
    int limit = (w->w - x) / 8;
    if (limit < 0)
        return;
    while (*s && limit-- > 0) {
        gfx_char(w->x + x, w->y + y, (unsigned char)*s++, color);
        x += 8;
    }
}

static void draw_folder_icon(int x, int y, u8 color)
{
    gfx_fill(x, y + 2, 14, 11, color);
    gfx_fill(x + 1, y, 6, 3, color);
}

static void draw_file_icon(int x, int y, u8 color, u8 fold)
{
    gfx_fill(x + 2, y, 10, 14, color);
    gfx_fill(x + 8, y, 4, 5, fold);
    gfx_fill(x + 8, y + 5, 4, 1, fold);
}

/* horizontal progress/divider line inside a window */
static void divider(struct window *w, int y)
{
    gfx_hline(w->x, w->y + y, w->w, C_BORDER);
}

/* ------------------------------------------------------------------ */
/* Files                                                              */
/* ------------------------------------------------------------------ */
static int files_rows(struct window *w)
{
    int rows = (w->h - 32) / ROW_H;
    if (rows < 1)
        rows = 1;
    return rows;
}

struct files_state {
    int dir;
    int selected;
    int scroll;
};

static struct files_state files_data;

static int is_html(const char *name)
{
    u32 n = strlen(name);
    if (n < 5)
        return 0;
    return strcmp(name + n - 5, ".html") == 0;
}

static void files_open(struct window *w)
{
    files_data.dir = fs_resolve("/home");
    if (files_data.dir < 0)
        files_data.dir = fs_root();
    files_data.selected = -1;
    files_data.scroll = 0;
    w->state = &files_data;
}

static void files_draw(struct window *w)
{
    struct files_state *s = w->state;
    clear_content(w, C_WIN);

    /* path bar */
    gfx_fill(w->x, w->y, w->w, 26, C_WIN_ALT);
    char path[96];
    fs_path(s->dir, path, sizeof(path));
    put(w, 10, 5, path, C_TEXT_DIM);
    ui_button(w->x + w->w - 44, w->y + 4, 36, 18, "Up", C_WIN_HI, C_TEXT);
    divider(w, 26);

    int count = fs_child_count(s->dir);
    if (count == 0)
        put(w, 12, 44, "This folder is empty.", C_TEXT_MUTED);

    int top = 30;
    int mx = mouse_x() - w->x;
    int my = mouse_y() - w->y;

    int visible = files_rows(w);
    for (int i = 0; i < visible && s->scroll + i < count; i++) {
        int index = fs_child(s->dir, s->scroll + i);
        struct fs_node *n = fs_get(index);
        if (!n)
            continue;

        int row_y = top + i * ROW_H;
        int hover = ui_hit(mx, my, 0, row_y, w->w, ROW_H);
        int selected = (s->scroll + i == s->selected);

        if (selected || hover)
            gfx_fill(w->x + 1, w->y + row_y, w->w - 2, ROW_H - 2,
                     selected ? C_SEL : C_WIN_HI);

        int icon_y = w->y + row_y + 6;
        if (n->is_dir)
            draw_folder_icon(w->x + 12, icon_y, C_ACCENT3);
        else if (is_html(n->name))
            draw_file_icon(w->x + 12, icon_y, C_ACCENT2, C_WIN);
        else
            draw_file_icon(w->x + 12, icon_y, C_GREY_2, C_WIN);

        put(w, 36, row_y + 4, n->name, n->is_dir ? C_TEXT : C_TEXT_DIM);

        if (!n->is_dir) {
            char size[16];
            ksnprintf(size, sizeof(size), "%u B", (u32)n->size);
            int sx = w->w - 12 - gfx_text_width(size);
            put(w, sx, row_y + 4, size, C_TEXT_MUTED);
        }
    }

    if (count > visible) {
        char more[32];
        ksnprintf(more, sizeof(more), "%d items", count);
        put(w, 12, w->h - 20, more, C_TEXT_MUTED);
    }
}

static void files_click(struct window *w, int x, int y)
{
    struct files_state *s = w->state;

    if (x >= w->w - 44 && y < 26) {
        struct fs_node *n = fs_get(s->dir);
        if (n && n->parent >= 0) {
            s->dir = n->parent;
            s->selected = -1;
            s->scroll = 0;
        }
        return;
    }

    int index = (y - 30) / ROW_H;
    if (index < 0 || index >= files_rows(w))
        return;

    index += s->scroll;
    int child = fs_child(s->dir, index);
    if (child < 0)
        return;

    struct fs_node *n = fs_get(child);
    s->selected = index;

    if (n->is_dir) {
        s->dir = child;
        s->selected = -1;
        s->scroll = 0;
        return;
    }

    extern void app_open_file(const char *path);
    char path[96];
    fs_path(child, path, sizeof(path));
    app_open_file(path);
}

static void files_scroll(struct window *w, int amount)
{
    struct files_state *s = w->state;
    int count = fs_child_count(s->dir);
    int visible = files_rows(w);
    s->scroll += amount;
    if (s->scroll < 0) s->scroll = 0;
    if (s->scroll > count - visible) {
        s->scroll = count - visible;
        if (s->scroll < 0) s->scroll = 0;
    }
}

const struct app app_files = {
    "Files", "Files", 356, 250,
    files_open, files_draw, NULL, files_click, files_scroll, NULL
};

/* ------------------------------------------------------------------ */
/* Terminal                                                           */
/* ------------------------------------------------------------------ */
#define TERM_COLS 58
#define TERM_ROWS 16

struct term_state {
    struct textbuf tb;
    char cells[TERM_COLS * TERM_ROWS];
    u8   attrs[TERM_COLS * TERM_ROWS];
    char line[80];
    int  length;
};

static struct term_state term_data;

static void term_open(struct window *w)
{
    w->state = &term_data;
    textbuf_init(&term_data.tb, TERM_COLS, TERM_ROWS,
                 term_data.cells, term_data.attrs);
    term_data.length = 0;

    console_set_target(&term_data.tb);
    console_set_color(C_ACCENT);
    kprintf("MyOS terminal\n");
    console_set_color(C_TEXT_MUTED);
    kprintf("Type help for the command list.\n\n");
    console_set_color(C_TEXT);
    shell_prompt();
    console_set_target(NULL);
}

static void term_draw(struct window *w)
{
    struct term_state *s = w->state;

    clear_content(w, C_WIN_ALT);

    int cols = w->w / 8;
    int rows = w->h / 16;
    if (cols > TERM_COLS) cols = TERM_COLS;
    if (rows > TERM_ROWS) rows = TERM_ROWS;

    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            int i = y * TERM_COLS + x;
            if (s->cells[i] != ' ')
                gfx_char(w->x + x * 8, w->y + y * 16, (unsigned char)s->cells[i],
                         s->attrs[i]);
        }
    }

    /* block cursor at the current input position */
    if (s->tb.cy < TERM_ROWS) {
        gfx_fill(w->x + s->tb.cx * 8, w->y + s->tb.cy * 16 + 12, 8, 3, C_ACCENT);
    }
}

static void term_key(struct window *w, int key)
{
    struct term_state *s = w->state;

    console_set_target(&s->tb);

    if (key == '\n') {
        s->line[s->length] = '\0';
        kprintf("\n");
        shell_execute_line(s->line);
        s->length = 0;
        shell_prompt();
    } else if (key == '\b') {
        if (s->length > 0) {
            s->length--;
            console_putc('\b');
        }
    } else if (key >= 32 && key < 127 && s->length < (int)sizeof(s->line) - 1) {
        s->line[s->length++] = (char)key;
        console_putc((char)key);
    } else if (key == KEY_PGUP || key == KEY_PGDN) {
        /* no scrollback yet */
    }

    console_set_target(NULL);
    wm_redraw();
}

const struct app app_terminal = {
    "Terminal", "Terminal", 468, 300,
    term_open, term_draw, term_key, NULL, NULL, NULL
};

/* ------------------------------------------------------------------ */
/* Editor                                                             */
/* ------------------------------------------------------------------ */
struct editor_state {
    int  file;
    char buffer[FS_FILE_MAX];
    int  length;
    int  cursor;
    int  scroll;
    int  saved_flash;
};

static struct editor_state editor_data;

static void editor_load(int file)
{
    struct editor_state *s = &editor_data;
    s->file = file;
    s->cursor = 0;
    s->scroll = 0;
    s->saved_flash = 0;
    s->length = 0;
    s->buffer[0] = '\0';

    struct fs_node *n = fs_get(file);
    if (n && !n->is_dir) {
        int i = 0;
        while (i < FS_FILE_MAX - 1 && n->data[i]) {
            s->buffer[i] = n->data[i];
            i++;
        }
        s->buffer[i] = '\0';
        s->length = i;
    }
}

static void editor_open(struct window *w)
{
    w->state = &editor_data;
    if (w->state && editor_data.file <= 0)
        editor_load(fs_resolve("/home/welcome.txt"));
}

static void editor_draw(struct window *w)
{
    struct editor_state *s = w->state;
    clear_content(w, C_WIN);

    char path[96] = "(no file)";
    if (s->file > 0)
        fs_path(s->file, path, sizeof(path));

    gfx_fill(w->x, w->y, w->w, 26, C_WIN_ALT);
    put(w, 10, 5, path, C_TEXT_DIM);
    ui_button(w->x + w->w - 58, w->y + 4, 50, 18, "Save",
                s->saved_flash ? C_ACCENT : C_WIN_HI,
                s->saved_flash ? C_BLACK : C_TEXT);
    divider(w, 26);

    int max_cols = (w->w - 16) / 8;
    int visible_lines = (w->h - 36) / 16;

    /* find the line the cursor is on */
    int line_of_cursor = 0;
    for (int i = 0; i < s->cursor; i++)
        if (s->buffer[i] == '\n')
            line_of_cursor++;

    if (line_of_cursor < s->scroll)
        s->scroll = line_of_cursor;
    if (line_of_cursor >= s->scroll + visible_lines)
        s->scroll = line_of_cursor - visible_lines + 1;
    if (s->scroll < 0)
        s->scroll = 0;

    int line = 0, col = 0;
    int y = 4;
    for (int i = 0; i <= s->length && line < s->scroll + visible_lines; i++) {
        char c = (i < s->length) ? s->buffer[i] : '\n';
        if (line >= s->scroll && col < max_cols) {
            if (c != '\n')
                gfx_char(w->x + 8 + col * 8, w->y + 32 + y, (unsigned char)c, C_TEXT);
        }
        if (c == '\n') {
            line++;
            if (line >= s->scroll)
                y += 16;
            col = 0;
            if (i == s->cursor && line - 1 >= s->scroll && line - 1 < s->scroll + visible_lines) {
                gfx_fill(w->x + 8, w->y + 32 + y - 14, 8, 14, C_SEL);
            }
        } else {
            col++;
            if (col >= max_cols) {
                line++;
                if (line >= s->scroll)
                    y += 16;
                col = 0;
            }
            if (i == s->cursor && line >= s->scroll)
                gfx_fill(w->x + 8 + col * 8, w->y + 32 + y, 1, 14, C_ACCENT);
        }
    }
}

static void editor_insert(char c)
{
    struct editor_state *s = &editor_data;
    if (s->length >= FS_FILE_MAX - 1)
        return;
    for (int i = s->length; i > s->cursor; i--)
        s->buffer[i] = s->buffer[i - 1];
    s->buffer[s->cursor] = c;
    s->cursor++;
    s->length++;
    s->buffer[s->length] = '\0';
    s->saved_flash = 0;
}

static void editor_key(struct window *w, int key)
{
    struct editor_state *s = w->state;
    s->saved_flash = 0;

    if (key == '\b') {
        if (s->cursor > 0) {
            for (int i = s->cursor - 1; i < s->length - 1; i++)
                s->buffer[i] = s->buffer[i + 1];
            s->cursor--;
            s->length--;
            s->buffer[s->length] = '\0';
        }
    } else if (key == KEY_LEFT) {
        if (s->cursor > 0) s->cursor--;
    } else if (key == KEY_RIGHT) {
        if (s->cursor < s->length) s->cursor++;
    } else if (key == KEY_UP || key == KEY_DOWN) {
        int target_line = 0, col = 0;
        int line = 0;
        for (int i = 0; i < s->cursor; i++) {
            if (s->buffer[i] == '\n') { line++; col = 0; }
            else col++;
        }
        target_line = line + (key == KEY_DOWN ? 1 : -1);
        if (target_line < 0)
            return;
        int pos = 0, cur_line = 0;
        while (pos < s->length && cur_line < target_line) {
            if (s->buffer[pos] == '\n') cur_line++;
            pos++;
        }
        if (cur_line != target_line)
            return;
        int end = pos;
        while (end < s->length && s->buffer[end] != '\n') end++;
        s->cursor = pos + col;
        if (s->cursor > end) s->cursor = end;
    } else if (key == '\n') {
        editor_insert('\n');
    } else if (key == 19 /* Ctrl+S */) {
        fs_write(s->file, s->buffer);
        s->saved_flash = 1;
    } else if (key >= 32 && key < 127) {
        editor_insert((char)key);
    }
    wm_redraw();
}

static void editor_click(struct window *w, int x, int y)
{
    struct editor_state *s = w->state;
    if (y < 26 && x >= w->w - 58) {
        fs_write(s->file, s->buffer);
        s->saved_flash = 1;
        wm_redraw();
    }
}

const struct app app_editor = {
    "Editor", "Editor", 460, 310,
    editor_open, editor_draw, editor_key, editor_click, NULL, NULL
};

/* ------------------------------------------------------------------ */
/* Browser                                                            */
/* ------------------------------------------------------------------ */
#define MAX_LINKS 16

struct link_rect {
    int x, y, w, h;
    char target[40];
};

struct browser_state {
    int page;
    int link_count;
    struct link_rect links[MAX_LINKS];
    int scroll;
};

static struct browser_state browser_data;

static void browser_go(int page)
{
    browser_data.page = page;
    browser_data.scroll = 0;
    browser_data.link_count = 0;
}

static void browser_open(struct window *w)
{
    w->state = &browser_data;
    int page = fs_resolve("/home/start.html");
    browser_go(page >= 0 ? page : fs_root());
}

/* add a link rectangle while rendering */
static void add_link(struct window *w, int x, int y, int px, const char *target)
{
    struct browser_state *s = w->state;
    if (s->link_count >= MAX_LINKS)
        return;

    struct link_rect *l = &s->links[s->link_count++];
    l->x = x;
    l->y = y;
    l->w = px;
    l->h = 16;

    u32 i = 0;
    while (target[i] && i < sizeof(l->target) - 1) {
        l->target[i] = target[i];
        i++;
    }
    l->target[i] = '\0';

    (void)w;
}

static int starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s++ != *prefix++)
            return 0;
    }
    return 1;
}

/* Draw a run of text with word wrapping, turning [label](target) into
 * clickable links. Returns the y of the last line drawn. */
static int render_inline(struct window *w, int x0, int y, const char *s,
                         u8 color, int allow_links)
{
    int limit = w->w - 16;
    int x = x0;

    while (*s) {
        while (*s == ' ') {
            s++;
            x += 8;
        }
        if (!*s)
            break;

        if (allow_links && *s == '[') {
            const char *close = s;
            while (*close && *close != ']')
                close++;
            if (*close == ']' && close[1] == '(') {
                const char *end = close + 2;
                while (*end && *end != ')')
                    end++;
                if (*end == ')') {
                    char label[64];
                    char target[40];
                    int a = 0, b = 0;
                    for (const char *t = s + 1; t < close && a < 63; t++)
                        label[a++] = *t;
                    label[a] = '\0';
                    for (const char *t = close + 2; t < end && b < 39; t++)
                        target[b++] = *t;
                    target[b] = '\0';

                    int px = a * 8;
                    if (x + px > x0 + limit) {
                        x = x0;
                        y += 16;
                    }
                    put(w, x, y, label, C_ACCENT2);
                    gfx_hline(w->x + x, w->y + y + 15, px, C_ACCENT2);
                    add_link(w, x, y, px, target);
                    x += px;
                    s = end + 1;
                    continue;
                }
            }
        }

        const char *word = s;
        while (*s && *s != ' ' && *s != '[')
            s++;
        int length = (int)(s - word);

        if (x + length * 8 > x0 + limit) {
            x = x0;
            y += 16;
        }

        char chunk[128];
        int n = 0;
        for (const char *t = word; t < s && n < 127; t++)
            chunk[n++] = *t;
        chunk[n] = '\0';
        put(w, x, y, chunk, color);
        x += n * 8;

        if (*s == ' ') {
            s++;
            x += 8;
        }
    }
    return y;
}

static void browser_draw(struct window *w)
{
    struct browser_state *s = w->state;
    clear_content(w, C_WIN);
    s->link_count = 0;

    struct fs_node *page = fs_get(s->page);

    /* toolbar */
    gfx_fill(w->x, w->y, w->w, 26, C_WIN_ALT);
    char path[96] = "";
    if (page)
        fs_path(s->page, path, sizeof(path));
    put(w, 10, 5, path, C_TEXT_MUTED);
    divider(w, 26);

    if (!page) {
        put(w, 12, 40, "Page not found.", C_RED);
        return;
    }

    int y = 34;
    int i = 0;

    while (page->data[i] && y < w->h - 24) {
        char line[200];
        int n = 0;
        while (page->data[i] && page->data[i] != '\n' && n < 199)
            line[n++] = page->data[i++];
        if (page->data[i] == '\n')
            i++;
        line[n] = '\0';

        if (n == 0) {
            y += 8;
        } else if (starts_with(line, "# ")) {
            y += 6;
            put(w, 12, y, line + 2, C_ACCENT);
            y += 22;
        } else if (starts_with(line, "## ")) {
            y += 4;
            put(w, 12, y, line + 3, C_ACCENT2);
            y += 20;
        } else if (starts_with(line, "* ")) {
            gfx_fill(w->x + 16, w->y + y + 7, 4, 4, C_ACCENT3);
            y = render_inline(w, 28, y, line + 2, C_TEXT_DIM, 1);
            y += 18;
        } else {
            y = render_inline(w, 12, y, line, C_TEXT, 1);
            y += 18;
        }
    }
}

static void browser_click(struct window *w, int x, int y)
{
    struct browser_state *s = w->state;
    (void)w;

    for (int i = 0; i < s->link_count; i++) {
        struct link_rect *l = &s->links[i];
        if (x >= l->x && x < l->x + l->w && y >= l->y && y < l->y + l->h) {
            char target[80];
            strcpy_s(target, sizeof(target), l->target);
            serial_printf("LINK %s\n", target);
            if (target[0] == '/') {
                int page = fs_resolve(target);
                if (page >= 0)
                    browser_go(page);
            } else {
                /* relative to the directory of the current page */
                struct fs_node *cur = fs_get(s->page);
                char base[96] = "/home";
                if (cur)
                    fs_path(cur->parent, base, sizeof(base));

                char full[160];
                strcpy_s(full, sizeof(full), base);
                strcat_s(full, sizeof(full), "/");
                strcat_s(full, sizeof(full), target);

                int page = fs_resolve(full);
                if (page >= 0)
                    browser_go(page);
            }
            wm_redraw();
            return;
        }
    }
}

const struct app app_browser = {
    "Browser", "Browser", 520, 356,
    browser_open, browser_draw, NULL, browser_click, NULL, NULL
};

/* ------------------------------------------------------------------ */
/* About                                                              */
/* ------------------------------------------------------------------ */
static void about_draw(struct window *w)
{
    clear_content(w, C_WIN);

    put(w, 14, 10, "MyOS 0.3", C_ACCENT);
    put(w, 14, 28, "\"Full desktop\" - a hand-written OS", C_TEXT_MUTED);

    divider(w, 52);

    char line[80];
    ksnprintf(line, sizeof(line), "display   %ux%u, 8-bit colour",
              (u32)gfx_width(), (u32)gfx_height());
    put(w, 14, 64, line, C_TEXT);

    ksnprintf(line, sizeof(line), "memory    %u MB", (u32)(memory_total_bytes() >> 20));
    put(w, 14, 82, line, C_TEXT);

    ksnprintf(line, sizeof(line), "uptime    %u s", timer_uptime_seconds());
    put(w, 14, 100, line, C_TEXT);

    u32 a, b, c, d;
    char vendor[13];
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0u), "c"(0u));
    memcpy(vendor + 0, &b, 4);
    memcpy(vendor + 4, &d, 4);
    memcpy(vendor + 8, &c, 4);
    vendor[12] = '\0';
    ksnprintf(line, sizeof(line), "cpu       %s", vendor);
    put(w, 14, 118, line, C_TEXT);

    divider(w, 146);

    put(w, 14, 158, "Boot sector  512 bytes of assembly", C_TEXT_DIM);
    put(w, 14, 176, "Kernel       C, 32-bit protected mode", C_TEXT_DIM);
    put(w, 14, 194, "Graphics     VESA linear framebuffer", C_TEXT_DIM);
    put(w, 14, 212, "Input        PS/2 keyboard and mouse", C_TEXT_DIM);
}

const struct app app_about = {
    "About MyOS", "About", 360, 240,
    NULL, about_draw, NULL, NULL, NULL, NULL
};


/* ------------------------------------------------------------------ */
/* Clock                                                              */
/* ------------------------------------------------------------------ */
/* A small analogue clock. The window manager calls tick() while the
 * desktop is idle, so the hands move without any mouse movement. */
static void line_to(int x0, int y0, int x1, int y1, u8 color)
{
    int dx = x1 - x0, dy = y1 - y0;
    int steps = (dx < 0 ? -dx : dx);
    if ((dy < 0 ? -dy : dy) > steps)
        steps = (dy < 0 ? -dy : dy);
    if (steps == 0) {
        gfx_pixel(x0, y0, color);
        return;
    }
    for (int i = 0; i <= steps; i++) {
        int x = x0 + dx * i / steps;
        int y = y0 + dy * i / steps;
        gfx_pixel(x, y, color);
    }
}

static void circle(int cx, int cy, int r, u8 color)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gfx_pixel(cx + x, cy + y, color); gfx_pixel(cx + y, cy + x, color);
        gfx_pixel(cx - y, cy + x, color); gfx_pixel(cx - x, cy + y, color);
        gfx_pixel(cx - x, cy - y, color); gfx_pixel(cx - y, cy - x, color);
        gfx_pixel(cx + y, cy - x, color); gfx_pixel(cx + x, cy - y, color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

static u32 clock_last_second = 0xFFFFFFFF;

/* sin/cos in degrees, scaled by 1000, on a 30-degree table.
 * Angle 0 points at 12 o'clock and grows clockwise, like a real dial. */
static void polar(int cx, int cy, int angle, int radius, int *out_x, int *out_y)
{
    static const int tab[12] = { 0, 500, 866, 1000, 866, 500, 0, -500, -866, -1000, -866, -500 };

    int i = (angle / 30) % 12;
    int frac = angle % 30;

    int sn = (tab[i] * (30 - frac) + tab[(i + 1) % 12] * frac) / 30;
    int cs = (tab[(i + 3) % 12] * (30 - frac) + tab[(i + 4) % 12] * frac) / 30;

    *out_x = cx + sn * radius / 1000;
    *out_y = cy - cs * radius / 1000;
}

static void clock_draw(struct window *w)
{
    clear_content(w, C_WIN);

    u32 year, month, day, hour, minute, second;
    rtc_read(&year, &month, &day, &hour, &minute, &second);

    int cx = w->x + w->w / 2;
    int cy = w->y + 24 + (w->h - 58) / 2;
    int r = (w->h - 76) / 2;
    if (w->w / 2 - 14 < r)
        r = w->w / 2 - 14;
    if (r < 26)
        r = 26;
    if (cy - r < w->y + 22)
        cy = w->y + 22 + r;
    if (cy + r > w->y + w->h - 26)
        cy = w->y + w->h - 26 - r;

    circle(cx, cy, r, C_BORDER);
    circle(cx, cy, r - 1, C_BORDER);

    /* hour marks */
    for (int i = 0; i < 12; i++) {
        int x1, y1, x2, y2;
        polar(cx, cy, i * 30, r - 3, &x1, &y1);
        polar(cx, cy, i * 30, r - (i % 3 == 0 ? 11 : 8), &x2, &y2);
        line_to(x2, y2, x1, y1, i % 3 == 0 ? C_TEXT : C_TEXT_MUTED);
    }

    /* hands */
    int hx, hy, mx2, my2, sx, sy;
    int hour_angle = ((int)(hour % 12) * 30) + (int)minute / 2;
    polar(cx, cy, hour_angle, r * 52 / 100, &hx, &hy);
    polar(cx, cy, (int)minute * 6, r * 76 / 100, &mx2, &my2);
    polar(cx, cy, (int)second * 6, r * 84 / 100, &sx, &sy);

    line_to(cx, cy, hx, hy, C_TEXT);
    line_to(hx, hy, (hx + cx) / 2, (hy + cy) / 2, C_TEXT);
    line_to(cx, cy, mx2, my2, C_TEXT);
    line_to(cx, cy, sx, sy, C_RED);

    gfx_fill(cx - 2, cy - 2, 5, 5, C_ACCENT);

    char text[48];
    ksnprintf(text, sizeof(text), "%02u:%02u:%02u", hour, minute, second);
    put(w, (w->w - gfx_text_width(text)) / 2, w->h - 24, text, C_ACCENT2);

    ksnprintf(text, sizeof(text), "%04u-%02u-%02u", year, month, day);
    put(w, (w->w - gfx_text_width(text)) / 2, 6, text, C_TEXT_MUTED);
}

static int clock_tick(struct window *w)
{
    u32 year, month, day, hour, minute, second;
    rtc_read(&year, &month, &day, &hour, &minute, &second);
    (void)w;
    if (second != clock_last_second) {
        clock_last_second = second;
        return 1;
    }
    return 0;
}

const struct app app_clock = {
    "Clock", "Clock", 240, 250,
    NULL, clock_draw, NULL, NULL, NULL, clock_tick
};

/* ------------------------------------------------------------------ */
/* Calculator                                                         */
/* ------------------------------------------------------------------ */
struct calc_state {
    char display[24];
    int  value;         /* the number on screen            */
    int  acc;           /* the number held for the next op */
    char op;            /* pending operator, 0 = none      */
    int  fresh;         /* next digit starts a new number  */
};

static struct calc_state calc_data;

static void calc_reset(void)
{
    calc_data.value = 0;
    calc_data.acc = 0;
    calc_data.op = 0;
    calc_data.fresh = 1;
    strcpy_s(calc_data.display, sizeof(calc_data.display), "0");
}

static void calc_show(void)
{
    ksnprintf(calc_data.display, sizeof(calc_data.display), "%d", calc_data.value);
}

static void calc_digit(int d)
{
    if (calc_data.fresh) {
        calc_data.value = d;
        calc_data.fresh = 0;
    } else {
        int next = calc_data.value * 10 + d;
        if (next > 99999999 / 10) {
            calc_show();
            return;
        }
        calc_data.value = next;
    }
    calc_show();
}

static int apply_op(int a, int b, char op)
{
    if (op == '+') return a + b;
    if (op == '-') return a - b;
    if (op == '*') return a * b;
    if (op == '/' && b != 0) return a / b;
    return 0;
}

static void calc_operator(char op)
{
    if (calc_data.op && !calc_data.fresh)
        calc_data.acc = apply_op(calc_data.acc, calc_data.value, calc_data.op);
    else
        calc_data.acc = calc_data.value;

    calc_data.value = calc_data.acc;
    calc_data.op = op;
    calc_data.fresh = 1;
    calc_show();
}

static void calc_equals(void)
{
    if (calc_data.op) {
        calc_data.value = apply_op(calc_data.acc, calc_data.value, calc_data.op);
        calc_data.op = 0;
        calc_data.fresh = 1;
        calc_show();
    }
}

static void calc_key_char(char c)
{
    if (c >= '0' && c <= '9')
        calc_digit(c - '0');
    else if (c == '+' || c == '-' || c == '*' || c == '/')
        calc_operator(c);
    else if (c == '=' || c == '\n')
        calc_equals();
    else if (c == 'c' || c == 'C' || c == 27)
        calc_reset();
    wm_redraw();
}

/* button table: label, row, col, colspan */
struct calc_button {
    const char *label;
    int row, col, span;
};

static const struct calc_button calc_buttons[] = {
    { "C",  0, 0, 2 }, { "<-", 0, 2, 1 }, { "/", 0, 3, 1 },
    { "7",  1, 0, 1 }, { "8",  1, 1, 1 }, { "9", 1, 2, 1 }, { "*", 1, 3, 1 },
    { "4",  2, 0, 1 }, { "5",  2, 1, 1 }, { "6", 2, 2, 1 }, { "-", 2, 3, 1 },
    { "1",  3, 0, 1 }, { "2",  3, 1, 1 }, { "3", 3, 2, 1 }, { "+", 3, 3, 1 },
    { "0",  4, 0, 2 }, { "=",  4, 2, 2 },
};
#define CALC_BUTTONS ((int)(sizeof(calc_buttons) / sizeof(calc_buttons[0])))

static void calc_draw(struct window *w)
{
    clear_content(w, C_WIN);

    /* display */
    gfx_fill(w->x + 8, w->y + 8, w->w - 16, 34, C_BLACK);
    gfx_frame(w->x + 8, w->y + 8, w->w - 16, 34, C_BORDER);
    int tw = gfx_text_width(calc_data.display);
    put(w, w->w - 16 - tw, 17, calc_data.display, C_ACCENT);

    int top = 52;
    int pad = 6;
    int bw = (w->w - 16 - pad * 3) / 4;
    int bh = (w->h - top - 10 - pad * 4) / 5;
    if (bh > 40) bh = 40;
    if (bh < 18) bh = 18;

    int mx = mouse_x() - w->x;
    int my = mouse_y() - w->y;

    for (int i = 0; i < CALC_BUTTONS; i++) {
        const struct calc_button *b = &calc_buttons[i];
        int x = 8 + b->col * (bw + pad);
        int y = top + b->row * (bh + pad);
        int width = b->span * bw + (b->span - 1) * pad;

        int hover = ui_hit(mx, my, x, y, width, bh);
        u8 bg = C_WIN_HI;
        u8 fg = C_TEXT;

        if (b->label[0] == '=') { bg = C_ACCENT; fg = C_BLACK; }
        else if (b->label[0] == 'C') { bg = C_RED; fg = C_WHITE; }
        else if (b->col == 3 || b->label[0] == '<') { bg = C_WIN_ALT; fg = C_ACCENT2; }

        if (hover) {
            bg = (bg == C_ACCENT) ? C_WHITE : (bg == C_RED ? C_WARN : C_HOVER);
            fg = C_WHITE;
        }

        ui_button(w->x + x, w->y + y, width, bh, b->label, bg, fg);
    }
}

static void calc_click(struct window *w, int x, int y)
{
    int top = 52;
    int pad = 6;
    int bw = (w->w - 16 - pad * 3) / 4;
    int bh = (w->h - top - 10 - pad * 4) / 5;
    if (bh > 40) bh = 40;
    if (bh < 18) bh = 18;

    for (int i = 0; i < CALC_BUTTONS; i++) {
        const struct calc_button *b = &calc_buttons[i];
        int bx = 8 + b->col * (bw + pad);
        int by = top + b->row * (bh + pad);
        int width = b->span * bw + (b->span - 1) * pad;

        if (ui_hit(x, y, bx, by, width, bh)) {
            if (b->label[0] == '<') {
                calc_data.value /= 10;
                calc_show();
                wm_redraw();
            } else if (b->label[0] == 'C') {
                calc_reset();
                wm_redraw();
            } else if (b->label[0] == '=') {
                calc_equals();
                wm_redraw();
            } else {
                calc_key_char(b->label[0]);
            }
            return;
        }
    }
}

static void calc_key(struct window *w, int key)
{
    (void)w;
    if (key == '\b') {
        calc_data.value /= 10;
        calc_show();
        wm_redraw();
    } else if (key < 128) {
        calc_key_char((char)key);
    }
}

static void calc_open(struct window *w)
{
    (void)w;
    calc_reset();
}

const struct app app_calc = {
    "Calculator", "Calc", 250, 300,
    calc_open, calc_draw, calc_key, calc_click, NULL, NULL
};

/* ------------------------------------------------------------------ */
/* Snake                                                              */
/* ------------------------------------------------------------------ */
#define SNAKE_COLS 20
#define SNAKE_ROWS 14
#define SNAKE_MAX  (SNAKE_COLS * SNAKE_ROWS)

struct snake_state {
    int  body_x[SNAKE_MAX];
    int  body_y[SNAKE_MAX];
    int  length;
    int  dir_x, dir_y;
    int  want_x, want_y;
    int  food_x, food_y;
    int  score;
    int  dead;
    int  started;
    u32  seed;
    u32  last_step;
};

static struct snake_state snake_data;

static u32 snake_random(void)
{
    snake_data.seed = snake_data.seed * 1103515245u + 12345u;
    return (snake_data.seed >> 16) & 0x7FFF;
}

static int snake_body_at(int x, int y)
{
    for (int i = 0; i < snake_data.length; i++)
        if (snake_data.body_x[i] == x && snake_data.body_y[i] == y)
            return 1;
    return 0;
}

static void snake_place_food(void)
{
    for (int tries = 0; tries < 400; tries++) {
        int x = (int)(snake_random() % SNAKE_COLS);
        int y = (int)(snake_random() % SNAKE_ROWS);
        if (!snake_body_at(x, y)) {
            snake_data.food_x = x;
            snake_data.food_y = y;
            return;
        }
    }
    snake_data.food_x = 0;
    snake_data.food_y = 0;
}

static void snake_reset(struct window *w)
{
    (void)w;
    snake_data.length = 4;
    snake_data.dir_x = 1;
    snake_data.dir_y = 0;
    snake_data.want_x = 1;
    snake_data.want_y = 0;
    snake_data.score = 0;
    snake_data.dead = 0;
    snake_data.started = 0;
    snake_data.seed = timer_ticks() * 2654435761u + 12345u;
    snake_data.last_step = timer_ticks();

    for (int i = 0; i < snake_data.length; i++) {
        snake_data.body_x[i] = 6 - i;
        snake_data.body_y[i] = 7;
    }
    snake_place_food();
}

static void snake_step(void)
{
    struct snake_state *s = &snake_data;

    s->dir_x = s->want_x;
    s->dir_y = s->want_y;

    int hx = s->body_x[0] + s->dir_x;
    int hy = s->body_y[0] + s->dir_y;

    if (hx < 0 || hy < 0 || hx >= SNAKE_COLS || hy >= SNAKE_ROWS) {
        s->dead = 1;
        return;
    }

    /* the tail moves out of the way, so it does not count as a collision */
    for (int i = 0; i < s->length - 1; i++) {
        if (s->body_x[i] == hx && s->body_y[i] == hy) {
            s->dead = 1;
            return;
        }
    }

    int ate = (hx == s->food_x && hy == s->food_y);

    int new_length = ate ? s->length + 1 : s->length;
    if (new_length > SNAKE_MAX)
        new_length = SNAKE_MAX;

    for (int i = new_length - 1; i > 0; i--) {
        s->body_x[i] = s->body_x[i - 1];
        s->body_y[i] = s->body_y[i - 1];
    }
    s->body_x[0] = hx;
    s->body_y[0] = hy;
    s->length = new_length;

    if (ate) {
        s->score += 10;
        snake_place_food();
    }
}

static void snake_draw(struct window *w)
{
    struct snake_state *s = &snake_data;

    clear_content(w, C_WIN);

    int cell = (w->w - 12) / SNAKE_COLS;
    int cell_h = (w->h - 44) / SNAKE_ROWS;
    if (cell_h < cell)
        cell = cell_h;
    if (cell < 4)
        cell = 4;

    int board_w = cell * SNAKE_COLS;
    int board_h = cell * SNAKE_ROWS;
    int bx = w->x + (w->w - board_w) / 2;
    int by = w->y + 34 + (w->h - 40 - board_h) / 2;
    if (by < w->y + 30)
        by = w->y + 30;

    char score[32];
    ksnprintf(score, sizeof(score), "Score %d", s->score);
    put(w, 10, 8, score, C_ACCENT);

    const char *hint = wm_is_focused(w) ? "arrows to steer" : "click to focus";
    put(w, w->w - 10 - gfx_text_width(hint), 8, hint, C_TEXT_MUTED);

    gfx_fill(bx - 2, by - 2, board_w + 4, board_h + 4, C_BORDER);
    gfx_fill(bx, by, board_w, board_h, C_BLACK);

    /* faint grid */
    for (int i = 1; i < SNAKE_COLS; i++)
        gfx_vline(bx + i * cell, by, board_h, C_WIN_ALT);
    for (int i = 1; i < SNAKE_ROWS; i++)
        gfx_hline(bx, by + i * cell, board_w, C_WIN_ALT);

    /* food */
    gfx_fill(bx + s->food_x * cell + 2, by + s->food_y * cell + 2,
             cell - 4, cell - 4, C_RED);

    /* body */
    for (int i = s->length - 1; i >= 0; i--) {
        u8 color = (i == 0) ? C_ACCENT : (i % 2 ? C_ACCENT2 : C_GREEN_DK);
        gfx_fill(bx + s->body_x[i] * cell + 1, by + s->body_y[i] * cell + 1,
                 cell - 2, cell - 2, color);
    }

    if (!s->started && !s->dead) {
        const char *go = "Press an arrow key to start";
        int tx = bx + (board_w - gfx_text_width(go)) / 2;
        int ty = by + board_h / 2 - 8;
        gfx_fill(tx - 6, ty - 6, gfx_text_width(go) + 12, 28, C_BLACK);
        gfx_frame(tx - 6, ty - 6, gfx_text_width(go) + 12, 28, C_ACCENT2);
        gfx_text(tx, ty, go, C_WHITE);
    }

    if (s->dead) {
        const char *over = "Game over - click to play again";
        int tx = bx + (board_w - gfx_text_width(over)) / 2;
        int ty = by + board_h / 2 - 8;
        gfx_fill(tx - 6, ty - 6, gfx_text_width(over) + 12, 28, C_BLACK);
        gfx_frame(tx - 6, ty - 6, gfx_text_width(over) + 12, 28, C_RED);
        gfx_text(tx, ty, over, C_WHITE);
    }
}

static void snake_key(struct window *w, int key)
{
    struct snake_state *s = &snake_data;
    (void)w;

    if (s->dead) {
        if (key == '\n' || key == ' ')
            snake_reset(w);
        else
            return;
        wm_redraw();
        return;
    }

    if (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN)
        s->started = 1;

    if (key == KEY_LEFT  && s->dir_x == 0) { s->want_x = -1; s->want_y = 0; }
    if (key == KEY_RIGHT && s->dir_x == 0) { s->want_x =  1; s->want_y = 0; }
    if (key == KEY_UP    && s->dir_y == 0) { s->want_x =  0; s->want_y = -1; }
    if (key == KEY_DOWN  && s->dir_y == 0) { s->want_x =  0; s->want_y =  1; }
}

static void snake_click(struct window *w, int x, int y)
{
    (void)x; (void)y;
    if (snake_data.dead)
        snake_reset(w);
    wm_redraw();
}

static int snake_tick(struct window *w)
{
    struct snake_state *s = &snake_data;

    if (!s->started || s->dead || !wm_is_focused(w))
        return 0;

    u32 now = timer_ticks();
    u32 interval = s->score > 90 ? 6 : (s->score > 50 ? 7 : (s->score > 20 ? 8 : 10));
    if (now - s->last_step < interval)
        return 0;

    s->last_step = now;
    snake_step();
    return 1;
}

static void snake_open(struct window *w)
{
    snake_reset(w);
}

const struct app app_snake = {
    "Snake", "Snake", 320, 300,
    snake_open, snake_draw, snake_key, snake_click, NULL, snake_tick
};

/* ------------------------------------------------------------------ */
/* Opening a file from the file manager                               */
/* ------------------------------------------------------------------ */
extern const struct app app_editor;
extern const struct app app_browser;

void app_open_file(const char *path)
{
    int node = fs_resolve(path);
    if (node < 0)
        return;

    struct fs_node *n = fs_get(node);
    if (!n || n->is_dir)
        return;

    if (is_html(n->name)) {
        browser_go(node);
        wm_open(&app_browser);
    } else {
        editor_load(node);
        wm_open(&app_editor);
    }
}
