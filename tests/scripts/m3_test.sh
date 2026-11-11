#!/usr/bin/env bash
# M3 test suite: line editor host test; every shell command, edge cases,
# kernel queue-full and a 2-minute soak, all driven through QEMU's PS/2
# keyboard (monitor sendkey; scripts in tests/m3/), with serial and VGA
# checks; plus negative self-tests for the shell checker.
# Exit 0 only if every case produced its expected result code.
set -u
cd "$(dirname "$0")/../.."

LOGDIR=build/test-logs
SUMMARY=$LOGDIR/summary-m3.txt
RUN="python3 tests/scripts/run_qemu.py"
VGA="python3 tests/scripts/check_vga.py"
IRQ="python3 tests/scripts/check_irq.py"
SHELLCK="python3 tests/scripts/check_shell.py"
READY='^RangerOS shell: type help$'
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

echo "== M3 tests $(date -Iseconds)" | tee -a "$SUMMARY"

case_run host-line-edit 0 $LOGDIR/host-line-edit.log build/host/test_line_edit

# 1. All commands, input continues afterwards (also during demo).
case_run shell-commands-run 0 $LOGDIR/m3-cmds.serial.log \
    $RUN --iso build/rangeros.iso --log $LOGDIR/m3-cmds.serial.log --timeout 20 \
         --expect '^BOOT_OK$' --expect "$READY" --script tests/m3/shell-commands.script \
         --vga-dump $LOGDIR/m3-cmds.vga.bin --screendump $LOGDIR/m3-cmds.ppm
case_run shell-commands 0 $LOGDIR/m3-cmds.shell.log \
    $SHELLCK --log $LOGDIR/m3-cmds.serial.log \
         --cmd help --cmd version --cmd ticks --cmd demo --cmd ticks --cmd clear --cmd version \
         --out '0:^commands:$' --out '0:^help +list commands$' --out '0:^version +' \
         --out '0:^ticks +' --out '0:^clear +' --out '0:^demo +' \
         --out '1:^RangerOS 0\.1\.0-mvp \(i386, multiboot1\)$' --out '1:^MVP: ' \
         --out '2:^ticks=[0-9]+ hz=100 uptime=[0-9]+\.[0-9]{2}s \(simulated\)$' \
         --out '3:^demo step 5/5 tick=' --out '3:^demo done after (9[0-9]|1[0-9][0-9]) ticks$' \
         --out '4:^ticks=' --out '5:^CLEAR vga$' --out '6:^MVP: ' \
         --ticks-increase --dropped 0
# After clear the screen restarts at row 0 with the next command.
case_run shell-commands-vga 0 $LOGDIR/m3-cmds.vga.log \
    $VGA --dump $LOGDIR/m3-cmds.vga.bin --text-out $LOGDIR/m3-cmds.vga.txt \
         --row '0=^> version$' --row '1=^RangerOS 0\.1\.0-mvp \(i386, multiboot1\)$' \
         --row '2=^MVP: ' --row '3=^>$' --row '4=^$' \
         --ppm $LOGDIR/m3-cmds.ppm --png-out $LOGDIR/m3-cmds.png

# 2. Edge cases.
case_run shell-edge-run 0 $LOGDIR/m3-edge.serial.log \
    $RUN --iso build/rangeros.iso --log $LOGDIR/m3-edge.serial.log --timeout 20 \
         --expect '^BOOT_OK$' --expect "$READY" --script tests/m3/shell-edge.script \
         --vga-dump $LOGDIR/m3-edge.vga.bin --screendump $LOGDIR/m3-edge.ppm
SP122=$(printf '%122s' '')
X127=$(printf 'x%.0s' $(seq 127))
case_run shell-edge 0 $LOGDIR/m3-edge.shell.log \
    $SHELLCK --log $LOGDIR/m3-edge.serial.log \
         --cmd '' --cmd foo --cmd version --cmd ticks --cmd "ticks$SP122" --cmd "ticks$SP122" \
         --cmd "$X127" --cmd version --cmd 'help me' \
         --no-out 0 --out '1:^unknown command: foo \(type help\)$' --out '2:^MVP: ' \
         --out '3:^ticks=' --out '4:^ticks=' \
         --out '5:^shell: line too long \(max 127 chars\), 1 rejected; discarded$' \
         --out '6:^shell: line too long \(max 127 chars\), 23 rejected; discarded$' \
         --out '7:^MVP: ' --out '8:^shell: help takes no arguments$' --dropped 0
case_run shell-edge-vga 0 $LOGDIR/m3-edge.vga.log \
    $VGA --dump $LOGDIR/m3-edge.vga.bin --text-out $LOGDIR/m3-edge.vga.txt \
         --expect '^shell: line too long .* 23 rejected' --expect '^> version$' \
         --expect '^MVP: ' --expect '^> help me$' --expect '^shell: help takes no arguments$' \
         --expect '^>$' \
         --ppm $LOGDIR/m3-edge.ppm --png-out $LOGDIR/m3-edge.png

# 3. Kernel queue full: consumer held, IRQ1 still receiving.
QFULL_TEXT=$(python3 -c 'print(("0123456789" * 14)[:128])')
case_run qfull-run 0 $LOGDIR/m3-qfull.serial.log \
    $RUN --iso build/test/selftest-qfull.iso --log $LOGDIR/m3-qfull.serial.log --timeout 30 \
         --expect '^IRQ_ON$' --expect '^QFULL_PAUSE ready ' --script tests/m3/qfull.script \
         --fail 'QFULL_TIMEOUT' --fail 'PANIC' --fail '^EXCEPTION ' --fail '^error: ' \
         --vga-dump $LOGDIR/m3-qfull.vga.bin --screendump $LOGDIR/m3-qfull.ppm
case_run qfull 0 $LOGDIR/m3-qfull.shell.log \
    $SHELLCK --log $LOGDIR/m3-qfull.serial.log \
         --cmd "${QFULL_TEXT:0:127}" --cmd ticks --cmd version \
         --out '0:^shell: line too long \(max 127 chars\), 1 rejected; discarded$' \
         --out '1:^ticks=' --out '2:^MVP: ' --dropped 12 --keys-before-first "$QFULL_TEXT"
case_run qfull-vga 0 $LOGDIR/m3-qfull.vga.log \
    $VGA --dump $LOGDIR/m3-qfull.vga.bin --text-out $LOGDIR/m3-qfull.vga.txt \
         --expect '^shell: line too long' --expect '^> ticks$' --expect '^ticks=' \
         --ppm $LOGDIR/m3-qfull.ppm --png-out $LOGDIR/m3-qfull.png

# 4. Soak: >= 120 s with IRQs on and commands spread across the run.
case_run soak-run 0 $LOGDIR/m3-soak.serial.log \
    $RUN --iso build/rangeros.iso --log $LOGDIR/m3-soak.serial.log --timeout 30 \
         --expect '^BOOT_OK$' --expect "$READY" --script tests/m3/soak.script \
         --vga-dump $LOGDIR/m3-soak.vga.bin --screendump $LOGDIR/m3-soak.ppm
case_run soak-irq 0 $LOGDIR/m3-soak.irq.log \
    $IRQ --log $LOGDIR/m3-soak.serial.log --min-ticks 12000 --min-reports 120
case_run soak-shell 0 $LOGDIR/m3-soak.shell.log \
    $SHELLCK --log $LOGDIR/m3-soak.serial.log \
         --cmd help --cmd ticks --cmd demo --cmd version --cmd foo --cmd ticks --cmd clear \
         --cmd ticks --cmd version --ticks-increase --dropped 0 \
         --out '7:^ticks=1[2-9][0-9]{3} '
case_run soak-vga 0 $LOGDIR/m3-soak.vga.log \
    $VGA --dump $LOGDIR/m3-soak.vga.bin --text-out $LOGDIR/m3-soak.vga.txt \
         --row '0=^> ticks$' --row '1=^ticks=1[2-9][0-9]{3} hz=100 ' --row '2=^> version$' \
         --ppm $LOGDIR/m3-soak.ppm --png-out $LOGDIR/m3-soak.png

# Negative self-tests.
case_run neg-shell-wrong-cmds 1 $LOGDIR/neg-shell-wrong-cmds.log \
    $SHELLCK --log $LOGDIR/m3-cmds.serial.log --cmd help --cmd version
case_run neg-shell-wrong-out 1 $LOGDIR/neg-shell-wrong-out.log \
    $SHELLCK --log $LOGDIR/m3-edge.serial.log \
         --cmd '' --cmd foo --cmd version --cmd ticks --cmd "ticks$SP122" --cmd "ticks$SP122" \
         --cmd "$X127" --cmd version --cmd 'help me' --out '5:^ticks='
case_run neg-shell-wrong-drop 1 $LOGDIR/neg-shell-wrong-drop.log \
    $SHELLCK --log $LOGDIR/m3-qfull.serial.log \
         --cmd "${QFULL_TEXT:0:127}" --cmd ticks --cmd version --dropped 0
case_run neg-script-timeout 1 $LOGDIR/neg-script-timeout.serial.log \
    $RUN --iso build/rangeros.iso --log $LOGDIR/neg-script-timeout.serial.log --timeout 5 \
         --expect "$READY" --script tests/m3/neg-timeout.script

if [ "$failed" -eq 0 ]; then
    echo "M3_TEST PASS" | tee -a "$SUMMARY"
    exit 0
fi
echo "M3_TEST FAIL ($failed case(s) did not match expectation)" | tee -a "$SUMMARY"
exit 1
