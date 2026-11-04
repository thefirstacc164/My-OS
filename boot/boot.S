/*
 * MyOS - Master Boot Record
 * -------------------------
 * This 512-byte program is the very first thing the computer runs.
 * The BIOS loads it at address 0x7C00 and jumps to it in 16-bit real mode.
 *
 * What it does, in order:
 *   1. Sets up the stack and saves the drive number the BIOS gave us.
 *   2. Asks the BIOS for the physical memory map (INT 15h, E820),
 *      and stores it at 0x5000 so the kernel can read it later.
 *   3. Reads the kernel off the floppy, sector by sector, to 0x10000.
 *   4. Opens the A20 line (needed to address more than 1 MB).
 *   5. Loads a GDT table and switches the CPU into 32-bit protected mode.
 *   6. Jumps into the kernel.
 */

.intel_syntax noprefix
.code16
.section .text
.global _start

/* KERNEL_SECTORS is supplied by the Makefile (-defsym) after the kernel
 * is compiled, so the bootloader always knows its exact size. */
.set KERNEL_SEG,   0x1000      /* kernel is loaded at 0x1000:0x0000 = 0x10000 */
.set E820_COUNT,   0x5000      /* u16: number of memory map entries          */
.set E820_BUF,     0x5004      /* 24-byte entries start here                 */

_start:
    jmp     start
    nop

start:
    cli
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     sp, 0x7C00
    sti

    mov     byte ptr [boot_drive], dl

    mov     si, offset s_boot
    call    print16

/* ---------------------------------------------------------------- */
/* Memory map from the BIOS                                          */
/* ---------------------------------------------------------------- */
    mov     di, E820_BUF
    xor     ebx, ebx
    xor     bp, bp                 /* bp = number of entries kept */

e820_loop:
    mov     eax, 0xE820
    mov     edx, 0x534D4150        /* 'SMAP' */
    mov     ecx, 24
    mov     dword ptr [di + 20], 1 /* ask for the ACPI 3 attributes */
    int     0x15
    jc      e820_done
    cmp     eax, 0x534D4150
    jne     e820_done
    test    ecx, ecx
    jz      e820_next              /* BIOS may report "entry ignored" */
    mov     eax, dword ptr [di + 12]   /* length high dword */
    test    eax, eax
    jnz     e820_keep
    cmp     dword ptr [di + 8], 0      /* length low dword */
    jz      e820_next              /* zero-length region: skip it */
e820_keep:
    add     di, 24
    inc     bp
e820_next:
    test    ebx, ebx
    jz      e820_done
    cmp     bp, 64
    jb      e820_loop

e820_done:
    mov     word ptr [E820_COUNT], bp

/* ---------------------------------------------------------------- */
/* Load the kernel: LBA -> CHS for a 1.44 MB floppy (18 sect, 2 heads)*/
/* ---------------------------------------------------------------- */
    mov     ax, KERNEL_SEG
    mov     es, ax
    mov     word ptr [cur_lba], 1  /* sector 0 is this boot sector */
    mov     word ptr [dest_off], 0

load_loop:
    mov     ax, word ptr [cur_lba]
    cmp     ax, KERNEL_SECTORS     /* KERNEL_SECTORS counts kernel sectors */
    ja      load_done

    xor     dx, dx
    mov     bx, 18
    div     bx                     /* ax = lba / 18, dx = lba % 18 */
    inc     dl
    mov     cl, dl                 /* cl = sector (1-based)         */
    xor     dx, dx
    mov     bx, 2
    div     bx                     /* ax = cylinder, dx = head      */
    mov     dh, dl                 /* dh = head                     */
    mov     ch, al                 /* ch = cylinder                 */

    mov     dl, byte ptr [boot_drive]
    mov     ax, 0x0201             /* read 1 sector                 */
    mov     bx, word ptr [dest_off]
    int     0x13
    jc      disk_error

    add     word ptr [dest_off], 512
    inc     word ptr [cur_lba]
    jmp     load_loop

load_done:
    xor     ax, ax
    mov     es, ax

/* ---------------------------------------------------------------- */
/* A20 line                                                          */
/* ---------------------------------------------------------------- */
    mov     ax, 0x2401
    int     0x15                   /* try the BIOS first            */
    in      al, 0x92               /* then the "fast A20" gate      */
    or      al, 2
    and     al, 0xFE
    out     0x92, al

/* ---------------------------------------------------------------- */
/* Enter protected mode                                              */
/* ---------------------------------------------------------------- */
    cli
    lgdt    [gdt_desc]
    mov     eax, cr0
    or      al, 1
    mov     cr0, eax
    .byte   0x66, 0xEA             /* jmp far 0x08:pm_start (32-bit) */
    .long   pm_start
    .word   0x08

/* ---------------------------------------------------------------- */
/* 32-bit land                                                       */
/* ---------------------------------------------------------------- */
.code32
pm_start:
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax
    mov     esp, 0x90000
    mov     eax, 0x10000           /* where the kernel was loaded */
    jmp     eax

disk_error:
    mov     si, offset s_err
    call    print16
1:  hlt
    jmp     1b

/* ---------------------------------------------------------------- */
/* Helpers (16-bit)                                                  */
/* ---------------------------------------------------------------- */
.code16
print16:
    pusha
1:  lodsb
    test    al, al
    jz      2f
    mov     ah, 0x0E
    mov     bx, 0x0007
    int     0x10
    jmp     1b
2:  popa
    ret

/* ---------------------------------------------------------------- */
/* Data                                                              */
/* ---------------------------------------------------------------- */
s_boot:     .asciz "MyOS: loading kernel..."
s_err:      .asciz " \r\nDISK ERROR - press reset."
boot_drive: .byte 0
cur_lba:    .word 0
dest_off:   .word 0

.align 8
gdt_start:
    .quad 0x0000000000000000       /* null descriptor */
    .quad 0x00CF9A000000FFFF       /* 0x08: 4 GB flat code, ring 0 */
    .quad 0x00CF92000000FFFF       /* 0x10: 4 GB flat data, ring 0 */
gdt_end:

gdt_desc:
    .word gdt_end - gdt_start - 1
    .long gdt_start

.org 510
.word 0xAA55
