# MyOS

**A real, bootable operating system — written from scratch, and runnable in a browser tab.**

No installation. No downloads. No admin rights. Just a link.

> ### ▶ Boot it: **https://thefirstacc164.github.io/My-OS/**
> Click **POWER ON**, then click the black screen and type `help` + `Enter`.

---

## What this is

Most "my own OS" projects are a menu screen or a Linux you configured. This is
different: it is a genuinely hand-written operating system for 32-bit x86.

| | |
|---|---|
| **Boot sector** | 512 bytes, written in x86 assembly. Loads the kernel off a floppy image. |
| **Kernel** | ~13 KB of C + assembly. No Linux, no Unix, no existing OS underneath. |
| **CPU mode** | 32-bit protected mode, flat 4 GB segments, own GDT and IDT. |
| **Drivers** | VGA text mode, PS/2 keyboard, PIT timer, CMOS real-time clock, serial port. |
| **Shell** | Interactive command line with 14 commands and a playable game. |

It runs on **v86**, an x86 PC emulator written in Rust/WebAssembly that runs
entirely inside the browser. The emulated BIOS reads the first sector of our
disk image, jumps into our boot sector, and our code takes over from there.

---

## Using it

1. Open the link above in any modern browser (Chrome, Edge, Firefox, Safari).
2. Click **POWER ON**.
3. Click the black screen once so it has keyboard focus.
4. Type `help` and press <kbd>Enter</kbd>.

### Commands

| Command | What it does |
|---|---|
| `help` | List every command |
| `about` | What MyOS is |
| `clear` | Wipe the screen |
| `echo <text>` | Print text back |
| `uptime` | Seconds since boot + timer ticks |
| `date` | Read the hardware clock (CMOS chip) |
| `mem` | The physical memory map reported by the BIOS (E820) |
| `cpu` | CPU vendor string via the `CPUID` instruction |
| `theme <0-15>` | Change the text colour |
| `beep` | Beep the PC speaker |
| `snake` | Play Snake — arrow keys to move, `q` to quit |
| `logo` | Redraw the logo |
| `reboot` | Restart the machine |
| `halt` | Stop the CPU |

---

## How the boot works

```
        power on
            │
            ▼
   BIOS (SeaBIOS, in the emulator)
   reads sector 0 of myos.img  ──────────►  512-byte boot sector
                                                  │  1. ask BIOS for memory map (INT 15h / E820)
                                                  │  2. read kernel from floppy (INT 13h / CHS)
                                                  │  3. open the A20 line
                                                  │  4. build a GDT, set CR0.PE
                                                  ▼
                                          32-bit protected mode
                                                  │
                                                  ▼
                                          kernel at 0x10000
                                           ├── VGA console
                                           ├── IDT + PIC remap
                                           ├── PIT timer 100 Hz
                                           ├── PS/2 keyboard
                                           └── shell
```

The two pieces talk through a fixed layout: the boot sector leaves the BIOS
memory map at `0x5000` and loads the kernel binary at `0x10000`.

---

## Source map

```
boot/boot.S         the 512-byte boot sector (assembly)
kernel/entry.S      kernel entry point + interrupt stubs
kernel/main.c       boot sequence, memory accounting
kernel/console.c    VGA text console, formatting, serial mirror
kernel/idt.c        IDT, PIC remapping, CPU exception reporting
kernel/pit.c        system timer
kernel/keyboard.c   PS/2 keyboard driver (US layout, shift + caps)
kernel/rtc.c        CMOS clock
kernel/shell.c      the shell and all commands
kernel/game.c       Snake
kernel/string.c     memcpy/memset/strcmp...
kernel/kernel.h     shared types and prototypes
kernel/linker.ld    places the kernel at 0x10000
tools/mkimage.py    builds the bootable 1.44 MB floppy image
docs/               the website (GitHub Pages serves this folder)
```

## Building it yourself

You need `gcc`, `as`, `ld`, `objcopy` and `python3` — all standard on Linux:

```sh
make          # produces docs/myos.img
make clean
```

The Makefile compiles the kernel, works out how many 512-byte sectors it needs,
bakes that number into the boot sector, and assembles the floppy image.

There is also a headless self-test that boots the image inside the same
emulator and checks the shell output:

```sh
npm install v86
node tools/selftest.js
```

---

## Hosting — why it is free

The site is plain static files in `docs/`, published with **GitHub Pages**
(free for public repositories). The emulator (`v86`, BSD-2-Clause) and the
BIOS ROMs (SeaBIOS/VGABIOS, LGPL) are vendored into `docs/` so the page needs
no third-party servers, no API keys and no login.

Total payload: about 5 MB, cached by the browser after the first visit.

---

## Honest limits

This is a real OS, but a small one. It has no multitasking, no file system, no
user accounts, no networking and no 64-bit mode. It boots, drives real hardware,
and gives you a shell. Everything beyond that is a next step, not a missing
promise.

---

## Licences

- MyOS source: MIT (see `LICENSE`).
- v86: BSD-2-Clause — `docs/v86/LICENSE`.
- SeaBIOS and VGABIOS: LGPL — `docs/bios/COPYING.LESSER`.
