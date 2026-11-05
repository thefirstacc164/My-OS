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
