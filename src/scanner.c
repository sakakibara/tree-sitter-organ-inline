#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

enum InlineExternal {
    EXT_BOLD_OPEN = 0,    EXT_BOLD_CLOSE,
    EXT_ITALIC_OPEN,      EXT_ITALIC_CLOSE,
    EXT_UNDERLINE_OPEN,   EXT_UNDERLINE_CLOSE,
    EXT_STRIKE_OPEN,      EXT_STRIKE_CLOSE,
    EXT_VERBATIM_TOKEN,
    EXT_CODE_TOKEN,
    EXT_PLAIN_TEXT_TOKEN,
    EXT_LINK_REGULAR_TOKEN,
    EXT_LINK_PLAIN_TOKEN,
    EXT_LINK_ANGLE_TOKEN,
    EXT_LINK_RADIO_TOKEN,
    EXT_TS_OPEN_ACTIVE,
    EXT_TS_CLOSE_ACTIVE,
    EXT_TS_OPEN_INACTIVE,
    EXT_TS_CLOSE_INACTIVE,
    EXT_TS_DATE_TOKEN,
    EXT_TS_DAYNAME_TOKEN,
    EXT_TS_TIME_TOKEN,
    EXT_TS_TIME_RANGE_TOKEN,
    EXT_TS_REPEATER_TOKEN,
    EXT_TS_REPEATER_ALARM_TOKEN,
    EXT_TS_REPEATER_FILTER_TOKEN,
    EXT_TS_WARNING_TOKEN,
    EXT_TS_DIARY_TOKEN,
    EXT_TS_RANGE_SEPARATOR,
    EXT_CITATION_TOKEN,
    EXT_CITATION_OPEN,
    EXT_CITATION_STYLE,
    EXT_CITATION_COLON,
    EXT_CITATION_TEXT,
    EXT_CITATION_KEY,
    EXT_CITATION_SEPARATOR,
    EXT_CITATION_CLOSE,
    EXT_MACRO_TOKEN,
    EXT_INLINE_SRC_BLOCK_TOKEN,
    EXT_EXPORT_SNIPPET_TOKEN,
    EXT_FOOTNOTE_REF_TOKEN,
    EXT_TARGET_TOKEN,
    EXT_STATISTICS_COOKIE_TOKEN,
    EXT_LINE_BREAK_TOKEN,
    EXT_SUBSCRIPT_TOKEN,
    EXT_SUPERSCRIPT_TOKEN,
    EXT_ENTITY_TOKEN,
    EXT_LATEX_FRAGMENT_TOKEN,
    EXT_INLINE_BABEL_CALL_TOKEN,
    EXT_COUNT,
};

#define SPAN_STACK_MAX 16

/* ts_substate values */
#define TS_OUTSIDE        0
#define TS_EXPECT_DATE    1
#define TS_EXPECT_FIELDS  2

typedef struct {
    uint8_t span_stack[SPAN_STACK_MAX];
    uint8_t span_depth;
    uint8_t ts_substate;
    uint8_t ts_active;
    /* Citation sub-token state machine.  0 = outside; 1 = just opened
     * (`[cite` consumed; expect /style or :); 2 = in body (expect text /
     * @key / ; / ]). */
    uint8_t citation_state;
} InlineState;

void *tree_sitter_org_inline_external_scanner_create(void) {
    return calloc(1, sizeof(InlineState));
}

void tree_sitter_org_inline_external_scanner_destroy(void *p) { free(p); }

unsigned tree_sitter_org_inline_external_scanner_serialize(void *p, char *b) {
    InlineState *s = (InlineState *)p;
    if (s->span_depth > SPAN_STACK_MAX) return 0;
    b[0] = (char)s->span_depth;
    for (uint8_t i = 0; i < s->span_depth; i++) b[1 + i] = (char)s->span_stack[i];
    b[1 + s->span_depth]     = (char)s->ts_substate;
    b[1 + s->span_depth + 1] = (char)s->ts_active;
    b[1 + s->span_depth + 2] = (char)s->citation_state;
    return (unsigned)(1 + s->span_depth + 3);
}

void tree_sitter_org_inline_external_scanner_deserialize(void *p, const char *b, unsigned l) {
    InlineState *s = (InlineState *)p;
    s->span_depth     = 0;
    s->ts_substate    = 0;
    s->ts_active      = 0;
    s->citation_state = 0;
    if (l == 0) return;
    s->span_depth = (uint8_t)b[0];
    if (s->span_depth > SPAN_STACK_MAX) { s->span_depth = 0; return; }
    for (uint8_t i = 0; i < s->span_depth && (1u + i) < l; i++)
        s->span_stack[i] = (uint8_t)b[1 + i];
    unsigned base = 1 + s->span_depth;
    if (base     < l) s->ts_substate    = (uint8_t)b[base];
    if (base + 1 < l) s->ts_active      = (uint8_t)b[base + 1];
    if (base + 2 < l) s->citation_state = (uint8_t)b[base + 2];
}

static void span_push(InlineState *s, uint8_t marker) {
    if (s->span_depth < SPAN_STACK_MAX) s->span_stack[s->span_depth++] = marker;
}

static void span_pop(InlineState *s) {
    if (s->span_depth > 0) s->span_depth--;
}

static bool is_inline_opener(int32_t c) {
    return c == '*' || c == '/' || c == '_' || c == '+';
}

static bool is_digit(int32_t c) { return c >= '0' && c <= '9'; }
static bool is_alpha(int32_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool try_verbatim(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    (void)s;
    if (!valid_symbols[EXT_VERBATIM_TOKEN]) return false;
    if (lexer->lookahead != '=') return false;
    lexer->advance(lexer, false);
    bool consumed = false;
    while (!lexer->eof(lexer)) {
        int32_t c = lexer->lookahead;
        if (c == '=') {
            lexer->advance(lexer, false);
            if (!consumed) return false;
            lexer->result_symbol = (TSSymbol)EXT_VERBATIM_TOKEN;
            return true;
        }
        if (c == '\n') return false;
        lexer->advance(lexer, false);
        consumed = true;
    }
    return false;
}

static bool try_code(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    (void)s;
    if (!valid_symbols[EXT_CODE_TOKEN]) return false;
    if (lexer->lookahead != '~') return false;
    lexer->advance(lexer, false);
    bool consumed = false;
    while (!lexer->eof(lexer)) {
        int32_t c = lexer->lookahead;
        if (c == '~') {
            lexer->advance(lexer, false);
            if (!consumed) return false;
            lexer->result_symbol = (TSSymbol)EXT_CODE_TOKEN;
            return true;
        }
        if (c == '\n') return false;
        lexer->advance(lexer, false);
        consumed = true;
    }
    return false;
}

static bool try_link_plain(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    (void)s;
    if (!valid_symbols[EXT_LINK_PLAIN_TOKEN]) return false;
    lexer->mark_end(lexer);
    bool got_proto = false;
    while (!lexer->eof(lexer) &&
           ((lexer->lookahead >= 'a' && lexer->lookahead <= 'z') ||
            (lexer->lookahead >= 'A' && lexer->lookahead <= 'Z'))) {
        /* If we've consumed at least one alpha and the next char starts
         * `src_LANG{...}` or `call_BABEL(...)`, stop here so the outer
         * scan can dispatch to inline_src_block / inline_babel_call.
         * Mirror of the same checks in the outer plain_text loop
         * (`try_plain_text` body) so we don't shadow those constructs
         * when they appear after alpha-only plain text. */
        if (got_proto && lexer->lookahead == 's'
            && valid_symbols[EXT_INLINE_SRC_BLOCK_TOKEN]) {
            lexer->mark_end(lexer);
            lexer->advance(lexer, false);
            bool is_src = false;
            if (lexer->lookahead == 'r') {
                lexer->advance(lexer, false);
                if (lexer->lookahead == 'c') {
                    lexer->advance(lexer, false);
                    if (lexer->lookahead == '_') is_src = true;
                }
            }
            if (is_src) {
                lexer->result_symbol = (TSSymbol)EXT_PLAIN_TEXT_TOKEN;
                return true;
            }
            got_proto = true;
            continue;
        }
        if (got_proto && lexer->lookahead == 'c'
            && valid_symbols[EXT_INLINE_BABEL_CALL_TOKEN]) {
            lexer->mark_end(lexer);
            lexer->advance(lexer, false);
            bool is_call = false;
            if (lexer->lookahead == 'a') {
                lexer->advance(lexer, false);
                if (lexer->lookahead == 'l') {
                    lexer->advance(lexer, false);
                    if (lexer->lookahead == 'l') {
                        lexer->advance(lexer, false);
                        if (lexer->lookahead == '_') is_call = true;
                    }
                }
            }
            if (is_call) {
                lexer->result_symbol = (TSSymbol)EXT_PLAIN_TEXT_TOKEN;
                return true;
            }
            got_proto = true;
            continue;
        }
        lexer->advance(lexer, false);
        got_proto = true;
    }
    if (!got_proto) return false;
    if (lexer->lookahead == ':') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == '/') {
            lexer->advance(lexer, false);
            if (lexer->lookahead == '/') {
                lexer->advance(lexer, false);
                bool consumed = false;
                while (!lexer->eof(lexer)) {
                    int32_t c = lexer->lookahead;
                    if (c == ' ' || c == '\t' || c == '\n' || c == ']' || c == '>') break;
                    lexer->advance(lexer, false);
                    consumed = true;
                }
                if (consumed) {
                    lexer->mark_end(lexer);
                    lexer->result_symbol = (TSSymbol)EXT_LINK_PLAIN_TOKEN;
                    return true;
                }
            }
        }
    }
    if (!valid_symbols[EXT_PLAIN_TEXT_TOKEN]) return false;
    while (!lexer->eof(lexer)) {
        int32_t c = lexer->lookahead;
        if (c == '[' && (valid_symbols[EXT_LINK_REGULAR_TOKEN] || valid_symbols[EXT_TS_OPEN_INACTIVE] || valid_symbols[EXT_CITATION_TOKEN] || valid_symbols[EXT_FOOTNOTE_REF_TOKEN] || valid_symbols[EXT_STATISTICS_COOKIE_TOKEN])) break;
        if (c == '{' && valid_symbols[EXT_MACRO_TOKEN]) break;
        if (c == '@' && valid_symbols[EXT_EXPORT_SNIPPET_TOKEN]) break;
        if (c == '\\' && (valid_symbols[EXT_LINE_BREAK_TOKEN] || valid_symbols[EXT_ENTITY_TOKEN] || valid_symbols[EXT_LATEX_FRAGMENT_TOKEN])) break;
        if (c == '$' && valid_symbols[EXT_LATEX_FRAGMENT_TOKEN]) break;
        if (c == '^' && valid_symbols[EXT_SUPERSCRIPT_TOKEN]) break;
        if (c == '_' && valid_symbols[EXT_SUBSCRIPT_TOKEN]) break;
        if (c == '=' && valid_symbols[EXT_VERBATIM_TOKEN]) break;
        if (c == '~' && valid_symbols[EXT_CODE_TOKEN]) break;
        if (c == '<' && (valid_symbols[EXT_LINK_ANGLE_TOKEN] || valid_symbols[EXT_LINK_RADIO_TOKEN] || valid_symbols[EXT_TS_OPEN_ACTIVE])) break;
        if (c == 's' && valid_symbols[EXT_INLINE_SRC_BLOCK_TOKEN]) {
            lexer->mark_end(lexer);
            lexer->advance(lexer, false);
            bool is_src = false;
            if (lexer->lookahead == 'r') {
                lexer->advance(lexer, false);
                if (lexer->lookahead == 'c') {
                    lexer->advance(lexer, false);
                    if (lexer->lookahead == '_') is_src = true;
                }
            }
            if (is_src) {
                lexer->result_symbol = (TSSymbol)EXT_PLAIN_TEXT_TOKEN;
                return true;
            }
            continue;
        }
        if (c == 'c' && valid_symbols[EXT_INLINE_BABEL_CALL_TOKEN]) {
            lexer->mark_end(lexer);
            lexer->advance(lexer, false);
            bool is_call = false;
            if (lexer->lookahead == 'a') {
                lexer->advance(lexer, false);
                if (lexer->lookahead == 'l') {
                    lexer->advance(lexer, false);
                    if (lexer->lookahead == 'l') {
                        lexer->advance(lexer, false);
                        if (lexer->lookahead == '_') is_call = true;
                    }
                }
            }
            if (is_call) {
                lexer->result_symbol = (TSSymbol)EXT_PLAIN_TEXT_TOKEN;
                return true;
            }
            continue;
        }
        if (is_inline_opener(c)) break;
        lexer->advance(lexer, false);
    }
    lexer->mark_end(lexer);
    lexer->result_symbol = (TSSymbol)EXT_PLAIN_TEXT_TOKEN;
    return true;
}

/* --- Combined bracket handlers -----------------------------------------
 * These functions are called AFTER the opening bracket has been consumed
 * and mark_end has been called. The bracket is the scan start; mark_end
 * is already set to after the bracket.
 *
 * By calling mark_end before dispatching, we guarantee that even if the
 * sub-matchers fail, the caller can still emit the bracket as part of
 * plain_text (by returning false and letting the plain_text handler
 * consume what's left). Since mark_end is already called after the
 * bracket, returning false is safe — the outer scan will not be confused
 * because we handle the bracket entirely within one entry point.
 *
 * Wait — that doesn't work either. The functions are called from the scan
 * function after we've advanced past the bracket. If they return false,
 * the outer scan function's subsequent try_* calls will see a post-bracket
 * position.
 *
 * CORRECT architecture: a combined function that advances the bracket and
 * then dispatches WITHOUT returning false mid-way. It must emit SOME token
 * (even plain_text) when it returns true, or emit nothing and return false
 * only if the bracket itself doesn't match any known pattern.
 *
 * For '<':
 *   - '<' + '<' + '<' = radio link (if valid)
 *   - '<' + alpha    = angle link (if valid)
 *   - '<' + digit    = timestamp active (if valid)
 *   - otherwise      = not handled (return false; bracket consumed as plain_text)
 *
 * For '[':
 *   - '[' + '['     = regular link
 *   - '[' + digit   = timestamp inactive
 *   - otherwise     = not handled
 * ----------------------------------------------------------------------- */

/* Called when lookahead is '<'. Advances past '<', then dispatches. */
static bool try_angle_or_ts_active(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    (void)s;
    if (lexer->lookahead != '<') return false;

    bool want_link_angle = valid_symbols[EXT_LINK_ANGLE_TOKEN];
    bool want_link_radio = valid_symbols[EXT_LINK_RADIO_TOKEN];
    bool want_ts_active  = valid_symbols[EXT_TS_OPEN_ACTIVE];
    bool want_target     = valid_symbols[EXT_TARGET_TOKEN];

    if (!want_link_angle && !want_link_radio && !want_ts_active && !want_target) return false;

    lexer->advance(lexer, false);  /* consume first '<' */

    /* Radio link: '<<<' */
    if (lexer->lookahead == '<' && (want_link_radio || want_target)) {
        lexer->advance(lexer, false);  /* second '<' */

        /* Target: '<<' + non-bracket content + '>>' (exactly 2 angle brackets, not 3) */
        if (lexer->lookahead != '<') {
            if (!want_target) return false;
            /* Consume until '>>' */
            while (!lexer->eof(lexer)) {
                int32_t c = lexer->lookahead;
                if (c == '\n' || c == '[' || c == ']') return false;
                if (c == '>') {
                    lexer->advance(lexer, false);
                    if (lexer->lookahead == '>') {
                        lexer->advance(lexer, false);
                        lexer->result_symbol = (TSSymbol)EXT_TARGET_TOKEN;
                        return true;
                    }
                    return false;
                }
                lexer->advance(lexer, false);
            }
            return false;
        }

        /* If we're here, lookahead is '<' (third '<' of radio link) */
        if (!want_link_radio) return false;
        lexer->advance(lexer, false);  /* third '<' */
        int gt_count = 0;
        while (!lexer->eof(lexer)) {
            int32_t c = lexer->lookahead;
            if (c == '\n') return false;
            if (c == '>') {
                gt_count++;
                lexer->advance(lexer, false);
                if (gt_count == 3) {
                    lexer->result_symbol = (TSSymbol)EXT_LINK_RADIO_TOKEN;
                    return true;
                }
            } else {
                gt_count = 0;
                lexer->advance(lexer, false);
            }
        }
        return false;
    }

    /* Diary timestamp: `<%%(SEXP)>` — sexp form per Emacs `org-element`.
     * Captured as a single opaque token; sub-decomposition is deferred. */
    if (lexer->lookahead == '%' && valid_symbols[EXT_TS_DIARY_TOKEN]) {
        lexer->advance(lexer, false);
        if (lexer->lookahead != '%') return false;
        lexer->advance(lexer, false);
        if (lexer->lookahead != '(') return false;
        lexer->advance(lexer, false);
        int paren_depth = 1;
        while (!lexer->eof(lexer) && paren_depth > 0) {
            int32_t cc = lexer->lookahead;
            if (cc == '\n') return false;
            if (cc == '(') paren_depth++;
            else if (cc == ')') paren_depth--;
            lexer->advance(lexer, false);
        }
        if (paren_depth != 0) return false;
        if (lexer->lookahead != '>') return false;
        lexer->advance(lexer, false);
        lexer->mark_end(lexer);
        lexer->result_symbol = (TSSymbol)EXT_TS_DIARY_TOKEN;
        return true;
    }

    /* Timestamp active: '<' + digit */
    if (is_digit(lexer->lookahead) && want_ts_active) {
        /* Already advanced past '<'. Mark token end = after '<'. */
        lexer->mark_end(lexer);
        /* Peek/verify YYYY-MM-DD pattern; bytes after mark_end will be re-lexed. */
        for (int i = 0; i < 4; i++) {
            if (!is_digit(lexer->lookahead)) return false;
            lexer->advance(lexer, false);
        }
        if (lexer->lookahead != '-') return false;
        lexer->advance(lexer, false);
        for (int i = 0; i < 2; i++) {
            if (!is_digit(lexer->lookahead)) return false;
            lexer->advance(lexer, false);
        }
        if (lexer->lookahead != '-') return false;
        lexer->advance(lexer, false);
        for (int i = 0; i < 2; i++) {
            if (!is_digit(lexer->lookahead)) return false;
            lexer->advance(lexer, false);
        }
        int32_t after = lexer->lookahead;
        if (after != '>' && after != ' ') return false;
        /* Pattern confirmed: token = just '<' (mark_end was called after '<'). */
        s->ts_substate = TS_EXPECT_DATE;
        s->ts_active   = 1;
        lexer->result_symbol = (TSSymbol)EXT_TS_OPEN_ACTIVE;
        return true;
    }

    /* Angle link: '<' + alpha + ':' + '//' */
    if (is_alpha(lexer->lookahead) && want_link_angle) {
        bool got_proto = false;
        while (!lexer->eof(lexer) && is_alpha(lexer->lookahead)) {
            lexer->advance(lexer, false);
            got_proto = true;
        }
        if (!got_proto || lexer->lookahead != ':') return false;
        lexer->advance(lexer, false);
        if (lexer->lookahead != '/') return false;
        lexer->advance(lexer, false);
        if (lexer->lookahead != '/') return false;
        lexer->advance(lexer, false);
        while (!lexer->eof(lexer)) {
            int32_t c = lexer->lookahead;
            if (c == '\n') return false;
            if (c == '>') {
                lexer->advance(lexer, false);
                lexer->result_symbol = (TSSymbol)EXT_LINK_ANGLE_TOKEN;
                return true;
            }
            lexer->advance(lexer, false);
        }
        return false;
    }

    return false;
}

/* Called when lookahead is '['. Advances past '[', then dispatches. */
static bool try_bracket_open(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    (void)s;
    if (lexer->lookahead != '[') return false;

    bool want_link_regular    = valid_symbols[EXT_LINK_REGULAR_TOKEN];
    bool want_ts_inactive     = valid_symbols[EXT_TS_OPEN_INACTIVE];
    bool want_citation        = valid_symbols[EXT_CITATION_TOKEN];
    bool want_footnote_ref    = valid_symbols[EXT_FOOTNOTE_REF_TOKEN];
    bool want_stats_cookie    = valid_symbols[EXT_STATISTICS_COOKIE_TOKEN];

    if (!want_link_regular && !want_ts_inactive && !want_citation && !want_footnote_ref && !want_stats_cookie) return false;

    lexer->advance(lexer, false);  /* consume first '[' */

    /* Regular link: '[[...]]'. We emit a ZERO-WIDTH validator token —
     * the JS rule consumes the literal `[[`, target text, optional
     * `][description]`, and `]]` so each component is a named node.
     * The validation here advances through the candidate to verify
     * `]]` exists before end-of-line; on failure tree-sitter restores
     * the lexer position. */
    if (lexer->lookahead == '[' && want_link_regular) {
        /* mark_end was at the very start of try_bracket_open's caller
         * scan(), but the unconditional `lexer->advance` above for the
         * first `[` advanced one char. We need mark_end to be at the
         * pre-`[` position so the OPEN token is zero-width and the JS
         * rule sees `[[` fresh. We can't go back, so instead: rely on
         * tree-sitter restoring the snapshot when we return false-then-
         * true via a state mechanism. Pragmatic alternative: encode
         * "we saw `[[`" in the result by emitting a 1-char-wide token
         * (the first `[`); JS rule consumes the second `[` and the
         * link content. This is simpler. */
        /* Actually cleanest: don't mark_end here. Just verify validity
         * by advancing through; on success, return true with mark_end
         * back at the pre-`[` position via... but we already advanced.
         *
         * Tree-sitter API: mark_end set THEN advance THEN return true
         * → token spans original_start..mark_end_pos, lexer continues
         * from mark_end_pos (not advance pos). So if we set mark_end
         * NOW (after advancing past `[`), then advance through link to
         * validate, then return true: token covers the first `[`,
         * lexer rewinds to after the first `[`. JS rule then sees `[`
         * and link content. We'd need JS to handle ONE `[` at start
         * (not `[[`) — possible but quirky.
         *
         * Cleanest: have the OPEN token cover `[[`. Advance past second
         * `[`, mark_end, then validate by advancing through. */
        lexer->advance(lexer, false);  /* second '[' */
        lexer->mark_end(lexer);  /* OPEN token covers `[[` */
        int rbracket_count = 0;
        while (!lexer->eof(lexer)) {
            int32_t c = lexer->lookahead;
            if (c == '\n') return false;
            if (c == ']') {
                rbracket_count++;
                lexer->advance(lexer, false);
                if (rbracket_count == 2) {
                    /* Validation succeeded. mark_end is at end of `[[`,
                     * so the OPEN token covers `[[` and the lexer
                     * rewinds to after `[[`. JS rule consumes the rest. */
                    lexer->result_symbol = (TSSymbol)EXT_LINK_REGULAR_TOKEN;
                    return true;
                }
            } else {
                rbracket_count = 0;
                lexer->advance(lexer, false);
            }
        }
        return false;
    }

    /* Statistics cookie with no leading digits: '[%]' or '[/digits]' */
    if (want_stats_cookie) {
        if (lexer->lookahead == '%') {
            lexer->advance(lexer, false);
            if (lexer->lookahead == ']') {
                lexer->advance(lexer, false);
                lexer->result_symbol = (TSSymbol)EXT_STATISTICS_COOKIE_TOKEN;
                return true;
            }
        } else if (lexer->lookahead == '/') {
            lexer->advance(lexer, false);
            while (!lexer->eof(lexer) && is_digit(lexer->lookahead))
                lexer->advance(lexer, false);
            if (lexer->lookahead == ']') {
                lexer->advance(lexer, false);
                lexer->result_symbol = (TSSymbol)EXT_STATISTICS_COOKIE_TOKEN;
                return true;
            }
        }
    }

    /* Statistics cookie OR timestamp inactive: '[' + digit */
    if (is_digit(lexer->lookahead) && (want_stats_cookie || want_ts_inactive)) {
        /* mark_end records position after '[', needed for timestamp (token = just '[') */
        lexer->mark_end(lexer);
        /* Consume all leading digits */
        int digit_count = 0;
        while (!lexer->eof(lexer) && is_digit(lexer->lookahead)) {
            lexer->advance(lexer, false);
            digit_count++;
        }
        int32_t after_digits = lexer->lookahead;
        if (want_stats_cookie) {
            if (after_digits == '%') {
                lexer->advance(lexer, false);
                if (lexer->lookahead == ']') {
                    lexer->advance(lexer, false);
                    lexer->mark_end(lexer);  /* update token end to after ']' */
                    lexer->result_symbol = (TSSymbol)EXT_STATISTICS_COOKIE_TOKEN;
                    return true;
                }
            } else if (after_digits == '/') {
                lexer->advance(lexer, false);
                while (!lexer->eof(lexer) && is_digit(lexer->lookahead))
                    lexer->advance(lexer, false);
                if (lexer->lookahead == ']') {
                    lexer->advance(lexer, false);
                    lexer->mark_end(lexer);  /* update token end to after ']' */
                    lexer->result_symbol = (TSSymbol)EXT_STATISTICS_COOKIE_TOKEN;
                    return true;
                }
            }
        }
        /* Timestamp inactive: YYYY-MM-DD
         * mark_end was already set to after '[', so token will be just '['. */
        if (want_ts_inactive && digit_count == 4 && after_digits == '-') {
            lexer->advance(lexer, false);  /* '-' after YYYY */
            for (int i = 0; i < 2; i++) {
                if (!is_digit(lexer->lookahead)) return false;
                lexer->advance(lexer, false);
            }
            if (lexer->lookahead != '-') return false;
            lexer->advance(lexer, false);
            for (int i = 0; i < 2; i++) {
                if (!is_digit(lexer->lookahead)) return false;
                lexer->advance(lexer, false);
            }
            int32_t after = lexer->lookahead;
            if (after != ']' && after != ' ') return false;
            s->ts_substate = TS_EXPECT_DATE;
            s->ts_active   = 0;
            lexer->result_symbol = (TSSymbol)EXT_TS_OPEN_INACTIVE;
            return true;
        }
        return false;
    }

    /* Citation: emit `_citation_open` for "[cite" and switch the scanner
     * into citation sub-token mode (handled before this dispatcher on
     * subsequent calls). */
    if (lexer->lookahead == 'c' && valid_symbols[EXT_CITATION_OPEN]) {
        static const char kCite[] = "cite";
        for (int i = 0; i < 4; i++) {
            if (lexer->lookahead != (int32_t)kCite[i]) return false;
            lexer->advance(lexer, false);
        }
        lexer->mark_end(lexer);
        s->citation_state = 1;
        lexer->result_symbol = (TSSymbol)EXT_CITATION_OPEN;
        return true;
    }
    /* Backwards-compat: still detect a complete `[cite…]` as a single
     * opaque token if EXT_CITATION_OPEN isn't valid (no decomposition
     * grammar arm).  This path should be unreachable once the grammar
     * is updated, but keep it for safety. */
    if (lexer->lookahead == 'c' && want_citation) {
        static const char kCite[] = "cite";
        for (int i = 0; i < 4; i++) {
            if (lexer->lookahead != (int32_t)kCite[i]) return false;
            lexer->advance(lexer, false);
        }
        if (lexer->lookahead == '/') {
            while (!lexer->eof(lexer) && lexer->lookahead != ':' && lexer->lookahead != '\n')
                lexer->advance(lexer, false);
        }
        if (lexer->lookahead != ':') return false;
        lexer->advance(lexer, false);
        while (!lexer->eof(lexer)) {
            int32_t c = lexer->lookahead;
            if (c == '\n') return false;
            if (c == ']') {
                lexer->advance(lexer, false);
                lexer->result_symbol = (TSSymbol)EXT_CITATION_TOKEN;
                return true;
            }
            lexer->advance(lexer, false);
        }
        return false;
    }

    /* Footnote ref: '[fn:' */
    if (lexer->lookahead == 'f' && want_footnote_ref) {
        static const char kFn[] = "fn:";
        for (int i = 0; i < 3; i++) {
            if (lexer->lookahead != (int32_t)kFn[i]) return false;
            lexer->advance(lexer, false);
        }
        /* consume until ']' */
        while (!lexer->eof(lexer)) {
            int32_t c = lexer->lookahead;
            if (c == '\n') return false;
            if (c == ']') {
                lexer->advance(lexer, false);
                lexer->result_symbol = (TSSymbol)EXT_FOOTNOTE_REF_TOKEN;
                return true;
            }
            lexer->advance(lexer, false);
        }
        return false;
    }

    return false;
}

static bool try_export_snippet(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    (void)s;
    if (!valid_symbols[EXT_EXPORT_SNIPPET_TOKEN]) return false;
    if (lexer->lookahead != '@') return false;
    lexer->advance(lexer, false);
    if (lexer->lookahead != '@') return false;
    lexer->advance(lexer, false);
    /* backend: 1*(ALPHA / '-') */
    if (!is_alpha(lexer->lookahead) && lexer->lookahead != '-') return false;
    while (!lexer->eof(lexer) && (is_alpha(lexer->lookahead) || lexer->lookahead == '-'))
        lexer->advance(lexer, false);
    if (lexer->lookahead != ':') return false;
    lexer->advance(lexer, false);
    /* value: *(any except @@) */
    while (!lexer->eof(lexer)) {
        int32_t c = lexer->lookahead;
        if (c == '\n') return false;
        if (c == '@') {
            lexer->advance(lexer, false);
            if (lexer->lookahead == '@') {
                lexer->advance(lexer, false);
                lexer->result_symbol = (TSSymbol)EXT_EXPORT_SNIPPET_TOKEN;
                return true;
            }
            continue;
        }
        lexer->advance(lexer, false);
    }
    return false;
}

static bool try_inline_src_block(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    (void)s;
    if (!valid_symbols[EXT_INLINE_SRC_BLOCK_TOKEN]) return false;
    if (lexer->lookahead != 's') return false;
    static const char kSrc[] = "src_";
    for (int i = 0; i < 4; i++) {
        if (lexer->lookahead != (int32_t)kSrc[i]) return false;
        lexer->advance(lexer, false);
    }
    /* consume language name: 1+ (ALPHA / DIGIT / '_' / '-') */
    if (!is_alpha(lexer->lookahead) && !is_digit(lexer->lookahead)) return false;
    while (!lexer->eof(lexer) &&
           (is_alpha(lexer->lookahead) || is_digit(lexer->lookahead) ||
            lexer->lookahead == '_' || lexer->lookahead == '-')) {
        lexer->advance(lexer, false);
    }
    /* optional args: '[' ... ']' */
    if (lexer->lookahead == '[') {
        lexer->advance(lexer, false);
        while (!lexer->eof(lexer) && lexer->lookahead != ']' && lexer->lookahead != '\n')
            lexer->advance(lexer, false);
        if (lexer->lookahead != ']') return false;
        lexer->advance(lexer, false);
    }
    /* body: '{' ... '}' */
    if (lexer->lookahead != '{') return false;
    lexer->advance(lexer, false);
    while (!lexer->eof(lexer)) {
        int32_t c = lexer->lookahead;
        if (c == '\n') return false;
        if (c == '\\') {
            lexer->advance(lexer, false);
            if (lexer->lookahead == '}') lexer->advance(lexer, false);
            continue;
        }
        if (c == '}') {
            lexer->advance(lexer, false);
            lexer->result_symbol = (TSSymbol)EXT_INLINE_SRC_BLOCK_TOKEN;
            return true;
        }
        lexer->advance(lexer, false);
    }
    return false;
}

/* Combined '\' dispatcher: entity, latex \(...\), latex \[...\], line_break \\ */
static bool try_backslash(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    (void)s;
    if (lexer->lookahead != '\\') return false;

    bool want_entity  = valid_symbols[EXT_ENTITY_TOKEN];
    bool want_latex   = valid_symbols[EXT_LATEX_FRAGMENT_TOKEN];
    bool want_lbreak  = valid_symbols[EXT_LINE_BREAK_TOKEN];

    if (!want_entity && !want_latex && !want_lbreak) return false;

    lexer->advance(lexer, false);  /* consume first '\' */

    /* Line break: '\\' then optional WS then EOL */
    if (lexer->lookahead == '\\' && want_lbreak) {
        lexer->advance(lexer, false);
        while (!lexer->eof(lexer) && (lexer->lookahead == ' ' || lexer->lookahead == '\t'))
            lexer->advance(lexer, false);
        if (lexer->lookahead == '\n' || lexer->eof(lexer)) {
            lexer->result_symbol = (TSSymbol)EXT_LINE_BREAK_TOKEN;
            return true;
        }
        return false;
    }

    /* LaTeX \(...\) */
    if (lexer->lookahead == '(' && want_latex) {
        lexer->advance(lexer, false);
        while (!lexer->eof(lexer)) {
            int32_t c = lexer->lookahead;
            if (c == '\n') return false;
            if (c == '\\') {
                lexer->advance(lexer, false);
                if (lexer->lookahead == ')') {
                    lexer->advance(lexer, false);
                    lexer->result_symbol = (TSSymbol)EXT_LATEX_FRAGMENT_TOKEN;
                    return true;
                }
                continue;
            }
            lexer->advance(lexer, false);
        }
        return false;
    }

    /* LaTeX \[...\] */
    if (lexer->lookahead == '[' && want_latex) {
        lexer->advance(lexer, false);
        while (!lexer->eof(lexer)) {
            int32_t c = lexer->lookahead;
            if (c == '\n') return false;
            if (c == '\\') {
                lexer->advance(lexer, false);
                if (lexer->lookahead == ']') {
                    lexer->advance(lexer, false);
                    lexer->result_symbol = (TSSymbol)EXT_LATEX_FRAGMENT_TOKEN;
                    return true;
                }
                continue;
            }
            lexer->advance(lexer, false);
        }
        return false;
    }

    /* Entity: '\' + 1*ALPHA + optional '{}' */
    if (is_alpha(lexer->lookahead) && want_entity) {
        while (!lexer->eof(lexer) && is_alpha(lexer->lookahead))
            lexer->advance(lexer, false);
        if (lexer->lookahead == '{') {
            lexer->advance(lexer, false);
            if (lexer->lookahead == '}') lexer->advance(lexer, false);
        }
        lexer->result_symbol = (TSSymbol)EXT_ENTITY_TOKEN;
        return true;
    }

    return false;
}

/* LaTeX dollar: $...$  or $$...$$ */
static bool try_latex_dollar(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    (void)s;
    if (!valid_symbols[EXT_LATEX_FRAGMENT_TOKEN]) return false;
    if (lexer->lookahead != '$') return false;
    lexer->advance(lexer, false);
    if (lexer->lookahead == '$') {
        /* $$...$$ display */
        lexer->advance(lexer, false);
        while (!lexer->eof(lexer)) {
            int32_t c = lexer->lookahead;
            if (c == '\n') return false;
            if (c == '$') {
                lexer->advance(lexer, false);
                if (lexer->lookahead == '$') {
                    lexer->advance(lexer, false);
                    lexer->result_symbol = (TSSymbol)EXT_LATEX_FRAGMENT_TOKEN;
                    return true;
                }
            } else {
                lexer->advance(lexer, false);
            }
        }
        return false;
    }
    /* $x$ inline: consume until '$' (no newline) */
    bool consumed = false;
    while (!lexer->eof(lexer)) {
        int32_t c = lexer->lookahead;
        if (c == '\n') return false;
        if (c == '$') {
            if (!consumed) return false;
            lexer->advance(lexer, false);
            lexer->result_symbol = (TSSymbol)EXT_LATEX_FRAGMENT_TOKEN;
            return true;
        }
        lexer->advance(lexer, false);
        consumed = true;
    }
    return false;
}

/* Inline babel call: call_name[(header)](args)[results] */
static bool try_inline_babel_call(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    (void)s;
    if (!valid_symbols[EXT_INLINE_BABEL_CALL_TOKEN]) return false;
    /* 'call_' prefix */
    static const char kCall[] = "call_";
    for (int i = 0; i < 5; i++) {
        if (lexer->lookahead != (int32_t)kCall[i]) return false;
        lexer->advance(lexer, false);
    }
    /* name: 1+(ALPHA/DIGIT/'_'/'-') */
    if (!is_alpha(lexer->lookahead) && !is_digit(lexer->lookahead)) return false;
    while (!lexer->eof(lexer) &&
           (is_alpha(lexer->lookahead) || is_digit(lexer->lookahead) ||
            lexer->lookahead == '_' || lexer->lookahead == '-'))
        lexer->advance(lexer, false);
    /* optional [header] */
    if (lexer->lookahead == '[') {
        lexer->advance(lexer, false);
        while (!lexer->eof(lexer) && lexer->lookahead != ']' && lexer->lookahead != '\n')
            lexer->advance(lexer, false);
        if (lexer->lookahead != ']') return false;
        lexer->advance(lexer, false);
    }
    /* required (args) */
    if (lexer->lookahead != '(') return false;
    lexer->advance(lexer, false);
    while (!lexer->eof(lexer) && lexer->lookahead != ')' && lexer->lookahead != '\n')
        lexer->advance(lexer, false);
    if (lexer->lookahead != ')') return false;
    lexer->advance(lexer, false);
    /* optional [results] */
    if (lexer->lookahead == '[') {
        lexer->advance(lexer, false);
        while (!lexer->eof(lexer) && lexer->lookahead != ']' && lexer->lookahead != '\n')
            lexer->advance(lexer, false);
        if (lexer->lookahead != ']') return false;
        lexer->advance(lexer, false);
    }
    lexer->result_symbol = (TSSymbol)EXT_INLINE_BABEL_CALL_TOKEN;
    return true;
}


static bool try_subscript(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    (void)s;
    if (!valid_symbols[EXT_SUBSCRIPT_TOKEN]) return false;
    if (lexer->lookahead != '_') return false;
    lexer->advance(lexer, false);
    if (lexer->lookahead != '{') return false;
    lexer->advance(lexer, false);
    while (!lexer->eof(lexer) && lexer->lookahead != '}' && lexer->lookahead != '\n')
        lexer->advance(lexer, false);
    if (lexer->lookahead != '}') return false;
    lexer->advance(lexer, false);
    lexer->result_symbol = (TSSymbol)EXT_SUBSCRIPT_TOKEN;
    return true;
}

static bool try_superscript(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    (void)s;
    if (!valid_symbols[EXT_SUPERSCRIPT_TOKEN]) return false;
    if (lexer->lookahead != '^') return false;
    lexer->advance(lexer, false);
    int32_t c = lexer->lookahead;
    if (c == '{') {
        lexer->advance(lexer, false);
        while (!lexer->eof(lexer) && lexer->lookahead != '}' && lexer->lookahead != '\n')
            lexer->advance(lexer, false);
        if (lexer->lookahead != '}') return false;
        lexer->advance(lexer, false);
        lexer->result_symbol = (TSSymbol)EXT_SUPERSCRIPT_TOKEN;
        return true;
    }
    if (is_alpha(c) || is_digit(c)) {
        while (!lexer->eof(lexer) && (is_alpha(lexer->lookahead) || is_digit(lexer->lookahead)))
            lexer->advance(lexer, false);
        lexer->result_symbol = (TSSymbol)EXT_SUPERSCRIPT_TOKEN;
        return true;
    }
    return false;
}

static bool try_macro(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    (void)s;
    if (!valid_symbols[EXT_MACRO_TOKEN]) return false;
    if (lexer->lookahead != '{') return false;
    lexer->advance(lexer, false);
    if (lexer->lookahead != '{') return false;
    lexer->advance(lexer, false);
    if (lexer->lookahead != '{') return false;
    lexer->advance(lexer, false);
    /* Cover only `{{{` so JS rules can decompose name + args + `}}}`. */
    lexer->mark_end(lexer);
    int brace_count = 0;
    while (!lexer->eof(lexer)) {
        int32_t c = lexer->lookahead;
        if (c == '\n') return false;
        if (c == '}') {
            brace_count++;
            lexer->advance(lexer, false);
            if (brace_count == 3) {
                lexer->result_symbol = (TSSymbol)EXT_MACRO_TOKEN;
                return true;
            }
        } else {
            brace_count = 0;
            lexer->advance(lexer, false);
        }
    }
    return false;
}

/* --- Timestamp field handlers ------------------------------------------- */

static bool advance_digits(TSLexer *lexer, int n) {
    for (int i = 0; i < n; i++) {
        if (!is_digit(lexer->lookahead)) return false;
        lexer->advance(lexer, false);
    }
    return true;
}

static bool try_ts_date(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    if (s->ts_substate != TS_EXPECT_DATE) return false;
    if (!valid_symbols[EXT_TS_DATE_TOKEN]) return false;

    if (!advance_digits(lexer, 4)) return false;
    if (lexer->lookahead != '-') return false;
    lexer->advance(lexer, false);
    if (!advance_digits(lexer, 2)) return false;
    if (lexer->lookahead != '-') return false;
    lexer->advance(lexer, false);
    if (!advance_digits(lexer, 2)) return false;

    s->ts_substate = TS_EXPECT_FIELDS;
    lexer->result_symbol = (TSSymbol)EXT_TS_DATE_TOKEN;
    return true;
}

static bool try_ts_close(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    if (s->ts_substate != TS_EXPECT_FIELDS) return false;
    int32_t c = lexer->lookahead;
    bool active   = (c == '>');
    bool inactive = (c == ']');
    if (!active && !inactive) return false;
    if (active   && !valid_symbols[EXT_TS_CLOSE_ACTIVE])   return false;
    if (inactive && !valid_symbols[EXT_TS_CLOSE_INACTIVE]) return false;

    lexer->advance(lexer, false);
    s->ts_substate = TS_OUTSIDE;
    s->ts_active   = 0;
    lexer->result_symbol = (TSSymbol)(active ? EXT_TS_CLOSE_ACTIVE : EXT_TS_CLOSE_INACTIVE);
    return true;
}

/* In TS_EXPECT_FIELDS, the scanner is called when lookahead is ' ' (before a field)
 * or '>' / ']' (before close). We handle space-skipping here: advance the space with
 * mark_end (so the space is excluded from the token), then dispatch to the field.
 *
 * The key invariant: this function is ONLY called in TS_EXPECT_FIELDS, and it always
 * either returns true (emitting one field or close token) or returns false (malformed
 * input). It never returns false having consumed only the space.
 */
static bool try_ts_fields(InlineState *s, TSLexer *lexer, const bool *valid_symbols) {
    if (s->ts_substate != TS_EXPECT_FIELDS) return false;

    /* Close bracket: no space before it */
    if (lexer->lookahead == '>' || lexer->lookahead == ']') {
        return try_ts_close(s, lexer, valid_symbols);
    }

    /* Optional space separator: skip it so the token starts after the space */
    if (lexer->lookahead == ' ') {
        lexer->advance(lexer, true);   /* skip=true: space excluded from token start */
    }

    /* Now dispatch on the first char of the field */
    int32_t c = lexer->lookahead;

    /* Close after space (unusual but handle it) */
    if (c == '>' || c == ']') {
        /* We already consumed the space and called mark_end; the close token
         * should NOT include the space. Reset: the space was a separator.
         * Emit the close token. */
        return try_ts_close(s, lexer, valid_symbols);
    }

    /* Dayname: alpha chars */
    if (is_alpha(c) && valid_symbols[EXT_TS_DAYNAME_TOKEN]) {
        int count = 0;
        while (is_alpha(lexer->lookahead) && count < 5) {
            lexer->advance(lexer, false);
            count++;
        }
        if (count >= 3) {
            lexer->result_symbol = (TSSymbol)EXT_TS_DAYNAME_TOKEN;
            return true;
        }
        return false;
    }

    /* Time or time range: digit digit ':' digit digit */
    if (is_digit(c) && (valid_symbols[EXT_TS_TIME_TOKEN] || valid_symbols[EXT_TS_TIME_RANGE_TOKEN])) {
        if (!advance_digits(lexer, 2)) return false;
        if (lexer->lookahead != ':') return false;
        lexer->advance(lexer, false);
        if (!advance_digits(lexer, 2)) return false;
        /* Check for time range: '-' followed by HH:MM */
        if (lexer->lookahead == '-' && valid_symbols[EXT_TS_TIME_RANGE_TOKEN]) {
            lexer->advance(lexer, false);
            if (!advance_digits(lexer, 2)) return false;
            if (lexer->lookahead != ':') return false;
            lexer->advance(lexer, false);
            if (!advance_digits(lexer, 2)) return false;
            lexer->result_symbol = (TSSymbol)EXT_TS_TIME_RANGE_TOKEN;
            return true;
        }
        if (!valid_symbols[EXT_TS_TIME_TOKEN]) return false;
        lexer->result_symbol = (TSSymbol)EXT_TS_TIME_TOKEN;
        return true;
    }

    /* Repeater: (.+|++|+) digit+ unit
     * Just the basic period; the optional `/Nu` alarm half (org-habit
     * `.+P/Q` syntax) and the optional `[filter]` skip-filter are
     * separate tokens so they highlight independently. */
    if ((c == '+' || c == '.') && valid_symbols[EXT_TS_REPEATER_TOKEN]) {
        if (c == '.') {
            lexer->advance(lexer, false);
            if (lexer->lookahead != '+') return false;
            lexer->advance(lexer, false);
        } else {
            lexer->advance(lexer, false);  /* consume '+' */
            if (lexer->lookahead == '+') lexer->advance(lexer, false);
        }
        if (!is_digit(lexer->lookahead)) return false;
        while (is_digit(lexer->lookahead)) lexer->advance(lexer, false);
        int32_t u = lexer->lookahead;
        if (u != 'h' && u != 'd' && u != 'w' && u != 'm' && u != 'y') return false;
        lexer->advance(lexer, false);
        lexer->result_symbol = (TSSymbol)EXT_TS_REPEATER_TOKEN;
        return true;
    }

    /* Repeater alarm: `/Nu` (where u ∈ h/d/w/m/y). Per Emacs `org-habit`,
     * this lives between the repeater and the closing bracket. */
    if (c == '/' && valid_symbols[EXT_TS_REPEATER_ALARM_TOKEN]) {
        lexer->advance(lexer, false);
        if (!is_digit(lexer->lookahead)) return false;
        while (is_digit(lexer->lookahead)) lexer->advance(lexer, false);
        int32_t u = lexer->lookahead;
        if (u != 'h' && u != 'd' && u != 'w' && u != 'm' && u != 'y') return false;
        lexer->advance(lexer, false);
        lexer->result_symbol = (TSSymbol)EXT_TS_REPEATER_ALARM_TOKEN;
        return true;
    }

    /* Repeater filter: `[...]` immediately after the repeater (or alarm).
     * Captures everything up to the closing `]`.  Highlighted as a single
     * scope; the inner tokens can be further captured by an injection or
     * a directive in highlights.scm if a richer view is needed. */
    if (c == '[' && valid_symbols[EXT_TS_REPEATER_FILTER_TOKEN]) {
        lexer->advance(lexer, false);
        while (lexer->lookahead != ']' && lexer->lookahead != '>'
               && lexer->lookahead != '\n' && !lexer->eof(lexer)) {
            lexer->advance(lexer, false);
        }
        if (lexer->lookahead != ']') return false;
        lexer->advance(lexer, false);
        lexer->result_symbol = (TSSymbol)EXT_TS_REPEATER_FILTER_TOKEN;
        return true;
    }

    /* Warning: (--|-)  digit+ unit */
    if (c == '-' && valid_symbols[EXT_TS_WARNING_TOKEN]) {
        lexer->advance(lexer, false);
        if (lexer->lookahead == '-') lexer->advance(lexer, false);
        if (!is_digit(lexer->lookahead)) return false;
        while (is_digit(lexer->lookahead)) lexer->advance(lexer, false);
        int32_t u = lexer->lookahead;
        if (u != 'h' && u != 'd' && u != 'w' && u != 'm' && u != 'y') return false;
        lexer->advance(lexer, false);
        lexer->result_symbol = (TSSymbol)EXT_TS_WARNING_TOKEN;
        return true;
    }

    return false;
}

bool tree_sitter_org_inline_external_scanner_scan(void *payload, TSLexer *lexer,
                                                    const bool *valid_symbols) {
    InlineState *s = (InlineState *)payload;

    if (lexer->eof(lexer)) return false;

    /* --- Inside-timestamp sub-state machine -------------------------------- */
    if (s->ts_substate == TS_EXPECT_DATE) {
        return try_ts_date(s, lexer, valid_symbols);
    }

    if (s->ts_substate == TS_EXPECT_FIELDS) {
        return try_ts_fields(s, lexer, valid_symbols);
    }

    /* --- Inside-citation sub-token dispatch -------------------------------- */
    if (s->citation_state == 1) {
        /* Just opened: expect `/style` or `:` */
        if (lexer->lookahead == '/' && valid_symbols[EXT_CITATION_STYLE]) {
            lexer->advance(lexer, false);
            while (!lexer->eof(lexer)
                   && (is_alpha(lexer->lookahead) || lexer->lookahead == '/'
                       || lexer->lookahead == '_' || lexer->lookahead == '-')) {
                lexer->advance(lexer, false);
            }
            lexer->mark_end(lexer);
            lexer->result_symbol = (TSSymbol)EXT_CITATION_STYLE;
            return true;
        }
        if (lexer->lookahead == ':' && valid_symbols[EXT_CITATION_COLON]) {
            lexer->advance(lexer, false);
            /* Eat optional trailing whitespace into the colon token. */
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                lexer->advance(lexer, false);
            }
            lexer->mark_end(lexer);
            s->citation_state = 2;
            lexer->result_symbol = (TSSymbol)EXT_CITATION_COLON;
            return true;
        }
        /* Malformed citation — fall through to plain scanning, reset state. */
        s->citation_state = 0;
    }
    if (s->citation_state == 2) {
        if (lexer->lookahead == ']' && valid_symbols[EXT_CITATION_CLOSE]) {
            lexer->advance(lexer, false);
            lexer->mark_end(lexer);
            s->citation_state = 0;
            lexer->result_symbol = (TSSymbol)EXT_CITATION_CLOSE;
            return true;
        }
        if (lexer->lookahead == ';' && valid_symbols[EXT_CITATION_SEPARATOR]) {
            lexer->advance(lexer, false);
            while (lexer->lookahead == ' ' || lexer->lookahead == '\t') {
                lexer->advance(lexer, false);
            }
            lexer->mark_end(lexer);
            lexer->result_symbol = (TSSymbol)EXT_CITATION_SEPARATOR;
            return true;
        }
        if (lexer->lookahead == '@' && valid_symbols[EXT_CITATION_KEY]) {
            lexer->advance(lexer, false);  /* `@` */
            while (!lexer->eof(lexer)
                   && (is_alpha(lexer->lookahead) || is_digit(lexer->lookahead)
                       || lexer->lookahead == '_' || lexer->lookahead == '-'
                       || lexer->lookahead == ':')) {
                lexer->advance(lexer, false);
            }
            lexer->mark_end(lexer);
            lexer->result_symbol = (TSSymbol)EXT_CITATION_KEY;
            return true;
        }
        /* Free-text prefix or suffix: anything until ] / ; / @ / newline. */
        if (valid_symbols[EXT_CITATION_TEXT]) {
            bool any = false;
            while (!lexer->eof(lexer)) {
                int32_t cc = lexer->lookahead;
                if (cc == '\n' || cc == ']' || cc == ';' || cc == '@') break;
                lexer->advance(lexer, false);
                any = true;
            }
            if (!any) return false;
            lexer->mark_end(lexer);
            lexer->result_symbol = (TSSymbol)EXT_CITATION_TEXT;
            return true;
        }
        return false;
    }

    /* --- Normal inline scanning ------------------------------------------- */
    int32_t c = lexer->lookahead;

    if (s->span_depth > 0) {
        uint8_t top = s->span_stack[s->span_depth - 1];
        bool match = false;
        int close_sym = -1;
        if      (top == '*' && c == '*' && valid_symbols[EXT_BOLD_CLOSE])      { match = true; close_sym = EXT_BOLD_CLOSE; }
        else if (top == '/' && c == '/' && valid_symbols[EXT_ITALIC_CLOSE])    { match = true; close_sym = EXT_ITALIC_CLOSE; }
        else if (top == '_' && c == '_' && valid_symbols[EXT_UNDERLINE_CLOSE]) { match = true; close_sym = EXT_UNDERLINE_CLOSE; }
        else if (top == '+' && c == '+' && valid_symbols[EXT_STRIKE_CLOSE])    { match = true; close_sym = EXT_STRIKE_CLOSE; }
        if (match) {
            lexer->advance(lexer, false);
            span_pop(s);
            lexer->result_symbol = (TSSymbol)close_sym;
            return true;
        }
    }

    if (lexer->lookahead == '=' && try_verbatim(s, lexer, valid_symbols)) return true;
    if (lexer->lookahead == '~' && try_code(s, lexer, valid_symbols))     return true;

    /* Combined handlers for '<' and '[' that never fail after consuming the bracket */
    if (lexer->lookahead == '<') {
        if (try_angle_or_ts_active(s, lexer, valid_symbols)) return true;
    }
    if (lexer->lookahead == '[') {
        if (try_bracket_open(s, lexer, valid_symbols)) return true;
    }

    if (lexer->lookahead == '{') {
        if (try_macro(s, lexer, valid_symbols)) return true;
    }

    if (lexer->lookahead == '@' && try_export_snippet(s, lexer, valid_symbols)) return true;

    if (lexer->lookahead == '\\' && try_backslash(s, lexer, valid_symbols)) return true;

    if (lexer->lookahead == '$' && try_latex_dollar(s, lexer, valid_symbols)) return true;

    /* Timestamp range separator: `--<` or `--[`.  Only valid in the GLR
     * arm that produces a range node; valid_symbols guards us. */
    if (lexer->lookahead == '-' && valid_symbols[EXT_TS_RANGE_SEPARATOR]) {
        lexer->advance(lexer, false);
        if (lexer->lookahead != '-') return false;
        lexer->advance(lexer, false);
        if (lexer->lookahead != '<' && lexer->lookahead != '[') return false;
        lexer->mark_end(lexer);
        lexer->result_symbol = (TSSymbol)EXT_TS_RANGE_SEPARATOR;
        return true;
    }

    if (lexer->lookahead == '^' && try_superscript(s, lexer, valid_symbols)) return true;

    if (lexer->lookahead == 's' && try_inline_src_block(s, lexer, valid_symbols)) return true;

    if (lexer->lookahead == 'c' && try_inline_babel_call(s, lexer, valid_symbols)) return true;

    if (s->span_depth == 0 &&
        (lexer->lookahead >= 'a' && lexer->lookahead <= 'z') &&
        try_link_plain(s, lexer, valid_symbols)) return true;

    int open_sym = -1;
    uint8_t marker = 0;
    if (c == '*' && valid_symbols[EXT_BOLD_OPEN])      { open_sym = EXT_BOLD_OPEN;      marker = '*'; }
    else if (c == '/' && valid_symbols[EXT_ITALIC_OPEN])    { open_sym = EXT_ITALIC_OPEN;    marker = '/'; }
    else if (c == '_' && valid_symbols[EXT_UNDERLINE_OPEN]) {
        /* `_` could be: underline open (`_text_`), braced subscript
         * (`_{…}`), or no-brace subscript (`_X` where X is alpha/digit).
         * We disambiguate by looking ahead. */
        if (valid_symbols[EXT_SUBSCRIPT_TOKEN]) {
            lexer->advance(lexer, false);  /* past `_` */
            lexer->mark_end(lexer);        /* fallback: token = just `_` */
            if (lexer->lookahead == '{') {
                /* Match `_{…}` as subscript */
                lexer->advance(lexer, false);
                while (!lexer->eof(lexer) && lexer->lookahead != '}' && lexer->lookahead != '\n')
                    lexer->advance(lexer, false);
                if (lexer->lookahead == '}') {
                    lexer->advance(lexer, false);
                    lexer->mark_end(lexer);
                    lexer->result_symbol = (TSSymbol)EXT_SUBSCRIPT_TOKEN;
                    return true;
                }
                /* Malformed `_{…` — fall back to underline open of `_`. */
                span_push(s, '_');
                lexer->result_symbol = (TSSymbol)EXT_UNDERLINE_OPEN;
                return true;
            }
            if (is_alpha(lexer->lookahead) || is_digit(lexer->lookahead)) {
                /* Could be no-brace subscript `_X` OR underline `_text_`.
                 * Walk through the alpha/digit run; if a closing `_`
                 * follows it, treat the leading `_` as underline open
                 * instead.  Otherwise commit subscript. */
                while (!lexer->eof(lexer)
                       && (is_alpha(lexer->lookahead) || is_digit(lexer->lookahead))) {
                    lexer->advance(lexer, false);
                }
                if (lexer->lookahead == '_') {
                    /* `_text_` shape — underline open; mark_end stays at 1. */
                    span_push(s, '_');
                    lexer->result_symbol = (TSSymbol)EXT_UNDERLINE_OPEN;
                    return true;
                }
                /* No closing `_` — commit subscript across the alpha/digit run. */
                lexer->mark_end(lexer);
                lexer->result_symbol = (TSSymbol)EXT_SUBSCRIPT_TOKEN;
                return true;
            }
            /* `_` followed by something else — underline open. */
            span_push(s, '_');
            lexer->result_symbol = (TSSymbol)EXT_UNDERLINE_OPEN;
            return true;
        } else {
            open_sym = EXT_UNDERLINE_OPEN; marker = '_';
        }
    }
    else if (c == '+' && valid_symbols[EXT_STRIKE_OPEN])    { open_sym = EXT_STRIKE_OPEN;    marker = '+'; }
    if (open_sym >= 0) {
        lexer->advance(lexer, false);
        span_push(s, marker);
        lexer->result_symbol = (TSSymbol)open_sym;
        return true;
    }

    /* Subscript: placed AFTER span openers so it doesn't interfere with underline */
    if (lexer->lookahead == '_' && try_subscript(s, lexer, valid_symbols)) return true;

    if (!valid_symbols[EXT_PLAIN_TEXT_TOKEN]) return false;
    bool consumed = false;
    while (!lexer->eof(lexer)) {
        int32_t cur = lexer->lookahead;
        if (cur == '[' && (valid_symbols[EXT_LINK_REGULAR_TOKEN] || valid_symbols[EXT_TS_OPEN_INACTIVE] || valid_symbols[EXT_CITATION_TOKEN] || valid_symbols[EXT_FOOTNOTE_REF_TOKEN] || valid_symbols[EXT_STATISTICS_COOKIE_TOKEN]) && consumed) break;
        if (cur == '{' && valid_symbols[EXT_MACRO_TOKEN] && consumed) break;
        if (cur == '@' && valid_symbols[EXT_EXPORT_SNIPPET_TOKEN] && consumed) break;
        if (cur == '\\' && (valid_symbols[EXT_LINE_BREAK_TOKEN] || valid_symbols[EXT_ENTITY_TOKEN] || valid_symbols[EXT_LATEX_FRAGMENT_TOKEN]) && consumed) break;
        if (cur == '$' && valid_symbols[EXT_LATEX_FRAGMENT_TOKEN] && consumed) break;
        if (cur == '^' && valid_symbols[EXT_SUPERSCRIPT_TOKEN] && consumed) break;
        if (cur == '_' && valid_symbols[EXT_SUBSCRIPT_TOKEN] && consumed) break;
        if (cur == '=' && valid_symbols[EXT_VERBATIM_TOKEN] && consumed) break;
        if (cur == '~' && valid_symbols[EXT_CODE_TOKEN] && consumed) break;
        if (cur == 's' && valid_symbols[EXT_INLINE_SRC_BLOCK_TOKEN] && consumed) {
            lexer->mark_end(lexer);
            lexer->advance(lexer, false);
            bool is_src = false;
            if (lexer->lookahead == 'r') {
                lexer->advance(lexer, false);
                if (lexer->lookahead == 'c') {
                    lexer->advance(lexer, false);
                    if (lexer->lookahead == '_') is_src = true;
                }
            }
            if (is_src) {
                lexer->result_symbol = (TSSymbol)EXT_PLAIN_TEXT_TOKEN;
                return true;
            }
            consumed = true;
            continue;
        }
        if (cur == 'c' && valid_symbols[EXT_INLINE_BABEL_CALL_TOKEN] && consumed) {
            lexer->mark_end(lexer);
            lexer->advance(lexer, false);
            bool is_call = false;
            static const char kCall2[] = "all_";
            bool match = true;
            for (int i = 0; i < 4 && match; i++) {
                if (lexer->lookahead != (int32_t)kCall2[i]) match = false;
                else lexer->advance(lexer, false);
            }
            is_call = match;
            if (is_call) {
                lexer->result_symbol = (TSSymbol)EXT_PLAIN_TEXT_TOKEN;
                return true;
            }
            consumed = true;
            continue;
        }
        if (cur == '<' && (valid_symbols[EXT_LINK_ANGLE_TOKEN] || valid_symbols[EXT_LINK_RADIO_TOKEN] ||
                           valid_symbols[EXT_TS_OPEN_ACTIVE]) && consumed) break;
        if (is_inline_opener(cur)) {
            if (consumed) break;
            if (s->span_depth > 0) {
                uint8_t top = s->span_stack[s->span_depth - 1];
                if (top == (uint8_t)cur) break;
            }
            lexer->advance(lexer, false);
            consumed = true;
            continue;
        }
        lexer->advance(lexer, false);
        consumed = true;
    }
    if (!consumed) return false;
    lexer->mark_end(lexer);
    lexer->result_symbol = (TSSymbol)EXT_PLAIN_TEXT_TOKEN;
    return true;
}
