# tree-sitter-organ-inline

Inline-level [tree-sitter](https://tree-sitter.github.io) grammar for
[Org mode](https://orgmode.org/) — markup, links, footnote refs,
citations, timestamps, inline source, macros, export snippets, LaTeX
entities, target syntax.

Designed to be injected by `tree-sitter-organ` (or any block-level
parser) for paragraph contents.

## Superset over vanilla Org

This grammar extends the standard timestamp repeater with an optional
`[filter]` decoration alongside the standard alarm and warning halves:

```
<DATE +1w        /1d   [wd]    -1d>
       ^repeater  ^alarm ^filter ^warning
```

- `alarm` — `/Nu` (`u` ∈ `h`/`d`/`w`/`m`/`y`). Standard Emacs
  `org-habit` syntax; this grammar exposes it as a distinct AST node.
- `filter` — `[…]` immediately after the repeater (or alarm). **Not in
  vanilla Org.** Semantics are consumer-defined (`[wd]` = weekdays
  only, `[cal:hk]` = a named holiday calendar, etc.).
- `warning` — `-Nu` / `--Nu`. Vanilla.

Inputs that parse cleanly under vanilla Org parse identically here;
the filter only kicks in when `[…]` appears inside a timestamp.

## Build

```sh
pnpm install
make
```

Outputs `build/<arch>/parser.{so,dylib,dll}`.

## Test

```sh
make test
```
