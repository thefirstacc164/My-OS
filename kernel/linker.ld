/*
 * MyOS linker script: the kernel is a flat binary that lives at 0x10000,
 * exactly where the boot sector loads it.
 */
ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : {
        *(.text.entry)      /* kernel entry point must sit at 0x10000 */
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    __bss_start = .;
    .bss : {
        *(COMMON)
        *(.bss*)
    }
    __bss_end = .;

    /* The kernel stack is part of the image, so it can never be overrun by
     * growing data structures again (it used to sit at a fixed 0x90000 and
     * a 64 KB file system buffer quietly landed on top of it). */
    . = ALIGN(16);
    __stack_bottom = .;
    . += 0x8000;
    __stack_top = .;

    /* Anything above 0xA0000 is the VGA aperture: the kernel must never
     * reach it. This assertion catches that at link time, not at 3 a.m. */
    ASSERT(__stack_top <= 0xA0000, "kernel image + bss + stack ran into VGA memory")

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
