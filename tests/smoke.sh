#!/usr/bin/env bash
# End-to-end smoke tests: drive trident_dbg with scripted commands on stdin
# and check its output.
#
# usage: tests/smoke.sh <build-dir>
set -uo pipefail

BUILD=${1:?usage: tests/smoke.sh <build-dir>}
DBG="$BUILD/trident_dbg"
DUMMY="$BUILD/dummy"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

failures=0

# run <name> <target> <commands...>  -> output in $TMP/<name>.out
run() {
    local name=$1 target=$2; shift 2
    printf '%s\n' "$@" | timeout 10 "$DBG" "$target" > "$TMP/$name.out" 2>&1
}

expect() {
    local name=$1 pattern=$2
    if grep -qE -- "$pattern" "$TMP/$name.out"; then
        echo "  ok    $name: /$pattern/"
    else
        echo "  FAIL  $name: expected /$pattern/"
        failures=$((failures + 1))
    fi
}

dump_on_fail() {
    local before=$1 name=$2
    if [ "$failures" -gt "$before" ]; then
        echo "  ---- output of $name ----"
        sed 's/^/  | /' "$TMP/$name.out"
    fi
}

echo "breakpoint on a symbol"
f=$failures
run bp "$DUMMY" "b main" "b main" "info" "c" "si" "quit"
expect bp 'Loaded [0-9]+ symbols'
expect bp 'Breakpoint set at 0x[0-9a-f]+'
expect bp 'Breakpoint already set at'
expect bp '\[0\] 0x[0-9a-f]+  enabled'
expect bp 'Breakpoint hit at 0x[0-9a-f]+'
expect bp 'Stopped \(SIGTRAP\)'
dump_on_fail $f bp

# Breakpoint address must be where `main` actually is: after the hit,
# RIP = bp + 1, and one `si` later RIP has advanced past the first instruction.
bp_addr=$(grep -oE 'Breakpoint hit at 0x[0-9a-f]+' "$TMP/bp.out" | grep -oE '0x[0-9a-f]+')
rips=($(grep -oE 'RIP: 0x[0-9a-f]+' "$TMP/bp.out" | grep -oE '0x[0-9a-f]+'))
if [ -n "$bp_addr" ] && [ "${#rips[@]}" -ge 2 ] &&
   [ $((rips[0])) -eq $((bp_addr + 1)) ] && [ $((rips[1])) -gt $((bp_addr + 1)) ]; then
    echo "  ok    bp: RIP after hit = bp+1, si advanced exactly one instruction"
else
    echo "  FAIL  bp: unexpected RIPs (bp=$bp_addr rips=${rips[*]:-none})"
    failures=$((failures + 1))
fi

echo "memory and registers"
f=$failures
run mem "$DUMMY" "mem main 2" "memset main 1122334455667788" "mem main" \
         "setreg rax 1234" "regs" "setreg foo 1" "quit"
expect mem '0x[0-9a-f]+:  0x[0-9a-f]+'
expect mem 'Wrote 0x1122334455667788 to 0x[0-9a-f]+'
expect mem '0x[0-9a-f]+:  0x1122334455667788'
expect mem 'rax = 0x1234'
expect mem 'RAX: 0x1234'
expect mem 'Unknown register: foo'
dump_on_fail $f mem

echo "bad input doesn't crash"
f=$failures
run bad "$DUMMY" "b nosuchsym" "mem 0x1" "mem main zz" "bogus" "help" "quit"
expect bad 'Symbol not found: nosuchsym'
expect bad 'read error'
expect bad 'Invalid number'
expect bad 'Unknown command'
expect bad 'Commands:'
dump_on_fail $f bad

echo "signals are forwarded"
f=$failures
printf 'int main() { volatile int* p = 0; *p = 1; }\n' > "$TMP/crash.c"
cc -g "$TMP/crash.c" -o "$TMP/crash"
run crash "$TMP/crash" "c" "c"
expect crash 'Stopped\. Signal: 11'
expect crash 'Process killed by signal 11'
dump_on_fail $f crash

echo "normal exit"
f=$failures
printf 'int main() { return 3; }\n' > "$TMP/exit3.c"
cc "$TMP/exit3.c" -o "$TMP/exit3"
run exit3 "$TMP/exit3" "c"
expect exit3 'Process exited with code 3'
dump_on_fail $f exit3

echo
if [ "$failures" -eq 0 ]; then
    echo "all smoke tests passed"
else
    echo "$failures check(s) failed"
    exit 1
fi
