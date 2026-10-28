#!/usr/bin/env python3
"""Boot an ISO in QEMU and judge its COM1 serial log.

Exit codes (decoded by m0_test.sh; never trust QEMU's own exit code):
  0 PASS        every --expect pattern matched a line, in order; no fail
                pattern; QEMU still running after the grace period
  1 TIMEOUT     not all --expect patterns seen within --timeout seconds
  2 EARLY_EXIT  QEMU exited by itself (shutdown, reset under -no-reboot,
                crash) before PASS was established
  3 FAIL_MARKER a --fail pattern matched a serial line
  4 ERROR       usage error or QEMU could not be started

isa-debug-exit is not used in M0: a passing kernel halts and the runner
stops QEMU itself after the grace period.
"""
import argparse
import os
import re
import signal
import subprocess
import sys
import time

CODES = {"PASS": 0, "TIMEOUT": 1, "EARLY_EXIT": 2, "FAIL_MARKER": 3, "ERROR": 4}
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][0-9A-Za-z]|\x1b[=>78cDEHM]")
DEFAULT_FAIL = [r"BOOT_FAIL", r"PANIC", r"^error: "]


def serial_lines(path):
    try:
        raw = open(path, "rb").read()
    except FileNotFoundError:
        return []
    text = ANSI.sub("", raw.decode("utf-8", "replace")).replace("\r", "\n")
    return [l.strip() for l in text.split("\n")]


def judge(lines, expects, fails):
    """Return (index of next unmatched expect, failing line or None)."""
    want = 0
    for line in lines:
        for f in fails:
            if f.search(line):
                return want, line
        if want < len(expects) and expects[want].search(line):
            want += 1
    return want, None


def stop(proc):
    if proc.poll() is not None:
        return
    try:
        os.killpg(proc.pid, signal.SIGTERM)
        proc.wait(timeout=3)
    except (subprocess.TimeoutExpired, ProcessLookupError):
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        proc.wait()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", required=True)
    ap.add_argument("--log", required=True, help="serial log path")
    ap.add_argument("--expect", action="append", default=[],
                    help="regex for a serial line; repeat, matched in order")
    ap.add_argument("--fail", action="append", default=None,
                    help="regex that fails the run (default: %s)" % DEFAULT_FAIL)
    ap.add_argument("--timeout", type=float, default=15.0)
    ap.add_argument("--grace", type=float, default=1.0,
                    help="seconds QEMU must stay up after the last marker")
    ap.add_argument("--qemu", default="qemu-system-i386")
    args = ap.parse_args()

    def finish(result, detail, qemu_rc, start):
        code = CODES[result]
        print("RESULT=%s code=%d elapsed=%.1fs qemu_rc=%s log=%s detail=%s" % (
            result, code, time.monotonic() - start,
            "running(killed)" if qemu_rc is None else qemu_rc, args.log, detail))
        return code

    start = time.monotonic()
    if not args.expect or not os.path.isfile(args.iso):
        return finish("ERROR", "need --expect and an existing --iso", "-", start)
    expects = [re.compile(e) for e in args.expect]
    fails = [re.compile(f) for f in (args.fail if args.fail is not None else DEFAULT_FAIL)]

    os.makedirs(os.path.dirname(os.path.abspath(args.log)), exist_ok=True)
    if os.path.exists(args.log):
        os.remove(args.log)
    cmd = [args.qemu, "-accel", "tcg", "-m", "128M", "-smp", "1", "-nic", "none",
           "-no-reboot", "-display", "none", "-monitor", "none",
           "-serial", "file:" + args.log, "-boot", "d", "-cdrom", args.iso]
    qemu_out = open(args.log + ".qemu", "w")
    qemu_out.write("# " + " ".join(cmd) + "\n")
    qemu_out.flush()
    try:
        proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=qemu_out,
                                stderr=subprocess.STDOUT, start_new_session=True)
    except OSError as e:
        return finish("ERROR", "cannot start QEMU: %s" % e, "-", start)

    deadline = start + args.timeout
    passed_at = None
    try:
        while True:
            rc = proc.poll()
            want, bad = judge(serial_lines(args.log), expects, fails)
            if bad is not None:
                return finish("FAIL_MARKER", repr(bad), rc, start)
            if rc is not None:
                return finish("EARLY_EXIT", "QEMU exited after %d/%d markers" % (
                    want, len(expects)), rc, start)
            now = time.monotonic()
            if want == len(expects):
                if passed_at is None:
                    passed_at = now
                elif now - passed_at >= args.grace:
                    return finish("PASS", "%d markers, alive after %.1fs grace" % (
                        want, args.grace), None, start)
            elif now >= deadline:
                return finish("TIMEOUT", "%d/%d markers within %.0fs, waiting for %r" % (
                    want, len(expects), args.timeout, expects[want].pattern), None, start)
            time.sleep(0.1)
    finally:
        stop(proc)
        qemu_out.close()


if __name__ == "__main__":
    sys.exit(main())
