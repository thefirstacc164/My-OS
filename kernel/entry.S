/*
 * MyOS - kernel entry point and interrupt stubs (32-bit protected mode)
 */
.intel_syntax noprefix
.code32
.section .text.entry, "ax"
.global _start
.extern kmain
.extern __bss_start
.extern __bss_end
.extern __stack_top
.extern interrupt_dispatch

/* ------------------------------------------------------------------ */
/* Kernel entry: called by the boot sector with flat 4 GB segments.    */
/* ------------------------------------------------------------------ */
_start:
    cli
    mov     esp, offset __stack_top /* stack is reserved in the linker script */

    cld
    mov     edi, offset __bss_start /* zero out the .bss section */
    mov     edx, offset __bss_end
    sub     edx, edi
    xor     eax, eax
    mov     ecx, edx
    shr     ecx, 2
    rep stosd
    mov     ecx, edx
    and     ecx, 3
    rep stosb

    call    kmain

1:  cli
    hlt
    jmp     1b

.section .text, "ax"

/* ------------------------------------------------------------------ */
/* Exception stubs. Vectors 8, 10-14 and 17 push their own error code. */
/* ------------------------------------------------------------------ */
.macro ISR_NOERR n
.global isr\n
isr\n:
    push   0
    push   \n
    jmp     isr_common
.endm

.macro ISR_ERR n
.global isr\n
isr\n:
    push   \n
    jmp     isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

/* ------------------------------------------------------------------ */
/* Hardware IRQ stubs: remapped to vectors 32..47.                     */
/* ------------------------------------------------------------------ */
.macro IRQ n, vec
.global irq\n
irq\n:
    push   0
    push   \vec
    jmp     isr_common
.endm

IRQ 0,  32
IRQ 1,  33
IRQ 2,  34
IRQ 3,  35
IRQ 4,  36
IRQ 5,  37
IRQ 6,  38
IRQ 7,  39
IRQ 8,  40
IRQ 9,  41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

/* ------------------------------------------------------------------ */
/* Common trampoline: save state, hand a pointer to C, restore, iret.  */
/* ------------------------------------------------------------------ */
isr_common:
    pusha                          /* edi esi ebp esp ebx edx ecx eax */
    mov     eax, esp
    push    eax
    call    interrupt_dispatch
    add     esp, 4
    popa
    add     esp, 8                  /* drop vector + error code */
    iret

.section .note.GNU-stack, "", @progbits
