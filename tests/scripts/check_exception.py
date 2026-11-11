#!/usr/bin/env python3
"""Check the serial diagnostics of an exception selftest.

Verifies, from the log of a run that already passed run_qemu.py:
  - exactly one EXCEPTION line, with the expected vector, name, error code;
  - reported EIP == address of --site symbol in --elf (nm);
  - cs=0x0008 and ds=0x0010 (our GDT selectors), IF clear in EFLAGS
    (printed on the line after EXCEPTION);
  - trap-frame layout: pusha_esp == frame + 48 (&vector), and the
    reported interrupted esp == frame + 68;
  - CR2 printed only for vector 14;
  - PANIC for that vector, then HALT; no SELFTEST_FAIL, no BOOT_OK.
Exit 0 only if all pass.
"""
import argparse
import re
import subprocess
import sys

ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][0-9A-Za-z]|\x1b[=>78cDEHM]")
EXC = re.compile(r"^EXCEPTION vector=(\d+) name=(\S+) error=0x([0-9a-f]{8}) "
                 r"eip=0x([0-9a-f]{8}) cs=0x([0-9a-f]{4})$")
EFLAGS = re.compile(r"^eflags=0x([0-9a-f]{8})$")
FRAME = re.compile(r"^frame=0x([0-9a-f]{8}) pusha_esp=0x([0-9a-f]{8})$")
REGS2 = re.compile(r"\besp=0x([0-9a-f]{8})$")
SEGS = re.compile(r"^ds=0x([0-9a-f]{4}) es=0x([0-9a-f]{4}) fs=0x([0-9a-f]{4}) gs=0x([0-9a-f]{4})$")
EFLAGS_IF = 0x200

failures = []


def check(name, ok, detail):
    print("%s %s: %s" % ("PASS" if ok else "FAIL", name, detail))
    if not ok:
        failures.append(name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--elf", required=True)
    ap.add_argument("--vector", type=int, required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--error", type=lambda s: int(s, 0), required=True)
    ap.add_argument("--site", default="selftest_fault_site")
    args = ap.parse_args()

    raw = open(args.log, "rb").read().decode("utf-8", "replace")
    lines = [l.strip() for l in ANSI.sub("", raw).replace("\r", "\n").split("\n")]

    exc = [(i, EXC.match(l)) for i, l in enumerate(lines) if l.startswith("EXCEPTION ")]
    check("one-exception-line", len(exc) == 1, "%d EXCEPTION lines" % len(exc))
    if len(exc) != 1 or exc[0][1] is None:
        check("exception-line-format", False, "unparsable or missing")
        print("EXC_CHECK FAIL (%d failed)" % len(failures))
        return 1
    at, m = exc[0]
    vector, name, error = int(m.group(1)), m.group(2), int(m.group(3), 16)
    eip, cs = int(m.group(4), 16), int(m.group(5), 16)
    following = [l for l in lines[at + 1:] if l]
    ef = EFLAGS.match(following[0]) if following else None
    check("eflags-line", ef is not None, "line after EXCEPTION is eflags=...")
    eflags = int(ef.group(1), 16) if ef else EFLAGS_IF

    check("vector", vector == args.vector, "got %d, want %d" % (vector, args.vector))
    check("name", name == args.name, "got %s, want %s" % (name, args.name))
    check("error-code", error == args.error, "got 0x%x, want 0x%x" % (error, args.error))

    syms = {}
    for line in subprocess.run(["nm", args.elf], stdout=subprocess.PIPE,
                               universal_newlines=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    site = syms.get(args.site)
    check("eip-is-fault-site", site is not None and eip == site,
          "eip 0x%08x, %s %s" % (eip, args.site, "0x%08x" % site if site is not None else "missing"))
    check("cs-selector", cs == 0x08, "cs=0x%04x" % cs)
    check("if-clear", not (eflags & EFLAGS_IF), "eflags=0x%08x" % eflags)

    after = lines[at + 1:]
    fr = next((FRAME.match(l) for l in after if FRAME.match(l)), None)
    esp = next((REGS2.search(l) for l in after if l.startswith("esi=") and REGS2.search(l)), None)
    if fr and esp:
        frame, pusha = int(fr.group(1), 16), int(fr.group(2), 16)
        interrupted = int(esp.group(1), 16)
        check("frame-pusha-esp", pusha == frame + 48,
              "frame 0x%08x, pusha_esp 0x%08x (want frame+0x30)" % (frame, pusha))
        check("frame-interrupted-esp", interrupted == frame + 68,
              "esp 0x%08x (want frame+0x44 = 0x%08x)" % (interrupted, frame + 68))
        check("frame-aligned", frame % 4 == 0, "frame 0x%08x" % frame)
    else:
        check("frame-lines", False, "frame/esp lines missing")
    seg = next((SEGS.match(l) for l in after if SEGS.match(l)), None)
    check("ds-selector", seg is not None and int(seg.group(1), 16) == 0x10,
          "ds=%s" % ("0x" + seg.group(1) if seg else "missing"))
    has_cr2 = any(l.startswith("cr2=") for l in after)
    check("cr2-only-for-pf", has_cr2 == (vector == 14), "cr2 line present: %s" % has_cr2)

    panic_re = re.compile(r"^PANIC unhandled exception %d$" % args.vector)
    p_at = next((i for i, l in enumerate(after) if panic_re.match(l)), None)
    check("panic-line", p_at is not None, "PANIC unhandled exception %d" % args.vector)
    check("halt-after-panic", p_at is not None and "HALT" in after[p_at + 1:], "HALT follows PANIC")
    bad = [l for l in lines if l.startswith("SELFTEST_FAIL") or l == "BOOT_OK"]
    check("no-continuation", not bad, "found: %s" % (bad or "none"))

    print("EXC_CHECK %s (%d failed)" % ("FAIL" if failures else "PASS", len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
