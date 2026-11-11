#!/usr/bin/env python3
"""Check the keyboard path from the serial log of an injection run.

The main loop logs one line per byte it took from the input queue:
  KEY 0x48 'H' | KEY 0x20 SPACE | KEY 0x08 BACKSPACE | KEY 0x0a ENTER
and after each ENTER:
  KBD chars=.. scancodes=.. dropped=.. ignored=..

Checks:
  - --keys KEY lines in total (one per injected key);
  - text rebuilt from KEY lines (BACKSPACE removes the previous byte,
    ENTER ends a line) equals the --line arguments, in order;
  - the last KBD line has chars == --chars, dropped=0, and
    scancodes == chars + ignored (every scan code either produced a byte
    or was deliberately ignored);
  - a TICKS line after it reports kbd_scancodes == --scancodes. The KBD
    line is printed while Enter is still held (QEMU sendkey releases a
    key ~100 ms later), so only a later report includes the final break
    code; this proves every injected scan code arrived through IRQ1;
  - Shift press and release: an upper-case letter directly followed by
    a lower-case one (needs a release in between).
Exit 0 only if all pass.
"""
import argparse
import re
import sys

ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][0-9A-Za-z]|\x1b[=>78cDEHM]")
KEY = re.compile(r"^KEY 0x([0-9a-f]{2})(?: .*)?$")
KBD = re.compile(r"^KBD chars=(\d+) scancodes=(\d+) dropped=(\d+) ignored=(\d+)$")
TICKS_SC = re.compile(r"^TICKS \d+ .*\bkbd_scancodes=(\d+)\b")

failures = []


def check(name, ok, detail):
    print("%s %s: %s" % ("PASS" if ok else "FAIL", name, detail))
    if not ok:
        failures.append(name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--line", action="append", default=[])
    ap.add_argument("--keys", type=int, required=True)
    ap.add_argument("--chars", type=int, required=True)
    ap.add_argument("--scancodes", type=int, required=True)
    args = ap.parse_args()

    raw = open(args.log, "rb").read().decode("utf-8", "replace")
    lines = [l.strip() for l in ANSI.sub("", raw).replace("\r", "\n").split("\n")]
    keys = [int(m.group(1), 16) for m in (KEY.match(l) for l in lines) if m]
    stats = [(i, m) for i, m in enumerate(KBD.match(l) for l in lines) if m]

    check("key-count", len(keys) == args.keys, "%d KEY lines (want %d)" % (len(keys), args.keys))

    text_lines, cur = [], []
    for k in keys:
        if k == 0x08:
            if cur:
                cur.pop()
        elif k == 0x0A:
            text_lines.append("".join(cur))
            cur = []
        else:
            cur.append(chr(k))
    check("lines", text_lines == args.line, "got %r, want %r" % (text_lines, args.line))
    check("backspace-seen", 0x08 in keys, "BACKSPACE byte reached the main loop")
    check("enter-seen", keys.count(0x0A) == len(args.line), "%d ENTER" % keys.count(0x0A))

    shift_ok = any(chr(a).isupper() and chr(b).islower()
                   for a, b in zip(keys, keys[1:]) if a < 128 and b < 128)
    check("shift-press-release", shift_ok, "upper-case followed by lower-case")

    if stats:
        at, m = stats[-1]
        chars, scancodes, dropped, ignored = (int(x) for x in m.groups())
        check("kbd-stats", chars == args.chars and dropped == 0 and scancodes == chars + ignored,
              "chars=%d dropped=%d scancodes=%d = chars+ignored(%d) (want chars=%d, dropped=0)"
              % (chars, dropped, scancodes, chars + ignored, args.chars))
        later = [int(t.group(1)) for t in (TICKS_SC.match(l) for l in lines[at + 1:]) if t]
        check("all-scancodes-via-irq1", bool(later) and later[-1] == args.scancodes,
              "kbd_scancodes in later TICKS: %s (want %d)" % (later or "none", args.scancodes))
    else:
        check("kbd-stats", False, "no KBD line")

    print("KBD_CHECK %s (%d failed)" % ("FAIL" if failures else "PASS", len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
