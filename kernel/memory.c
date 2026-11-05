/*
 * MyOS - physical memory information left behind by the boot sector.
 */
#include "kernel.h"

#define E820_COUNT_ADDR 0x5000
#define E820_MAP_ADDR   0x5004

u16 e820_entry_count(void)
{
    return *(volatile u16 *)E820_COUNT_ADDR;
}

struct e820_entry *e820_entries(void)
{
    return (struct e820_entry *)E820_MAP_ADDR;
}

u64 memory_total_bytes(void)
{
    u64 total = 0;
    u16 count = e820_entry_count();
    struct e820_entry *map = e820_entries();

    for (u16 i = 0; i < count && i < E820_MAX; i++) {
        if (map[i].type == 1)
            total += map[i].length;
    }
    return total;
}
