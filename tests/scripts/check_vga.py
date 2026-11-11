#!/usr/bin/env python3
"""Judge a VGA text-buffer dump (pmemsave of 0xB8000, 80x25 cells).

  --expect REGEX   a screen row must match; repeat, matched in row order
  --scroll-test    the whole screen must equal the output of
                   tests/kernel/selftest_vga.c, recomputed here
  --text-out FILE  write the decoded screen as plain text (evidence)
  --ppm FILE --png-out FILE  convert a QEMU screendump to PNG (evidence)

Every cell's attribute must be 0x07 (the only colour the driver uses).
Exit 0 only if every check passes.
"""
import argparse
import re
import struct
import sys
import zlib

COLS, ROWS = 80, 25
ATTR = 0x07

failures = []


def check(name, ok, detail):
    print("%s %s: %s" % ("PASS" if ok else "FAIL", name, detail))
    if not ok:
        failures.append(name)


def decode(data):
    chars = [[" "] * COLS for _ in range(ROWS)]
    attrs = [[0] * COLS for _ in range(ROWS)]
    for i in range(ROWS * COLS):
        ch, at = data[2 * i], data[2 * i + 1]
        chars[i // COLS][i % COLS] = chr(ch) if 32 <= ch < 127 else "?"
        attrs[i // COLS][i % COLS] = at
    return ["".join(r) for r in chars], attrs


def simulate(text):
    """Same rules as drivers/vga.c for '\\n', wrap at column 80, scroll."""
    screen = [[" "] * COLS for _ in range(ROWS)]
    row = col = 0

    def newline():
        nonlocal row, col
        col = 0
        row += 1
        if row == ROWS:
            del screen[0]
            screen.append([" "] * COLS)
            row = ROWS - 1

    for c in text:
        if c == "\n":
            newline()
        else:
            screen[row][col] = c
            col += 1
            if col == COLS:
                newline()
    return ["".join(r) for r in screen]


def selftest_vga_text():
    """Mirror of tests/kernel/selftest_vga.c."""
    out = "".join("row %02d dec=%d neg=%d hex=%x pad=%08X\n" % (
        i, i * 1000, -(i * 37), i * 0x1234, 0xDEAD0000 + i) for i in range(30))
    out += "".join(chr(ord("A") + i % 26) for i in range(90))
    out += "\nVGA_END"
    return out


def ppm_to_png(ppm_path, png_path):
    data = open(ppm_path, "rb").read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", data)
    if not m or int(m.group(3)) != 255:
        raise ValueError("not an 8-bit P6 PPM")
    w, h = int(m.group(1)), int(m.group(2))
    pixels = data[m.end():m.end() + w * h * 3]
    raw = b"".join(b"\x00" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    open(png_path, "wb").write(png)
    return w, h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", required=True)
    ap.add_argument("--expect", action="append", default=[])
    ap.add_argument("--scroll-test", action="store_true")
    ap.add_argument("--text-out")
    ap.add_argument("--ppm")
    ap.add_argument("--png-out")
    args = ap.parse_args()

    try:
        data = open(args.dump, "rb").read()
    except OSError as e:
        data = b""
        check("dump-readable", False, str(e))
    check("dump-size", len(data) == ROWS * COLS * 2, "%d bytes" % len(data))
    if failures:
        print("VGA_CHECK FAIL (%d failed)" % len(failures))
        return 1
    rows, attrs = decode(data)
    if args.text_out:
        with open(args.text_out, "w") as f:
            for i, r in enumerate(rows):
                f.write("%02d|%s|\n" % (i, r))

    odd = sorted({a for r in attrs for a in r} - {ATTR})
    check("attributes", not odd, "all cells 0x%02x; other values: %s" % (
        ATTR, ["0x%02x" % a for a in odd] or "none"))

    stripped = [r.rstrip() for r in rows]
    want = 0
    expects = [re.compile(e) for e in args.expect]
    for r in stripped:
        if want < len(expects) and expects[want].search(r):
            want += 1
    if expects:
        check("expected-rows", want == len(expects), "%d/%d matched in order%s" % (
            want, len(expects),
            "" if want == len(expects) else ", missing %r" % expects[want].pattern))

    if args.scroll_test:
        expect_rows = simulate(selftest_vga_text())
        diff = [i for i in range(ROWS) if rows[i] != expect_rows[i]]
        check("scroll-test-screen", not diff, "rows differing from simulation: %s" % (
            diff or "none"))
        for i in diff[:3]:
            print("  row %02d got    |%s|" % (i, stripped[i]))
            print("  row %02d expect |%s|" % (i, expect_rows[i].rstrip()))

    if args.ppm and args.png_out:
        try:
            w, h = ppm_to_png(args.ppm, args.png_out)
            check("screenshot", True, "%dx%d -> %s" % (w, h, args.png_out))
        except (OSError, ValueError) as e:
            check("screenshot", False, str(e))

    print("VGA_CHECK %s (%d failed)" % ("FAIL" if failures else "PASS", len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
