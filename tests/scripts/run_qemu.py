#!/usr/bin/env python3
"""Boot an ISO in QEMU and judge its COM1 serial log.

Exit codes (decoded by m0..m3_test.sh; never trust QEMU's own exit code):
  0 PASS        every --expect pattern and every script step succeeded, in
                order; no fail pattern; QEMU still running after the grace
                period; any requested VGA capture succeeded
  1 TIMEOUT     an expected line did not appear within --timeout seconds
                (counted from boot for --expect, from the start of each
                script "expect" step)
  2 EARLY_EXIT  QEMU exited by itself (shutdown, reset under -no-reboot,
                crash) before PASS was established
  3 FAIL_MARKER a --fail pattern matched a serial line (checked
                continuously, including during sleeps)
  4 ERROR       usage/script error, QEMU could not be started, or the QEMU
                monitor (key injection or VGA capture) failed

After every --expect pattern has matched and the grace period has passed,
the runner executes steps, in order, from --script FILE or from the
M2-style --sendkey/--expect-after options (all keys, then all expects):
  delay S        seconds between keys (default --key-delay)
  hold MS|none   sendkey hold time in ms (default: QEMU's 100 ms)
  key NAME       one QEMU sendkey key, e.g. ret, backspace, shift-h
  type TEXT      one key per character of TEXT (letters, digits, space,
                 shifted digits); everything after "type " is literal
  repeat N STEP  run a key/type step N times
  sleep S        wait S seconds (fail patterns and QEMU exit still checked)
  expect REGEX   wait for the next serial line matching REGEX, after the
                 line matched by the previous expect
Keys go through the QEMU monitor's "sendkey", i.e. QEMU's PS/2 keyboard
model: the guest sees real i8042 scan codes and IRQ1s; nothing is typed
on COM1.

VGA capture (--vga-dump / --screendump) runs through the monitor after
PASS is established and before QEMU is stopped:
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
SHIFTED_DIGITS = {")": "0", "!": "1", "@": "2", "#": "3", "$": "4",
                  "%": "5", "^": "6", "&": "7", "*": "8", "(": "9"}


class ScriptError(Exception):
    pass


def serial_lines(path):
    try:
        raw = open(path, "rb").read()
    except FileNotFoundError:
        return []
    text = ANSI.sub("", raw.decode("utf-8", "replace")).replace("\r", "\n")
    return [l.strip() for l in text.split("\n")]


def match_seq(lines, patterns, start=0):
    """Match patterns in order from lines[start:]; return (count, next index)."""
    n, pos = 0, start
    for i in range(start, len(lines)):
        if n == len(patterns):
            break
        if patterns[n].search(lines[i]):
            n += 1
            pos = i + 1
    return n, pos


def first_fail(lines, fails):
    for line in lines:
        for f in fails:
            if f.search(line):
                return line
    return None


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


class Monitor:
    """HMP over the monitor's unix socket, one command per prompt."""

    def __init__(self, path, timeout=10.0):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect(path)
        self.buf = b""
        self._until_prompt()

    def _until_prompt(self):
        while b"(qemu)" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise OSError("QEMU monitor closed the connection")
            self.buf += chunk
        out, self.buf = self.buf.split(b"(qemu)", 1)
        return out

    def cmd(self, command):
        self.sock.sendall(command.encode() + b"\n")
        reply = ANSI.sub("", self._until_prompt().decode("utf-8", "replace"))
        # The reply echoes the command; anything after it is an error text.
        tail = reply.rsplit(command, 1)[-1].strip()
        if tail:
            raise OSError("monitor %r: %s" % (command, tail))

    def close(self):
        self.sock.close()


def char_key(c):
    if "a" <= c <= "z" or "0" <= c <= "9":
        return c
    if "A" <= c <= "Z":
        return "shift-" + c.lower()
    if c == " ":
        return "spc"
    if c in SHIFTED_DIGITS:
        return "shift-" + SHIFTED_DIGITS[c]
    raise ScriptError("cannot type %r" % c)


def parse_script(text, key_delay):
    """Turn script text into a flat list of (op, arg) steps."""
    steps = [("delay", key_delay)]

    def expand(line, lineno):
        word, _, rest = line.partition(" ")
        if word == "key" and rest.strip():
            return [("key", rest.strip())]
        if word == "type" and rest:
            return [("key", char_key(c)) for c in rest]
        if word == "repeat":
            count, _, inner = rest.partition(" ")
            if not count.isdigit() or not inner:
                raise ScriptError("line %d: repeat N STEP" % lineno)
            return expand(inner, lineno) * int(count)
        if word == "sleep":
            return [("sleep", float(rest))]
        if word == "delay":
            return [("delay", float(rest))]
        if word == "hold":
            return [("hold", None if rest.strip() == "none" else int(rest))]
        if word == "expect" and rest:
            return [("expect", re.compile(rest))]
        raise ScriptError("line %d: cannot parse %r" % (lineno, line))

    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip("\n")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        steps.extend(expand(line.lstrip() if not line.startswith("type ") else line, lineno))
    return steps


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
    ap.add_argument("--script", help="step file run after the --expect patterns")
    ap.add_argument("--sendkey", action="append", default=[],
                    help="QEMU sendkey key (e.g. a, shift-h, spc, ret); repeat")
    ap.add_argument("--key-delay", type=float, default=0.25,
                    help="default seconds between injected keys")
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
    if args.script and args.sendkey:
        return finish("ERROR", "use --script or --sendkey, not both", "-", start)
    try:
        if args.script:
            steps = parse_script(open(args.script).read(), args.key_delay)
        else:
            steps = [("delay", args.key_delay)] + [("key", k) for k in args.sendkey] + \
                    [("expect", re.compile(e)) for e in args.expect_after]
    except (OSError, ScriptError, re.error, ValueError) as e:
        return finish("ERROR", "script: %s" % e, "-", start)
    expects = [re.compile(e) for e in args.expect]
    fails = [re.compile(f) for f in (args.fail if args.fail is not None else DEFAULT_FAIL)]
    n_keys = sum(1 for op, _ in steps if op == "key")
    n_expect = sum(1 for op, _ in steps if op == "expect")

    log = os.path.abspath(args.log)
    os.makedirs(os.path.dirname(log), exist_ok=True)
    captures = [p for p in (args.vga_dump, args.screendump) if p]
    for p in [log] + captures:
        if os.path.exists(p):
            os.remove(p)
    mon_sock = log + ".mon"
    if os.path.exists(mon_sock):
        os.remove(mon_sock)
    use_monitor = bool(captures or n_keys)
    monitor_arg = "unix:%s,server,nowait" % mon_sock if use_monitor else "none"

    cmd = [args.qemu, "-accel", "tcg", "-m", "128M", "-smp", "1", "-nic", "none",
           "-no-reboot", "-display", "none", "-monitor", monitor_arg,
           "-serial", "file:" + log, "-boot", "d", "-cdrom", args.iso]
    qemu_out = open(log + ".qemu", "w")
    qemu_out.write("# " + " ".join(cmd) + "\n")
    qemu_out.flush()
    try:
        proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=qemu_out,
                                stderr=subprocess.STDOUT, start_new_session=True)
    except OSError as e:
        return finish("ERROR", "cannot start QEMU: %s" % e, "-", start)

    monitor = None
    try:
        # Phase 1: boot markers, then grace.
        deadline = start + args.timeout
        passed_at = None
        cursor = 0
        while True:
            rc = proc.poll()
            lines = serial_lines(log)
            bad = first_fail(lines, fails)
            want, cursor = match_seq(lines, expects)
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
                    break
            elif now >= deadline:
                return finish("TIMEOUT", "%d/%d markers within %.0fs, waiting for %r" % (
                    want, len(expects), args.timeout, expects[want].pattern), None, start)
            time.sleep(0.1)

        if use_monitor:
            try:
                monitor = Monitor(mon_sock)
            except OSError as e:
                return finish("ERROR", "monitor: %s" % e, proc.poll(), start)

        # Phase 2: script steps. Every wait polls QEMU and the fail patterns.
        delay, hold = args.key_delay, None
        done_expects = 0
        for op, arg in steps:
            if op == "delay":
                delay = arg
                continue
            if op == "hold":
                hold = arg
                continue
            if op == "key":
                try:
                    monitor.cmd("sendkey %s" % arg + ("" if hold is None else " %d" % hold))
                except OSError as e:
                    return finish("ERROR", "sendkey: %s" % e, proc.poll(), start)
                wait_until = time.monotonic() + delay
                pattern = None
            elif op == "sleep":
                wait_until = time.monotonic() + arg
                pattern = None
            else:
                wait_until = time.monotonic() + args.timeout
                pattern = arg
            while True:
                rc = proc.poll()
                lines = serial_lines(log)
                bad = first_fail(lines, fails)
                if bad is not None:
                    return finish("FAIL_MARKER", repr(bad), rc, start)
                if rc is not None:
                    return finish("EARLY_EXIT", "QEMU exited during script (%d/%d expects)" % (
                        done_expects, n_expect), rc, start)
                if pattern is not None:
                    n, pos = match_seq(lines, [pattern], cursor)
                    if n:
                        cursor = pos
                        done_expects += 1
                        break
                now = time.monotonic()
                if now >= wait_until:
                    if pattern is None:
                        break
                    return finish("TIMEOUT", "script expect %d/%d within %.0fs: %r" % (
                        done_expects + 1, n_expect, args.timeout, pattern.pattern), None, start)
                time.sleep(min(0.05, max(0.0, wait_until - now)))

        # Final grace after the script.
        if steps and (n_keys or n_expect):
            grace_end = time.monotonic() + args.grace
            while time.monotonic() < grace_end:
                lines = serial_lines(log)
                bad = first_fail(lines, fails)
                if bad is not None:
                    return finish("FAIL_MARKER", repr(bad), proc.poll(), start)
                if proc.poll() is not None:
                    return finish("EARLY_EXIT", "QEMU exited after script", proc.poll(), start)
                time.sleep(0.1)

        detail = "%d markers, alive after %.1fs grace" % (len(expects), args.grace)
        if n_keys or n_expect:
            detail += ", script: %d keys, %d expects" % (n_keys, n_expect)
        if captures:
            try:
                if args.vga_dump:
                    # Filename must be quoted: HMP would parse "4000 /path" as
                    # the expression 4000 / path.
                    monitor.cmd('pmemsave 0x%x %d "%s"' % (
                        VGA_TEXT_BASE, VGA_TEXT_BYTES, os.path.abspath(args.vga_dump)))
                if args.screendump:
                    monitor.cmd("screendump %s" % os.path.abspath(args.screendump))
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
        if monitor is not None:
            monitor.close()
        stop(proc)
        qemu_out.close()
        if os.path.exists(mon_sock):
            os.remove(mon_sock)


def wait_for_file(path, min_size, timeout=3.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if os.path.isfile(path) and os.path.getsize(path) >= min_size:
            return True
        time.sleep(0.05)
    return False


if __name__ == "__main__":
    sys.exit(main())
