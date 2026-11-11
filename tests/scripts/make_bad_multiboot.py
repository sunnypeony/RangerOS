#!/usr/bin/env python3
"""Copy a kernel ELF and destroy its Multiboot 1 magic (negative test image)."""
import struct
import sys

MB_MAGIC = 0x1BADB002
SEARCH_LIMIT = 8192


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: make_bad_multiboot.py IN.elf OUT.elf")
    data = bytearray(open(sys.argv[1], "rb").read())
    for off in range(0, min(len(data), SEARCH_LIMIT) - 11, 4):
        magic, flags, checksum = struct.unpack_from("<III", data, off)
        if magic == MB_MAGIC and (magic + flags + checksum) & 0xFFFFFFFF == 0:
            struct.pack_into("<I", data, off, 0)
            open(sys.argv[2], "wb").write(data)
            print("zeroed multiboot magic at file offset 0x%x" % off)
            return
    sys.exit("no multiboot header found in " + sys.argv[1])


if __name__ == "__main__":
    main()
