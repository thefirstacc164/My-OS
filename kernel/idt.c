/*
 * MyOS - Interrupt Descriptor Table, PIC remapping and CPU exception
 * handling.
 */
#include "kernel.h"
#include "gfx.h"

struct idt_entry {
    u16 base_lo;
    u16 selector;
    u8  always_zero;
    u8  flags;
    u16 base_hi;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u32 base;
} __attribute__((packed));

/* Layout must match the stack built by isr_common in entry.S */
struct interrupt_frame {
    u32 edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
    u32 vector, error;
    u32 eip, cs, eflags;
};

static struct idt_entry idt[256];
static struct idt_ptr   idtp;
static void (*irq_handlers[16])(void);

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

static void (*const exception_stubs[32])(void) = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
};

static void (*const irq_stubs[16])(void) = {
    irq0,  irq1,  irq2,  irq3,  irq4,  irq5,  irq6,  irq7,
    irq8,  irq9,  irq10, irq11, irq12, irq13, irq14, irq15
};

static const char *const exception_names[32] = {
    "Divide by zero",
    "Debug",
    "Non-maskable interrupt",
    "Breakpoint",
    "Overflow",
    "Bound range exceeded",
    "Invalid opcode",
    "Device not available",
    "Double fault",
    "Coprocessor segment overrun",
    "Invalid TSS",
    "Segment not present",
    "Stack-segment fault",
    "General protection fault",
    "Page fault",
    "Reserved",
    "x87 floating-point exception",
    "Alignment check",
    "Machine check",
    "SIMD floating-point exception",
    "Virtualization exception",
    "Control protection exception",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved"
};

static void set_gate(int n, void (*handler)(void), u16 selector, u8 flags)
{
    u32 addr = (u32)handler;
    idt[n].base_lo     = (u16)(addr & 0xFFFF);
    idt[n].selector    = selector;
    idt[n].always_zero = 0;
    idt[n].flags       = flags;
    idt[n].base_hi     = (u16)((addr >> 16) & 0xFFFF);
}

static void pic_remap(void)
{
    /* Start the master and slave PICs at vectors 32 and 40 so they no
     * longer collide with the CPU's own exception vectors. */
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait();
    outb(0xA1, 0x28); io_wait();
    outb(0x21, 0x04); io_wait();
    outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait();
    outb(0xA1, 0x01); io_wait();

    /* Mask everything; individual drivers unmask what they need. */
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

void irq_install(u8 irq, void (*handler)(void))
{
    irq_handlers[irq & 15] = handler;

    u16 port = (irq < 8) ? 0x21 : 0xA1;
    u8  bit  = (u8)(irq & 7);
    u8  mask = inb(port);
    mask &= (u8)~(1 << bit);
    outb(port, mask);

    /* Interrupts 8-15 arrive through the slave PIC, which is wired to
     * IRQ2 on the master. That cascade line has to be unmasked too. */
    if (irq >= 8) {
        u8 master = inb(0x21);
        master &= (u8)~(1 << 2);
        outb(0x21, master);
    }
}

void idt_init(void)
{
    idtp.limit = (u16)(sizeof(idt) - 1);
    idtp.base  = (u32)&idt;

    memset(&idt, 0, sizeof(idt));

    for (int i = 0; i < 32; i++)
        set_gate(i, exception_stubs[i], 0x08, 0x8E);
    for (int i = 0; i < 16; i++)
        set_gate(32 + i, irq_stubs[i], 0x08, 0x8E);

    pic_remap();

    __asm__ volatile("lidt %0" : : "m"(idtp));
}

/* ------------------------------------------------------------------ */
/* Called from isr_common for every interrupt                         */
/* ------------------------------------------------------------------ */
void interrupt_dispatch(struct interrupt_frame *f)
{
    if (f->vector < 32) {
        /* A CPU exception is always a bug in the kernel - report and stop. */
        console_set_color(C_WHITE);
        kprintf("\n\n KERNEL PANIC ");
        console_set_color(C_RED);
        kprintf("\n %s (vector %u, error 0x%x)\n eip = 0x%x\n",
                exception_names[f->vector], f->vector, f->error, f->eip);
        kprintf("\n The system has been halted. Press the reset button.\n");
        for (;;) {
            interrupts_disable();
            cpu_halt();
        }
    }

    if (f->vector < 48) {
        u8 irq = (u8)(f->vector - 32);
        if (irq_handlers[irq])
            irq_handlers[irq]();

        if (irq >= 8)
            outb(0xA0, 0x20);   /* end-of-interrupt to the slave PIC */
        outb(0x20, 0x20);       /* end-of-interrupt to the master PIC */
    }
}
