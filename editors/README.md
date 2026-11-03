# Editor support

```
tree-sitter-dscript/   DScript grammar (grammar.js; src/ is generated and committed)
tree-sitter-dasm/      dasm grammar (.dss files, and the low { } blocks of DScript)
zed/                   the Zed extension: extension.toml + languages/{dscript,dasm}/*.scm,
                       src/lib.rs (starts the language server)
check.sh               checks the grammars and the queries
```
The DScript language server, `dsls`, is built with the rest (`make dsls` in `bin/`); its source
is `src/lsp/dsls_main.cpp`.

## Zed

Install: command palette -> `zed: install dev extension` -> pick `editors/zed`. Zed needs Rust
installed with rustup for this (it compiles `src/lib.rs` to WebAssembly). It fetches the grammars
(this repository, at the `rev` in `extension.toml`) and compiles them itself. `.ds` files open
as DScript, `.dss` files as dasm, and the inside of `low { }` blocks is highlighted as dasm.

### The language server (dsls)

For DScript files Zed starts `dsls`: errors as you type, go to definition (ctrl+click; also on
`#include` lines and `$name`s in low blocks), hover (types, signatures, constant values, struct
layouts), document and project symbols. Zed finds it on the `PATH`, or with this setting:
```
"lsp": { "dsls": { "binary": { "path": "/path/to/DragonVM/bin/dsls" } } }
```
Each analysis compiles the program the file belongs to (without generating code): a file with a
`main` sub is a program; another file is checked as part of the closest program that includes
it (or on its own). Unsaved changes are used. The options come from `dsls.json` in the
workspace root (this repository has one):
```
{
    "args": ["-I", "extra/ds/sdk"],                     dsc options for every program
    "files": { "extra/ds/bios/entry.ds": ["--rom"] }    more options for one program
}
```
(`-I <dir>`, `-D NAME[=value]`, `--rom`, `--raw`; paths relative to the workspace root.) Errors
are reported one per declaration or sub (the first one in each). Tests:
`tests/dsls/run_tests.py`. The log (Zed: `dev: open language server logs`) shows what dsls did.

## Changing a grammar

Setup once: `cd editors && npm install` (the tree-sitter CLI, in `node_modules/`).

1. Edit `tree-sitter-<lang>/grammar.js`.
2. `npm run generate` (or `npx tree-sitter generate --abi 14` in the grammar's directory).
3. `./check.sh`: corpus tests, every DScript / dasm source of the repo, the queries.
   New syntax: add a case to `test/corpus/*.txt`, then `npx tree-sitter test --update` (and
   check the tree it wrote).
4. Commit the grammar (with its regenerated `src/`).
5. `zed/update-grammar-rev.sh`, then commit `extension.toml`: Zed builds the grammar from that
   commit.
6. In Zed: Extensions -> DragonVM -> Rebuild.

Query changes (`zed/languages/*/*.scm`, `config.toml`) need only steps 3 and 6.

## Publishing

Zed fetches grammars with git, so the extension can only be published with a public URL of
this repository: `zed/update-grammar-rev.sh <url>` (after pushing). Zed's extension registry
(github.com/zed-industries/extensions) adds the repository as a submodule; its entries can
name a subdirectory (`editors/zed` here) with `path` (check that repository's README first).
