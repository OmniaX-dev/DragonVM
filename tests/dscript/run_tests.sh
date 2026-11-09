#!/bin/bash
# DScript compiler tests. Run from anywhere: tests/dscript/run_tests.sh [test.ds ...]
#
# Each test is a .ds file with comments telling what to expect:
#   // expect-rv: <n>          main's return value (RV)
#   // expect-output: <text>   what the program prints (\n for new lines); the BIOS runs first
#   // expect-error: <text>    the compilation must fail with this text in the message
#   // expect-rom-rv: <n>      compiled with --rom as the machine's BIOS (16 MiB image), booted;
#                              main's return value (instead of the DRX and raw runs)
#   // dsc-flags: <options>    extra dsc options (for every compilation of the test)
#   // expect-sources: <file.ds> ...  the raw build's disassembly (--save-disassembly, for the
#                              debugger) has the source lines of these files (also the libraries')
#   // dsc-lib: <file.ds> ...  libraries to compile first (dsc --lib, in this order, into a
#                              directory given to the test with -L: #import(<file>))
# Every test runs twice: as a DRX (relocatable, loaded at 0x01020000) and as a raw binary
# (--raw --load 0x01030000).

cd "$(dirname "$0")"
ROOT="$(cd ../.. && pwd)"
BIN="$ROOT/bin"
DSC="$BIN/dsc"
DRUN="$BIN/drun"
CONFIG="${DSCRIPT_TEST_CONFIG:-config/testMachine.dvm}"	# another machine: DSCRIPT_TEST_CONFIG=<file.dvm>
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

# The SDK library, built from its sources (the tests use it with #import(<dragon-sdk>)).
mkdir -p "$OUT/sdk"
if ! "$DSC" --lib "$ROOT"/extra/ds/dragon-sdk/src/*.ds -o "$OUT/sdk/dragon-sdk.dlib"; then
	echo "cannot build the SDK library"
	exit 1
fi

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
	romrv="$(grep -m1 '^// expect-rom-rv:' "$t" | sed 's|^// expect-rom-rv: *||')"
	read -r -a flags <<< "$(grep -m1 '^// dsc-flags:' "$t" | sed 's|^// dsc-flags: *||')"
	read -r -a libs <<< "$(grep -m1 '^// dsc-lib:' "$t" | sed 's|^// dsc-lib: *||')"
	read -r -a sources <<< "$(grep -m1 '^// expect-sources:' "$t" | sed 's|^// expect-sources: *||')"
	ok=1
	msg=""
	if [ ${#libs[@]} -gt 0 ]; then
		mkdir -p "$OUT/$name.libs"
		for lib in "${libs[@]}"; do
			# a.ds,b.ds: one library from several files (named after the first one)
			IFS=, read -r -a lib_sources <<< "$lib"
			if ! "$DSC" "${lib_sources[@]}" --lib -L "$OUT/sdk" -L "$OUT/$name.libs" -o "$OUT/$name.libs/$(basename "${lib_sources[0]}" .ds).dlib" > "$OUT/$name.log" 2>&1; then
				ok=0; msg="library $lib: compile error: $(cat "$OUT/$name.log")"
			fi
		done
		flags+=(-L "$OUT/$name.libs")
	fi
	if [ $ok -eq 0 ]; then
		:
	elif [ -n "$err" ]; then
		if "$DSC" "$t" -L "$OUT/sdk" "${flags[@]}" -o "$OUT/$name.drx" > "$OUT/$name.log" 2>&1; then
			ok=0; msg="compiled, but should fail with: $err"
		elif ! grep -qF -- "$err" "$OUT/$name.log"; then
			ok=0; msg="wrong error: $(head -1 "$OUT/$name.log") (expected: $err)"
		fi
	elif [ -n "$romrv" ]; then
		cfg="$CONFIG"; [[ "$cfg" = /* ]] || cfg="$BIN/$cfg"
		if ! "$DSC" "$t" -L "$OUT/sdk" "${flags[@]}" --rom --fixed 16777216,0xFF -o "$OUT/$name.rom" > "$OUT/$name.log" 2>&1; then
			ok=0; msg="rom: compile error: $(cat "$OUT/$name.log")"
		else
			sed "s|^Bios = .*|Bios = $OUT/$name.rom|" "$cfg" > "$OUT/$name.dvm"
			(cd "$BIN" && "$DRUN" "$OUT/$name.dvm" --boot --max-cycles 20000000 > "$OUT/$name.out" 2> "$OUT/$name.err")
			code=$?
			got_rv="$(grep -o 'RV = 0x[0-9A-F]* (-\?[0-9]*)' "$OUT/$name.err" | sed 's/.*(\(.*\))/\1/')"
			if [ $code -ne 0 ]; then
				ok=0; msg="rom: drun exit code $code: $(grep -v '^$' "$OUT/$name.err" "$OUT/$name.out" | head -3)"
			elif [ "$got_rv" != "$romrv" ]; then
				ok=0; msg="rom: RV = $got_rv, expected $romrv"
			fi
		fi
	else
		for mode in drx raw; do
			if [ $mode = drx ]; then
				"$DSC" "$t" -L "$OUT/sdk" "${flags[@]}" -o "$OUT/$name.drx" > "$OUT/$name.log" 2>&1 || { ok=0; msg="$mode: compile error: $(cat "$OUT/$name.log")"; break; }
				prog="$OUT/$name.drx"; at=()
			else
				"$DSC" "$t" -L "$OUT/sdk" "${flags[@]}" --raw --load 0x01030000 --save-disassembly "$OUT/$name.dds" -o "$OUT/$name.bin" > "$OUT/$name.log" 2>&1 || { ok=0; msg="$mode: compile error: $(cat "$OUT/$name.log")"; break; }
				for src in "${sources[@]}"; do
					grep -qaF "/$src" "$OUT/$name.dds" || { ok=0; msg="the disassembly has no source lines of $src"; }
				done
				[ $ok -eq 0 ] && break
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
