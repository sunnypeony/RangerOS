#!/usr/bin/env bash
# M2 test suite: host tests for the input queue and scan code decoder,
# PIT/IRQ accounting with interrupts on, IRQ-return integrity probe (and
# a sabotaged return path it must catch), PS/2 keyboard injection through
# the QEMU monitor, plus negative self-tests for the new checkers.
# Exit 0 only if every case produced its expected result code.
set -u
cd "$(dirname "$0")/../.."

LOGDIR=build/test-logs
SUMMARY=$LOGDIR/summary-m2.txt
RUN="python3 tests/scripts/run_qemu.py"
VGA="python3 tests/scripts/check_vga.py"
IRQ="python3 tests/scripts/check_irq.py"
KBD="python3 tests/scripts/check_kbd.py"
BANNER='^RangerOS [0-9][^ ]* \(i386, multiboot1\)$'
PIC_OK='^PIC_OK vectors=0x20/0x28 mask=0xfc/0xff$'
PIT_OK='^PIT_OK hz=100 divisor=11932$'
KBD_OK='^KBD_OK set=1 translate=1 config=0x[0-9a-f]{2} queue=128$'
mkdir -p "$LOGDIR"
: > "$SUMMARY"
failed=0

# case NAME EXPECTED_RC LOGFILE COMMAND...
case_run() {
    local name=$1 want=$2 log=$3
    shift 3
    "$@" > "$log.out" 2>&1
    local got=$?
    local verdict=PASS
    [ "$got" -eq "$want" ] || { verdict=FAIL; failed=$((failed + 1)); }
    printf '%-4s %-24s expected_rc=%s got_rc=%s log=%s\n' \
        "$verdict" "$name" "$want" "$got" "$log" | tee -a "$SUMMARY"
    tail -n 1 "$log.out" | sed 's/^/       /' | tee -a "$SUMMARY"
}

echo "== M2 tests $(date -Iseconds)" | tee -a "$SUMMARY"

case_run host-input-queue 0 $LOGDIR/host-input-queue.log build/host/test_input_queue
case_run host-kbd-decode 0 $LOGDIR/host-kbd-decode.log build/host/test_kbd_decode

# Normal kernel must not carry any test hook.
case_run kernel-no-test-symbols 0 $LOGDIR/kernel-no-test-symbols.log \
    bash -c '! nm build/kernel.elf | grep -iE "selftest|irqret|sabotage" && echo "no test symbols in build/kernel.elf"'

# IRQs on: init lines, then >= 5 periodic reports (>= 500 PIT IRQs).
case_run irq-ticks-run 0 $LOGDIR/m2-ticks.serial.log \
    $RUN --iso build/rangeros.iso --log $LOGDIR/m2-ticks.serial.log --timeout 15 \
         --expect "$BANNER" --expect "$PIT_OK" --expect "$KBD_OK" --expect "$PIC_OK" \
         --expect '^IRQ_ON$' --expect '^BOOT_OK$' \
         --expect '^TICKS 100 ' --expect '^TICKS 200 ' --expect '^TICKS 300 ' \
         --expect '^TICKS 400 ' --expect '^TICKS 500 '
case_run irq-ticks 0 $LOGDIR/m2-ticks.irq.log \
    $IRQ --log $LOGDIR/m2-ticks.serial.log --min-ticks 500 --min-reports 5

# IRQ return path: registers, ESP, stack, DF/IF and an LCG result across
# >= 150 PIT IRQs; then the kernel keeps running (two more reports).
case_run irqret-run 0 $LOGDIR/m2-irqret.serial.log \
    $RUN --iso build/test/selftest-irqret.iso --log $LOGDIR/m2-irqret.serial.log --timeout 30 \
         --expect '^BOOT_OK$' --expect '^SELFTEST irqret: start .* if=1$' \
         --expect '^IRQRET_OK iters=[1-9][0-9]* ticks=[0-9]+ irq0=[0-9]+ ' \
         --expect '^TICKS [0-9]+ ' --expect '^TICKS [0-9]+ ' \
         --fail '^IRQRET_FAIL' --fail 'PANIC' --fail '^EXCEPTION ' --fail '^error: '
case_run irqret-ticks 0 $LOGDIR/m2-irqret.irq.log \
    $IRQ --log $LOGDIR/m2-irqret.serial.log --min-ticks 100 --min-reports 2

# Negative: return path that corrupts ESI must be caught (code 4).
case_run neg-irqret-sabotage 0 $LOGDIR/neg-irqret-sabotage.serial.log \
    $RUN --iso build/test/selftest-irqret-sabotage.iso \
         --log $LOGDIR/neg-irqret-sabotage.serial.log --timeout 30 \
         --expect '^IRQRET_FAIL code=4 \(esi\) ' \
         --fail '^IRQRET_OK' --fail 'PANIC' --fail '^EXCEPTION ' --fail '^error: '

# PS/2 keyboard through QEMU's monitor sendkey (never COM1 input):
# 22 keys = "Hi RangerOS 123!x" + Backspace + Enter, then "ok" + Enter.
# Shifted keys give 4 scan codes (Shift make, key make, key break, Shift
# break), others 2: 5*4 + 17*2 = 54 scan codes, 22 bytes, 32 ignored.
# The last Enter's break code arrives after the KBD line, so the full 54
# is checked in a later TICKS report.
KEYS=(shift-h i spc shift-r a n g e r shift-o shift-s spc 1 2 3 shift-1 x backspace ret o k ret)
SENDKEYS=()
for k in "${KEYS[@]}"; do SENDKEYS+=(--sendkey "$k"); done
case_run kbd-run 0 $LOGDIR/m2-kbd.serial.log \
    $RUN --iso build/rangeros.iso --log $LOGDIR/m2-kbd.serial.log --timeout 20 \
         --expect '^BOOT_OK$' --expect '^TICKS 100 ' "${SENDKEYS[@]}" \
         --expect-after '^KEY 0x0a ENTER$' --expect-after '^KEY 0x0a ENTER$' \
         --expect-after '^KBD chars=22 ' --expect-after '^TICKS [0-9]+ .* kbd_scancodes=54 ' \
         --vga-dump $LOGDIR/m2-kbd.vga.bin --screendump $LOGDIR/m2-kbd.ppm
case_run kbd 0 $LOGDIR/m2-kbd.kbd.log \
    $KBD --log $LOGDIR/m2-kbd.serial.log --keys 22 --chars 22 --scancodes 54 \
         --line 'Hi RangerOS 123!' --line 'ok'
case_run kbd-vga 0 $LOGDIR/m2-kbd.vga.log \
    $VGA --dump $LOGDIR/m2-kbd.vga.bin --text-out $LOGDIR/m2-kbd.vga.txt \
         --expect '^BOOT_OK$' --expect '^Hi RangerOS 123!$' --expect '^ok$' \
         --ppm $LOGDIR/m2-kbd.ppm --png-out $LOGDIR/m2-kbd.png
case_run kbd-irq 0 $LOGDIR/m2-kbd.irq.log \
    $IRQ --log $LOGDIR/m2-kbd.serial.log --min-ticks 100 --min-reports 1

# Negative self-tests.
case_run neg-kbd-no-injection 1 $LOGDIR/neg-kbd-no-injection.serial.log \
    $RUN --iso build/rangeros.iso --log $LOGDIR/neg-kbd-no-injection.serial.log --timeout 6 \
         --expect '^BOOT_OK$' --expect '^KEY '
case_run neg-kbd-wrong-line 1 $LOGDIR/neg-kbd-wrong-line.log \
    $KBD --log $LOGDIR/m2-kbd.serial.log --keys 22 --chars 22 --scancodes 54 \
         --line 'hi rangeros 123!' --line 'ok'
case_run neg-kbd-wrong-stats 1 $LOGDIR/neg-kbd-wrong-stats.log \
    $KBD --log $LOGDIR/m2-kbd.serial.log --keys 22 --chars 22 --scancodes 44 \
         --line 'Hi RangerOS 123!' --line 'ok'
case_run neg-irq-too-few 1 $LOGDIR/neg-irq-too-few.log \
    $IRQ --log $LOGDIR/m2-ticks.serial.log --min-ticks 100000
case_run neg-irq-no-reports 1 $LOGDIR/neg-irq-no-reports.log \
    $IRQ --log $LOGDIR/neg-irqret-sabotage.serial.log --min-ticks 1 --min-reports 1

if [ "$failed" -eq 0 ]; then
    echo "M2_TEST PASS" | tee -a "$SUMMARY"
    exit 0
fi
echo "M2_TEST FAIL ($failed case(s) did not match expectation)" | tee -a "$SUMMARY"
exit 1
