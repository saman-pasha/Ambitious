/* src/html/tokenizer.c -- the WHATWG tokenizer (HTML 13.2.5), bytes to the token
   table of the HTML module's store (contracts/html.pl).

   The input's newlines are normalized first (13.2.3.5: CR LF and CR become LF).
   The state machine then reads the normalized bytes once; the states are the
   standard's, by its names. Tokens, their attributes and the errors are records
   of the store, their text spans of its byte pool, so nothing here holds a
   pointer into the store across a call that may grow it.

   The tokenizer switches to RCDATA, RAWTEXT, script data or PLAINTEXT after the
   start tag of title and textarea; style, xmp, iframe, noembed and noframes;
   script; plaintext -- the switches the tree builder makes for HTML content
   (13.2.6.4), made here so that the token table is whole before the tree is
   built. Scripting is off, so noscript is not RAWTEXT; there is no foreign
   content yet, so CDATA is a bogus comment. U+0000 in the data state is a parse
   error and is dropped, which is what the tree builder does with it in HTML
   content. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>
#include "entities.h"

amb_interface(dom);
amb_interface(html);

enum html_state {
    S_DATA, S_RCDATA, S_RAWTEXT, S_SCRIPT, S_PLAINTEXT, S_TAG_OPEN, S_END_TAG_OPEN, S_TAG_NAME,
    S_RCDATA_LT, S_RCDATA_END_OPEN, S_RCDATA_END_NAME, S_RAWTEXT_LT, S_RAWTEXT_END_OPEN, S_RAWTEXT_END_NAME,
    S_SCRIPT_LT, S_SCRIPT_END_OPEN, S_SCRIPT_END_NAME, S_SCRIPT_ESC_START, S_SCRIPT_ESC_START_DASH,
    S_SCRIPT_ESC, S_SCRIPT_ESC_DASH, S_SCRIPT_ESC_DASH_DASH, S_SCRIPT_ESC_LT, S_SCRIPT_ESC_END_OPEN,
    S_SCRIPT_ESC_END_NAME, S_SCRIPT_DESC_START, S_SCRIPT_DESC, S_SCRIPT_DESC_DASH, S_SCRIPT_DESC_DASH_DASH,
    S_SCRIPT_DESC_LT, S_SCRIPT_DESC_END, S_BEFORE_ATTR_NAME, S_ATTR_NAME, S_AFTER_ATTR_NAME,
    S_BEFORE_ATTR_VALUE, S_ATTR_VALUE_DQ, S_ATTR_VALUE_SQ, S_ATTR_VALUE_UNQ, S_AFTER_ATTR_VALUE_Q,
    S_SELF_CLOSING, S_BOGUS_COMMENT, S_MARKUP_DECL, S_COMMENT_START, S_COMMENT_START_DASH, S_COMMENT,
    S_COMMENT_LT, S_COMMENT_LT_BANG, S_COMMENT_LT_BANG_DASH, S_COMMENT_LT_BANG_DASH_DASH,
    S_COMMENT_END_DASH, S_COMMENT_END, S_COMMENT_END_BANG, S_DOCTYPE, S_BEFORE_DOCTYPE_NAME,
    S_DOCTYPE_NAME, S_AFTER_DOCTYPE_NAME, S_AFTER_DOCTYPE_PUBLIC_KW, S_BEFORE_DOCTYPE_PUBLIC_ID,
    S_DOCTYPE_PUBLIC_ID_DQ, S_DOCTYPE_PUBLIC_ID_SQ, S_AFTER_DOCTYPE_PUBLIC_ID, S_BETWEEN_DOCTYPE_IDS,
    S_AFTER_DOCTYPE_SYSTEM_KW, S_BEFORE_DOCTYPE_SYSTEM_ID, S_DOCTYPE_SYSTEM_ID_DQ, S_DOCTYPE_SYSTEM_ID_SQ,
    S_AFTER_DOCTYPE_SYSTEM_ID, S_BOGUS_DOCTYPE
};

/* the input, where the tokenizer is in it, and where that is as a line and a column */
typedef struct html_cursor { size_t i; uint32_t line, col; } html_cursor;

static int html_ws(int c) { return c == '\t' || c == '\n' || c == '\f' || c == ' '; }
static int html_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int html_digit(int c) { return c >= '0' && c <= '9'; }
static int html_hexdigit(int c) { return html_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int html_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static void html_err(html_parser *p, html_error_code code, uint32_t line, uint32_t col) {
    html_error_h e = error_new(p);
    p->error[e.index].code = code;
    p->error[e.index].line = line;
    p->error[e.index].col = col;
}

static void html_put_utf8(vector<char> *b, uint32_t cp) {
    if (cp < 0x80) push_back(b, (char)cp);
    else if (cp < 0x800) { push_back(b, (char)(0xC0 | (cp >> 6))); push_back(b, (char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        push_back(b, (char)(0xE0 | (cp >> 12))); push_back(b, (char)(0x80 | ((cp >> 6) & 0x3F)));
        push_back(b, (char)(0x80 | (cp & 0x3F)));
    } else {
        push_back(b, (char)(0xF0 | (cp >> 18))); push_back(b, (char)(0x80 | ((cp >> 12) & 0x3F)));
        push_back(b, (char)(0x80 | ((cp >> 6) & 0x3F))); push_back(b, (char)(0x80 | (cp & 0x3F)));
    }
}

/* U+FFFD, the replacement character */
static void html_put_fffd(vector<char> *b) { html_put_utf8(b, 0xFFFD); }

/* the code points 0x80 to 0x9F of a numeric reference, as Windows-1252 reads them (13.2.5.80) */
static uint32_t html_c1(uint32_t cp) {
    static const uint32_t t[32] = { 0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030,
        0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
    return cp >= 0x80 && cp <= 0x9F ? t[cp - 0x80] : cp;
}

/* the entity whose name is exactly in[0..n), or -1 */
static int html_entity_find(const char *in, size_t n) {
    int lo = 0, hi = HTML_ENTITY_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        size_t m = html_entities[mid].len;
        size_t k = m < n ? m : n;
        int c = memcmp(html_entity_names + html_entities[mid].off, in, k);
        if (c == 0) c = m < n ? -1 : (m > n ? 1 : 0);
        if (c == 0) return mid;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

/* A character reference at in[at] == '&' (13.2.5.72 to .80): what it stands for
   appended to out; answers the bytes consumed. In an attribute's value a named
   reference without its ; and followed by = or an alphanumeric stays text. */
static size_t html_charref(html_parser *p, const char *in, size_t n, size_t at, int in_attr,
                           vector<char> *out, uint32_t line, uint32_t col) {
    size_t i = at + 1;
    if (i < n && in[i] == '#') {
        size_t j = i + 1;
        int hex = 0;
        if (j < n && (in[j] == 'x' || in[j] == 'X')) { hex = 1; j++; }
        size_t start = j;
        uint32_t v = 0;
        int big = 0;
        while (j < n && (hex ? html_hexdigit(in[j]) : html_digit(in[j]))) {
            int dv = html_digit(in[j]) ? in[j] - '0' : html_lower(in[j]) - 'a' + 10;
            if (v > 0x10FFFF) big = 1; else v = v * (hex ? 16 : 10) + (uint32_t)dv;
            j++;
        }
        if (j == start) {
            html_err(p, HTML_ERROR_CODE_ABSENCE_OF_DIGITS_IN_NUMERIC_CHARACTER_REFERENCE, line, col);
            for (size_t k = at; k < start; k++) push_back(out, in[k]);
            return start - at;
        }
        if (j < n && in[j] == ';') j++;
        else html_err(p, HTML_ERROR_CODE_MISSING_SEMICOLON_AFTER_CHARACTER_REFERENCE, line, col);
        if (big || v > 0x10FFFF) { html_err(p, HTML_ERROR_CODE_CHARACTER_REFERENCE_OUTSIDE_UNICODE_RANGE, line, col); v = 0xFFFD; }
        else if (v == 0) { html_err(p, HTML_ERROR_CODE_NULL_CHARACTER_REFERENCE, line, col); v = 0xFFFD; }
        else if (v >= 0xD800 && v <= 0xDFFF) { html_err(p, HTML_ERROR_CODE_SURROGATE_CHARACTER_REFERENCE, line, col); v = 0xFFFD; }
        else {
            if ((v >= 0xFDD0 && v <= 0xFDEF) || (v & 0xFFFE) == 0xFFFE)
                html_err(p, HTML_ERROR_CODE_NONCHARACTER_CHARACTER_REFERENCE, line, col);
            else if (v == 0x0D || (v < 0x20 && v != 0x09 && v != 0x0A && v != 0x0C) || (v >= 0x7F && v <= 0x9F))
                html_err(p, HTML_ERROR_CODE_CONTROL_CHARACTER_REFERENCE, line, col);
            v = html_c1(v);
        }
        html_put_utf8(out, v);
        return j - at;
    }
    /* named: the longest name in the table that the input begins with */
    size_t avail = 0;
    while (i + avail < n && avail < HTML_ENTITY_LONGEST && (html_alpha(in[i + avail]) || html_digit(in[i + avail]))) avail++;
    size_t have = avail;
    if (i + avail < n && in[i + avail] == ';' && avail < HTML_ENTITY_LONGEST) have = avail + 1;
    for (size_t m = have; m >= 1; m--) {
        int e = html_entity_find(in + i, m);
        if (e < 0) continue;
        int semi = in[i + m - 1] == ';';
        if (in_attr && !semi && i + m < n && (in[i + m] == '=' || html_alpha(in[i + m]) || html_digit(in[i + m]))) {
            for (size_t k = at; k < i + m; k++) push_back(out, in[k]);
            return i + m - at;
        }
        if (!semi) html_err(p, HTML_ERROR_CODE_MISSING_SEMICOLON_AFTER_CHARACTER_REFERENCE, line, col);
        html_put_utf8(out, html_entities[e].cp1);
        if (html_entities[e].cp2) html_put_utf8(out, html_entities[e].cp2);
        return i + m - at;
    }
    if (avail > 0 && i + avail < n && in[i + avail] == ';')
        html_err(p, HTML_ERROR_CODE_UNKNOWN_NAMED_CHARACTER_REFERENCE, line, col);
    push_back(out, '&');
    return 1;
}

/* the characters gathered so far, as one characters token */
static void html_flush_chars(html_parser *p, vector<char> *chars, uint32_t line, uint32_t col) {
    if (len(*chars) == 0) return;
    amb_span sp = bytes_add(p, data(*chars), len(*chars));
    html_token_h t = token_new(p);
    p->token[t.index].kind = HTML_TOKEN_KIND_CHARACTERS;
    p->token[t.index].data = sp;
    p->token[t.index].line = line;
    p->token[t.index].col = col;
    clear(chars);
}

/* the attribute being read, added to the tag being read unless it has one of that name */
static void html_commit_attr(html_parser *p, html_token_h tok, vector<char> *an, vector<char> *av, uint32_t line, uint32_t col) {
    if (len(*an) == 0 && len(*av) == 0) return;
    for (uint32_t a = p->token[tok.index].first_attr; a; a = p->tattr[a].next) {
        amb_span sp = p->tattr[a].name;
        if (sp.len == len(*an) && memcmp(bytes_ptr(p, sp), data(*an), sp.len) == 0) {
            html_err(p, HTML_ERROR_CODE_DUPLICATE_ATTRIBUTE, line, col);
            clear(an);
            clear(av);
            return;
        }
    }
    amb_span nm = bytes_add(p, data(*an), len(*an));
    amb_span vl = bytes_add(p, data(*av), len(*av));
    html_tattr_h h = tattr_new(p);
    p->tattr[h.index].name = nm;
    p->tattr[h.index].value = vl;
    attributes_append(p, tok.index, h.index);
    clear(an);
    clear(av);
}

static int html_name_is(const vector<char> *b, const char *w) {
    size_t n = strlen(w);
    return len(*b) == n && memcmp(data(*b), w, n) == 0;
}

/* the tag being read, emitted: its name, its attributes, what the tokenizer reads next */
static int html_emit_tag(html_parser *p, html_token_h tok, vector<char> *name, vector<char> *an, vector<char> *av,
                         vector<char> *last_start, int state_after, uint32_t line, uint32_t col) {
    html_commit_attr(p, tok, an, av, line, col);
    amb_span sp = bytes_add(p, data(*name), len(*name));
    p->token[tok.index].name = sp;
    if (p->token[tok.index].kind == HTML_TOKEN_KIND_END_TAG) {
        if (p->token[tok.index].first_attr) html_err(p, HTML_ERROR_CODE_END_TAG_WITH_ATTRIBUTES, line, col);
        if (p->token[tok.index].self_closing) html_err(p, HTML_ERROR_CODE_END_TAG_WITH_TRAILING_SOLIDUS, line, col);
        return state_after;
    }
    clear(last_start);
    append_range(last_start, data(*name), len(*name));
    if (html_name_is(name, "title") || html_name_is(name, "textarea")) return S_RCDATA;
    if (html_name_is(name, "style") || html_name_is(name, "xmp") || html_name_is(name, "iframe") ||
        html_name_is(name, "noembed") || html_name_is(name, "noframes")) return S_RAWTEXT;
    if (html_name_is(name, "script")) return S_SCRIPT;
    if (html_name_is(name, "plaintext")) return S_PLAINTEXT;
    return state_after;
}

static html_token_h html_new_token(html_parser *p, html_token_kind kind, uint32_t line, uint32_t col) {
    html_token_h t = token_new(p);
    p->token[t.index].kind = kind;
    p->token[t.index].line = line;
    p->token[t.index].col = col;
    return t;
}

/* a comment or a doctype, finished */
static void html_emit_comment(html_parser *p, vector<char> *text, uint32_t line, uint32_t col) {
    amb_span sp = bytes_add(p, data(*text), len(*text));
    html_token_h t = html_new_token(p, HTML_TOKEN_KIND_COMMENT, line, col);
    p->token[t.index].data = sp;
    clear(text);
}

static void html_emit_doctype(html_parser *p, vector<char> *name, vector<char> *pub, vector<char> *sys, uint8_t ids,
                              int name_set, int quirks, uint32_t line, uint32_t col) {
    amb_span n = bytes_add(p, data(*name), len(*name));
    amb_span pu = bytes_add(p, data(*pub), len(*pub));
    amb_span sy = bytes_add(p, data(*sys), len(*sys));
    html_token_h t = html_new_token(p, HTML_TOKEN_KIND_DOCTYPE, line, col);
    p->token[t.index].name = n;
    p->token[t.index].public_id = pu;
    p->token[t.index].system_id = sy;
    p->token[t.index].ids = (uint8_t)(ids | (name_set ? 4 : 0));
    p->token[t.index].force_quirks = (uint8_t)quirks;
    clear(name);
    clear(pub);
    clear(sys);
}

/* a tag cut off by the end of the input: dropped, with the attributes read so far */
static void html_drop_token(html_parser *p, html_token_h tok) {
    uint32_t a = p->token[tok.index].first_attr;
    while (a) {
        uint32_t next = p->tattr[a].next;
        attributes_unlink(p, a);
        tattr_free(p, tattr_handle(p, a));
        a = next;
    }
    token_free(p, tok);
}

/* the input from i case-insensitively begins with w (ASCII) */
static int html_starts_ci(const char *in, size_t n, size_t i, const char *w) {
    size_t k = strlen(w);
    if (i + k > n) return 0;
    for (size_t j = 0; j < k; j++) if (html_lower(in[i + j]) != w[j]) return 0;
    return 1;
}

/* the tokenizer from a state of the fragment algorithm (13.4): 0 data, 1 RCDATA,
   2 RAWTEXT, 3 script data, 4 PLAINTEXT, last the context's name, the start tag
   an end tag must match there */
html_status html_tokenize_in(html_parser *p, const char *src, size_t src_len, int state0, const char *last, size_t last_len) {
    /* 13.2.3.5: CR LF and CR to LF */
    vector<char> norm;
    reserve(norm, src_len);
    for (size_t k = 0; k < src_len; k++) {
        if (src[k] == '\r') { push_back(norm, '\n'); if (k + 1 < src_len && src[k + 1] == '\n') k++; }
        else push_back(norm, src[k]);
    }
    vector<char> chars, tname, an, av, cdata, last_start, tmp, dname, dpub, dsys;
    uint32_t cline = 1, ccol = 1;          /* where the characters being gathered began */
    uint32_t tline = 1, tcol = 1;          /* where the token being read began */
    html_token_h tok = { 0, 0, 0 };
    uint8_t dids = 0;
    int dname_set = 0, dquirks = 0;
    int state = state0 == 1 ? S_RCDATA : state0 == 2 ? S_RAWTEXT : state0 == 3 ? S_SCRIPT : state0 == 4 ? S_PLAINTEXT : S_DATA;
    /* in a fragment no end tag is appropriate until this tokenizer emits a start
       tag (13.2.5, the appropriate end tag token): the context's name is not one */
    (void)last;
    (void)last_len;
    size_t n = len(norm);
    size_t i = 0;
    uint32_t line = 1, col = 1;
    for (;;) {
        const char *in = data(norm);
        int c = i < n ? (unsigned char)in[i] : -1;
        /* consume: c is read, and the cursor moves past it */
#define ADV() do { if (i < n) { if (in[i] == '\n') { line++; col = 1; } else col++; i++; } } while (0)
#define CHAR(x) do { if (len(chars) == 0) { cline = line; ccol = col; } push_back(chars, (char)(x)); } while (0)
#define ERR(code) html_err(p, HTML_ERROR_CODE_##code, line, col)
        switch (state) {
        case S_DATA:
            if (c == '&') {
                if (len(chars) == 0) { cline = line; ccol = col; }
                size_t k = html_charref(p, in, n, i, 0, &chars, line, col);
                for (size_t m = 0; m < k; m++) ADV();
            } else if (c == '<') { tline = line; tcol = col; ADV(); state = S_TAG_OPEN; }
            else if (c == 0) { ERR(UNEXPECTED_NULL_CHARACTER); ADV(); }
            else if (c == -1) {
                html_flush_chars(p, &chars, cline, ccol);
                html_new_token(p, HTML_TOKEN_KIND_EOF, line, col);
                return HTML_OK;
            } else { CHAR(c); ADV(); }
            break;
        case S_RCDATA:
        case S_RAWTEXT:
        case S_SCRIPT:
        case S_PLAINTEXT:
            if (c == '&' && state == S_RCDATA) {
                if (len(chars) == 0) { cline = line; ccol = col; }
                size_t k = html_charref(p, in, n, i, 0, &chars, line, col);
                for (size_t m = 0; m < k; m++) ADV();
            } else if (c == '<' && state != S_PLAINTEXT) {
                tline = line; tcol = col; ADV();
                state = state == S_RCDATA ? S_RCDATA_LT : state == S_RAWTEXT ? S_RAWTEXT_LT : S_SCRIPT_LT;
            } else if (c == 0) {
                ERR(UNEXPECTED_NULL_CHARACTER); ADV();
                if (len(chars) == 0) { cline = line; ccol = col; }
                html_put_fffd(&chars);
            } else if (c == -1) {
                html_flush_chars(p, &chars, cline, ccol);
                html_new_token(p, HTML_TOKEN_KIND_EOF, line, col);
                return HTML_OK;
            } else { CHAR(c); ADV(); }
            break;
        case S_TAG_OPEN:
            if (c == '!') { ADV(); state = S_MARKUP_DECL; }
            else if (c == '/') { ADV(); state = S_END_TAG_OPEN; }
            else if (html_alpha(c)) {
                html_flush_chars(p, &chars, cline, ccol);
                tok = html_new_token(p, HTML_TOKEN_KIND_START_TAG, tline, tcol);
                clear(tname);
                state = S_TAG_NAME;
            } else if (c == '?') {
                ERR(UNEXPECTED_QUESTION_MARK_INSTEAD_OF_TAG_NAME);
                html_flush_chars(p, &chars, cline, ccol);
                clear(cdata);
                state = S_BOGUS_COMMENT;
            } else if (c == -1) {
                ERR(EOF_BEFORE_TAG_NAME);
                CHAR('<');
                state = S_DATA;
            } else {
                ERR(INVALID_FIRST_CHARACTER_OF_TAG_NAME);
                CHAR('<');
                state = S_DATA;
            }
            break;
        case S_END_TAG_OPEN:
            if (html_alpha(c)) {
                html_flush_chars(p, &chars, cline, ccol);
                tok = html_new_token(p, HTML_TOKEN_KIND_END_TAG, tline, tcol);
                clear(tname);
                state = S_TAG_NAME;
            } else if (c == '>') { ERR(MISSING_END_TAG_NAME); ADV(); state = S_DATA; }
            else if (c == -1) { ERR(EOF_BEFORE_TAG_NAME); CHAR('<'); CHAR('/'); state = S_DATA; }
            else {
                ERR(INVALID_FIRST_CHARACTER_OF_TAG_NAME);
                html_flush_chars(p, &chars, cline, ccol);
                clear(cdata);
                state = S_BOGUS_COMMENT;
            }
            break;
        case S_TAG_NAME:
            if (html_ws(c)) { ADV(); state = S_BEFORE_ATTR_NAME; }
            else if (c == '/') { ADV(); state = S_SELF_CLOSING; }
            else if (c == '>') { ADV(); state = html_emit_tag(p, tok, &tname, &an, &av, &last_start, S_DATA, tline, tcol); }
            else if (c == 0) { ERR(UNEXPECTED_NULL_CHARACTER); ADV(); html_put_fffd(&tname); }
            else if (c == -1) { ERR(EOF_IN_TAG); html_drop_token(p, tok); html_new_token(p, HTML_TOKEN_KIND_EOF, line, col); return HTML_OK; }
            else { push_back(tname, (char)html_lower(c)); ADV(); }
            break;
        case S_RCDATA_LT:
        case S_RAWTEXT_LT:
        case S_SCRIPT_LT: {
            int base = state == S_RCDATA_LT ? S_RCDATA : state == S_RAWTEXT_LT ? S_RAWTEXT : S_SCRIPT;
            if (c == '/') {
                ADV(); clear(tmp);
                state = base == S_RCDATA ? S_RCDATA_END_OPEN : base == S_RAWTEXT ? S_RAWTEXT_END_OPEN : S_SCRIPT_END_OPEN;
            } else if (base == S_SCRIPT && c == '!') {
                ADV(); CHAR('<'); CHAR('!'); state = S_SCRIPT_ESC_START;
            } else { CHAR('<'); state = base; }
            break;
        }
        case S_RCDATA_END_OPEN:
        case S_RAWTEXT_END_OPEN:
        case S_SCRIPT_END_OPEN:
        case S_SCRIPT_ESC_END_OPEN: {
            int base = state == S_RCDATA_END_OPEN ? S_RCDATA : state == S_RAWTEXT_END_OPEN ? S_RAWTEXT :
                       state == S_SCRIPT_END_OPEN ? S_SCRIPT : S_SCRIPT_ESC;
            if (html_alpha(c)) {
                clear(tname);
                state = base == S_RCDATA ? S_RCDATA_END_NAME : base == S_RAWTEXT ? S_RAWTEXT_END_NAME :
                        base == S_SCRIPT ? S_SCRIPT_END_NAME : S_SCRIPT_ESC_END_NAME;
            } else { CHAR('<'); CHAR('/'); state = base; }
            break;
        }
        case S_RCDATA_END_NAME:
        case S_RAWTEXT_END_NAME:
        case S_SCRIPT_END_NAME:
        case S_SCRIPT_ESC_END_NAME: {
            int base = state == S_RCDATA_END_NAME ? S_RCDATA : state == S_RAWTEXT_END_NAME ? S_RAWTEXT :
                       state == S_SCRIPT_END_NAME ? S_SCRIPT : S_SCRIPT_ESC;
            int appropriate = len(tname) == len(last_start) && memcmp(data(tname), data(last_start), len(tname)) == 0;
            if (appropriate && (html_ws(c) || c == '/' || c == '>')) {
                html_flush_chars(p, &chars, cline, ccol);
                tok = html_new_token(p, HTML_TOKEN_KIND_END_TAG, tline, tcol);
                if (html_ws(c)) { ADV(); state = S_BEFORE_ATTR_NAME; }
                else if (c == '/') { ADV(); state = S_SELF_CLOSING; }
                else { ADV(); state = html_emit_tag(p, tok, &tname, &an, &av, &last_start, S_DATA, tline, tcol); }
            } else if (html_alpha(c)) {
                push_back(tname, (char)html_lower(c)); push_back(tmp, (char)c); ADV();
            } else {
                CHAR('<'); CHAR('/');
                for (size_t k = 0; k < len(tmp); k++) CHAR(at(tmp, k));
                state = base;
            }
            break;
        }
        case S_SCRIPT_ESC_START:
        case S_SCRIPT_ESC_START_DASH:
            if (c == '-') { ADV(); CHAR('-'); state = state == S_SCRIPT_ESC_START ? S_SCRIPT_ESC_START_DASH : S_SCRIPT_ESC_DASH_DASH; }
            else state = S_SCRIPT;
            break;
        case S_SCRIPT_ESC:
        case S_SCRIPT_ESC_DASH:
        case S_SCRIPT_ESC_DASH_DASH:
            if (c == '-') {
                ADV(); CHAR('-');
                state = state == S_SCRIPT_ESC ? S_SCRIPT_ESC_DASH : S_SCRIPT_ESC_DASH_DASH;
            } else if (c == '<') { tline = line; tcol = col; ADV(); state = S_SCRIPT_ESC_LT; }
            else if (c == '>' && state == S_SCRIPT_ESC_DASH_DASH) { ADV(); CHAR('>'); state = S_SCRIPT; }
            else if (c == 0) { ERR(UNEXPECTED_NULL_CHARACTER); ADV(); html_put_fffd(&chars); state = S_SCRIPT_ESC; }
            else if (c == -1) {
                ERR(EOF_IN_SCRIPT_HTML_COMMENT_LIKE_TEXT);
                html_flush_chars(p, &chars, cline, ccol);
                html_new_token(p, HTML_TOKEN_KIND_EOF, line, col);
                return HTML_OK;
            } else { CHAR(c); ADV(); state = S_SCRIPT_ESC; }
            break;
        case S_SCRIPT_ESC_LT:
            if (c == '/') { ADV(); clear(tmp); state = S_SCRIPT_ESC_END_OPEN; }
            else if (html_alpha(c)) { clear(tmp); CHAR('<'); state = S_SCRIPT_DESC_START; }
            else { CHAR('<'); state = S_SCRIPT_ESC; }
            break;
        case S_SCRIPT_DESC_START:
        case S_SCRIPT_DESC_END:
            if (html_ws(c) || c == '/' || c == '>') {
                int is_script = len(tmp) == 6 && memcmp(data(tmp), "script", 6) == 0;
                CHAR(c); ADV();
                if (state == S_SCRIPT_DESC_START) state = is_script ? S_SCRIPT_DESC : S_SCRIPT_ESC;
                else state = is_script ? S_SCRIPT_ESC : S_SCRIPT_DESC;
            } else if (html_alpha(c)) { push_back(tmp, (char)html_lower(c)); CHAR(c); ADV(); }
            else state = state == S_SCRIPT_DESC_START ? S_SCRIPT_ESC : S_SCRIPT_DESC;
            break;
        case S_SCRIPT_DESC:
        case S_SCRIPT_DESC_DASH:
        case S_SCRIPT_DESC_DASH_DASH:
            if (c == '-') {
                ADV(); CHAR('-');
                state = state == S_SCRIPT_DESC ? S_SCRIPT_DESC_DASH : S_SCRIPT_DESC_DASH_DASH;
            } else if (c == '<') { ADV(); CHAR('<'); state = S_SCRIPT_DESC_LT; }
            else if (c == '>' && state == S_SCRIPT_DESC_DASH_DASH) { ADV(); CHAR('>'); state = S_SCRIPT; }
            else if (c == 0) { ERR(UNEXPECTED_NULL_CHARACTER); ADV(); html_put_fffd(&chars); state = S_SCRIPT_DESC; }
            else if (c == -1) {
                ERR(EOF_IN_SCRIPT_HTML_COMMENT_LIKE_TEXT);
                html_flush_chars(p, &chars, cline, ccol);
                html_new_token(p, HTML_TOKEN_KIND_EOF, line, col);
                return HTML_OK;
            } else { CHAR(c); ADV(); state = S_SCRIPT_DESC; }
            break;
        case S_SCRIPT_DESC_LT:
            if (c == '/') { ADV(); CHAR('/'); clear(tmp); state = S_SCRIPT_DESC_END; }
            else state = S_SCRIPT_DESC;
            break;
        case S_BEFORE_ATTR_NAME:
            if (html_ws(c)) ADV();
            else if (c == '/' || c == '>' || c == -1) state = S_AFTER_ATTR_NAME;
            else if (c == '=') {
                ERR(UNEXPECTED_EQUALS_SIGN_BEFORE_ATTRIBUTE_NAME);
                html_commit_attr(p, tok, &an, &av, line, col);
                push_back(an, '='); ADV(); state = S_ATTR_NAME;
            } else { html_commit_attr(p, tok, &an, &av, line, col); state = S_ATTR_NAME; }
            break;
        case S_ATTR_NAME:
            if (html_ws(c) || c == '/' || c == '>' || c == -1) state = S_AFTER_ATTR_NAME;
            else if (c == '=') { ADV(); state = S_BEFORE_ATTR_VALUE; }
            else if (c == 0) { ERR(UNEXPECTED_NULL_CHARACTER); ADV(); html_put_fffd(&an); }
            else {
                if (c == '"' || c == '\'' || c == '<') ERR(UNEXPECTED_CHARACTER_IN_ATTRIBUTE_NAME);
                push_back(an, (char)html_lower(c)); ADV();
            }
            break;
        case S_AFTER_ATTR_NAME:
            if (html_ws(c)) ADV();
            else if (c == '/') { ADV(); state = S_SELF_CLOSING; }
            else if (c == '=') { ADV(); state = S_BEFORE_ATTR_VALUE; }
            else if (c == '>') { ADV(); state = html_emit_tag(p, tok, &tname, &an, &av, &last_start, S_DATA, tline, tcol); }
            else if (c == -1) { ERR(EOF_IN_TAG); html_drop_token(p, tok); html_new_token(p, HTML_TOKEN_KIND_EOF, line, col); return HTML_OK; }
            else { html_commit_attr(p, tok, &an, &av, line, col); state = S_ATTR_NAME; }
            break;
        case S_BEFORE_ATTR_VALUE:
            if (html_ws(c)) ADV();
            else if (c == '"') { ADV(); state = S_ATTR_VALUE_DQ; }
            else if (c == '\'') { ADV(); state = S_ATTR_VALUE_SQ; }
            else if (c == '>') { ERR(MISSING_ATTRIBUTE_VALUE); ADV(); state = html_emit_tag(p, tok, &tname, &an, &av, &last_start, S_DATA, tline, tcol); }
            else state = S_ATTR_VALUE_UNQ;
            break;
        case S_ATTR_VALUE_DQ:
        case S_ATTR_VALUE_SQ: {
            int q = state == S_ATTR_VALUE_DQ ? '"' : '\'';
            if (c == q) { ADV(); state = S_AFTER_ATTR_VALUE_Q; }
            else if (c == '&') {
                size_t k = html_charref(p, in, n, i, 1, &av, line, col);
                for (size_t m = 0; m < k; m++) ADV();
            } else if (c == 0) { ERR(UNEXPECTED_NULL_CHARACTER); ADV(); html_put_fffd(&av); }
            else if (c == -1) { ERR(EOF_IN_TAG); html_drop_token(p, tok); html_new_token(p, HTML_TOKEN_KIND_EOF, line, col); return HTML_OK; }
            else { push_back(av, (char)c); ADV(); }
            break;
        }
        case S_ATTR_VALUE_UNQ:
            if (html_ws(c)) { ADV(); state = S_BEFORE_ATTR_NAME; }
            else if (c == '&') {
                size_t k = html_charref(p, in, n, i, 1, &av, line, col);
                for (size_t m = 0; m < k; m++) ADV();
            } else if (c == '>') { ADV(); state = html_emit_tag(p, tok, &tname, &an, &av, &last_start, S_DATA, tline, tcol); }
            else if (c == 0) { ERR(UNEXPECTED_NULL_CHARACTER); ADV(); html_put_fffd(&av); }
            else if (c == -1) { ERR(EOF_IN_TAG); html_drop_token(p, tok); html_new_token(p, HTML_TOKEN_KIND_EOF, line, col); return HTML_OK; }
            else {
                if (c == '"' || c == '\'' || c == '<' || c == '=' || c == '`') ERR(UNEXPECTED_CHARACTER_IN_UNQUOTED_ATTRIBUTE_VALUE);
                push_back(av, (char)c); ADV();
            }
            break;
        case S_AFTER_ATTR_VALUE_Q:
            if (html_ws(c)) { ADV(); state = S_BEFORE_ATTR_NAME; }
            else if (c == '/') { ADV(); state = S_SELF_CLOSING; }
            else if (c == '>') { ADV(); state = html_emit_tag(p, tok, &tname, &an, &av, &last_start, S_DATA, tline, tcol); }
            else if (c == -1) { ERR(EOF_IN_TAG); html_drop_token(p, tok); html_new_token(p, HTML_TOKEN_KIND_EOF, line, col); return HTML_OK; }
            else { ERR(MISSING_WHITESPACE_BETWEEN_ATTRIBUTES); state = S_BEFORE_ATTR_NAME; }
            break;
        case S_SELF_CLOSING:
            if (c == '>') { ADV(); p->token[tok.index].self_closing = 1; state = html_emit_tag(p, tok, &tname, &an, &av, &last_start, S_DATA, tline, tcol); }
            else if (c == -1) { ERR(EOF_IN_TAG); html_drop_token(p, tok); html_new_token(p, HTML_TOKEN_KIND_EOF, line, col); return HTML_OK; }
            else { ERR(UNEXPECTED_SOLIDUS_IN_TAG); state = S_BEFORE_ATTR_NAME; }
            break;
        case S_BOGUS_COMMENT:
            if (c == '>') { ADV(); html_emit_comment(p, &cdata, tline, tcol); state = S_DATA; }
            else if (c == -1) { html_emit_comment(p, &cdata, tline, tcol); state = S_DATA; }
            else if (c == 0) { ERR(UNEXPECTED_NULL_CHARACTER); ADV(); html_put_fffd(&cdata); }
            else { push_back(cdata, (char)c); ADV(); }
            break;
        case S_MARKUP_DECL:
            html_flush_chars(p, &chars, cline, ccol);
            clear(cdata);
            if (i + 1 < n && in[i] == '-' && in[i + 1] == '-') { ADV(); ADV(); state = S_COMMENT_START; }
            else if (html_starts_ci(in, n, i, "doctype")) { for (int k = 0; k < 7; k++) ADV(); state = S_DOCTYPE; }
            else if (i + 7 <= n && memcmp(in + i, "[CDATA[", 7) == 0) {
                ERR(CDATA_IN_HTML_CONTENT);
                for (int k = 0; k < 7; k++) { push_back(cdata, in[i]); ADV(); }
                state = S_BOGUS_COMMENT;
            } else { ERR(INCORRECTLY_OPENED_COMMENT); state = S_BOGUS_COMMENT; }
            break;
        case S_COMMENT_START:
            if (c == '-') { ADV(); state = S_COMMENT_START_DASH; }
            else if (c == '>') { ERR(ABRUPT_CLOSING_OF_EMPTY_COMMENT); ADV(); html_emit_comment(p, &cdata, tline, tcol); state = S_DATA; }
            else state = S_COMMENT;
            break;
        case S_COMMENT_START_DASH:
            if (c == '-') { ADV(); state = S_COMMENT_END; }
            else if (c == '>') { ERR(ABRUPT_CLOSING_OF_EMPTY_COMMENT); ADV(); html_emit_comment(p, &cdata, tline, tcol); state = S_DATA; }
            else if (c == -1) { ERR(EOF_IN_COMMENT); html_emit_comment(p, &cdata, tline, tcol); state = S_DATA; }
            else { push_back(cdata, '-'); state = S_COMMENT; }
            break;
        case S_COMMENT:
            if (c == '<') { ADV(); push_back(cdata, '<'); state = S_COMMENT_LT; }
            else if (c == '-') { ADV(); state = S_COMMENT_END_DASH; }
            else if (c == 0) { ERR(UNEXPECTED_NULL_CHARACTER); ADV(); html_put_fffd(&cdata); }
            else if (c == -1) { ERR(EOF_IN_COMMENT); html_emit_comment(p, &cdata, tline, tcol); state = S_DATA; }
            else { push_back(cdata, (char)c); ADV(); }
            break;
        case S_COMMENT_LT:
            if (c == '!') { ADV(); push_back(cdata, '!'); state = S_COMMENT_LT_BANG; }
            else if (c == '<') { ADV(); push_back(cdata, '<'); }
            else state = S_COMMENT;
            break;
        case S_COMMENT_LT_BANG:
            if (c == '-') { ADV(); state = S_COMMENT_LT_BANG_DASH; } else state = S_COMMENT;
            break;
        case S_COMMENT_LT_BANG_DASH:
            if (c == '-') { ADV(); state = S_COMMENT_LT_BANG_DASH_DASH; } else state = S_COMMENT_END_DASH;
            break;
        case S_COMMENT_LT_BANG_DASH_DASH:
            if (c != '>' && c != -1) ERR(NESTED_COMMENT);
            state = S_COMMENT_END;
            break;
        case S_COMMENT_END_DASH:
            if (c == '-') { ADV(); state = S_COMMENT_END; }
            else if (c == -1) { ERR(EOF_IN_COMMENT); html_emit_comment(p, &cdata, tline, tcol); state = S_DATA; }
            else { push_back(cdata, '-'); state = S_COMMENT; }
            break;
        case S_COMMENT_END:
            if (c == '>') { ADV(); html_emit_comment(p, &cdata, tline, tcol); state = S_DATA; }
            else if (c == '!') { ADV(); state = S_COMMENT_END_BANG; }
            else if (c == '-') { ADV(); push_back(cdata, '-'); }
            else if (c == -1) { ERR(EOF_IN_COMMENT); html_emit_comment(p, &cdata, tline, tcol); state = S_DATA; }
            else { push_back(cdata, '-'); push_back(cdata, '-'); state = S_COMMENT; }
            break;
        case S_COMMENT_END_BANG:
            if (c == '-') { ADV(); push_back(cdata, '-'); push_back(cdata, '-'); push_back(cdata, '!'); state = S_COMMENT_END_DASH; }
            else if (c == '>') { ERR(INCORRECTLY_CLOSED_COMMENT); ADV(); html_emit_comment(p, &cdata, tline, tcol); state = S_DATA; }
            else if (c == -1) { ERR(EOF_IN_COMMENT); html_emit_comment(p, &cdata, tline, tcol); state = S_DATA; }
            else { push_back(cdata, '-'); push_back(cdata, '-'); push_back(cdata, '!'); state = S_COMMENT; }
            break;
        case S_DOCTYPE:
            clear(dname); clear(dpub); clear(dsys); dids = 0; dname_set = 0; dquirks = 0;
            if (html_ws(c)) { ADV(); state = S_BEFORE_DOCTYPE_NAME; }
            else if (c == '>') state = S_BEFORE_DOCTYPE_NAME;
            else if (c == -1) { ERR(EOF_IN_DOCTYPE); html_emit_doctype(p, &dname, &dpub, &dsys, 0, 0, 1, tline, tcol); state = S_DATA; }
            else { ERR(MISSING_WHITESPACE_BEFORE_DOCTYPE_NAME); state = S_BEFORE_DOCTYPE_NAME; }
            break;
        case S_BEFORE_DOCTYPE_NAME:
            if (html_ws(c)) ADV();
            else if (c == '>') { ERR(MISSING_DOCTYPE_NAME); ADV(); html_emit_doctype(p, &dname, &dpub, &dsys, 0, 0, 1, tline, tcol); state = S_DATA; }
            else if (c == -1) { ERR(EOF_IN_DOCTYPE); html_emit_doctype(p, &dname, &dpub, &dsys, 0, 0, 1, tline, tcol); state = S_DATA; }
            else if (c == 0) { ERR(UNEXPECTED_NULL_CHARACTER); ADV(); html_put_fffd(&dname); dname_set = 1; state = S_DOCTYPE_NAME; }
            else { push_back(dname, (char)html_lower(c)); ADV(); dname_set = 1; state = S_DOCTYPE_NAME; }
            break;
        case S_DOCTYPE_NAME:
            if (html_ws(c)) { ADV(); state = S_AFTER_DOCTYPE_NAME; }
            else if (c == '>') { ADV(); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, dquirks, tline, tcol); state = S_DATA; }
            else if (c == 0) { ERR(UNEXPECTED_NULL_CHARACTER); ADV(); html_put_fffd(&dname); }
            else if (c == -1) { ERR(EOF_IN_DOCTYPE); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, 1, tline, tcol); state = S_DATA; }
            else { push_back(dname, (char)html_lower(c)); ADV(); }
            break;
        case S_AFTER_DOCTYPE_NAME:
            if (html_ws(c)) ADV();
            else if (c == '>') { ADV(); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, dquirks, tline, tcol); state = S_DATA; }
            else if (c == -1) { ERR(EOF_IN_DOCTYPE); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, 1, tline, tcol); state = S_DATA; }
            else if (html_starts_ci(in, n, i, "public")) { for (int k = 0; k < 6; k++) ADV(); state = S_AFTER_DOCTYPE_PUBLIC_KW; }
            else if (html_starts_ci(in, n, i, "system")) { for (int k = 0; k < 6; k++) ADV(); state = S_AFTER_DOCTYPE_SYSTEM_KW; }
            else { ERR(INVALID_CHARACTER_SEQUENCE_AFTER_DOCTYPE_NAME); dquirks = 1; state = S_BOGUS_DOCTYPE; }
            break;
        case S_AFTER_DOCTYPE_PUBLIC_KW:
        case S_BEFORE_DOCTYPE_PUBLIC_ID:
            if (html_ws(c)) { ADV(); state = S_BEFORE_DOCTYPE_PUBLIC_ID; }
            else if (c == '"' || c == '\'') {
                if (state == S_AFTER_DOCTYPE_PUBLIC_KW) ERR(MISSING_WHITESPACE_AFTER_DOCTYPE_PUBLIC_KEYWORD);
                ADV(); dids |= 1; state = c == '"' ? S_DOCTYPE_PUBLIC_ID_DQ : S_DOCTYPE_PUBLIC_ID_SQ;
            } else if (c == '>') { ERR(MISSING_DOCTYPE_PUBLIC_IDENTIFIER); ADV(); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, 1, tline, tcol); state = S_DATA; }
            else if (c == -1) { ERR(EOF_IN_DOCTYPE); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, 1, tline, tcol); state = S_DATA; }
            else { ERR(MISSING_QUOTE_BEFORE_DOCTYPE_PUBLIC_IDENTIFIER); dquirks = 1; state = S_BOGUS_DOCTYPE; }
            break;
        case S_DOCTYPE_PUBLIC_ID_DQ:
        case S_DOCTYPE_PUBLIC_ID_SQ:
        case S_DOCTYPE_SYSTEM_ID_DQ:
        case S_DOCTYPE_SYSTEM_ID_SQ: {
            int pub = state == S_DOCTYPE_PUBLIC_ID_DQ || state == S_DOCTYPE_PUBLIC_ID_SQ;
            int q = (state == S_DOCTYPE_PUBLIC_ID_DQ || state == S_DOCTYPE_SYSTEM_ID_DQ) ? '"' : '\'';
            if (c == q) { ADV(); state = pub ? S_AFTER_DOCTYPE_PUBLIC_ID : S_AFTER_DOCTYPE_SYSTEM_ID; }
            else if (c == 0) { ERR(UNEXPECTED_NULL_CHARACTER); ADV(); html_put_fffd(pub ? &dpub : &dsys); }
            else if (c == '>') {
                if (pub) ERR(ABRUPT_DOCTYPE_PUBLIC_IDENTIFIER); else ERR(ABRUPT_DOCTYPE_SYSTEM_IDENTIFIER);
                ADV(); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, 1, tline, tcol); state = S_DATA;
            } else if (c == -1) { ERR(EOF_IN_DOCTYPE); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, 1, tline, tcol); state = S_DATA; }
            else { if (pub) push_back(dpub, (char)c); else push_back(dsys, (char)c); ADV(); }
            break;
        }
        case S_AFTER_DOCTYPE_PUBLIC_ID:
        case S_BETWEEN_DOCTYPE_IDS:
            if (html_ws(c)) { ADV(); state = S_BETWEEN_DOCTYPE_IDS; }
            else if (c == '>') { ADV(); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, dquirks, tline, tcol); state = S_DATA; }
            else if (c == '"' || c == '\'') {
                if (state == S_AFTER_DOCTYPE_PUBLIC_ID) ERR(MISSING_WHITESPACE_BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS);
                ADV(); dids |= 2; state = c == '"' ? S_DOCTYPE_SYSTEM_ID_DQ : S_DOCTYPE_SYSTEM_ID_SQ;
            } else if (c == -1) { ERR(EOF_IN_DOCTYPE); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, 1, tline, tcol); state = S_DATA; }
            else { ERR(MISSING_QUOTE_BEFORE_DOCTYPE_SYSTEM_IDENTIFIER); dquirks = 1; state = S_BOGUS_DOCTYPE; }
            break;
        case S_AFTER_DOCTYPE_SYSTEM_KW:
        case S_BEFORE_DOCTYPE_SYSTEM_ID:
            if (html_ws(c)) { ADV(); state = S_BEFORE_DOCTYPE_SYSTEM_ID; }
            else if (c == '"' || c == '\'') {
                if (state == S_AFTER_DOCTYPE_SYSTEM_KW) ERR(MISSING_WHITESPACE_AFTER_DOCTYPE_SYSTEM_KEYWORD);
                ADV(); dids |= 2; state = c == '"' ? S_DOCTYPE_SYSTEM_ID_DQ : S_DOCTYPE_SYSTEM_ID_SQ;
            } else if (c == '>') { ERR(MISSING_DOCTYPE_SYSTEM_IDENTIFIER); ADV(); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, 1, tline, tcol); state = S_DATA; }
            else if (c == -1) { ERR(EOF_IN_DOCTYPE); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, 1, tline, tcol); state = S_DATA; }
            else { ERR(MISSING_QUOTE_BEFORE_DOCTYPE_SYSTEM_IDENTIFIER); dquirks = 1; state = S_BOGUS_DOCTYPE; }
            break;
        case S_AFTER_DOCTYPE_SYSTEM_ID:
            if (html_ws(c)) ADV();
            else if (c == '>') { ADV(); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, dquirks, tline, tcol); state = S_DATA; }
            else if (c == -1) { ERR(EOF_IN_DOCTYPE); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, 1, tline, tcol); state = S_DATA; }
            else { ERR(UNEXPECTED_CHARACTER_AFTER_DOCTYPE_SYSTEM_IDENTIFIER); state = S_BOGUS_DOCTYPE; }
            break;
        case S_BOGUS_DOCTYPE:
            if (c == '>') { ADV(); html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, dquirks, tline, tcol); state = S_DATA; }
            else if (c == -1) { html_emit_doctype(p, &dname, &dpub, &dsys, dids, dname_set, dquirks, tline, tcol); state = S_DATA; }
            else if (c == 0) { ERR(UNEXPECTED_NULL_CHARACTER); ADV(); }
            else ADV();
            break;
        default:
            state = S_DATA;
            break;
        }
#undef ADV
#undef CHAR
#undef ERR
    }
}
