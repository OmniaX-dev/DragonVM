#!/bin/bash
# DScript compiler tests. Run from anywhere: tests/dscript/run_tests.sh [test.ds ...]
#
# Each test is a .ds file with comments telling what to expect:
#   // expect-rv: <n>          main's return value (RV)
#   // expect-output: <text>   what the program prints (\n for new lines); the BIOS runs first
#   // expect-error: <text>    the compilation must fail with this text in the message
# Every test runs twice: as a DRX (relocatable, loaded at 0x01020000) and as a raw binary
# (--raw --load 0x01030000).

cd "$(dirname "$0")"
ROOT="$(cd ../.. && pwd)"
BIN="$ROOT/bin"
DSC="$BIN/dsc"
DRUN="$BIN/drun"
CONFIG="config/testMachine.dvm"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

pass=0
fail=0
tests=("$@")
[ ${#tests[@]} -eq 0 ] && tests=(*.ds)

for t in "${tests[@]}"; do
	name="$(basename "$t" .ds)"
	[ "$name" = "check" ] && continue
	rv="$(grep -m1 '^// expect-rv:' "$t" | sed 's|^// expect-rv: *||')"
	output="$(grep -m1 '^// expect-output:' "$t" | sed 's|^// expect-output: *||')"
	err="$(grep -m1 '^// expect-error:' "$t" | sed 's|^// expect-error: *||')"
	ok=1
	msg=""
	if [ -n "$err" ]; then
		if "$DSC" "$t" -I "$ROOT/extra/ds/sdk" -o "$OUT/$name.drx" > "$OUT/$name.log" 2>&1; then
			ok=0; msg="compiled, but should fail with: $err"
		elif ! grep -qF -- "$err" "$OUT/$name.log"; then
			ok=0; msg="wrong error: $(head -1 "$OUT/$name.log") (expected: $err)"
		fi
	else
		for mode in drx raw; do
			if [ $mode = drx ]; then
				"$DSC" "$t" -I "$ROOT/extra/ds/sdk" -o "$OUT/$name.drx" > "$OUT/$name.log" 2>&1 || { ok=0; msg="$mode: compile error: $(cat "$OUT/$name.log")"; break; }
				prog="$OUT/$name.drx"; at=()
			else
				"$DSC" "$t" -I "$ROOT/extra/ds/sdk" --raw --load 0x01030000 -o "$OUT/$name.bin" > "$OUT/$name.log" 2>&1 || { ok=0; msg="$mode: compile error: $(cat "$OUT/$name.log")"; break; }
				prog="$OUT/$name.bin"; at=(--at 0x01030000)
			fi
			boot=()
			[ -n "$output" ] && boot=(--boot)
			(cd "$BIN" && "$DRUN" "$CONFIG" "$prog" "${at[@]}" "${boot[@]}" --max-cycles 20000000 > "$OUT/$name.out" 2> "$OUT/$name.err")
			code=$?
			if [ $code -ne 0 ]; then
				ok=0; msg="$mode: drun exit code $code: $(grep -v '^$' "$OUT/$name.err" | head -3)"; break
			fi
			got_rv="$(grep -o 'RV = 0x[0-9A-F]* (-\?[0-9]*)' "$OUT/$name.err" | sed 's/.*(\(.*\))/\1/')"
			if [ -n "$rv" ] && [ "$got_rv" != "$rv" ]; then
				ok=0; msg="$mode: RV = $got_rv, expected $rv"; break
			fi
			if [ -n "$output" ]; then
				expected="$(printf '%b' "$output")"
				got="$(cat "$OUT/$name.out")"
				if [ "$got" != "$expected" ]; then
					ok=0; msg="$mode: output '$got', expected '$expected'"; break
				fi
			fi
		done
	fi
	if [ $ok -eq 1 ]; then
		pass=$((pass + 1))
		echo "  ok    $name"
	else
		fail=$((fail + 1))
		echo "  FAIL  $name: $msg"
	fi
done
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
