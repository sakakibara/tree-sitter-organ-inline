# tree-sitter-organ-inline build
#
# Per-platform output to avoid clobbering across hosts on a shared checkout:
#   build/<os>-<arch>/org_inline.so

.PHONY: build clean test

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

clean:
	rm -rf build/
	rm -f org_inline.so   # legacy artefact
	rm -rf node_modules
