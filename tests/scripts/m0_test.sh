#!/usr/bin/env bash
# M0 test suite: static ELF checks + real GRUB ISO boot, plus negative
# self-tests proving the checker and runner cannot report a broken image,
# a missing marker, a timeout or an early QEMU exit as success.
# Exit 0 only if every case produced its expected result code.
set -u
cd "$(dirname "$0")/../.."

LOGDIR=build/test-logs
SUMMARY=$LOGDIR/summary-m0.txt
RUN="python3 tests/scripts/run_qemu.py"
BANNER='^RangerOS [0-9][^ ]* \(i386, multiboot1\)$'
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

echo "== M0 tests $(date -Iseconds)" | tee -a "$SUMMARY"

case_run elf-check 0 $LOGDIR/elf-check.log \
    python3 tests/scripts/check_elf.py build/kernel.elf
cp $LOGDIR/elf-check.log.out $LOGDIR/elf-check.log

# Negative: checker must reject an ELF without a valid Multiboot header.
case_run neg-elf-bad-multiboot 1 $LOGDIR/neg-elf-bad-multiboot.log \
    python3 tests/scripts/check_elf.py build/neg/bad-multiboot.elf

# Negative: .bss linked NOLOAD is outside every PT_LOAD -> checker rc=1.
case_run neg-elf-noload-bss 1 $LOGDIR/neg-elf-noload-bss.log \
    python3 tests/scripts/check_elf.py build/neg/noload-bss.elf

case_run boot 0 $LOGDIR/boot.serial.log \
    $RUN --iso build/rangeros.iso --log $LOGDIR/boot.serial.log \
         --timeout 15 --expect "$BANNER" --expect '^BOOT_OK$'

# BOOT_FAIL path: test entry enters _start with EAX=0xDEADBEEF. Must print
# BOOT_FAIL with that value, must not print BOOT_OK, and must stay halted.
case_run boot-fail-bad-magic 0 $LOGDIR/boot-fail-bad-magic.serial.log \
    $RUN --iso build/test/bad-magic.iso --log $LOGDIR/boot-fail-bad-magic.serial.log \
         --timeout 15 --expect "$BANNER" --expect '^BOOT_FAIL bad multiboot magic 0xdeadbeef$' \
         --fail '^BOOT_OK$' --fail 'PANIC' --fail '^error: '

# Negative: GRUB refuses the image ("error: ..." on serial) -> FAIL_MARKER.
case_run neg-boot-bad-multiboot 3 $LOGDIR/neg-bad-multiboot.serial.log \
    $RUN --iso build/neg/bad-multiboot.iso --log $LOGDIR/neg-bad-multiboot.serial.log \
         --timeout 15 --expect "$BANNER" --expect '^BOOT_OK$'

# Negative: real kernel boots but the awaited marker never appears -> TIMEOUT.
case_run neg-missing-marker 1 $LOGDIR/neg-missing-marker.serial.log \
    $RUN --iso build/rangeros.iso --log $LOGDIR/neg-missing-marker.serial.log \
         --timeout 10 --expect "$BANNER" --expect '^BOOT_NEVER$'

# Negative: machine resets before the kernel; -no-reboot exits QEMU -> EARLY_EXIT.
case_run neg-early-exit 2 $LOGDIR/neg-early-exit.serial.log \
    $RUN --iso build/neg/early-exit.iso --log $LOGDIR/neg-early-exit.serial.log \
         --timeout 15 --expect "$BANNER" --expect '^BOOT_OK$'

if [ "$failed" -eq 0 ]; then
    echo "M0_TEST PASS" | tee -a "$SUMMARY"
    exit 0
fi
echo "M0_TEST FAIL ($failed case(s) did not match expectation)" | tee -a "$SUMMARY"
exit 1
