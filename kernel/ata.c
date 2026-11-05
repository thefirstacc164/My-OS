/*
 * MyOS - ATA (IDE) disk driver, programmed I/O, 28-bit LBA.
 *
 * Talks to the primary bus master only. Everything is done by polling the
 * status register instead of using interrupts: this disk is a few hundred
 * kilobytes of saved files, not a database.
 */
#include "kernel.h"

#define ATA_DATA    0x1F0
#define ATA_ERROR   0x1F1
#define ATA_FEAT    0x1F1
#define ATA_COUNT   0x1F2
#define ATA_LBA0    0x1F3
#define ATA_LBA1    0x1F4
#define ATA_LBA2    0x1F5
#define ATA_DRIVE   0x1F6
#define ATA_STATUS  0x1F7
#define ATA_COMMAND 0x1F7
#define ATA_CTRL    0x3F6

#define ST_ERR  0x01
#define ST_DRQ  0x08
#define ST_DF   0x20
#define ST_DRDY 0x40
#define ST_BSY  0x80

static int disk_present;
static u32 disk_sectors;

static inline void insw(u16 port, void *buffer, u32 words)
{
    __asm__ volatile("rep insw"
                     : "+D"(buffer), "+c"(words)
                     : "d"(port)
                     : "memory");
}

static inline void outsw(u16 port, const void *buffer, u32 words)
{
    __asm__ volatile("rep outsw"
                     : "+S"(buffer), "+c"(words)
                     : "d"(port)
                     : "memory");
}

/* the spec asks for a 400 ns pause after selecting a drive */
static void disk_delay(void)
{
    for (int i = 0; i < 4; i++)
        (void)inb(ATA_CTRL);
}

static int wait_not_busy(void)
{
    for (u32 i = 0; i < 4000000; i++) {
        u8 status = inb(ATA_STATUS);
        if (status == 0xFF)
            return 0;               /* no controller on this bus */
        if (!(status & ST_BSY))
            return (status & ST_ERR) ? 0 : 1;
    }
    return 0;
}

static int wait_for_data(void)
{
    for (u32 i = 0; i < 4000000; i++) {
        u8 status = inb(ATA_STATUS);
        if (status & ST_BSY)
            continue;
        if (status & ST_ERR)
            return 0;
        if (status & ST_DRQ)
            return 1;
    }
    return 0;
}

int ata_present(void)
{
    return disk_present;
}

u32 ata_sectors(void)
{
    return disk_sectors;
}

int ata_init(void)
{
    disk_present = 0;
    disk_sectors = 0;

    outb(ATA_DRIVE, 0xA0);          /* master, head 0 */
    disk_delay();

    outb(ATA_COUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_COMMAND, 0xEC);        /* IDENTIFY DEVICE */

    u8 status = inb(ATA_STATUS);
    if (status == 0 || status == 0xFF)
        return 0;                   /* nothing there */

    if (!wait_not_busy())
        return 0;
    if (!wait_for_data())
        return 0;

    u16 id[256];
    insw(ATA_DATA, id, 256);

    if (id[0] == 0 && id[1] == 0)
        return 0;                   /* not a hard disk */

    disk_sectors = (u32)id[60] | ((u32)id[61] << 16);
    disk_present = 1;
    return 1;
}

/* one PIO transfer, at most 255 sectors */
static int transfer(u32 lba, u8 *buffer, u32 sectors, u8 command)
{
    outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    disk_delay();

    outb(ATA_FEAT, 0);
    outb(ATA_COUNT, (u8)sectors);
    outb(ATA_LBA0, (u8)(lba & 0xFF));
    outb(ATA_LBA1, (u8)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (u8)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, command);

    for (u32 i = 0; i < sectors; i++) {
        if (!wait_for_data())
            return -1;
        if (command == 0x20)
            insw(ATA_DATA, buffer + i * 512, 256);
        else
            outsw(ATA_DATA, buffer + i * 512, 256);
    }

    if (command == 0x30) {
        outb(ATA_COMMAND, 0xE7);    /* flush the write cache */
        if (!wait_not_busy())
            return -1;
    }
    return 0;
}

int ata_read(u32 lba, void *buffer, u32 sectors)
{
    if (!disk_present)
        return -1;

    u8 *out = (u8 *)buffer;
    while (sectors > 0) {
        u32 chunk = sectors > 255 ? 255 : sectors;
        if (transfer(lba, out, chunk, 0x20) < 0)
            return -1;
        lba += chunk;
        out += chunk * 512;
        sectors -= chunk;
    }
    return 0;
}

int ata_write(u32 lba, const void *buffer, u32 sectors)
{
    if (!disk_present)
        return -1;

    const u8 *in = (const u8 *)buffer;
    while (sectors > 0) {
        u32 chunk = sectors > 255 ? 255 : sectors;
        if (transfer(lba, (u8 *)in, chunk, 0x30) < 0)
            return -1;
        lba += chunk;
        in += chunk * 512;
        sectors -= chunk;
    }
    return 0;
}
