/*
 * MyOS - PS/2 mouse driver.
 *
 * The 8042 controller delivers three-byte packets on IRQ12. The handler
 * turns them into a position and pushes events into a small ring buffer
 * that the window manager drains.
 */
#include "kernel.h"

#define MOUSE_QUEUE 64

static volatile struct mouse_event queue[MOUSE_QUEUE];
static volatile int q_head, q_tail;

static volatile int pos_x = 320, pos_y = 240;
static volatile int buttons;
static int bounds_w = 640, bounds_h = 480;

static u8 packet[3];
static int phase;

static void wait_write(void)
{
    for (int i = 0; i < 100000 && (inb(0x64) & 0x02); i++)
        ;
}

static void wait_read(void)
{
    for (int i = 0; i < 100000 && !(inb(0x64) & 0x01); i++)
        ;
}

static void aux_write(u8 value)
{
    wait_write();
    outb(0x64, 0xD4);      /* next byte goes to the mouse */
    wait_write();
    outb(0x60, value);
}

static u8 aux_read(void)
{
    wait_read();
    return inb(0x60);
}

static void push_event(u8 type)
{
    int next = (q_head + 1) % MOUSE_QUEUE;
    if (next == q_tail)
        return;
    queue[q_head].type = type;
    queue[q_head].button = (u8)(buttons & 0x07);
    queue[q_head].x = pos_x;
    queue[q_head].y = pos_y;
    q_head = next;
}

static void clamp_position(void)
{
    if (pos_x < 0) pos_x = 0;
    if (pos_y < 0) pos_y = 0;
    if (pos_x > bounds_w - 1) pos_x = bounds_w - 1;
    if (pos_y > bounds_h - 1) pos_y = bounds_h - 1;
}

static void mouse_handler(void)
{
    u8 data = inb(0x60);

    if (phase == 0) {
        /* A packet always starts with bit 3 set and bits 6/7 clear.
         * Anything else (an ACK byte, a stray byte) is dropped. */
        if ((data & 0xC8) != 0x08) {
            phase = 0;
            return;
        }
        packet[0] = data;
        phase = 1;
        return;
    }
    if (phase == 1) {
        packet[1] = data;
        phase = 2;
        return;
    }

    packet[2] = data;
    phase = 0;

    int delta_x = packet[1];
    int delta_y = packet[2];
    if (packet[0] & 0x10)
        delta_x -= 256;
    if (packet[0] & 0x20)
        delta_y -= 256;

    pos_x += delta_x;
    pos_y -= delta_y;
    clamp_position();

    int new_buttons = packet[0] & 0x07;
    int changed = new_buttons ^ buttons;
    buttons = new_buttons;

    if (delta_x || delta_y)
        push_event(1);
    if (changed & 0x01)
        push_event((buttons & 0x01) ? 2 : 3);
}

void mouse_set_bounds(int w, int h)
{
    bounds_w = w;
    bounds_h = h;
}

void mouse_init(void)
{
    q_head = q_tail = 0;
    phase = 0;
    buttons = 0;

    /* enable the auxiliary (mouse) port */
    wait_write();
    outb(0x64, 0xA8);

    /* set the controller command byte: aux interrupt on, mouse clock on */
    wait_write();
    outb(0x64, 0x20);
    u8 status = aux_read();
    status |= 0x02;
    status &= (u8)~0x20;
    wait_write();
    outb(0x64, 0x60);
    wait_write();
    outb(0x60, status);

    aux_write(0xF6);   /* set defaults */
    aux_read();
    aux_write(0xF4);   /* enable data reporting */
    aux_read();

    /* Throw away anything left in the controller so the first packet
     * we see is a real one, not an ACK byte. */
    while (inb(0x64) & 0x01)
        (void)inb(0x60);
    phase = 0;

    irq_install(12, mouse_handler);
}

int mouse_x(void) { return pos_x; }
int mouse_y(void) { return pos_y; }
int mouse_buttons(void) { return buttons; }

int mouse_pop(struct mouse_event *out)
{
    if (q_tail == q_head)
        return 0;
    out->type = queue[q_tail].type;
    out->button = queue[q_tail].button;
    out->x = queue[q_tail].x;
    out->y = queue[q_tail].y;
    q_tail = (q_tail + 1) % MOUSE_QUEUE;
    return 1;
}
