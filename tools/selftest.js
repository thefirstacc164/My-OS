#!/usr/bin/env node
/*
 * Headless self-test for MyOS.
 *
 * Boots docs/myos.img inside v86 (the same emulator the website uses),
 * drives the desktop with real PS/2 mouse packets and keyboard scancodes,
 * and checks what the kernel reports on the serial port.
 *
 *   npm install v86
 *   node tools/selftest.js
 */
"use strict";

const fs = require("fs");
const path = require("path");
const { V86 } = require("v86");

const V86_DIR = path.dirname(require.resolve("v86/package.json"));
const ROOT = path.resolve(__dirname, "..");
const DOCS = path.join(ROOT, "docs");

/* start menu item centres (see kernel/wm.c) */
const MENU_START = [40, 15];
const ITEM = {
    files: 52, terminal: 80, editor: 108, browser: 136,
    clock: 164, calc: 192, snake: 220, about: 248,
    restart: 285, shutdown: 313,
};

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const read = (p) => {
    const b = fs.readFileSync(p);
    return b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
};

const failures = [];
function check(label, ok) {
    console.log(`${ok ? "  ok  " : " FAIL "} ${label}`);
    if (!ok) failures.push(label);
}

/* ------------------------------------------------------------------ */
/* One emulated machine                                               */
/* ------------------------------------------------------------------ */
async function createMachine(disk) {
    const m = { serial: "" };

    const config = {
        wasm_path: path.join(V86_DIR, "build/v86.wasm"),
        memory_size: 32 * 1024 * 1024,
        vga_memory_size: 4 * 1024 * 1024,
        bios: { buffer: read(path.join(DOCS, "bios/seabios.bin")) },
        vga_bios: { buffer: read(path.join(DOCS, "bios/vgabios.bin")) },
        fda: { buffer: read(path.join(DOCS, "myos.img")) },
        autostart: true,
    };
    if (disk)
        config.hda = { buffer: disk.buffer };

    m.emu = new V86(config);
    m.emu.add_listener("serial0-output-byte", (b) => {
        m.serial += String.fromCharCode(b);
    });

    const ps2 = () => m.emu.v86.cpu.devices.ps2;
    let mx = 320, my = 240;

    m.moveTo = async (x, y) => {
        while (mx !== x || my !== y) {
            const dx = Math.max(-8, Math.min(8, x - mx));
            const dy = Math.max(-8, Math.min(8, y - my));
            ps2().mouse_send_delta(dx, -dy);
            mx += dx;
            my += dy;
            await sleep(14);
        }
        await sleep(90);
    };
    m.click = async () => {
        ps2().mouse_send_click(1, 0, 0);
        await sleep(70);
        ps2().mouse_send_click(0, 0, 0);
        await sleep(200);
    };
    m.clickAt = async (x, y) => { await m.moveTo(x, y); await m.click(); };
    m.type = (text) => m.emu.keyboard_send_text(text);
    m.waitFor = async (needle, timeout = 8000) => {
        const end = Date.now() + timeout;
        while (Date.now() < end) {
            if (m.serial.includes(needle)) return true;
            await sleep(100);
        }
        return false;
    };
    m.openFromMenu = async (item) => {
        await m.clickAt(MENU_START[0], MENU_START[1]);
        await sleep(250);
        await m.clickAt(100, ITEM[item]);
        await sleep(450);
    };
    m.boot = async () => {
        m.serial = "";
        const ok = await m.waitFor("starting desktop", 25000);
        await sleep(900);
        return ok;
    };
    m.destroy = () => m.emu.destroy();

    return m;
}

async function main() {
    /* ---- phase 1: the desktop ------------------------------------- */
    const m = await createMachine(null);
    check("kernel boots", await m.boot());
    check("VESA framebuffer initialised", m.serial.includes("VESA framebuffer"));
    check("mouse driver started", m.serial.includes("PS/2 mouse driver"));
    check("file system mounted", m.serial.includes("file system"));

    m.serial = "";
    await m.clickAt(MENU_START[0], MENU_START[1]);
    check("the start menu opens", await m.waitFor("MENU"));

    m.serial = "";
    await m.clickAt(100, ITEM.terminal);
    check("the menu opens the Terminal", await m.waitFor("OPEN Terminal"));

    m.serial = "";
    m.type("about\n");
    check("typing runs a shell command", await m.waitFor("written from scratch"));

    m.serial = "";
    m.type("ls\n");
    check("ls lists the file system", await m.waitFor("welcome.txt"));

    m.serial = "";
    m.type("mem\n");
    check("mem reads the BIOS memory map", await m.waitFor("usable memory"));

    m.serial = "";
    await m.openFromMenu("browser");
    check("the Browser opens from the menu", await m.waitFor("OPEN Browser"));

    m.serial = "";
    await m.clickAt(297, 243);
    check("clicking a link navigates", await m.waitFor("LINK about.html"));

    m.serial = "";
    await m.openFromMenu("files");
    check("the Files window opens", await m.waitFor("OPEN Files"));

    /* third window, cascaded: 40 + 2*28, 50 + 2*28 */
    m.serial = "";
    await m.clickAt(40 + 56 + 356 - 36, 50 + 56 + 13);
    check("the maximize button maximises", await m.waitFor("MAXIMIZE Files"));

    m.serial = "";
    await m.clickAt(640 - 36, 30 + 13);
    check("clicking again restores the window", await m.waitFor("RESTORE Files"));

    m.serial = "";
    await m.openFromMenu("clock");
    check("the Clock opens", await m.waitFor("OPEN Clock"));

    m.serial = "";
    await m.openFromMenu("calc");
    check("the Calculator opens", await m.waitFor("OPEN Calculator"));

    m.serial = "";
    await m.openFromMenu("snake");
    check("Snake opens", await m.waitFor("OPEN Snake"));

    m.serial = "";
    await m.clickAt(MENU_START[0], MENU_START[1]);
    await sleep(250);
    await m.clickAt(100, ITEM.shutdown);
    check("the Shut down menu item halts MyOS", await m.waitFor("ACTION shutdown"));

    m.destroy();
    await sleep(400);

    /* ---- phase 2: the disk ---------------------------------------- */
    const disk = new Uint8Array(256 * 1024);

    const first = await createMachine(disk);
    await first.boot();
    check("an ATA disk is detected", first.serial.includes("ATA disk"));

    await first.openFromMenu("terminal");
    first.serial = "";
    first.type("write /home/keep.txt hello from last night\n");
    check("writing a file succeeds", await first.waitFor("wrote 21 bytes"));
    await sleep(2200);                                  /* let the sync fire */
    check("the file reached the disk",
          Buffer.from(disk).toString("latin1").includes("hello from last night"));
    first.destroy();
    await sleep(400);

    const second = await createMachine(disk);           /* a new machine, same disk */
    await second.boot();
    check("the file system is restored after a power cycle",
          second.serial.includes("restored from disk"));

    await second.openFromMenu("terminal");
    second.serial = "";
    second.type("cat /home/keep.txt\n");
    check("the saved file survived the reboot", await second.waitFor("hello from last night"));

    second.destroy();

    if (failures.length) {
        console.log(`\n${failures.length} check(s) failed`);
        process.exit(1);
    }
    console.log(`\nall checks passed`);
    process.exit(0);
}

main().catch((err) => {
    console.error(err);
    process.exit(1);
});
