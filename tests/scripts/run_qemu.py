#!/usr/bin/env python3
"""Boot an ISO in QEMU and judge its COM1 serial log.

Exit codes (decoded by m0_test.sh/m1_test.sh/m2_test.sh; never trust
QEMU's own exit code):
  0 PASS        every --expect (and --expect-after) pattern matched a line,
                in order; no fail pattern; QEMU still running after the
                grace period; any requested key injection and VGA capture
                succeeded
  1 TIMEOUT     not all patterns seen within --timeout seconds (counted
                from boot for --expect, from the end of key injection for
                --expect-after)
  2 EARLY_EXIT  QEMU exited by itself (shutdown, reset under -no-reboot,
                crash) before PASS was established
  3 FAIL_MARKER a --fail pattern matched a serial line
  4 ERROR       usage error, QEMU could not be started, or the QEMU monitor
                (key injection or VGA capture) failed

Key injection (--sendkey, repeatable) runs once every --expect pattern has
matched and the grace period has passed. Each key goes through the QEMU
monitor's "sendkey" command, i.e. QEMU's PS/2 keyboard model, so the
guest sees real i8042 scan codes and IRQ1s; nothing is typed on COM1.
--expect-after patterns are then matched after the --expect ones.

VGA capture (--vga-dump / --screendump) runs through the same monitor
socket after PASS is established and before QEMU is stopped:
  --vga-dump   pmemsave of the 80x25 text buffer at 0xB8000 (4000 bytes)
  --screendump the rendered VGA screen as PPM (a real screenshot)

isa-debug-exit is not used: a passing kernel halts or idles and the runner
stops QEMU itself after the grace period.
"""
import argparse
import os
import re
import signal
import socket
import subprocess
import sys
import time

CODES = {"PASS": 0, "TIMEOUT": 1, "EARLY_EXIT": 2, "FAIL_MARKER": 3, "ERROR": 4}
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]|\x1b[()][0-9A-Za-z]|\x1b[=>78cDEHM]")
DEFAULT_FAIL = [r"BOOT_FAIL", r"PANIC", r"^EXCEPTION ", r"^error: "]
VGA_TEXT_BASE = 0xB8000
VGA_TEXT_BYTES = 80 * 25 * 2


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


def monitor_run(sock_path, commands, delay=0.0, timeout=10.0):
    """Send HMP commands over the monitor socket, one prompt at a time."""
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(sock_path)
    buf = b""

    def until_prompt():
        nonlocal buf
        while b"(qemu)" not in buf:
            chunk = s.recv(4096)
            if not chunk:
                raise OSError("QEMU monitor closed the connection")
            buf += chunk
        out, buf = buf.split(b"(qemu)", 1)
        return out

    until_prompt()
    for cmd in commands:
        s.sendall(cmd.encode() + b"\n")
        reply = ANSI.sub("", until_prompt().decode("utf-8", "replace"))
        # The reply echoes the command; anything after it is an error text.
        tail = reply.rsplit(cmd, 1)[-1].strip()
        if tail:
            raise OSError("monitor %r: %s" % (cmd, tail))
        if delay:
            time.sleep(delay)
    s.close()


def wait_for_file(path, min_size, timeout=3.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if os.path.isfile(path) and os.path.getsize(path) >= min_size:
            return True
        time.sleep(0.05)
    return False


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
    ap.add_argument("--sendkey", action="append", default=[],
                    help="QEMU sendkey key (e.g. a, shift-h, spc, ret); repeat")
    ap.add_argument("--key-delay", type=float, default=0.25,
                    help="seconds between injected keys")
    ap.add_argument("--expect-after", action="append", default=[],
                    help="regex expected after key injection; repeat, in order")
    ap.add_argument("--vga-dump", help="write the VGA text buffer here after PASS")
    ap.add_argument("--screendump", help="write a PPM screenshot here after PASS")
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
    if args.expect_after and not args.sendkey:
        return finish("ERROR", "--expect-after needs --sendkey", "-", start)
    expects = [re.compile(e) for e in args.expect + args.expect_after]
    phase1 = len(args.expect)
    fails = [re.compile(f) for f in (args.fail if args.fail is not None else DEFAULT_FAIL)]

    log = os.path.abspath(args.log)
    os.makedirs(os.path.dirname(log), exist_ok=True)
    captures = [p for p in (args.vga_dump, args.screendump) if p]
    for p in [log] + captures:
        if os.path.exists(p):
            os.remove(p)
    mon_sock = log + ".mon"
    if os.path.exists(mon_sock):
        os.remove(mon_sock)
    use_monitor = bool(captures or args.sendkey)
    monitor = "unix:%s,server,nowait" % mon_sock if use_monitor else "none"

    cmd = [args.qemu, "-accel", "tcg", "-m", "128M", "-smp", "1", "-nic", "none",
           "-no-reboot", "-display", "none", "-monitor", monitor,
           "-serial", "file:" + log, "-boot", "d", "-cdrom", args.iso]
    qemu_out = open(log + ".qemu", "w")
    qemu_out.write("# " + " ".join(cmd) + "\n")
    qemu_out.flush()
    try:
        proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=qemu_out,
                                stderr=subprocess.STDOUT, start_new_session=True)
    except OSError as e:
        return finish("ERROR", "cannot start QEMU: %s" % e, "-", start)

    deadline = start + args.timeout
    passed_at = None
    injected = not args.sendkey
    try:
        while True:
            rc = proc.poll()
            want, bad = judge(serial_lines(log), expects, fails)
            if bad is not None:
                return finish("FAIL_MARKER", repr(bad), rc, start)
            if rc is not None:
                return finish("EARLY_EXIT", "QEMU exited after %d/%d markers" % (
                    want, len(expects)), rc, start)
            now = time.monotonic()
            target = len(expects) if injected else phase1
            if want >= target:
                if passed_at is None:
                    passed_at = now
                elif now - passed_at >= args.grace:
                    if injected:
                        break
                    try:
                        monitor_run(mon_sock, ["sendkey %s" % k for k in args.sendkey],
                                    delay=args.key_delay)
                    except OSError as e:
                        return finish("ERROR", "sendkey: %s" % e, proc.poll(), start)
                    injected = True
                    passed_at = None
                    deadline = time.monotonic() + args.timeout
                    continue
            elif now >= deadline:
                return finish("TIMEOUT", "%d/%d markers within %.0fs, waiting for %r" % (
                    want, len(expects), args.timeout, expects[want].pattern), None, start)
            time.sleep(0.1)

        detail = "%d markers, alive after %.1fs grace" % (len(expects), args.grace)
        if args.sendkey:
            detail += ", injected %d keys" % len(args.sendkey)
        if captures:
            mon_cmds = []
            if args.vga_dump:
                # Filename must be quoted: HMP would parse "4000 /path" as
                # the expression 4000 / path.
                mon_cmds.append('pmemsave 0x%x %d "%s"' % (
                    VGA_TEXT_BASE, VGA_TEXT_BYTES, os.path.abspath(args.vga_dump)))
            if args.screendump:
                mon_cmds.append("screendump %s" % os.path.abspath(args.screendump))
            try:
                monitor_run(mon_sock, mon_cmds)
            except OSError as e:
                return finish("ERROR", "monitor: %s" % e, proc.poll(), start)
            if args.vga_dump and not wait_for_file(args.vga_dump, VGA_TEXT_BYTES):
                return finish("ERROR", "no VGA dump written", proc.poll(), start)
            if args.screendump and not wait_for_file(args.screendump, 16):
                return finish("ERROR", "no screendump written", proc.poll(), start)
            if proc.poll() is not None:
                return finish("EARLY_EXIT", "QEMU exited during capture", proc.poll(), start)
            detail += ", captured " + "+".join(
                n for n, p in (("vga", args.vga_dump), ("screen", args.screendump)) if p)
        return finish("PASS", detail, None, start)
    finally:
        stop(proc)
        qemu_out.close()
        if os.path.exists(mon_sock):
            os.remove(mon_sock)


if __name__ == "__main__":
    sys.exit(main())
