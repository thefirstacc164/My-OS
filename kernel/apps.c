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
#define FILES_VISIBLE 7

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

    for (int i = 0; i < FILES_VISIBLE && s->scroll + i < count; i++) {
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

    if (count > FILES_VISIBLE) {
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
    if (index < 0 || index >= FILES_VISIBLE)
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
    s->scroll += amount;
    if (s->scroll < 0) s->scroll = 0;
    if (s->scroll > count - FILES_VISIBLE) {
        s->scroll = count - FILES_VISIBLE;
        if (s->scroll < 0) s->scroll = 0;
    }
}

const struct app app_files = {
    "Files", "Files", 356, 250,
    files_open, files_draw, NULL, files_click, files_scroll
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

    for (int y = 0; y < TERM_ROWS; y++) {
        for (int x = 0; x < TERM_COLS; x++) {
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
    term_open, term_draw, term_key, NULL, NULL
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
    editor_open, editor_draw, editor_key, editor_click, NULL
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
    browser_open, browser_draw, NULL, browser_click, NULL
};

/* ------------------------------------------------------------------ */
/* About                                                              */
/* ------------------------------------------------------------------ */
static void about_draw(struct window *w)
{
    clear_content(w, C_WIN);

    put(w, 14, 10, "MyOS 0.2", C_ACCENT);
    put(w, 14, 28, "\"Desktop\" - a hand-written OS", C_TEXT_MUTED);

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
    NULL, about_draw, NULL, NULL, NULL
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
