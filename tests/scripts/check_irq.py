#!/usr/bin/env python3
"""Check the periodic TICKS lines of a serial log (M2 IRQ accounting).

  TICKS <n> irq0=.. eoi0=.. irq1=.. eoi1=.. spurious=.. unexpected=..
        maxdepth=.. ifin=.. kbd_scancodes=.. kbd_empty=.. dropped=..

Requires at least --min-reports lines and a final tick >= --min-ticks;
tick values strictly increasing (wrap-aware 32-bit compare) and so are
irq0 counts; every IRQ got its EOI (irqN == eoiN); irq0 >= ticks; no
nesting (maxdepth == 1); IF never set inside a handler; no unexpected
IRQs. Tick rate is deliberately not compared with wall-clock time.
Exit 0 only if all pass.
"""
import argparse
import re
import sys

ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][0-9A-Za-z]|\x1b[=>78cDEHM]")
TICKS = re.compile(r"^TICKS (\d+)((?: \w+=\d+)+)$")

failures = []


def check(name, ok, detail):
    print("%s %s: %s" % ("PASS" if ok else "FAIL", name, detail))
    if not ok:
        failures.append(name)


def after(a, b):
    """a comes after b in 32-bit wrap-around order."""
    d = (a - b) & 0xFFFFFFFF
    return d != 0 and d < 0x80000000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--min-ticks", type=int, default=300)
    ap.add_argument("--min-reports", type=int, default=3)
    args = ap.parse_args()

    raw = open(args.log, "rb").read().decode("utf-8", "replace")
    lines = [l.strip() for l in ANSI.sub("", raw).replace("\r", "\n").split("\n")]
    reports = []
    for l in lines:
        m = TICKS.match(l)
        if m:
            fields = dict(kv.split("=") for kv in m.group(2).split())
            reports.append((int(m.group(1)), {k: int(v) for k, v in fields.items()}))

    check("reports", len(reports) >= args.min_reports,
          "%d TICKS lines (need >= %d)" % (len(reports), args.min_reports))
    if not reports:
        print("IRQ_CHECK FAIL (%d failed)" % len(failures))
        return 1
    last_tick, last = reports[-1]
    check("min-ticks", last_tick >= args.min_ticks and last["irq0"] >= args.min_ticks,
          "last TICKS %d, irq0=%d (need >= %d)" % (last_tick, last["irq0"], args.min_ticks))

    ticks_inc = all(after(b[0], a[0]) for a, b in zip(reports, reports[1:]))
    irq0_inc = all(after(b[1]["irq0"], a[1]["irq0"]) for a, b in zip(reports, reports[1:]))
    check("ticks-increase", ticks_inc, "tick values %s" % [r[0] for r in reports])
    check("irq0-increase", irq0_inc, "irq0 keeps arriving after EOI")

    bad = []
    for tick, f in reports:
        if f["irq0"] != f["eoi0"] or f["irq1"] != f["eoi1"]:
            bad.append("TICKS %d: eoi mismatch" % tick)
        if f["irq0"] < tick:
            bad.append("TICKS %d: irq0=%d < ticks" % (tick, f["irq0"]))
        if f["maxdepth"] != 1:
            bad.append("TICKS %d: maxdepth=%d" % (tick, f["maxdepth"]))
        if f["ifin"] != 0:
            bad.append("TICKS %d: ifin=%d" % (tick, f["ifin"]))
        if f["unexpected"] != 0:
            bad.append("TICKS %d: unexpected=%d" % (tick, f["unexpected"]))
    check("per-report-invariants", not bad,
          "eoi==irq, irq0>=ticks, maxdepth=1, ifin=0, unexpected=0: %s" % (bad[:3] or "ok"))
    check("spurious", last["spurious"] == 0, "spurious=%d" % last["spurious"])

    print("IRQ_CHECK %s (%d failed)" % ("FAIL" if failures else "PASS", len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
