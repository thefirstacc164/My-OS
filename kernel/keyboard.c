/*
 * MyOS - PS/2 keyboard driver (scan code set 1, US layout).
 *
 * The IRQ handler translates hardware scan codes into ASCII characters
 * and pushes them into a ring buffer. The shell pulls characters out of
 * that buffer at its own pace.
 */
#include "kernel.h"

#define BUFFER_SIZE 256

static volatile int buffer[BUFFER_SIZE];
static volatile u32 head, tail;

static int shift_down, ctrl_down, caps_lock, extended;

/* Printable keys, unshifted (index = scan code) */
static const char keymap[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
    [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
    [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
    [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
    [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',',
    [0x34] = '.', [0x35] = '/',
    [0x39] = ' ',
};

/* Same keys with Shift held */
static const char keymap_shift[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
    [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',
    [0x1A] = '{', [0x1B] = '}', [0x27] = ':', [0x28] = '"',
    [0x29] = '~', [0x2B] = '|', [0x33] = '<', [0x34] = '>',
    [0x35] = '?',
};

static void push(int value)
{
    u32 next = (head + 1) % BUFFER_SIZE;
    if (next != tail) {
        buffer[head] = value;
        head = next;
    }
}

static void keyboard_handler(void)
{
    u8 scancode = inb(0x60);

    if (scancode == 0xE0) {          /* start of an extended key */
        extended = 1;
        return;
    }

    if (scancode & 0x80) {           /* key released */
        u8 code = (u8)(scancode & 0x7F);
        if (code == 0x2A || code == 0x36)
            shift_down = 0;
        else if (code == 0x1D)
            ctrl_down = 0;
        extended = 0;
        return;
    }

    if (extended) {
        switch (scancode) {
        case 0x48: push(KEY_UP);    break;
        case 0x50: push(KEY_DOWN);  break;
        case 0x4B: push(KEY_LEFT);  break;
        case 0x4D: push(KEY_RIGHT); break;
        case 0x49: push(KEY_PGUP);  break;
        case 0x51: push(KEY_PGDN);  break;
        case 0x53: push(KEY_DELETE); break;
        default: break;
        }
        extended = 0;
        return;
    }

    switch (scancode) {
    case 0x2A: case 0x36: shift_down = 1; return;
    case 0x1D: ctrl_down = 1; return;
    case 0x3A: caps_lock ^= 1; return;
    default: break;
    }

    char c = keymap[scancode];
    if (c >= 'a' && c <= 'z') {
        if (shift_down ^ caps_lock)
            c = (char)(c - 'a' + 'A');
    } else if (shift_down && keymap_shift[scancode]) {
        c = keymap_shift[scancode];
    }

    if (c)
        push((int)(u8)c);
}

void keyboard_init(void)
{
    head = tail = 0;
    shift_down = ctrl_down = caps_lock = extended = 0;

    /* Drain anything the BIOS may have left in the controller. */
    while (inb(0x64) & 0x01)
        (void)inb(0x60);

    irq_install(1, keyboard_handler);
}

int keyboard_getchar(void)
{
    for (;;) {
        if (head == tail)
            return -1;
        int value = buffer[tail];
        tail = (tail + 1) % BUFFER_SIZE;
        if (value < 0x100)
            return value;
    }
}

int keyboard_getkey(void)
{
    if (head == tail)
        return -1;
    int value = buffer[tail];
    tail = (tail + 1) % BUFFER_SIZE;
    return value;
}

int keyboard_getchar_blocking(void)
{
    for (;;) {
        int c = keyboard_getchar();
        if (c >= 0)
            return c;
        cpu_wait_interrupt();
    }
}
