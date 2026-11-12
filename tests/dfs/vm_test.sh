#!/bin/bash
# DFS between the two implementations, on a real disk image: dtools (the C++ core) formats and
# fills partition 1, vm_test.ds (the SDK's DScript port, booted with drun) reads it, writes to
# it and formats partition 2, then dtools checks both (fsck and the contents).
# Run from anywhere: tests/dfs/vm_test.sh
set -u
cd "$(dirname "$0")"
ROOT="$(cd ../.. && pwd)"
BIN="$ROOT/bin"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

# byte i = i * 7 + seed + (i >> 8)  (vm_test.ds's pattern())
pattern() { python3 -c "import sys; n, s = int(sys.argv[1]), int(sys.argv[2]); sys.stdout.buffer.write(bytes(((i * 7 + s + (i >> 8)) & 0xFF) for i in range(n)))" "$1" "$2"; }

DISK="$OUT/disk.drd"
DT="$BIN/dtools"
"$DT" new-vdisk "$DISK" 8388608 > /dev/null || fail "new-vdisk"
"$DT" new-dpt "$DISK" -p 1048576 -l one -p 1048576 -l two > /dev/null || fail "new-dpt"
"$DT" dfs "$DISK" 1 mkfs --block-size 1024 > /dev/null || fail "mkfs"
printf 'Hello, DFS!\n\n' > "$OUT/hello.txt"
pattern 20000 5 > "$OUT/big.bin"
"$DT" dfs "$DISK" 1 put "$OUT/hello.txt" /hello.txt > /dev/null || fail "put hello"
"$DT" dfs "$DISK" 1 mkdir /docs > /dev/null || fail "mkdir"
"$DT" dfs "$DISK" 1 put "$OUT/big.bin" /docs/big.bin > /dev/null || fail "put big"

mkdir -p "$OUT/sdk"
"$BIN/dsc" --lib "$ROOT"/extra/ds/dragon-sdk/src/*.ds -o "$OUT/sdk/dragon-sdk.dlib" || fail "the SDK doesn't build"
"$BIN/dsc" vm_test.ds -L "$OUT/sdk" -o "$OUT/vm_test.drx" || fail "vm_test.ds doesn't build"
sed -e "s|^Disks = .*|Disks = $DISK|" "$BIN/config/testMachine.dvm" > "$OUT/machine.dvm"
(cd "$BIN" && ./drun "$OUT/machine.dvm" "$OUT/vm_test.drx" --boot --max-cycles 400000000 > "$OUT/run.out" 2> "$OUT/run.err")
rv="$(grep -o 'RV = 0x[0-9A-F]* (-\?[0-9]*)' "$OUT/run.err" | sed 's/.*(\(.*\))/\1/')"
[ "$rv" = "0" ] || fail "vm_test.ds: RV = $rv (the failed check) $(cat "$OUT/run.out" "$OUT/run.err" | grep -v '^$' | tail -3)"

"$DT" dfs "$DISK" 1 fsck > "$OUT/fsck1" || fail "fsck partition 1: $(cat "$OUT/fsck1")"
"$DT" dfs "$DISK" 2 fsck > "$OUT/fsck2" || fail "fsck partition 2: $(cat "$OUT/fsck2")"
"$DT" dfs "$DISK" 1 get /vm/out.bin "$OUT/out.bin" > /dev/null || fail "get out.bin"
pattern 307200 9 | cmp - "$OUT/out.bin" || fail "/vm/out.bin differs"
"$DT" dfs "$DISK" 1 get /vm/moved.bin "$OUT/moved.bin" > /dev/null || fail "get moved.bin"
cmp "$OUT/big.bin" "$OUT/moved.bin" || fail "/vm/moved.bin differs"
"$DT" dfs "$DISK" 1 ls /docs > /dev/null 2>&1 && fail "/docs still exists"
"$DT" dfs "$DISK" 1 stat /hello.txt | grep -q "^Links *2$" || fail "/hello.txt should have 2 links: $("$DT" dfs "$DISK" 1 stat /hello.txt)"
[ "$("$DT" dfs "$DISK" 2 cat /etc/motd)" = "Written by DScript" ] || fail "/etc/motd on partition 2"
"$DT" dfs "$DISK" 2 info | grep -q "fromvm" || fail "partition 2's label"
echo "ok: DScript and C++ DFS agree"
