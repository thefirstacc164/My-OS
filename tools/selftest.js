#!/usr/bin/env node
/*
 * Headless self-test for MyOS.
 *
 * Boots docs/myos.img inside v86 (the same emulator the website uses),
 * clicks around the desktop with real PS/2 mouse packets, types on the
 * emulated keyboard and checks what the kernel reports on the serial port.
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

    async function wait_for(needle, timeout = 6000) {
        const deadline = Date.now() + timeout;
        while (Date.now() < deadline) {
            if (serial.includes(needle))
                return true;
            await sleep(100);
        }
        return false;
    }

    check("kernel boots", await wait_for("starting desktop", 20000));
    check("VESA framebuffer initialised", serial.includes("VESA framebuffer"));
    check("mouse driver started", serial.includes("PS/2 mouse driver"));
    check("file system mounted", serial.includes("in-memory file system"));

    serial = "";
    await move_to(190, 15);          /* "Terminal" in the top bar */
    await click();
    check("clicking the top bar opens the Terminal", await wait_for("OPEN Terminal"));

    serial = "";
    emulator.keyboard_send_text("about\n");
    check("typing runs a shell command", await wait_for("written from scratch"));

    serial = "";
    emulator.keyboard_send_text("ls\n");
    check("ls lists the file system", await wait_for("welcome.txt"));

    serial = "";
    emulator.keyboard_send_text("mem\n");
    check("mem reads the BIOS memory map", await wait_for("usable memory"));

    serial = "";
    await move_to(346, 15);          /* "Browser" */
    await click();
    check("the Browser opens", await wait_for("OPEN Browser"));

    serial = "";
    await move_to(297, 243);         /* the "About MyOS" link */
    await click();
    check("clicking a link navigates", await wait_for("LINK about.html"));

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
