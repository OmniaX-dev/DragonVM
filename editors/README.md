# Editor support

```
tree-sitter-dscript/   DScript grammar (grammar.js; src/ is generated and committed)
tree-sitter-dasm/      dasm grammar (.dss files, and the low { } blocks of DScript)
zed/                   the Zed extension: extension.toml + languages/{dscript,dasm}/*.scm
check.sh               checks the grammars and the queries
```

## Zed

Install: command palette -> `zed: install dev extension` -> pick `editors/zed`. The extension has
no Rust code, so no toolchain is needed: Zed fetches the grammars (this repository, at the
`rev` in `extension.toml`) and compiles them itself. `.ds` files open as DScript, `.dss` files
as dasm, and the inside of `low { }` blocks is highlighted as dasm.

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
