#!/usr/bin/env python3
"""
Assemble a bootable 1.44 MB floppy image from the boot sector and kernel.

Layout:
    sector 0        boot sector (exactly 512 bytes, ends with 0x55AA)
    sector 1..n     kernel binary
    rest            zeroes
"""
import sys
import os

FLOPPY_SIZE = 1474560   # 1.44 MB: 80 cylinders x 2 heads x 18 sectors x 512


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 1

    boot_path, kernel_path, out_path = sys.argv[1:4]

    with open(boot_path, "rb") as f:
        boot = f.read()
    with open(kernel_path, "rb") as f:
        kernel = f.read()

    if len(boot) != 512:
        print(f"error: boot sector is {len(boot)} bytes, expected 512")
        return 1
    if boot[510] != 0x55 or boot[511] != 0xAA:
        print("error: boot sector is missing the 0x55AA signature")
        return 1

    if len(boot) + len(kernel) > FLOPPY_SIZE:
        print("error: kernel does not fit on the floppy")
        return 1

    image = bytearray(FLOPPY_SIZE)
    image[0:512] = boot
    image[512:512 + len(kernel)] = kernel

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(image)

    sectors = (len(kernel) + 511) // 512
    print(f"  {out_path}: {len(image)} bytes "
          f"(boot {len(boot)} B + kernel {len(kernel)} B = {sectors} sectors)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
