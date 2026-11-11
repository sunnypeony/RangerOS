#!/usr/bin/env bash
# M1 test suite: host formatter unit test, normal boot (serial + VGA),
# VGA scroll/wrap, #UD and #GP exception selftests, plus negative
# self-tests proving the VGA and exception checkers can fail.
# Exit 0 only if every case produced its expected result code.
set -u
cd "$(dirname "$0")/../.."

LOGDIR=build/test-logs
SUMMARY=$LOGDIR/summary-m1.txt
RUN="python3 tests/scripts/run_qemu.py"
VGA="python3 tests/scripts/check_vga.py"
EXC="python3 tests/scripts/check_exception.py"
BANNER='^RangerOS [0-9][^ ]* \(i386, multiboot1\)$'
GDT_OK='^GDT_OK cs=0x0008 ds=0x0010 ss=0x0010$'
IDT_OK='^IDT_OK vectors=256$'
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
    printf '%-4s %-22s expected_rc=%s got_rc=%s log=%s\n' \
        "$verdict" "$name" "$want" "$got" "$log" | tee -a "$SUMMARY"
    tail -n 1 "$log.out" | sed 's/^/       /' | tee -a "$SUMMARY"
}

echo "== M1 tests $(date -Iseconds)" | tee -a "$SUMMARY"

case_run host-format 0 $LOGDIR/host-format.log build/host/test_format

# Normal kernel: serial markers, then VGA text buffer and screenshot.
case_run boot-m1 0 $LOGDIR/m1-boot.serial.log \
    $RUN --iso build/rangeros.iso --log $LOGDIR/m1-boot.serial.log --timeout 15 \
         --expect "$BANNER" --expect "$GDT_OK" --expect "$IDT_OK" --expect '^BOOT_OK$' \
         --vga-dump $LOGDIR/m1-boot.vga.bin --screendump $LOGDIR/m1-boot.ppm

case_run vga-boot 0 $LOGDIR/m1-boot.vga.log \
    $VGA --dump $LOGDIR/m1-boot.vga.bin --text-out $LOGDIR/m1-boot.vga.txt \
         --expect "$BANNER" --expect "$GDT_OK" --expect "$IDT_OK" --expect '^BOOT_OK$' \
         --ppm $LOGDIR/m1-boot.ppm --png-out $LOGDIR/m1-boot.png

# VGA-only output: 30 formatted rows + 90-char wrapped line -> scrolled.
case_run vga-scroll-run 0 $LOGDIR/m1-vga.serial.log \
    $RUN --iso build/test/selftest-vga.iso --log $LOGDIR/m1-vga.serial.log --timeout 15 \
         --expect "$IDT_OK" --expect '^SELFTEST vga: start$' --expect '^VGA_TEST_DONE$' \
         --fail '^BOOT_OK$' --fail 'PANIC' --fail '^EXCEPTION ' --fail '^error: ' \
         --vga-dump $LOGDIR/m1-vga.vga.bin --screendump $LOGDIR/m1-vga.ppm

case_run vga-scroll 0 $LOGDIR/m1-vga.vga.log \
    $VGA --dump $LOGDIR/m1-vga.vga.bin --text-out $LOGDIR/m1-vga.vga.txt --scroll-test \
         --ppm $LOGDIR/m1-vga.ppm --png-out $LOGDIR/m1-vga.png

# Exception selftests: each boots on its own, must panic and stay halted
# (no triple-fault reset: -no-reboot would make QEMU exit -> EARLY_EXIT).
exc_case() {
    local tag=$1 vector=$2 name=$3 error=$4
    case_run exc-$tag-run 0 $LOGDIR/m1-$tag.serial.log \
        $RUN --iso build/test/selftest-$tag.iso --log $LOGDIR/m1-$tag.serial.log --timeout 15 \
             --expect "$IDT_OK" --expect "^SELFTEST $tag:" \
             --expect "^EXCEPTION vector=$vector name=$name " \
             --expect "^PANIC unhandled exception $vector$" --expect '^HALT$' \
             --fail '^SELFTEST_FAIL' --fail '^BOOT_OK$' --fail 'BOOT_FAIL' --fail '^error: ' \
             --vga-dump $LOGDIR/m1-$tag.vga.bin --screendump $LOGDIR/m1-$tag.ppm
    case_run exc-$tag 0 $LOGDIR/m1-$tag.exc.log \
        $EXC --log $LOGDIR/m1-$tag.serial.log --elf build/test/selftest-$tag.elf \
             --vector $vector --name "$name" --error $error
    case_run exc-$tag-vga 0 $LOGDIR/m1-$tag.vga.log \
        $VGA --dump $LOGDIR/m1-$tag.vga.bin --text-out $LOGDIR/m1-$tag.vga.txt \
             --expect "^EXCEPTION vector=$vector name=$name error=$(printf '0x%08x' $error) eip=0x[0-9a-f]{8} cs=0x0008$" \
             --expect '^  eflags=0x[0-9a-f]{8}$' \
             --expect "^PANIC unhandled exception $vector$" --expect '^HALT$' \
             --ppm $LOGDIR/m1-$tag.ppm --png-out $LOGDIR/m1-$tag.png
}
exc_case ud 6 '#UD' 0
exc_case gp 13 '#GP' 0x28

# Negative self-tests: the checkers must reject wrong expectations.
case_run neg-exc-wrong-vector 1 $LOGDIR/neg-exc-wrong-vector.log \
    $EXC --log $LOGDIR/m1-ud.serial.log --elf build/test/selftest-ud.elf \
         --vector 13 --name '#GP' --error 0
case_run neg-exc-wrong-error 1 $LOGDIR/neg-exc-wrong-error.log \
    $EXC --log $LOGDIR/m1-gp.serial.log --elf build/test/selftest-gp.elf \
         --vector 13 --name '#GP' --error 0
case_run neg-exc-normal-boot 1 $LOGDIR/neg-exc-normal-boot.log \
    $EXC --log $LOGDIR/m1-boot.serial.log --elf build/kernel.elf \
         --vector 6 --name '#UD' --error 0
case_run neg-vga-scroll-on-boot 1 $LOGDIR/neg-vga-scroll-on-boot.log \
    $VGA --dump $LOGDIR/m1-boot.vga.bin --scroll-test
case_run neg-vga-missing-row 1 $LOGDIR/neg-vga-missing-row.log \
    $VGA --dump $LOGDIR/m1-boot.vga.bin --expect '^NO_SUCH_ROW$'

if [ "$failed" -eq 0 ]; then
    echo "M1_TEST PASS" | tee -a "$SUMMARY"
    exit 0
fi
echo "M1_TEST FAIL ($failed case(s) did not match expectation)" | tee -a "$SUMMARY"
exit 1
