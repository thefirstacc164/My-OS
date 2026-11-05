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

const read = (p) => {
    const b = fs.readFileSync(p);
    return b.buffer.slice(b.byteOffset, b.byteOffset + b.byteLength);
};

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const failures = [];
function check(label, ok) {
    console.log(`${ok ? "  ok  " : " FAIL "} ${label}`);
    if (!ok) failures.push(label);
}

async function main() {
    let serial = "";
    const emulator = new V86({
        wasm_path: path.join(V86_DIR, "build/v86.wasm"),
        memory_size: 32 * 1024 * 1024,
        vga_memory_size: 4 * 1024 * 1024,
        bios: { buffer: read(path.join(DOCS, "bios/seabios.bin")) },
        vga_bios: { buffer: read(path.join(DOCS, "bios/vgabios.bin")) },
        fda: { buffer: read(path.join(DOCS, "myos.img")) },
        autostart: true,
    });

    emulator.add_listener("serial0-output-byte", (byte) => {
        serial += String.fromCharCode(byte);
    });

    const ps2 = () => emulator.v86.cpu.devices.ps2;
    let mouse_x = 320;
    let mouse_y = 240;

    async function move_to(x, y) {
        while (mouse_x !== x || mouse_y !== y) {
            const dx = Math.max(-8, Math.min(8, x - mouse_x));
            const dy = Math.max(-8, Math.min(8, y - mouse_y));
            ps2().mouse_send_delta(dx, -dy);
            mouse_x += dx;
            mouse_y += dy;
            await sleep(14);
        }
        await sleep(90);
    }

    async function click() {
        ps2().mouse_send_click(1, 0, 0);
        await sleep(70);
        ps2().mouse_send_click(0, 0, 0);
        await sleep(200);
    }

    async function click_at(x, y) {
        await move_to(x, y);
        await click();
    }

    async function wait_for(needle, timeout = 6000) {
        const deadline = Date.now() + timeout;
        while (Date.now() < deadline) {
            if (serial.includes(needle))
                return true;
            await sleep(100);
        }
        return false;
    }

    /* every new window is cascaded by 28 px from the one before it */
    let opened = 0;
    function cascade() {
        const offset = (opened % 5) * 28;
        return { x: 40 + offset, y: 50 + offset };
    }

    async function open_from_menu(item) {
        await click_at(MENU_START[0], MENU_START[1]);
        await sleep(250);
        await click_at(100, ITEM[item]);
        await sleep(450);
        opened++;
    }

    check("kernel boots", await wait_for("starting desktop", 25000));
    check("VESA framebuffer initialised", serial.includes("VESA framebuffer"));
    check("mouse driver started", serial.includes("PS/2 mouse driver"));
    check("file system mounted", serial.includes("in-memory file system"));

    /* --- start menu --------------------------------------------------- */
    serial = "";
    await click_at(MENU_START[0], MENU_START[1]);
    check("the start menu opens", await wait_for("MENU"));

    serial = "";
    await click_at(100, ITEM.terminal);
    check("the menu opens the Terminal", await wait_for("OPEN Terminal"));

    /* --- typing in the terminal --------------------------------------- */
    serial = "";
    emulator.keyboard_send_text("about\n");
    check("typing runs a shell command", await wait_for("written from scratch"));

    serial = "";
    emulator.keyboard_send_text("ls\n");
    check("ls lists the file system", await wait_for("welcome.txt"));

    serial = "";
    emulator.keyboard_send_text("mem\n");
    check("mem reads the BIOS memory map", await wait_for("usable memory"));

    /* --- browser ------------------------------------------------------ */
    serial = "";
    await open_from_menu("browser");
    check("the Browser opens from the menu", await wait_for("OPEN Browser"));

    serial = "";
    await click_at(297, 243);          /* the "About MyOS" link */
    check("clicking a link navigates", await wait_for("LINK about.html"));

    /* --- window controls ---------------------------------------------- */
    serial = "";
    await open_from_menu("files");
    check("the Files window opens", await wait_for("OPEN Files"));

    const files = cascade();                       /* Files is the third window */
    serial = "";
    await click_at(files.x + 356 - 36, files.y + 13);   /* maximize button */
    check("the maximize button maximises", await wait_for("MAXIMIZE Files"));

    serial = "";
    await click_at(640 - 44 + 8, 30 + 13);        /* same button, now top right */
    check("clicking again restores the window", await wait_for("RESTORE Files"));

    /* --- clock and calculator ----------------------------------------- */
    serial = "";
    await open_from_menu("clock");
    check("the Clock opens", await wait_for("OPEN Clock"));

    serial = "";
    await open_from_menu("calc");
    check("the Calculator opens", await wait_for("OPEN Calculator"));

    serial = "";
    await open_from_menu("snake");
    check("Snake opens", await wait_for("OPEN Snake"));

    /* --- shutting down (last: it halts the machine) -------------------- */
    serial = "";
    await click_at(MENU_START[0], MENU_START[1]);
    await sleep(250);
    await click_at(100, ITEM.shutdown);
    check("the Shut down menu item halts MyOS", await wait_for("ACTION shutdown"));

    if (failures.length) {
        console.log(`\n${failures.length} check(s) failed`);
        process.exit(1);
    }
    console.log("\nall checks passed");
    process.exit(0);
}

main().catch((err) => {
    console.error(err);
    process.exit(1);
});
