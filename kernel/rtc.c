/*
 * MyOS - CMOS real-time clock driver.
 */
#include "kernel.h"

static u8 cmos_read(u8 reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static u8 bcd_to_binary(u8 value)
{
    return (u8)((value & 0x0F) + ((value >> 4) * 10));
}

void rtc_read(u32 *year, u32 *month, u32 *day,
              u32 *hour, u32 *minute, u32 *second)
{
    while (cmos_read(0x0A) & 0x80)   /* wait while the clock updates */
        ;

    u8 sec  = cmos_read(0x00);
    u8 min  = cmos_read(0x02);
    u8 hr   = cmos_read(0x04);
    u8 dys  = cmos_read(0x07);
    u8 mon  = cmos_read(0x08);
    u8 yr   = cmos_read(0x09);
    u8 stat = cmos_read(0x0B);

    int is_bcd = !(stat & 0x04);
    if (is_bcd) {
        sec = bcd_to_binary(sec);
        min = bcd_to_binary(min);
        hr  = bcd_to_binary((u8)(hr & 0x7F));
        dys = bcd_to_binary(dys);
        mon = bcd_to_binary(mon);
        yr  = bcd_to_binary(yr);
    }

    if (!(stat & 0x02) && (hr & 0x80))   /* 12-hour clock, PM */
        hr = (u8)(((hr & 0x7F) + 12) % 24);

    *second = sec;
    *minute = min;
    *hour   = hr;
    *day    = dys;
    *month  = mon;
    *year   = 2000u + yr;
}
