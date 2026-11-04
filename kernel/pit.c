/*
 * MyOS - Programmable Interval Timer (IRQ0), the system clock.
 */
#include "kernel.h"

#define PIT_BASE_FREQUENCY 1193182

static volatile u32 ticks;
static u32 pit_hz = 100;

static void pit_handler(void)
{
    ticks++;
}

void timer_init(u32 hz)
{
    pit_hz = hz;
    u32 divisor = PIT_BASE_FREQUENCY / hz;

    outb(0x43, 0x36);                       /* channel 0, square wave */
    outb(0x40, (u8)(divisor & 0xFF));
    outb(0x40, (u8)((divisor >> 8) & 0xFF));

    irq_install(0, pit_handler);
}

u32 timer_ticks(void)
{
    return ticks;
}

u32 timer_uptime_seconds(void)
{
    return ticks / pit_hz;
}

void timer_sleep_ms(u32 ms)
{
    u32 end = ticks + (ms * pit_hz) / 1000 + 1;
    while ((i32)(ticks - end) < 0)
        cpu_wait_interrupt();
}
