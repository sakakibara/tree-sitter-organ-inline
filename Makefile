# tree-sitter-organ-inline build
#
# Per-platform output to avoid clobbering across hosts on a shared checkout:
#   build/<os>-<arch>/org_inline.so

.PHONY: build clean test spec-check test-spec

UNAME_S := $(shell uname -s | tr '[:upper:]' '[:lower:]')
UNAME_M := $(shell uname -m)
PLATFORM := $(UNAME_S)-$(UNAME_M)
BUILD_DIR := build/$(PLATFORM)
INLINE_SO := $(BUILD_DIR)/org_inline.so

INLINE_SOURCES = src/parser.c src/scanner.c
INLINE_HEADERS =
INLINE_CFLAGS  = -std=c99 -O2 -Wall -Wextra -Wpedantic -fPIC -I src

build: $(INLINE_SO)

$(BUILD_DIR):
	@mkdir -p $@

$(INLINE_SO): $(INLINE_SOURCES) $(INLINE_HEADERS) | $(BUILD_DIR)
	$(CC) $(INLINE_CFLAGS) -shared -o $@ $(INLINE_SOURCES)

src/parser.c: grammar.js node_modules/.bin/tree-sitter
	./node_modules/.bin/tree-sitter generate

node_modules/.bin/tree-sitter:
	@if command -v pnpm >/dev/null 2>&1; then \
		pnpm install --silent; \
	else \
		npm install --silent; \
	fi

test: build
	./node_modules/.bin/tree-sitter test

# Verify spec/org-inline.abnf and grammar.js list the same set of named
# rules.  Reads spec/.spec-check-ignores for shape-only ABNF rules
# (primitives, character classes, body shapes) that have no 1:1
# grammar.js counterpart by design.
spec-check:
	@node scripts/check-abnf-sync.js grammar.js spec/org-inline.abnf

# Per-rule positive/negative example tests under spec/examples/.
# Behavior matching: feeds each `+ input` line through the parser and
# asserts the named node appears; feeds each `- input` and asserts it
# does not.  Stronger than rule-name sync alone.
test-spec: build
	@node scripts/test-rule-examples.js

clean:
	rm -rf build/
	rm -f org_inline.so   # legacy artefact
	rm -rf node_modules
