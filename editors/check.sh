#!/bin/bash
# Checks the grammars and the Zed queries:
#   - regenerates both parsers (src/ must be committed: Zed compiles src/parser.c)
#   - runs the corpus tests (test/corpus)
#   - parses every DScript / dasm source of the repo (and the dasm dsc generates for the tests)
#   - compiles every Zed query against its grammar
# Setup once: (cd editors && npm install)

cd "$(dirname "$0")"
TS="$PWD/node_modules/.bin/tree-sitter"
ROOT="$(cd .. && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
[ -x "$TS" ] || { echo "tree-sitter-cli missing: run 'npm install' in editors/"; exit 1; }
quiet() { grep -v "Warning\|Please run\|configuration file\|language grammars\|^$"; }
fail=0

for g in dscript dasm; do
	echo "== $g"
	(cd "tree-sitter-$g" && "$TS" generate --abi 14) || { echo "  generate failed"; fail=1; continue; }
	if ! git diff --quiet -- "tree-sitter-$g/src" 2>/dev/null; then
		echo "  note: src/ changed after regenerating (commit it)"
	fi
	(cd "tree-sitter-$g" && "$TS" test 2>&1 | quiet | grep -q "failed parses: 0") || { echo "  corpus tests failed (cd editors/tree-sitter-$g && npx tree-sitter test)"; fail=1; }

	files=()
	if [ $g = dscript ]; then
		for f in "$ROOT"/tests/dscript/*.ds "$ROOT"/extra/ds/sdk/*.ds "$ROOT"/other/*.ds; do
			case "$(basename "$f")" in error_syntax.ds|error_static_toplevel.ds|error_static_field.ds|error_priv_toplevel.ds) continue;; esac
			files+=("$f")
		done
	else
		while IFS= read -r f; do files+=("$f"); done < <(find "$ROOT/extra/dss" -name '*.dss')
		if [ -x "$ROOT/bin/dsc" ]; then
			for f in "$ROOT"/tests/dscript/*.ds; do
				n="$(basename "$f" .ds)"
				case "$n" in error_*|check) continue;; esac
				"$ROOT/bin/dsc" "$f" -I "$ROOT/extra/ds/sdk" -o "$OUT/$n.drx" --emit-dasm "$OUT/$n.dasm" > /dev/null 2>&1 && files+=("$OUT/$n.dasm")
			done
		fi
	fi
	result="$(cd "tree-sitter-$g" && "$TS" parse --quiet --stat "${files[@]}" 2>&1 | quiet)"
	echo "$result" | grep -v "^Total" | sed 's/^/  parse error: /'
	echo "  $(echo "$result" | grep '^Total')"
	echo "$result" | grep -q "failed parses: 0" || fail=1

	for q in zed/languages/$g/*.scm; do
		if ! (cd "tree-sitter-$g" && "$TS" query "../$q" "${files[0]}" > /dev/null 2>&1); then
			echo "  query error: $q"
			fail=1
		fi
	done
done
[ $fail -eq 0 ] && echo "all good" || echo "FAILED"
exit $fail
