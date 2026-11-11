#!/usr/bin/env python3
"""Check shell sessions in a serial log (M3).

Each submitted line produces a block on COM1:
  SHELL len=<n> rejected=<r> [<line>]
  <command output lines...>
  KBD chars=.. scancodes=.. dropped=.. ignored=..

  --cmd TEXT          expected submitted lines, in order (repeat; exact,
                      may be empty; compared with the text inside [...])
  --out I:REGEX       some output line of block I (0-based) matches REGEX
  --no-out I          block I has no output lines
  --ticks-increase    ticks=N values printed by successive ticks commands
                      strictly increase
  --dropped N         every KBD line reports dropped=N
  --keys-before-first TEXT
                      text rebuilt from the KEY lines before the first
                      SHELL line (ENTER excluded) equals TEXT
Exit 0 only if all pass.
"""
import argparse
import re
import sys

ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][0-9A-Za-z]|\x1b[=>78cDEHM]")
SHELL = re.compile(r"^SHELL len=(\d+) rejected=(\d+) \[(.*)\]$")
KBD = re.compile(r"^KBD chars=\d+ scancodes=\d+ dropped=(\d+) ignored=\d+$")
KEY = re.compile(r"^KEY 0x([0-9a-f]{2})(?: .*)?$")
TICKS_OUT = re.compile(r"^ticks=(\d+) hz=100 ")

failures = []


def check(name, ok, detail):
    print("%s %s: %s" % ("PASS" if ok else "FAIL", name, detail))
    if not ok:
        failures.append(name)


def load(path):
    raw = open(path, "rb").read().decode("utf-8", "replace")
    # Keep trailing spaces inside [...]; only strip line ends.
    return [l.rstrip("\n") for l in ANSI.sub("", raw).replace("\r\n", "\n")
            .replace("\r", "\n").split("\n")]


def blocks_of(lines):
    blocks, cur = [], None
    for l in lines:
        m = SHELL.match(l.strip()) if l.strip().startswith("SHELL ") else None
        if m:
            cur = {"len": int(m.group(1)), "rejected": int(m.group(2)),
                   "text": SHELL.match(l.lstrip()).group(3), "out": []}
            blocks.append(cur)
        elif cur is not None and KBD.match(l.strip()):
            cur["kbd_dropped"] = int(KBD.match(l.strip()).group(1))
            cur = None
        elif cur is not None and l.strip():
            cur["out"].append(l.strip())
    return blocks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--cmd", action="append", default=[])
    ap.add_argument("--out", action="append", default=[])
    ap.add_argument("--no-out", action="append", type=int, default=[])
    ap.add_argument("--ticks-increase", action="store_true")
    ap.add_argument("--dropped", type=int)
    ap.add_argument("--keys-before-first")
    args = ap.parse_args()

    lines = load(args.log)
    blocks = blocks_of(lines)
    texts = [b["text"] for b in blocks]
    check("commands", texts == args.cmd,
          "%d submitted lines %s" % (len(texts), "match" if texts == args.cmd else
                                     "got %r, want %r" % (texts, args.cmd)))
    check("blocks-closed", all("kbd_dropped" in b for b in blocks),
          "every SHELL block ends with a KBD line")
    check("len-field", all(b["len"] == len(b["text"]) and b["len"] <= 127 for b in blocks),
          "len matches text and never exceeds 127")

    for spec in args.out:
        idx, _, rx = spec.partition(":")
        i = int(idx)
        ok = i < len(blocks) and any(re.search(rx, o) for o in blocks[i]["out"])
        check("out[%d]" % i, ok, "%r in %s" % (rx, blocks[i]["out"][:4] if i < len(blocks) else "-"))
    for i in args.no_out:
        ok = i < len(blocks) and not blocks[i]["out"]
        check("no-out[%d]" % i, ok, "output %s" % (blocks[i]["out"] if i < len(blocks) else "-"))

    if args.ticks_increase:
        vals = [int(m.group(1)) for b in blocks for m in (TICKS_OUT.match(o) for o in b["out"]) if m]
        check("ticks-increase", len(vals) >= 2 and all(b > a for a, b in zip(vals, vals[1:])),
              "ticks outputs %s" % vals)
    if args.dropped is not None:
        drops = [b.get("kbd_dropped") for b in blocks]
        check("dropped", drops and all(d == args.dropped for d in drops),
              "KBD dropped per block %s (want %d)" % (drops, args.dropped))
    if args.keys_before_first is not None:
        first = next((i for i, l in enumerate(lines) if l.strip().startswith("SHELL ")), len(lines))
        keys = [int(m.group(1), 16) for m in (KEY.match(l.strip()) for l in lines[:first]) if m]
        text = "".join(chr(k) for k in keys if k != 0x0A)
        check("keys-before-first", text == args.keys_before_first,
              "%d bytes before first SHELL line%s" % (
                  len(text), "" if text == args.keys_before_first else
                  ": got %r" % text))

    print("SHELL_CHECK %s (%d failed)" % ("FAIL" if failures else "PASS", len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
