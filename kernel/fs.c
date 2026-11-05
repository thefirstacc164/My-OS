/*
 * MyOS - a very small in-memory file system.
 *
 * Everything lives in RAM and disappears on reboot. It is enough for the
 * file manager, the text editor and the browser to have something real
 * to work with.
 */
#include "kernel.h"

static struct fs_node nodes[FS_MAX_NODES];
static int node_count;

/* The saved image is a flat record list; 64 KB is plenty for a desktop
 * this size, and it means a save is a handful of ATA sectors. */
#define FS_IMAGE_SECTORS 128
#define FS_IMAGE_BYTES   (FS_IMAGE_SECTORS * 512)
#define FS_RECORD_HEADER 36      /* used, is_dir, pad, parent, size, name */

/* the save buffer lives at a fixed address, not in .bss */
static u8 *const image = (u8 *)FS_IMAGE_ADDR;
static int dirty;

static void put32(u8 *p, u32 v)
{
    p[0] = (u8)(v & 0xFF);
    p[1] = (u8)((v >> 8) & 0xFF);
    p[2] = (u8)((v >> 16) & 0xFF);
    p[3] = (u8)((v >> 24) & 0xFF);
}

static u32 get32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int new_node(int parent, const char *name, int is_dir)
{
    if (node_count >= FS_MAX_NODES)
        return -1;

    struct fs_node *n = &nodes[node_count];
    n->parent = parent;
    n->is_dir = is_dir;
    n->size = 0;
    n->used = 1;
    n->data[0] = '\0';

    u32 i = 0;
    while (name[i] && i < FS_NAME_MAX - 1) {
        n->name[i] = name[i];
        i++;
    }
    n->name[i] = '\0';

    return node_count++;
}

int fs_create(int dir, const char *name, int is_dir)
{
    if (dir < 0 || dir >= node_count || !nodes[dir].is_dir)
        return -1;
    if (fs_find(dir, name) >= 0)
        return -1;
    dirty = 1;
    return new_node(dir, name, is_dir);
}

int fs_write(int index, const char *data)
{
    if (index < 0 || index >= node_count || nodes[index].is_dir)
        return -1;

    struct fs_node *n = &nodes[index];
    int i = 0;
    while (data[i] && i < FS_FILE_MAX - 1) {
        n->data[i] = data[i];
        i++;
    }
    n->data[i] = '\0';
    n->size = i;
    dirty = 1;
    return i;
}

struct fs_node *fs_get(int index)
{
    if (index < 0 || index >= node_count || !nodes[index].used)
        return NULL;
    return &nodes[index];
}

int fs_node_count(void) { return node_count; }
int fs_root(void)       { return 0; }

int fs_find(int dir, const char *name)
{
    if (dir < 0 || dir >= node_count)
        return -1;
    for (int i = 1; i < node_count; i++) {
        if (!nodes[i].used || nodes[i].parent != dir)
            continue;
        if (strcmp(nodes[i].name, name) == 0)
            return i;
    }
    return -1;
}

int fs_child_count(int dir)
{
    int n = 0;
    for (int i = 1; i < node_count; i++)
        if (nodes[i].used && nodes[i].parent == dir)
            n++;
    return n;
}

int fs_child(int dir, int nth)
{
    for (int i = 1; i < node_count; i++) {
        if (!nodes[i].used || nodes[i].parent != dir)
            continue;
        if (nth-- == 0)
            return i;
    }
    return -1;
}

static void remove_tree(int index)
{
    for (int i = 1; i < node_count; i++)
        if (nodes[i].used && nodes[i].parent == index)
            remove_tree(i);
    nodes[index].used = 0;
}

int fs_remove(int index)
{
    if (index <= 0 || index >= node_count)
        return -1;
    remove_tree(index);
    dirty = 1;
    return 0;
}

int fs_path(int index, char *out, u32 size)
{
    if (index < 0 || index >= node_count)
        return -1;

    int chain[16];
    int depth = 0;
    int cur = index;
    while (cur > 0 && depth < 16) {
        chain[depth++] = cur;
        cur = nodes[cur].parent;
    }

    u32 pos = 0;
    out[pos++] = '/';
    for (int i = depth - 1; i >= 0; i--) {
        const char *name = nodes[chain[i]].name;
        while (*name && pos < size - 2)
            out[pos++] = *name++;
        if (i > 0 && pos < size - 2)
            out[pos++] = '/';
    }
    out[pos] = '\0';
    return 0;
}

int fs_resolve(const char *path)
{
    if (!path || path[0] != '/')
        return -1;

    int cur = 0;
    const char *p = path;
    char name[FS_NAME_MAX];

    while (*p == '/')
        p++;

    while (*p) {
        u32 n = 0;
        while (*p && *p != '/') {
            if (n < FS_NAME_MAX - 1)
                name[n++] = *p;
            p++;
        }
        name[n] = '\0';
        while (*p == '/')
            p++;

        cur = fs_find(cur, name);
        if (cur < 0)
            return -1;
    }
    return cur;
}

/* ------------------------------------------------------------------ */
/* Saving to and loading from the ATA disk                            */
/* ------------------------------------------------------------------ */
void fs_mark_dirty(void) { dirty = 1; }
int  fs_is_dirty(void)   { return dirty; }

int fs_save(void)
{
    if (!ata_present())
        return -1;

    u32 pos = 16;
    u32 count = 0;

    for (int i = 0; i < node_count; i++) {
        struct fs_node *n = &nodes[i];
        u32 size = (n->used && !n->is_dir) ? (u32)n->size : 0;
        u32 need = (FS_RECORD_HEADER + size + 3) & ~3u;

        if (pos + need > FS_IMAGE_BYTES)
            break;

        u8 *r = image + pos;
        r[0] = (u8)(n->used ? 1 : 0);
        r[1] = (u8)(n->is_dir ? 1 : 0);
        r[2] = 0;
        r[3] = 0;
        put32(r + 4, n->parent < 0 ? 0xFFFFFFFFu : (u32)n->parent);
        put32(r + 8, size);
        memset(r + 12, 0, FS_NAME_MAX);
        memcpy(r + 12, n->name, strlen(n->name));
        if (size)
            memcpy(r + FS_RECORD_HEADER, n->data, size);

        pos += need;
        count++;
    }

    memcpy(image, "MYOSFS01", 8);
    put32(image + 8, count);
    put32(image + 12, pos);

    if (ata_write(0, image, (pos + 511) / 512) < 0)
        return -1;

    dirty = 0;
    return (int)count;
}

int fs_load(void)
{
    if (!ata_present())
        return -1;
    if (ata_read(0, image, FS_IMAGE_SECTORS) < 0)
        return -1;
    if (strncmp((const char *)image, "MYOSFS01", 8) != 0)
        return -1;

    u32 count = get32(image + 8);
    u32 bytes = get32(image + 12);

    if (count < 2 || count > FS_MAX_NODES)
        return -1;
    if (bytes < 16 || bytes > FS_IMAGE_BYTES)
        return -1;

    memset(nodes, 0, sizeof(nodes));
    node_count = (int)count;

    u32 pos = 16;
    for (u32 i = 0; i < count; i++) {
        if (pos + FS_RECORD_HEADER > bytes)
            goto bad;

        u8 *r = image + pos;
        struct fs_node *n = &nodes[i];

        n->used = r[0] ? 1 : 0;
        n->is_dir = r[1] ? 1 : 0;

        u32 parent = get32(r + 4);
        n->parent = (parent == 0xFFFFFFFFu) ? -1 : (int)parent;

        u32 size = get32(r + 8);
        if (size > FS_FILE_MAX - 1)
            goto bad;

        memcpy(n->name, r + 12, FS_NAME_MAX);
        n->name[FS_NAME_MAX - 1] = '\0';
        n->size = n->is_dir ? 0 : (int)size;

        pos += FS_RECORD_HEADER;
        n->data[0] = '\0';
        if (size) {
            if (pos + size > bytes)
                goto bad;
            memcpy(n->data, image + pos, size);
            n->data[size] = '\0';
            pos += size;
        }
        pos = (pos + 3) & ~3u;
    }

    dirty = 0;
    return (int)count;

bad:
    node_count = 0;
    return -1;
}

/* write the file system back if anything changed */
int fs_sync(void)
{
    if (!dirty || !ata_present())
        return 0;
    return fs_save();
}

/* wipe the disk and start from the built-in files again */
void fs_format(void)
{
    fs_init();
    if (ata_present())
        fs_save();
    dirty = 0;
}

/* ------------------------------------------------------------------ */
/* Contents the machine boots with                                    */
/* ------------------------------------------------------------------ */
void fs_init(void)
{
    node_count = 0;
    memset(nodes, 0, sizeof(nodes));

    int root = new_node(-1, "/", 1);

    int home = new_node(root, "home", 1);
    int system = new_node(root, "system", 1);

    int welcome = new_node(home, "welcome.txt", 0);
    fs_write(welcome,
        "Welcome to MyOS\n"
        "===============\n"
        "\n"
        "This is a real operating system, written from scratch:\n"
        "a 512-byte boot sector and a C kernel that talks straight to\n"
        "the hardware. No Linux underneath.\n"
        "\n"
        "Things to try:\n"
        "  - open the Terminal from the top bar and type help\n"
        "  - edit this file and press Save\n"
        "  - open the Browser and click around the local pages\n"
        "  - drag windows by their title bar\n"
        "\n"
        "Everything lives in RAM, so a reboot forgets it all.\n");

    int notes = new_node(home, "notes.txt", 0);
    fs_write(notes,
        "MyOS notes\n"
        "----------\n"
        "\n"
        "- 640x480, 8-bit colour, drawn by our own code\n"
        "- keyboard and mouse are driven by interrupts\n"
        "- the top bar clock reads the CMOS chip\n");

    int page = new_node(home, "start.html", 0);
    fs_write(page,
        "# MyOS Browser\n"
        "\n"
        "This is a page stored inside the operating system and rendered\n"
        "by our own little layout engine.\n"
        "\n"
        "* Headings, paragraphs and lists work\n"
        "* Links work too, try [About MyOS](about.html)\n"
        "* A 640x480 screen with an 8x16 font keeps it honest\n"
        "\n"
        "## Where to go next\n"
        "\n"
        "Read [Notes](notes.txt) with the editor, or go back to the\n"
        "[Start page](start.html).\n");

    int about_page = new_node(home, "about.html", 0);
    fs_write(about_page,
        "# About MyOS\n"
        "\n"
        "Version 0.2 - written by hand, one file at a time.\n"
        "\n"
        "* Boot sector: 512 bytes of assembly\n"
        "* Kernel: C, 32-bit protected mode\n"
        "* Graphics: VESA 640x480x8 linear framebuffer\n"
        "* Input: PS/2 keyboard and mouse\n"
        "* Storage: this in-memory file system\n"
        "\n"
        "Back to the [Start page](start.html).\n");

    int version = new_node(system, "version.txt", 0);
    fs_write(version,
        "MyOS 0.2 \"Desktop\"\n"
        "built with gcc, GNU as and ld\n"
        "runs on v86 in your browser\n");

    (void)system;
    (void)version;
}
