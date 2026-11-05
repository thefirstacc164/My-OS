# MyOS

**A desktop operating system written from scratch — and runnable in a browser tab.**

Windows you can drag, a mouse, a file manager, a text editor, a terminal, a browser.
No installation, no account, no downloads.

> ### ▶ Boot it: **https://cdn.jsdelivr.net/gh/thefirstacc164/My-OS@main/docs/index.html**
> Click **POWER ON**, then use the top bar to open apps.
> (The permanent address `https://thefirstacc164.github.io/My-OS/` works after a one-time
> switch in the repository settings — see [Hosting](#hosting--why-it-is-free).)

![The MyOS desktop](docs/screenshot-desktop.png)

---

## What this is

Not a menu screen, and not a Linux you configured. This is a hand-written
operating system for 32-bit x86, from the first byte the CPU executes.

| | |
|---|---|
| **Boot sector** | 512 bytes of assembly: memory map, kernel loader, A20, GDT, protected mode, VESA mode |
| **Kernel** | ~40 KB of C: interrupts, timer, keyboard, mouse, graphics, window manager, shell, apps |
| **Display** | 640×480, 8-bit colour, VESA linear framebuffer, drawn pixel by pixel |
| **Input** | PS/2 keyboard and mouse, both interrupt driven |
| **Storage** | a small in-memory file system (its own tree, files and sizes) |
| **Desktop** | wallpaper, top bar with a live clock, draggable windows, focus, close buttons |
| **Apps** | Files, Terminal, Editor, Browser, About |

It runs on **v86**, an x86 PC emulator written in Rust and compiled to WebAssembly.
The emulated BIOS reads the first sector of our disk image, jumps into our boot
sector, and from there every instruction is ours.

---

## Using it

1. Open the link at the top in any modern browser.
2. Click **POWER ON** (the first load pulls ~5 MB of emulator files).
3. Click around: the top bar opens apps, title bars drag windows, `×` closes them.

| App | What it does |
|---|---|
| **Files** | Browse the file system, open folders, click a file to open it |
| **Terminal** | `help`, `ls`, `cat`, `write`, `rm`, `open`, `mem`, `cpu`, `date`, `uptime`, `echo`, `clear`, `logo`, `reboot`, `halt` |
| **Editor** | Type into a file, `Save` writes it back to the file system, arrow keys move |
| **Browser** | Renders `.html` files from the file system: headings, paragraphs, bullets, clickable links |
| **About** | Live system information: display mode, RAM, uptime, CPU vendor |

Everything is in RAM: reboot and the machine is factory-fresh again.

---

## How it boots

```
   power on
      │
      ▼
  BIOS (SeaBIOS inside the emulator)
  reads sector 0 of myos.img ──────────►  512-byte boot sector
                                                │ 1. BIOS memory map        (INT 15h / E820)
                                                │ 2. read kernel from disk (INT 13h / CHS)
                                                │ 3. open the A20 line
                                                │ 4. VESA mode 640x480x8   (INT 10h / 4F02)
                                                │ 5. GDT, CR0.PE
                                                ▼
                                        32-bit protected mode @ 0x10000
                                                │
                                                ├── gfx       framebuffer + 8x16 font
                                                ├── console   text output, boot log
                                                ├── idt/pic   interrupts, exception reporting
                                                ├── pit       100 Hz clock
                                                ├── keyboard  PS/2, IRQ1
                                                ├── mouse     PS/2, IRQ12
                                                ├── fs        in-memory files
                                                └── wm        desktop, windows, apps
```

The boot sector leaves data behind at fixed addresses for the kernel:
the BIOS memory map at `0x5000`, and the VESA mode information block at `0x4800`
(framebuffer address, pitch, resolution).

---

## Source map

```
boot/boot.S          the 512-byte boot sector (assembly)
kernel/entry.S       entry point, interrupt stubs
kernel/main.c        boot log, driver start-up
kernel/gfx.c         framebuffer, palette, drawing, 8x16 font renderer
kernel/console.c     text console (screen or window buffer) + printf
kernel/wm.c          desktop: wallpaper, top bar, windows, mouse/keyboard events
kernel/apps.c        Files, Terminal, Editor, Browser, About
kernel/fs.c          in-memory file system
kernel/idt.c         IDT, PIC remapping, CPU exception reporting
kernel/pit.c         system timer
kernel/keyboard.c    PS/2 keyboard driver (US layout, shift, caps)
kernel/mouse.c       PS/2 mouse driver (3-byte packets, IRQ12)
kernel/rtc.c         CMOS clock
kernel/memory.c      E820 memory accounting
kernel/string.c      memcpy / memset / strcmp / ...
tools/mkfont.py      pulls the 8x16 font out of the VGA BIOS at build time
tools/mkimage.py     builds the bootable 1.44 MB floppy image
tools/selftest.js    boots the image headlessly and clicks around
docs/                the website (GitHub Pages / jsDelivr)
```

## Building it

You need `gcc`, `as`, `ld`, `objcopy` and `python3`:

```sh
make          # produces docs/myos.img
make clean
```

The Makefile compiles the kernel, counts how many 512-byte sectors it needs,
bakes that number into the boot sector, generates the font, and assembles the
floppy image.

## Testing it

`tools/selftest.js` boots the exact published image in v86, then drives the
desktop with real PS/2 packets — it moves the emulated mouse, clicks the top
bar, types in the terminal and clicks a link in the browser:

```sh
npm install v86
node tools/selftest.js
```

```
  ok   kernel boots
  ok   VESA framebuffer initialised
  ok   mouse driver started
  ok   file system mounted
  ok   clicking the top bar opens the Terminal
  ok   typing runs a shell command
  ok   ls lists the file system
  ok   mem reads the BIOS memory map
  ok   the Browser opens
  ok   clicking a link navigates

all checks passed
```

---

## Hosting — why it is free

`docs/` is a plain static folder. The emulator (`v86`, BSD-2-Clause) and the
BIOS ROMs (SeaBIOS/VGABIOS, LGPL) are vendored inside it, so the page needs no
server, no API key and no account.

1. **jsDelivr** (already live, zero setup) — serves any public GitHub folder:
   `https://cdn.jsdelivr.net/gh/thefirstacc164/My-OS@main/docs/index.html`
2. **GitHub Pages** (permanent, nicer URL) — enable once:
   <https://github.com/thefirstacc164/My-OS/settings/pages> →
   *Deploy from a branch* → **main** + **/docs** → Save.

---

## Honest limits

This is a real desktop, but a small one. There is no multitasking (apps share one
loop), no networking, no 64-bit mode, no user accounts, and the file system lives
in RAM. Windows cannot be resized yet.

Natural next steps: resizable windows, a taskbar clock menu, real scrollback in the
terminal, saving the file system to disk, a snake game as a window, and a simple
network stack for the browser.

---

## Licences

- MyOS source: MIT (see `LICENSE`).
- v86: BSD-2-Clause — `docs/v86/LICENSE`.
- SeaBIOS and VGABIOS: LGPL — `docs/bios/COPYING.LESSER`.
