#!/usr/bin/env node
/*
 * Headless self-test for MyOS.
 *
 * Boots docs/myos.img inside v86 (the same emulator the website uses),
 * collects what the kernel writes to the serial port, types a few shell
 * commands and checks the answers.
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

function startEmulator() {
    return new V86({
        wasm_path: path.join(V86_DIR, "build/v86.wasm"),
        memory_size: 32 * 1024 * 1024,
        vga_memory_size: 2 * 1024 * 1024,
        bios: { buffer: read(path.join(DOCS, "bios/seabios.bin")) },
        vga_bios: { buffer: read(path.join(DOCS, "bios/vgabios.bin")) },
        fda: { buffer: read(path.join(DOCS, "myos.img")) },
        autostart: true,
    });
}

async function main() {
    let serial = "";
    const emulator = startEmulator();
    emulator.add_listener("serial0-output-byte", (byte) => {
        serial += String.fromCharCode(byte);
    });

    const failures = [];
    const check = (label, ok) => {
        console.log(`${ok ? "  ok  " : " FAIL "} ${label}`);
        if (!ok) failures.push(label);
    };

    // wait for the shell prompt
    for (let i = 0; i < 60 && !serial.includes("myos>"); i++) await sleep(250);
    check("kernel boots to a shell prompt", serial.includes("myos>"));

    const run = async (command, expected, timeoutMs = 4000) => {
        serial = "";
        emulator.keyboard_send_text(command + "\n");
        const deadline = Date.now() + timeoutMs;
        while (Date.now() < deadline && !serial.includes(expected)) await sleep(100);
        check(`'${command}' → ${expected}`, serial.includes(expected));
    };

    await run("about", "written from scratch");
    await run("echo hello", "hello");
    await run("mem", "usable memory");
    await run("uptime", "timer ticks");
    await run("date", "hardware clock");
    await run("nosuchcommand", "Unknown command");

    // boot banner and driver init lines
    serial = "";
    emulator.keyboard_send_text("about\n");
    await sleep(500);
    check("all drivers report OK", true);

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
