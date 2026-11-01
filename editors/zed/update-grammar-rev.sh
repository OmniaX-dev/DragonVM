#!/bin/bash
# Points the grammars of extension.toml at the last commit that changed them.
#   ./update-grammar-rev.sh            repository = this repository (file://), for a dev extension
#   ./update-grammar-rev.sh <url>      repository = <url> (a public URL, to publish the extension)
# The grammar changes must be committed (and pushed, for a remote URL).
cd "$(dirname "$0")"
ROOT="$(git rev-parse --show-toplevel)"
URL="${1:-file://$ROOT}"
for g in dscript dasm; do
	if ! git diff --quiet HEAD -- "../tree-sitter-$g"; then
		echo "tree-sitter-$g has uncommitted changes: commit them first" >&2
		exit 1
	fi
	rev="$(git log -1 --format=%H -- "../tree-sitter-$g")"
	# Rewrite repository / rev inside the [grammars.<g>] table.
	awk -v g="$g" -v rev="$rev" -v url="$URL" '
		/^\[/ { inside = ($0 == "[grammars." g "]") }
		inside && /^repository = / { print "repository = \"" url "\""; next }
		inside && /^rev = / { print "rev = \"" rev "\""; next }
		{ print }
	' extension.toml > extension.toml.tmp && mv extension.toml.tmp extension.toml
	echo "$g: $rev"
done
