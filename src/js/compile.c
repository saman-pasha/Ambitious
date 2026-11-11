/* src/js/compile.c -- a script's text into bytecode (src/js/js.h): a lexer, a parser to an
   AST in an index arena, the scopes resolved (each variable a frame slot, an environment
   cell when a closure captures it, or a global), and the code written function by
   function.

   The language: ES2020's statements and expressions, with classes (fields, accessors,
   static members, super), destructuring, spread and rest, default parameters, template
   literals, optional chaining and nullish coalescing, getters and setters, labels,
   for-in and for-of, generators (yield and yield*) and async functions (await), as
   functions, arrows and methods. Not read: async generators and for await, modules,
   with, BigInt and regular expressions' semantics (a literal makes a RegExp object
   whose methods throw), and direct eval. Each is a SyntaxError that names it. */
#include <stdlib.h>
#include <math.h>
#include "js.h"

/* ---- tokens ---- */

enum { T_EOF, T_NAME, T_NUM, T_STR, T_TEMPLATE, T_REGEX, T_PUNCT };
enum {
    P_LBRACE = 1, P_RBRACE, P_LPAREN, P_RPAREN, P_LBRACK, P_RBRACK, P_DOT, P_ELLIPSIS, P_SEMI, P_COMMA,
    P_LT, P_GT, P_LE, P_GE, P_EQ, P_NE, P_SEQ, P_SNE, P_PLUS, P_MINUS, P_STAR, P_SLASH, P_PERCENT, P_POW,
    P_INC, P_DEC, P_SHL, P_SAR, P_SHR, P_AMP, P_PIPE, P_CARET, P_BANG, P_TILDE, P_AND, P_OR, P_NULLISH,
    P_QUESTION, P_OPTCHAIN, P_COLON, P_ASSIGN, P_ADD_ASSIGN, P_SUB_ASSIGN, P_MUL_ASSIGN, P_DIV_ASSIGN,
    P_MOD_ASSIGN, P_POW_ASSIGN, P_SHL_ASSIGN, P_SAR_ASSIGN, P_SHR_ASSIGN, P_BAND_ASSIGN, P_BOR_ASSIGN,
    P_BXOR_ASSIGN, P_LAND_ASSIGN, P_LOR_ASSIGN, P_NULLISH_ASSIGN, P_ARROW, P_IN, P_INSTANCEOF
};

/* the keywords, by their atoms */
enum {
    K_BREAK, K_CASE, K_CATCH, K_CLASS, K_CONST, K_CONTINUE, K_DEBUGGER, K_DEFAULT, K_DELETE, K_DO, K_ELSE,
    K_EXPORT, K_EXTENDS, K_FALSE, K_FINALLY, K_FOR, K_FUNCTION, K_IF, K_IMPORT, K_IN, K_INSTANCEOF, K_LET,
    K_NEW, K_NULL, K_OF, K_RETURN, K_STATIC, K_SUPER, K_SWITCH, K_THIS, K_THROW, K_TRUE, K_TRY, K_TYPEOF,
    K_VAR, K_VOID, K_WHILE, K_WITH, K_YIELD, K_ASYNC, K_AWAIT, K_GET, K_SET, K_TARGET, K_CONSTRUCTOR,
    K_ARGUMENTS, K_SLICE, K_ENUM, KW_N
};
static const char *const js_kw_names[KW_N] = {
    "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else",
    "export", "extends", "false", "finally", "for", "function", "if", "import", "in", "instanceof", "let",
    "new", "null", "of", "return", "static", "super", "switch", "this", "throw", "true", "try", "typeof",
    "var", "void", "while", "with", "yield", "async", "await", "get", "set", "target", "constructor",
    "arguments", "slice", "enum" };

typedef struct jtok { uint32_t type, op, start, end, line, nl, str, aux; double num; } jtok;

/* ---- the AST ---- */

enum {
    N_NONE, N_NUM, N_STR, N_TEMPLATE, N_REGEX, N_IDENT, N_THIS, N_SUPER_MEMBER, N_SUPER_CALL, N_NEW_TARGET,
    N_ARRAY, N_OBJECT, N_PROP, N_FUNC, N_CLASS, N_UNARY, N_UPDATE, N_BINARY, N_LOGICAL, N_COND, N_ASSIGN,
    N_SEQ, N_MEMBER, N_CALL, N_NEW, N_SPREAD, N_HOLE, N_CHAIN, N_PATTERN_OBJ, N_PATTERN_ARR, N_PELEM,
    N_PARAM,
    N_VAR, N_DECL, N_EXPR, N_BLOCK, N_IF, N_FOR, N_FORIN, N_WHILE, N_DO, N_RETURN, N_BREAK, N_CONTINUE,
    N_THROW, N_TRY, N_SWITCH, N_CASE, N_LABEL, N_EMPTY, N_FUNCDECL, N_CLASSDECL,
    N_YIELD,           /* a its operand (0: none), op 1 for yield* */
    N_AWAIT            /* a its operand */
};
/* N_PROP's kinds */
enum { PK_INIT, PK_GET, PK_SET, PK_METHOD, PK_SPREAD, PK_FIELD, PK_COVER };
#define NF_COMPUTED 1      /* a member's or a property's key is an expression */
#define NF_OPTIONAL 2      /* ?. */
#define NF_STATIC 4
#define NF_PREFIX 8
#define NF_REST 16

typedef struct jn { uint16_t k, op; uint32_t a, b, c, d, line, s; double num; } jn;

enum { SC_SCRIPT, SC_FUNC, SC_BLOCK, SC_SELF };
typedef struct jscope { uint32_t parent, fn, kind, has_env, nenv, first; } jscope;
enum { D_VAR, D_LET, D_CONST, D_FUNC, D_PARAM, D_CLASS, D_SELF, D_ARGS };
typedef struct jdecl { uint32_t scope, atom, kind, captured, slot, next, fn, param, init, envslot; } jdecl;
typedef struct jref { uint32_t scope, atom, decl, fn; } jref;
#define FN_EXPR 0x10000            /* a concise arrow's body: an expression */
#define FN_FIELDS 0x20000          /* a class's field initializer */
#define FN_DEFAULT_CTOR 0x40000    /* a class's constructor, not written */
#define FN_PARAMS 0x80000          /* its parameters being read: no yield or await there */
typedef struct jfn {
    uint32_t params, nparams, body, scope, flags, name, line, src_start, src_end, parent, id, nslots, cls, args_decl, nformal;
} jfn;
typedef struct jclass { uint32_t name, extends, ctor, members, nmembers, fields_fn, line, decl, nfields; } jclass;

/* the code generator's open contexts, innermost last. A loop's label is 1 when its
   iterator is on the stack (for-in), 2 for a for-of's, whose CX_ITER is just below it:
   the handler that closes the iterator when an exception leaves the loop */
enum { CX_ENV, CX_TRY, CX_FINALLY, CX_ITER, CX_LOOP, CX_SWITCH, CX_LABEL };
typedef struct jctx { uint32_t kind, node, label, brk, cont, cont_pc, has_cont; } jctx;

typedef struct js_cc {
    vector<jn> nodes;
    vector<uint32_t> lists;
    vector<uint32_t> tmp;
    vector<jscope> scopes;
    vector<jdecl> decls;
    vector<jref> refs;
    vector<jfn> fns;
    vector<jclass> classes;
    vector<jctx> ctx;
    jtok tok, prev;
    uint32_t pos, line, cur_fn, cur_scope, script, gen_fn, nslots, chain, in_class, in_fields;
    uint32_t kw[KW_N];
    uint32_t depth;                   /* the parser's nesting, bounded (JS_NEST_MAX) */
    int err;
    uint32_t err_line;
    char errmsg[240];
} js_cc;

#define JS_CC_DROP(c) vector_jn_drop(&(c).nodes); vector_uint32_t_drop(&(c).lists); vector_uint32_t_drop(&(c).tmp); \
    vector_jscope_drop(&(c).scopes); vector_jdecl_drop(&(c).decls); vector_jref_drop(&(c).refs); vector_jfn_drop(&(c).fns); \
    vector_jclass_drop(&(c).classes); vector_jctx_drop(&(c).ctx)

#define CC js_vm *vm, const char *src, uint32_t srcn, js_cc *cc
#define CCA vm, src, srcn, cc

static void cc_error(js_cc *cc, uint32_t line, const char *m) {
    if (cc->err) return;
    cc->err = 1;
    cc->err_line = line;
    snprintf(cc->errmsg, sizeof cc->errmsg, "%s", m);
}

/* ---- the lexer ---- */

static int lx_idstart(uint8_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '$' || c == '_' || c >= 0x80 || c == '\\'; }
static int lx_idchar(uint8_t c) { return lx_idstart(c) || (c >= '0' && c <= '9'); }
static int lx_hex(uint8_t c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static void lx_push_cp(vector<uint16_t> *u, uint32_t cp) {
    if (cp >= 0x10000) {
        push_back(u, (uint16_t)(0xD800 + ((cp - 0x10000) >> 10)));
        push_back(u, (uint16_t)(0xDC00 + ((cp - 0x10000) & 0x3FF)));
    } else push_back(u, (uint16_t)cp);
}

/* one UTF-8 character at src[i]: its code point, its length in *k */
static uint32_t lx_utf8(const char *src, uint32_t srcn, uint32_t i, uint32_t *k) {
    uint32_t c = (uint8_t)src[i];
    *k = 1;
    if (c < 0x80) return c;
    if (c >= 0xC2 && c <= 0xDF && i + 1 < srcn) { *k = 2; return (c & 0x1F) << 6 | ((uint8_t)src[i + 1] & 0x3F); }
    if (c >= 0xE0 && c <= 0xEF && i + 2 < srcn) { *k = 3; return (c & 0x0F) << 12 | ((uint8_t)src[i + 1] & 0x3F) << 6 | ((uint8_t)src[i + 2] & 0x3F); }
    if (c >= 0xF0 && c <= 0xF4 && i + 3 < srcn) {
        *k = 4;
        return (c & 0x07) << 18 | ((uint8_t)src[i + 1] & 0x3F) << 12 | ((uint8_t)src[i + 2] & 0x3F) << 6 | ((uint8_t)src[i + 3] & 0x3F);
    }
    return 0xFFFD;
}

/* an escape after a backslash at src[i] (the character after it): into u; the index after it */
static uint32_t lx_escape(CC, uint32_t i, vector<uint16_t> *u) {
    if (i >= srcn) { cc_error(cc, cc->line, "an escape at the end of the script"); return i; }
    char c = src[i];
    if (c == 'n') { push_back(u, (uint16_t)'\n'); return i + 1; }
    if (c == 't') { push_back(u, (uint16_t)'\t'); return i + 1; }
    if (c == 'r') { push_back(u, (uint16_t)'\r'); return i + 1; }
    if (c == 'b') { push_back(u, (uint16_t)'\b'); return i + 1; }
    if (c == 'f') { push_back(u, (uint16_t)'\f'); return i + 1; }
    if (c == 'v') { push_back(u, (uint16_t)0x0B); return i + 1; }
    if (c == '0' && !(i + 1 < srcn && src[i + 1] >= '0' && src[i + 1] <= '9')) { push_back(u, (uint16_t)0); return i + 1; }
    if (c == 'x') {
        int h1 = i + 1 < srcn ? lx_hex((uint8_t)src[i + 1]) : -1, h2 = i + 2 < srcn ? lx_hex((uint8_t)src[i + 2]) : -1;
        if (h1 < 0 || h2 < 0) { cc_error(cc, cc->line, "an invalid \\x escape"); return i + 1; }
        push_back(u, (uint16_t)(h1 * 16 + h2));
        return i + 3;
    }
    if (c == 'u') {
        uint32_t cp = 0;
        if (i + 1 < srcn && src[i + 1] == '{') {
            uint32_t j = i + 2;
            while (j < srcn && src[j] != '}') {
                int h = lx_hex((uint8_t)src[j]);
                if (h < 0 || cp > 0x10FFFF) { cc_error(cc, cc->line, "an invalid \\u{} escape"); return j; }
                cp = cp * 16 + (uint32_t)h;
                j++;
            }
            if (cp > 0x10FFFF) { cc_error(cc, cc->line, "an invalid \\u{} escape"); return j; }
            lx_push_cp(u, cp);
            return j + 1;
        }
        for (uint32_t j = 1; j <= 4; j++) {
            int h = i + j < srcn ? lx_hex((uint8_t)src[i + j]) : -1;
            if (h < 0) { cc_error(cc, cc->line, "an invalid \\u escape"); return i + j; }
            cp = cp * 16 + (uint32_t)h;
        }
        push_back(u, (uint16_t)cp);
        return i + 5;
    }
    if (c == '\r') { if (i + 1 < srcn && src[i + 1] == '\n') i++; cc->line++; return i + 1; }
    if (c == '\n') { cc->line++; return i + 1; }
    uint32_t k;
    uint32_t cp = lx_utf8(src, srcn, i, &k);
    if (cp == 0x2028 || cp == 0x2029) return i + k;
    lx_push_cp(u, cp);
    return i + k;
}

/* a template's characters from i to its next ` or ${: the token's string; aux 1 when it ends the template */
static void lx_template(CC, uint32_t i) {
    jtok t;
    memset(&t, 0, sizeof t);
    t.type = T_TEMPLATE;
    t.start = i;
    t.line = cc->line;
    vector<uint16_t> u;
    for (;;) {
        if (i >= srcn) { cc_error(cc, t.line, "an unterminated template literal"); break; }
        char c = src[i];
        if (c == '`') { t.aux = 1; i++; break; }
        if (c == '$' && i + 1 < srcn && src[i + 1] == '{') { t.aux = 0; i += 2; break; }
        if (c == '\\') { i = lx_escape(CCA, i + 1, &u); continue; }
        if (c == '\r') { push_back(u, (uint16_t)'\n'); if (i + 1 < srcn && src[i + 1] == '\n') i++; cc->line++; i++; continue; }
        if (c == '\n') cc->line++;
        uint32_t k;
        uint32_t cp = lx_utf8(src, srcn, i, &k);
        lx_push_cp(&u, cp);
        i += k;
    }
    t.str = js_str_new(vm, data(u), len(u));
    t.end = i;
    cc->prev = cc->tok;
    cc->tok = t;
    cc->pos = i;
}

static void lx(CC) {
    cc->prev = cc->tok;
    jtok t;
    memset(&t, 0, sizeof t);
    uint32_t i = cc->pos;
    int nl = 0;
    while (i < srcn) {
        uint8_t c = (uint8_t)src[i];
        if (c == '\n') { nl = 1; cc->line++; i++; }
        else if (c == ' ' || c == '\t' || c == '\r' || c == 0x0B || c == 0x0C) i++;
        else if (c == 0xC2 && i + 1 < srcn && (uint8_t)src[i + 1] == 0xA0) i += 2;
        else if (c == 0xEF && i + 2 < srcn && (uint8_t)src[i + 1] == 0xBB && (uint8_t)src[i + 2] == 0xBF) i += 3;
        else if (c == 0xE2 && i + 2 < srcn && (uint8_t)src[i + 1] == 0x80 && ((uint8_t)src[i + 2] == 0xA8 || (uint8_t)src[i + 2] == 0xA9)) { nl = 1; cc->line++; i += 3; }
        else if (c == '/' && i + 1 < srcn && src[i + 1] == '/') { while (i < srcn && src[i] != '\n') i++; }
        else if (c == '/' && i + 1 < srcn && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < srcn && !(src[i] == '*' && src[i + 1] == '/')) { if (src[i] == '\n') { nl = 1; cc->line++; } i++; }
            if (i + 1 >= srcn) { cc_error(cc, cc->line, "an unterminated comment"); i = srcn; break; }
            i += 2;
        } else if (c == '<' && i + 3 < srcn && src[i + 1] == '!' && src[i + 2] == '-' && src[i + 3] == '-') { while (i < srcn && src[i] != '\n') i++; }
        else if (c == '-' && (nl || cc->tok.type == T_EOF) && i + 2 < srcn && src[i + 1] == '-' && src[i + 2] == '>') { while (i < srcn && src[i] != '\n') i++; }
        else break;
    }
    t.start = i;
    t.line = cc->line;
    t.nl = (uint32_t)nl;
    if (i >= srcn || cc->err) {
        t.type = T_EOF;
        t.end = i;
        cc->tok = t;
        cc->pos = i;
        return;
    }
    uint8_t c = (uint8_t)src[i];
    if (lx_idstart(c) || c == '#') {
        /* a name: its characters (escapes read) as UTF-8, then its atom */
        vector<char> b;
        if (c == '#') { push_back(b, '#'); i++; }
        while (i < srcn && lx_idchar((uint8_t)src[i])) {
            if (src[i] == '\\') {
                vector<uint16_t> u;
                if (i + 1 >= srcn || src[i + 1] != 'u') { cc_error(cc, cc->line, "an invalid escape in a name"); break; }
                i = lx_escape(CCA, i + 1, &u);
                for (size_t q = 0; q < len(u); q++) {
                    uint32_t cp = at(u, q);
                    if (cp < 0x80) push_back(b, (char)cp);
                    else if (cp < 0x800) { push_back(b, (char)(0xC0 | cp >> 6)); push_back(b, (char)(0x80 | (cp & 0x3F))); }
                    else { push_back(b, (char)(0xE0 | cp >> 12)); push_back(b, (char)(0x80 | (cp >> 6 & 0x3F))); push_back(b, (char)(0x80 | (cp & 0x3F))); }
                }
                t.aux = 1;
                continue;
            }
            if ((uint8_t)src[i] >= 0x80) {
                uint32_t k;
                uint32_t cp = lx_utf8(src, srcn, i, &k);
                if (cp == 0xA0 || cp == 0x2028 || cp == 0x2029 || cp == 0xFEFF || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x3000) break;
                for (uint32_t q = 0; q < k; q++) push_back(b, src[i + q]);
                i += k;
                continue;
            }
            push_back(b, src[i]);
            i++;
        }
        t.type = T_NAME;
        t.str = js_atom(vm, js_str_utf8(vm, data(b), len(b)));
    } else if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < srcn && src[i + 1] >= '0' && src[i + 1] <= '9')) {
        t.type = T_NUM;
        if (c == '0' && i + 1 < srcn && (src[i + 1] == 'x' || src[i + 1] == 'X' || src[i + 1] == 'o' || src[i + 1] == 'O' || src[i + 1] == 'b' || src[i + 1] == 'B')) {
            int base = src[i + 1] == 'x' || src[i + 1] == 'X' ? 16 : src[i + 1] == 'o' || src[i + 1] == 'O' ? 8 : 2;
            double v = 0;
            int digits = 0;
            i += 2;
            while (i < srcn) {
                if (src[i] == '_') { i++; continue; }
                int d = lx_hex((uint8_t)src[i]);
                if (d < 0 || d >= base) break;
                v = v * base + d;
                digits++;
                i++;
            }
            if (!digits) cc_error(cc, cc->line, "a number with no digits");
            t.num = v;
        } else {
            char nb[400];
            uint32_t k = 0;
            int legacy_octal = c == '0' && i + 1 < srcn && src[i + 1] >= '0' && src[i + 1] <= '7';
            while (i < srcn && k + 2 < sizeof nb) {
                char d = src[i];
                if (d == '_') { i++; continue; }
                if ((d >= '0' && d <= '9') || d == '.') { nb[k++] = d; i++; continue; }
                if ((d == 'e' || d == 'E') && !legacy_octal) {
                    nb[k++] = d;
                    i++;
                    if (i < srcn && (src[i] == '+' || src[i] == '-')) nb[k++] = src[i++];
                    continue;
                }
                break;
            }
            nb[k] = 0;
            if (legacy_octal) {
                double v = 0;
                for (uint32_t q = 0; q < k; q++) v = v * 8 + (nb[q] - '0');
                t.num = v;
            } else t.num = strtod(nb, 0);
        }
        if (i < srcn && src[i] == 'n') cc_error(cc, cc->line, "BigInt literals are not supported");
        else if (i < srcn && lx_idstart((uint8_t)src[i])) cc_error(cc, cc->line, "an identifier starts right after a number");
    } else if (c == '"' || c == '\'') {
        vector<uint16_t> u;
        uint32_t j = i + 1;
        for (;;) {
            if (j >= srcn || src[j] == '\n') { cc_error(cc, cc->line, "an unterminated string"); break; }
            char d = src[j];
            if (d == (char)c) { j++; break; }
            if (d == '\\') { j = lx_escape(CCA, j + 1, &u); continue; }
            uint32_t k;
            uint32_t cp = lx_utf8(src, srcn, j, &k);
            lx_push_cp(&u, cp);
            j += k;
        }
        t.type = T_STR;
        t.str = js_str_new(vm, data(u), len(u));
        i = j;
    } else if (c == '`') {
        cc->pos = i + 1;
        uint32_t ln = cc->line;
        lx_template(CCA, i + 1);
        cc->tok.nl = (uint32_t)nl;
        cc->tok.line = ln;
        return;
    } else {
        t.type = T_PUNCT;
        char c1 = i + 1 < srcn ? src[i + 1] : 0, c2 = i + 2 < srcn ? src[i + 2] : 0, c3 = i + 3 < srcn ? src[i + 3] : 0;
        uint32_t op = 0, n = 1;
        switch (c) {
        case '{': op = P_LBRACE; break;
        case '}': op = P_RBRACE; break;
        case '(': op = P_LPAREN; break;
        case ')': op = P_RPAREN; break;
        case '[': op = P_LBRACK; break;
        case ']': op = P_RBRACK; break;
        case ';': op = P_SEMI; break;
        case ',': op = P_COMMA; break;
        case ':': op = P_COLON; break;
        case '~': op = P_TILDE; break;
        case '.': if (c1 == '.' && c2 == '.') { op = P_ELLIPSIS; n = 3; } else op = P_DOT; break;
        case '<':
            if (c1 == '<' && c2 == '=') { op = P_SHL_ASSIGN; n = 3; } else if (c1 == '<') { op = P_SHL; n = 2; }
            else if (c1 == '=') { op = P_LE; n = 2; } else op = P_LT;
            break;
        case '>':
            if (c1 == '>' && c2 == '>' && c3 == '=') { op = P_SHR_ASSIGN; n = 4; } else if (c1 == '>' && c2 == '>') { op = P_SHR; n = 3; }
            else if (c1 == '>' && c2 == '=') { op = P_SAR_ASSIGN; n = 3; } else if (c1 == '>') { op = P_SAR; n = 2; }
            else if (c1 == '=') { op = P_GE; n = 2; } else op = P_GT;
            break;
        case '=':
            if (c1 == '=' && c2 == '=') { op = P_SEQ; n = 3; } else if (c1 == '=') { op = P_EQ; n = 2; }
            else if (c1 == '>') { op = P_ARROW; n = 2; } else op = P_ASSIGN;
            break;
        case '!': if (c1 == '=' && c2 == '=') { op = P_SNE; n = 3; } else if (c1 == '=') { op = P_NE; n = 2; } else op = P_BANG; break;
        case '+': if (c1 == '+') { op = P_INC; n = 2; } else if (c1 == '=') { op = P_ADD_ASSIGN; n = 2; } else op = P_PLUS; break;
        case '-': if (c1 == '-') { op = P_DEC; n = 2; } else if (c1 == '=') { op = P_SUB_ASSIGN; n = 2; } else op = P_MINUS; break;
        case '*':
            if (c1 == '*' && c2 == '=') { op = P_POW_ASSIGN; n = 3; } else if (c1 == '*') { op = P_POW; n = 2; }
            else if (c1 == '=') { op = P_MUL_ASSIGN; n = 2; } else op = P_STAR;
            break;
        case '/': if (c1 == '=') { op = P_DIV_ASSIGN; n = 2; } else op = P_SLASH; break;
        case '%': if (c1 == '=') { op = P_MOD_ASSIGN; n = 2; } else op = P_PERCENT; break;
        case '&': if (c1 == '&' && c2 == '=') { op = P_LAND_ASSIGN; n = 3; } else if (c1 == '&') { op = P_AND; n = 2; } else if (c1 == '=') { op = P_BAND_ASSIGN; n = 2; } else op = P_AMP; break;
        case '|': if (c1 == '|' && c2 == '=') { op = P_LOR_ASSIGN; n = 3; } else if (c1 == '|') { op = P_OR; n = 2; } else if (c1 == '=') { op = P_BOR_ASSIGN; n = 2; } else op = P_PIPE; break;
        case '^': if (c1 == '=') { op = P_BXOR_ASSIGN; n = 2; } else op = P_CARET; break;
        case '?':
            if (c1 == '?' && c2 == '=') { op = P_NULLISH_ASSIGN; n = 3; } else if (c1 == '?') { op = P_NULLISH; n = 2; }
            else if (c1 == '.' && !(c2 >= '0' && c2 <= '9')) { op = P_OPTCHAIN; n = 2; } else op = P_QUESTION;
            break;
        default: break;
        }
        if (!op) {
            char m[64];
            snprintf(m, sizeof m, "an unexpected character '%c'", c >= 32 && c < 127 ? c : '?');
            cc_error(cc, cc->line, m);
            t.type = T_EOF;
        }
        t.op = op;
        i += n;
    }
    t.end = i;
    cc->tok = t;
    cc->pos = i;
}

/* a regular expression literal, its slash at start: re-read as one */
static void lx_regex(CC, uint32_t start) {
    uint32_t i = start + 1;
    int cls = 0;
    for (;;) {
        if (i >= srcn || src[i] == '\n') { cc_error(cc, cc->line, "an unterminated regular expression"); break; }
        char c = src[i];
        if (c == '\\') { i += 2; continue; }
        if (c == '[') cls = 1;
        else if (c == ']') cls = 0;
        else if (c == '/' && !cls) break;
        i++;
    }
    uint32_t pend = i;
    i++;
    uint32_t fstart = i;
    while (i < srcn && lx_idchar((uint8_t)src[i])) i++;
    jtok t = cc->tok;
    t.type = T_REGEX;
    t.str = js_str_utf8(vm, src + start + 1, pend > start + 1 ? pend - start - 1 : 0);
    t.aux = js_str_utf8(vm, src + fstart, i - fstart);
    t.end = i;
    cc->tok = t;
    cc->pos = i;
}

/* ---- the parser ---- */

static int is_p(js_cc *cc, uint32_t op) { return cc->tok.type == T_PUNCT && cc->tok.op == op; }
static int is_kw(js_cc *cc, uint32_t k) { return cc->tok.type == T_NAME && cc->tok.str == cc->kw[k] && !cc->tok.aux; }

static void expect_p(CC, uint32_t op, const char *what) {
    if (is_p(cc, op)) { lx(CCA); return; }
    char m[120];
    snprintf(m, sizeof m, "expected %s", what);
    cc_error(cc, cc->tok.line, m);
}

/* the end of a statement: a semicolon, or one the grammar supplies (ASI) */
static void semi(CC) {
    if (is_p(cc, P_SEMI)) { lx(CCA); return; }
    if (is_p(cc, P_RBRACE) || cc->tok.type == T_EOF || cc->tok.nl) return;
    cc_error(cc, cc->tok.line, "expected ';' or a line break between two statements");
}

static uint32_t node(js_cc *cc, uint32_t k, uint32_t line) {
    jn n;
    memset(&n, 0, sizeof n);
    n.k = (uint16_t)k;
    n.line = line;
    push_back(cc->nodes, n);
    return (uint32_t)(len(cc->nodes) - 1);
}

/* a list built on the scratch stack from mark: into the lists, (offset, count) */
static void list_take(js_cc *cc, size_t mark, uint32_t *off, uint32_t *count) {
    *off = (uint32_t)len(cc->lists);
    *count = (uint32_t)(len(cc->tmp) - mark);
    for (size_t i = mark; i < len(cc->tmp); i++) push_back(cc->lists, at(cc->tmp, i));
    resize(cc->tmp, mark);
}

static uint32_t scope_new(js_cc *cc, uint32_t kind) {
    jscope s = { cc->cur_scope, cc->cur_fn, kind, 0, 0, 0 };
    push_back(cc->scopes, s);
    return (uint32_t)(len(cc->scopes) - 1);
}

/* the scope a var goes to: the function's (or the script's) */
static uint32_t var_scope(js_cc *cc, uint32_t s) {
    while (cc->scopes[s].kind != SC_FUNC && cc->scopes[s].kind != SC_SCRIPT) s = cc->scopes[s].parent;
    return s;
}

static uint32_t declare(CC, uint32_t atom, uint32_t kind, uint32_t line) {
    uint32_t sc = kind == D_VAR ? var_scope(cc, cc->cur_scope) : cc->cur_scope;
    for (uint32_t d = cc->scopes[sc].first; d; d = cc->decls[d].next) {
        if (cc->decls[d].atom != atom) continue;
        uint32_t dk = cc->decls[d].kind;
        if ((kind == D_VAR || kind == D_FUNC) && (dk == D_VAR || dk == D_FUNC || dk == D_PARAM)) {
            if (kind == D_FUNC) cc->decls[d].kind = dk == D_PARAM ? D_PARAM : D_FUNC;
            return d;
        }
        if (kind == D_PARAM && dk == D_PARAM) return d;
        vector<char> m;
        append_range(m, "Identifier '", 12);
        js_str_to_utf8(vm, atom, &m);
        append_range(m, "' has already been declared", 27);
        push_back(m, 0);
        cc_error(cc, line, data(m));
        return d;
    }
    jdecl d;
    memset(&d, 0, sizeof d);
    d.scope = sc;
    d.atom = atom;
    d.kind = kind;
    d.fn = cc->scopes[sc].fn;
    d.next = cc->scopes[sc].first;
    push_back(cc->decls, d);
    uint32_t id = (uint32_t)(len(cc->decls) - 1);
    /* the scope's list in declaration order: appended at its end */
    if (!cc->scopes[sc].first) cc->scopes[sc].first = id;
    else {
        cc->decls[id].next = 0;
        uint32_t x = cc->scopes[sc].first;
        while (cc->decls[x].next) x = cc->decls[x].next;
        cc->decls[x].next = id;
    }
    return id;
}

static uint32_t ref_new(js_cc *cc, uint32_t atom) {
    jref r = { cc->cur_scope, atom, 0, cc->cur_fn };
    push_back(cc->refs, r);
    return (uint32_t)(len(cc->refs) - 1);
}

static uint32_t ident_node(CC, uint32_t atom, uint32_t line) {
    uint32_t n = node(cc, N_IDENT, line);
    cc->nodes[n].s = atom;
    cc->nodes[n].a = ref_new(cc, atom);
    return n;
}

static uint32_t p_expr(CC, int no_in);
static uint32_t p_assign(CC, int no_in);
static uint32_t p_stmt(CC);
static uint32_t p_function(CC, uint32_t name, uint32_t flags, int is_decl, uint32_t line, uint32_t start);
static uint32_t p_class(CC, int is_decl);
static uint32_t p_binding(CC, uint32_t kind);
static uint32_t p_unary(CC);
static uint32_t p_unary_in(CC);
static uint32_t p_call(CC);

static int is_name_tok(js_cc *cc) { return cc->tok.type == T_NAME; }

/* a property's name in a literal or a class: a node (N_STR of an atom, or an expression with *computed) */
static uint32_t p_prop_key(CC, int *computed) {
    *computed = 0;
    uint32_t line = cc->tok.line;
    if (is_p(cc, P_LBRACK)) {
        lx(CCA);
        uint32_t k = p_assign(CCA, 0);
        expect_p(CCA, P_RBRACK, "']'");
        *computed = 1;
        return k;
    }
    uint32_t n = node(cc, N_STR, line);
    if (cc->tok.type == T_NAME || cc->tok.type == T_STR) cc->nodes[n].s = js_atom(vm, cc->tok.str);
    else if (cc->tok.type == T_NUM) cc->nodes[n].s = js_atom(vm, js_num_to_str(vm, cc->tok.num));
    else { cc_error(cc, line, "expected a property name"); return n; }
    lx(CCA);
    return n;
}

/* the parameters of a function, its scope current */
static void p_params(CC, uint32_t fi) {
    expect_p(CCA, P_LPAREN, "'(' before the parameters");
    cc->fns[fi].flags |= FN_PARAMS;
    size_t mark = len(cc->tmp);
    uint32_t formal = 0;
    int counting = 1;
    while (!is_p(cc, P_RPAREN) && !cc->err) {
        uint32_t line = cc->tok.line;
        uint32_t p = node(cc, N_PARAM, line);
        if (is_p(cc, P_ELLIPSIS)) { lx(CCA); cc->nodes[p].op = NF_REST; cc->fns[fi].flags |= JF_HAS_REST; counting = 0; }
        uint32_t t;
        if (is_name_tok(cc) && !cc->tok.aux) {
            uint32_t atom = cc->tok.str;
            lx(CCA);
            uint32_t d = declare(CCA, atom, D_PARAM, line);
            cc->decls[d].param = (uint32_t)(len(cc->tmp) - mark) + 1;
            t = ident_node(CCA, atom, line);
        } else t = p_binding(CCA, D_VAR);
        cc->nodes[p].a = t;
        if (is_p(cc, P_ASSIGN)) { lx(CCA); cc->nodes[p].b = p_assign(CCA, 0); counting = 0; }
        if (counting) formal++;
        push_back(cc->tmp, p);
        if (!is_p(cc, P_RPAREN)) expect_p(CCA, P_COMMA, "',' between parameters");
    }
    expect_p(CCA, P_RPAREN, "')' after the parameters");
    cc->fns[fi].flags &= ~(uint32_t)FN_PARAMS;
    list_take(cc, mark, &cc->fns[fi].params, &cc->fns[fi].nparams);
    cc->fns[fi].nformal = formal;
}

/* the function being read is a generator (yield an operator in it) or async (await
   one); 2 while its parameters are read, where neither may stand */
static int in_generator(js_cc *cc) {
    uint32_t f = cc->fns[cc->cur_fn].flags;
    return !(f & JF_GENERATOR) ? 0 : (f & FN_PARAMS) ? 2 : 1;
}
static int in_async(js_cc *cc) {
    uint32_t f = cc->fns[cc->cur_fn].flags;
    return !(f & JF_ASYNC) ? 0 : (f & FN_PARAMS) ? 2 : 1;
}

/* statements to a closing brace (not taken): a list */
static void p_statements(CC, uint32_t *off, uint32_t *count) {
    size_t mark = len(cc->tmp);
    while (!is_p(cc, P_RBRACE) && cc->tok.type != T_EOF && !cc->err) push_back(cc->tmp, p_stmt(CCA));
    list_take(cc, mark, off, count);
}

/* a body's statements to its closing brace (not taken) or the script's end: a
   directive prologue's 'use strict' makes the function strict, and every function
   within it (fn_new) */
static void p_body(CC, uint32_t fi, uint32_t *off, uint32_t *count) {
    size_t mark = len(cc->tmp);
    int prologue = 1;
    while (!is_p(cc, P_RBRACE) && cc->tok.type != T_EOF && !cc->err) {
        jtok t = cc->tok;
        uint32_t st = p_stmt(CCA);
        push_back(cc->tmp, st);
        if (!prologue) continue;
        jn sn = cc->nodes[st];
        if (t.type != T_STR || sn.k != N_EXPR || cc->nodes[sn.a].k != N_STR) { prologue = 0; continue; }
        /* the directive as written: no escape, no parentheses */
        const char *ds = "use strict";
        int is_strict = t.end - t.start == 12;
        for (uint32_t i = 0; is_strict && i < 10; i++) if (src[t.start + 1 + i] != ds[i]) is_strict = 0;
        if (is_strict) cc->fns[fi].flags |= JF_STRICT;
    }
    list_take(cc, mark, off, count);
}

static uint32_t fn_new(js_cc *cc, uint32_t name, uint32_t flags, uint32_t line, uint32_t start) {
    jfn f;
    memset(&f, 0, sizeof f);
    f.name = name;
    f.flags = flags;
    if (cc->fns[cc->cur_fn].flags & JF_STRICT) f.flags |= JF_STRICT;
    f.line = line;
    f.src_start = start;
    f.parent = cc->cur_fn;
    push_back(cc->fns, f);
    return (uint32_t)(len(cc->fns) - 1);
}

/* a function's parameters and body; the current token its '(' (or an arrow's first token) */
static uint32_t p_function(CC, uint32_t name, uint32_t flags, int is_decl, uint32_t line, uint32_t start) {
    (void)is_decl;
    uint32_t fi = fn_new(cc, name, flags, line, start);
    uint32_t saved_fn = cc->cur_fn, saved_scope = cc->cur_scope;
    /* a named function expression sees its own name, beneath its parameters */
    if (name && !is_decl && !(flags & (JF_ARROW | JF_METHOD))) {
        cc->cur_fn = fi;
        uint32_t ss = scope_new(cc, SC_SELF);
        cc->cur_scope = ss;
        declare(CCA, name, D_SELF, line);
    }
    cc->cur_fn = fi;
    uint32_t fs = scope_new(cc, SC_FUNC);
    cc->cur_scope = fs;
    cc->fns[fi].scope = fs;
    p_params(CCA, fi);
    if (!is_p(cc, P_LBRACE)) { cc_error(cc, cc->tok.line, "expected '{' before a function's body"); }
    lx(CCA);
    uint32_t b = node(cc, N_BLOCK, cc->tok.line);
    { uint32_t so, sn; p_body(CCA, fi, &so, &sn); cc->nodes[b].a = so; cc->nodes[b].b = sn; }
    cc->nodes[b].c = fs;
    cc->fns[fi].src_end = cc->tok.end;
    expect_p(CCA, P_RBRACE, "'}' after a function's body");
    cc->fns[fi].body = b;
    cc->cur_fn = saved_fn;
    cc->cur_scope = saved_scope;
    uint32_t n = node(cc, N_FUNC, line);
    cc->nodes[n].a = fi;
    return n;
}

/* an arrow function, its parameters already a list on the scratch stack or a single name */
static uint32_t p_arrow_body(CC, uint32_t fi, uint32_t line) {
    uint32_t saved_fn = cc->cur_fn, saved_scope = cc->cur_scope;
    cc->cur_fn = fi;
    cc->cur_scope = cc->fns[fi].scope;
    expect_p(CCA, P_ARROW, "'=>'");
    if (is_p(cc, P_LBRACE)) {
        lx(CCA);
        uint32_t b = node(cc, N_BLOCK, line);
        { uint32_t so, sn; p_body(CCA, fi, &so, &sn); cc->nodes[b].a = so; cc->nodes[b].b = sn; }
        cc->nodes[b].c = cc->fns[fi].scope;
        cc->fns[fi].src_end = cc->tok.end;
        expect_p(CCA, P_RBRACE, "'}' after an arrow function's body");
        cc->fns[fi].body = b;
    } else {
        cc->fns[fi].body = p_assign(CCA, 0);
        cc->fns[fi].flags |= FN_EXPR;
        cc->fns[fi].src_end = cc->prev.end;
    }
    cc->cur_fn = saved_fn;
    cc->cur_scope = saved_scope;
    uint32_t n = node(cc, N_FUNC, line);
    cc->nodes[n].a = fi;
    return n;
}

/* an arrow of parameters in parentheses (the current token their '('), flags its
   JF_ASYNC or none */
static uint32_t p_paren_arrow(CC, uint32_t flags, uint32_t line, uint32_t start) {
    uint32_t fi = fn_new(cc, 0, JF_ARROW | flags, line, start);
    uint32_t saved_fn = cc->cur_fn, saved_scope = cc->cur_scope;
    cc->cur_fn = fi;
    uint32_t fs = scope_new(cc, SC_FUNC);
    cc->cur_scope = fs;
    cc->fns[fi].scope = fs;
    p_params(CCA, fi);
    cc->cur_fn = saved_fn;
    cc->cur_scope = saved_scope;
    return p_arrow_body(CCA, fi, line);
}
/* an arrow of one parameter, its name a (the current token the '=>') */
static uint32_t p_name_arrow(CC, uint32_t a, uint32_t flags, uint32_t line, uint32_t start) {
    uint32_t fi = fn_new(cc, 0, JF_ARROW | flags, line, start);
    uint32_t saved_fn = cc->cur_fn, saved_scope = cc->cur_scope;
    cc->cur_fn = fi;
    uint32_t fs = scope_new(cc, SC_FUNC);
    cc->cur_scope = fs;
    cc->fns[fi].scope = fs;
    uint32_t d = declare(CCA, a, D_PARAM, line);
    cc->decls[d].param = 1;
    uint32_t p = node(cc, N_PARAM, line);
    cc->nodes[p].a = ident_node(CCA, a, line);
    size_t mark = len(cc->tmp);
    push_back(cc->tmp, p);
    list_take(cc, mark, &cc->fns[fi].params, &cc->fns[fi].nparams);
    cc->fns[fi].nformal = 1;
    cc->cur_fn = saved_fn;
    cc->cur_scope = saved_scope;
    return p_arrow_body(CCA, fi, line);
}

/* does the parenthesis at the current token close before an arrow? (read ahead, then
   put back: a template's substitutions read as the template's, and an error met on the
   way forgotten, the parse proper to find it) */
static int p_is_arrow(CC) {
    jtok t0 = cc->tok, p0 = cc->prev;
    uint32_t pos0 = cc->pos, line0 = cc->line;
    int err0 = cc->err;
    int depth = 0, r = 0, nt = 0;
    int tdepth[64];
    while (cc->tok.type != T_EOF && !cc->err) {
        if (cc->tok.type == T_PUNCT) {
            uint32_t op = cc->tok.op;
            if (op == P_LPAREN || op == P_LBRACK || op == P_LBRACE) depth++;
            else if (op == P_RBRACE && nt > 0 && tdepth[nt - 1] == depth - 1) {
                /* a substitution's end: the template goes on */
                depth--;
                nt--;
                lx_template(CCA, cc->tok.start + 1);
                if (cc->tok.type == T_TEMPLATE && cc->tok.aux == 0 && nt < 64) { tdepth[nt++] = depth; depth++; }
            } else if (op == P_RPAREN || op == P_RBRACK || op == P_RBRACE) {
                depth--;
                if (depth == 0) {
                    lx(CCA);
                    r = is_p(cc, P_ARROW) && !cc->tok.nl;
                    break;
                }
            }
        } else if (cc->tok.type == T_TEMPLATE && cc->tok.aux == 0 && nt < 64) { tdepth[nt++] = depth; depth++; }
        lx(CCA);
    }
    cc->tok = t0;
    cc->prev = p0;
    cc->pos = pos0;
    cc->line = line0;
    if (!err0) cc->err = 0;
    return r;
}

/* a template part's raw text, as written (a tag reads it): the source between its
   delimiters, CR LF and CR read as LF */
static uint32_t raw_part(CC, jtok t) {
    vector<uint16_t> u;
    uint32_t end = t.end - (t.aux ? 1 : 2);
    for (uint32_t i = t.start; i < end && i < srcn; ) {
        if (src[i] == '\r') { push_back(u, (uint16_t)'\n'); i += i + 1 < end && src[i + 1] == '\n' ? 2 : 1; continue; }
        uint32_t k;
        uint32_t cp = lx_utf8(src, srcn, i, &k);
        lx_push_cp(&u, cp);
        i += k;
    }
    return js_str_new(vm, data(u), len(u));
}

static uint32_t p_template(CC) {
    uint32_t line = cc->tok.line;
    uint32_t n = node(cc, N_TEMPLATE, line);
    size_t mark = len(cc->tmp);
    for (;;) {
        uint32_t s = node(cc, N_STR, cc->tok.line);
        cc->nodes[s].s = cc->tok.str;
        cc->nodes[s].b = raw_part(CCA, cc->tok);
        push_back(cc->tmp, s);
        if (cc->tok.aux) { lx(CCA); break; }
        lx(CCA);
        push_back(cc->tmp, p_expr(CCA, 0));
        if (!is_p(cc, P_RBRACE)) { cc_error(cc, cc->tok.line, "expected '}' to close a template's substitution"); break; }
        lx_template(CCA, cc->tok.start + 1);
        if (cc->err) break;
    }
    list_take(cc, mark, &cc->nodes[n].a, &cc->nodes[n].b);
    return n;
}

static uint32_t p_array(CC) {
    uint32_t n = node(cc, N_ARRAY, cc->tok.line);
    lx(CCA);
    size_t mark = len(cc->tmp);
    while (!is_p(cc, P_RBRACK) && !cc->err) {
        if (is_p(cc, P_COMMA)) { lx(CCA); push_back(cc->tmp, node(cc, N_HOLE, cc->tok.line)); continue; }
        if (is_p(cc, P_ELLIPSIS)) {
            uint32_t s = node(cc, N_SPREAD, cc->tok.line);
            lx(CCA);
            cc->nodes[s].a = p_assign(CCA, 0);
            push_back(cc->tmp, s);
        } else push_back(cc->tmp, p_assign(CCA, 0));
        if (!is_p(cc, P_RBRACK)) expect_p(CCA, P_COMMA, "',' between array elements");
    }
    expect_p(CCA, P_RBRACK, "']'");
    list_take(cc, mark, &cc->nodes[n].a, &cc->nodes[n].b);
    return n;
}

static uint32_t p_object(CC) {
    uint32_t n = node(cc, N_OBJECT, cc->tok.line);
    lx(CCA);
    size_t mark = len(cc->tmp);
    while (!is_p(cc, P_RBRACE) && !cc->err) {
        uint32_t line = cc->tok.line, start = cc->tok.start;
        uint32_t p = node(cc, N_PROP, line);
        if (is_p(cc, P_ELLIPSIS)) {
            lx(CCA);
            cc->nodes[p].op = PK_SPREAD;
            cc->nodes[p].b = p_assign(CCA, 0);
        } else {
            uint32_t kind = PK_INIT;
            if ((is_kw(cc, K_GET) || is_kw(cc, K_SET))) {
                uint32_t which = is_kw(cc, K_GET) ? PK_GET : PK_SET;
                jtok t0 = cc->tok, p0 = cc->prev;
                uint32_t pos0 = cc->pos, line0 = cc->line;
                lx(CCA);
                if (!is_p(cc, P_LPAREN) && !is_p(cc, P_COMMA) && !is_p(cc, P_COLON) && !is_p(cc, P_RBRACE) && !is_p(cc, P_ASSIGN)) kind = which;
                else { cc->tok = t0; cc->prev = p0; cc->pos = pos0; cc->line = line0; }
            }
            /* *m() a generator method; async m() an async one (async alone a key) */
            uint32_t mfl = 0;
            if (kind == PK_INIT && is_p(cc, P_STAR)) { lx(CCA); mfl = JF_GENERATOR; }
            else if (kind == PK_INIT && is_kw(cc, K_ASYNC)) {
                jtok t0 = cc->tok, p0 = cc->prev;
                uint32_t pos0 = cc->pos, line0 = cc->line;
                lx(CCA);
                if (!cc->tok.nl && !is_p(cc, P_LPAREN) && !is_p(cc, P_COMMA) && !is_p(cc, P_COLON) && !is_p(cc, P_RBRACE) && !is_p(cc, P_ASSIGN)) {
                    if (is_p(cc, P_STAR)) { cc_error(cc, line, "async generator methods are not supported"); break; }
                    mfl = JF_ASYNC;
                } else { cc->tok = t0; cc->prev = p0; cc->pos = pos0; cc->line = line0; }
            }
            int computed;
            int shorthand_ok = cc->tok.type == T_NAME;
            uint32_t atom_tok = cc->tok.str;
            uint32_t key = p_prop_key(CCA, &computed);
            cc->nodes[p].a = key;
            cc->nodes[p].d = computed ? NF_COMPUTED : 0;
            if (kind == PK_GET || kind == PK_SET) {
                cc->nodes[p].op = (uint16_t)kind;
                cc->nodes[p].b = p_function(CCA, computed ? 0 : cc->nodes[key].s, JF_METHOD | (kind == PK_GET ? JF_GETTER : 0), 0, line, start);
            } else if (is_p(cc, P_LPAREN)) {
                cc->nodes[p].op = PK_METHOD;
                cc->nodes[p].b = p_function(CCA, computed ? 0 : cc->nodes[key].s, JF_METHOD | mfl, 0, line, start);
            } else if (mfl) { cc_error(cc, line, "expected '(' after a generator's or an async method's name"); break;
            } else if (is_p(cc, P_COLON)) {
                lx(CCA);
                cc->nodes[p].op = PK_INIT;
                cc->nodes[p].b = p_assign(CCA, 0);
            } else if (shorthand_ok && !computed) {
                cc->nodes[p].op = PK_INIT;
                uint32_t id = ident_node(CCA, atom_tok, line);
                if (is_p(cc, P_ASSIGN)) {
                    /* {a = 1}: only a pattern's */
                    lx(CCA);
                    uint32_t as = node(cc, N_ASSIGN, line);
                    cc->nodes[as].op = P_ASSIGN;
                    cc->nodes[as].a = id;
                    cc->nodes[as].b = p_assign(CCA, 0);
                    cc->nodes[p].op = PK_COVER;
                    cc->nodes[p].b = as;
                } else cc->nodes[p].b = id;
            } else { cc_error(cc, line, "expected ':' after a property name"); break; }
        }
        push_back(cc->tmp, p);
        if (!is_p(cc, P_RBRACE)) expect_p(CCA, P_COMMA, "',' between properties");
    }
    expect_p(CCA, P_RBRACE, "'}'");
    list_take(cc, mark, &cc->nodes[n].a, &cc->nodes[n].b);
    return n;
}

/* an expression's node turned into an assignment's target pattern */
static uint32_t to_pattern(CC, uint32_t e) {
    uint32_t k = cc->nodes[e].k;
    if (k == N_IDENT || k == N_MEMBER) return e;
    if (k == N_ASSIGN && cc->nodes[e].op == P_ASSIGN) {
        uint32_t pe = node(cc, N_PELEM, cc->nodes[e].line);
        cc->nodes[pe].a = to_pattern(CCA, cc->nodes[e].a);
        cc->nodes[pe].b = cc->nodes[e].b;
        return pe;
    }
    if (k == N_ARRAY) {
        uint32_t p = node(cc, N_PATTERN_ARR, cc->nodes[e].line);
        size_t mark = len(cc->tmp);
        uint32_t off = cc->nodes[e].a, n = cc->nodes[e].b;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t el = cc->lists[off + i];
            uint32_t pe = node(cc, N_PELEM, cc->nodes[el].line);
            if (cc->nodes[el].k == N_HOLE) cc->nodes[pe].a = 0;
            else if (cc->nodes[el].k == N_SPREAD) { cc->nodes[pe].op = NF_REST; cc->nodes[pe].a = to_pattern(CCA, cc->nodes[el].a); }
            else {
                uint32_t t = to_pattern(CCA, el);
                if (cc->nodes[t].k == N_PELEM) pe = t;
                else cc->nodes[pe].a = t;
            }
            push_back(cc->tmp, pe);
        }
        list_take(cc, mark, &cc->nodes[p].a, &cc->nodes[p].b);
        return p;
    }
    if (k == N_OBJECT) {
        uint32_t p = node(cc, N_PATTERN_OBJ, cc->nodes[e].line);
        size_t mark = len(cc->tmp);
        uint32_t off = cc->nodes[e].a, n = cc->nodes[e].b;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t pr = cc->lists[off + i];
            uint32_t pe = node(cc, N_PELEM, cc->nodes[pr].line);
            if (cc->nodes[pr].op == PK_SPREAD) { cc->nodes[pe].op = NF_REST; cc->nodes[pe].a = to_pattern(CCA, cc->nodes[pr].b); }
            else if (cc->nodes[pr].op == PK_INIT || cc->nodes[pr].op == PK_COVER) {
                cc->nodes[pe].c = cc->nodes[pr].a;
                cc->nodes[pe].op = (uint16_t)(cc->nodes[pr].d & NF_COMPUTED);
                uint32_t v = cc->nodes[pr].b;
                if (cc->nodes[v].k == N_ASSIGN && cc->nodes[v].op == P_ASSIGN) {
                    cc->nodes[pe].a = to_pattern(CCA, cc->nodes[v].a);
                    cc->nodes[pe].b = cc->nodes[v].b;
                } else cc->nodes[pe].a = to_pattern(CCA, v);
            } else cc_error(cc, cc->nodes[pr].line, "a method is no destructuring target");
            push_back(cc->tmp, pe);
        }
        list_take(cc, mark, &cc->nodes[p].a, &cc->nodes[p].b);
        return p;
    }
    if (k == N_PATTERN_ARR || k == N_PATTERN_OBJ || k == N_PELEM) return e;
    cc_error(cc, cc->nodes[e].line, "invalid assignment target");
    return e;
}

/* a binding: a name declared as kind, or a pattern of them */
static uint32_t p_binding(CC, uint32_t kind) {
    uint32_t line = cc->tok.line;
    if (cc->tok.type == T_NAME) {
        uint32_t atom = cc->tok.str;
        if (atom == cc->kw[K_LET] && (kind == D_LET || kind == D_CONST)) cc_error(cc, line, "let is no name for a let or const binding");
        lx(CCA);
        declare(CCA, atom, kind, line);
        return ident_node(CCA, atom, line);
    }
    if (is_p(cc, P_LBRACK)) {
        uint32_t p = node(cc, N_PATTERN_ARR, line);
        lx(CCA);
        size_t mark = len(cc->tmp);
        while (!is_p(cc, P_RBRACK) && !cc->err) {
            uint32_t pe = node(cc, N_PELEM, cc->tok.line);
            if (is_p(cc, P_COMMA)) { lx(CCA); push_back(cc->tmp, pe); continue; }
            if (is_p(cc, P_ELLIPSIS)) { lx(CCA); cc->nodes[pe].op = NF_REST; }
            cc->nodes[pe].a = p_binding(CCA, kind);
            if (is_p(cc, P_ASSIGN)) { lx(CCA); cc->nodes[pe].b = p_assign(CCA, 0); }
            push_back(cc->tmp, pe);
            if (!is_p(cc, P_RBRACK)) expect_p(CCA, P_COMMA, "',' in an array pattern");
        }
        expect_p(CCA, P_RBRACK, "']'");
        list_take(cc, mark, &cc->nodes[p].a, &cc->nodes[p].b);
        return p;
    }
    if (is_p(cc, P_LBRACE)) {
        uint32_t p = node(cc, N_PATTERN_OBJ, line);
        lx(CCA);
        size_t mark = len(cc->tmp);
        while (!is_p(cc, P_RBRACE) && !cc->err) {
            uint32_t pe = node(cc, N_PELEM, cc->tok.line);
            if (is_p(cc, P_ELLIPSIS)) {
                lx(CCA);
                cc->nodes[pe].op = NF_REST;
                cc->nodes[pe].a = p_binding(CCA, kind);
            } else {
                int computed;
                int shorthand = cc->tok.type == T_NAME;
                uint32_t atom = cc->tok.str, kline = cc->tok.line;
                uint32_t key = p_prop_key(CCA, &computed);
                cc->nodes[pe].c = key;
                cc->nodes[pe].op = computed ? NF_COMPUTED : 0;
                if (is_p(cc, P_COLON)) { lx(CCA); cc->nodes[pe].a = p_binding(CCA, kind); }
                else if (shorthand && !computed) {
                    declare(CCA, atom, kind, kline);
                    cc->nodes[pe].a = ident_node(CCA, atom, kline);
                } else cc_error(cc, kline, "expected ':' in an object pattern");
                if (is_p(cc, P_ASSIGN)) { lx(CCA); cc->nodes[pe].b = p_assign(CCA, 0); }
            }
            push_back(cc->tmp, pe);
            if (!is_p(cc, P_RBRACE)) expect_p(CCA, P_COMMA, "',' in an object pattern");
        }
        expect_p(CCA, P_RBRACE, "'}'");
        list_take(cc, mark, &cc->nodes[p].a, &cc->nodes[p].b);
        return p;
    }
    cc_error(cc, line, "expected a name or a pattern");
    lx(CCA);
    return node(cc, N_HOLE, line);
}

static uint32_t p_args(CC, uint32_t n) {
    /* the current token is '(' */
    lx(CCA);
    size_t mark = len(cc->tmp);
    while (!is_p(cc, P_RPAREN) && !cc->err) {
        if (is_p(cc, P_ELLIPSIS)) {
            uint32_t s = node(cc, N_SPREAD, cc->tok.line);
            lx(CCA);
            cc->nodes[s].a = p_assign(CCA, 0);
            push_back(cc->tmp, s);
        } else push_back(cc->tmp, p_assign(CCA, 0));
        if (!is_p(cc, P_RPAREN)) expect_p(CCA, P_COMMA, "',' between arguments");
    }
    expect_p(CCA, P_RPAREN, "')' after the arguments");
    list_take(cc, mark, &cc->nodes[n].b, &cc->nodes[n].c);
    return n;
}

static uint32_t p_primary(CC) {
    uint32_t line = cc->tok.line, start = cc->tok.start;
    jtok t = cc->tok;
    if (t.type == T_NUM) { uint32_t n = node(cc, N_NUM, line); cc->nodes[n].num = t.num; lx(CCA); return n; }
    if (t.type == T_STR) { uint32_t n = node(cc, N_STR, line); cc->nodes[n].s = t.str; lx(CCA); return n; }
    if (t.type == T_TEMPLATE) return p_template(CCA);
    if (t.type == T_PUNCT && (t.op == P_SLASH || t.op == P_DIV_ASSIGN)) {
        lx_regex(CCA, t.start);
        uint32_t n = node(cc, N_REGEX, line);
        cc->nodes[n].s = cc->tok.str;
        cc->nodes[n].a = cc->tok.aux;
        lx(CCA);
        return n;
    }
    if (t.type == T_PUNCT && t.op == P_LPAREN) {
        if (p_is_arrow(CCA)) return p_paren_arrow(CCA, 0, line, start);
        lx(CCA);
        uint32_t e = p_expr(CCA, 0);
        expect_p(CCA, P_RPAREN, "')'");
        return e;
    }
    if (t.type == T_PUNCT && t.op == P_LBRACK) return p_array(CCA);
    if (t.type == T_PUNCT && t.op == P_LBRACE) return p_object(CCA);
    if (t.type == T_NAME) {
        uint32_t a = t.str;
        if (!t.aux) {
            if (a == cc->kw[K_THIS]) { lx(CCA); return node(cc, N_THIS, line); }
            if (a == cc->kw[K_NULL]) { lx(CCA); uint32_t n = node(cc, N_NUM, line); cc->nodes[n].op = 1; return n; }
            if (a == cc->kw[K_TRUE] || a == cc->kw[K_FALSE]) { lx(CCA); uint32_t n = node(cc, N_NUM, line); cc->nodes[n].op = a == cc->kw[K_TRUE] ? 2 : 3; return n; }
            if (a == cc->kw[K_FUNCTION]) {
                lx(CCA);
                uint32_t fl = 0;
                if (is_p(cc, P_STAR)) { lx(CCA); fl = JF_GENERATOR; }
                uint32_t name = 0;
                if (cc->tok.type == T_NAME) { name = cc->tok.str; lx(CCA); }
                return p_function(CCA, name, fl, 0, line, start);
            }
            if (a == cc->kw[K_CLASS]) return p_class(CCA, 0);
            if (a == cc->kw[K_ASYNC]) {
                /* async function, async (..) =>, async x =>; else async is a name */
                jtok t0 = cc->tok, p0 = cc->prev;
                uint32_t pos0 = cc->pos, line0 = cc->line;
                lx(CCA);
                if (!cc->tok.nl && is_kw(cc, K_FUNCTION)) {
                    lx(CCA);
                    if (is_p(cc, P_STAR)) { cc_error(cc, line, "async generators are not supported"); return node(cc, N_HOLE, line); }
                    uint32_t name = 0;
                    if (cc->tok.type == T_NAME) { name = cc->tok.str; lx(CCA); }
                    return p_function(CCA, name, JF_ASYNC, 0, line, start);
                }
                if (!cc->tok.nl && is_p(cc, P_LPAREN) && p_is_arrow(CCA)) return p_paren_arrow(CCA, JF_ASYNC, line, start);
                if (!cc->tok.nl && cc->tok.type == T_NAME && !cc->tok.aux) {
                    uint32_t pa = cc->tok.str;
                    lx(CCA);
                    if (is_p(cc, P_ARROW) && !cc->tok.nl) return p_name_arrow(CCA, pa, JF_ASYNC, line, start);
                }
                cc->tok = t0; cc->prev = p0; cc->pos = pos0; cc->line = line0;
            }
            if (a == cc->kw[K_SUPER]) {
                lx(CCA);
                if (is_p(cc, P_LPAREN)) { uint32_t n = node(cc, N_SUPER_CALL, line); return p_args(CCA, n); }
                uint32_t n = node(cc, N_SUPER_MEMBER, line);
                if (is_p(cc, P_DOT)) {
                    lx(CCA);
                    if (cc->tok.type != T_NAME) cc_error(cc, line, "expected a name after super.");
                    cc->nodes[n].s = cc->tok.str;
                    lx(CCA);
                } else if (is_p(cc, P_LBRACK)) {
                    lx(CCA);
                    cc->nodes[n].b = p_expr(CCA, 0);
                    cc->nodes[n].op = NF_COMPUTED;
                    expect_p(CCA, P_RBRACK, "']'");
                } else cc_error(cc, line, "super must be called or have a member read");
                return n;
            }
            if (a == cc->kw[K_NEW]) {
                lx(CCA);
                if (is_p(cc, P_DOT)) {
                    lx(CCA);
                    if (!is_kw(cc, K_TARGET)) cc_error(cc, line, "expected new.target");
                    lx(CCA);
                    return node(cc, N_NEW_TARGET, line);
                }
                uint32_t n = node(cc, N_NEW, line);
                /* the callee: a member expression with no call */
                uint32_t callee = p_primary(CCA);
                for (;;) {
                    if (is_p(cc, P_DOT)) {
                        lx(CCA);
                        uint32_t m = node(cc, N_MEMBER, cc->tok.line);
                        cc->nodes[m].a = callee;
                        cc->nodes[m].s = cc->tok.str;
                        if (cc->tok.type != T_NAME) cc_error(cc, cc->tok.line, "expected a name after '.'");
                        lx(CCA);
                        callee = m;
                    } else if (is_p(cc, P_LBRACK)) {
                        lx(CCA);
                        uint32_t m = node(cc, N_MEMBER, cc->tok.line);
                        cc->nodes[m].a = callee;
                        cc->nodes[m].b = p_expr(CCA, 0);
                        cc->nodes[m].op = NF_COMPUTED;
                        expect_p(CCA, P_RBRACK, "']'");
                        callee = m;
                    } else break;
                }
                cc->nodes[n].a = callee;
                if (is_p(cc, P_LPAREN)) p_args(CCA, n);
                return n;
            }
            /* yield and await: names outside generators and async functions; in one,
               an operator only where an assignment (a unary for await) may stand */
            if (a == cc->kw[K_YIELD] && in_generator(cc)) { cc_error(cc, line, "a yield stands where only an assignment's whole expression may (put it in parentheses)"); lx(CCA); return node(cc, N_HOLE, line); }
            if (a == cc->kw[K_IMPORT]) { cc_error(cc, line, "modules (import) are not supported"); lx(CCA); return node(cc, N_HOLE, line); }
        }
        lx(CCA);
        if (is_p(cc, P_ARROW) && !cc->tok.nl) return p_name_arrow(CCA, a, 0, line, start);
        return ident_node(CCA, a, line);
    }
    if (t.type == T_EOF) cc_error(cc, line, "the script ends in the middle of an expression");
    else cc_error(cc, line, "an unexpected token where an expression should be");
    lx(CCA);
    return node(cc, N_HOLE, line);
}

/* member accesses, calls and optional chains after a primary */
static uint32_t p_call(CC) {
    uint32_t e = p_primary(CCA);
    int chain = 0;
    while (!cc->err) {
        uint32_t line = cc->tok.line;
        if (is_p(cc, P_DOT) || is_p(cc, P_OPTCHAIN)) {
            int opt = is_p(cc, P_OPTCHAIN);
            lx(CCA);
            if (opt) chain = 1;
            if (opt && is_p(cc, P_LPAREN)) {
                uint32_t n = node(cc, N_CALL, line);
                cc->nodes[n].a = e;
                cc->nodes[n].op = NF_OPTIONAL;
                e = p_args(CCA, n);
                continue;
            }
            if (opt && is_p(cc, P_LBRACK)) {
                lx(CCA);
                uint32_t m = node(cc, N_MEMBER, line);
                cc->nodes[m].a = e;
                cc->nodes[m].b = p_expr(CCA, 0);
                cc->nodes[m].op = NF_COMPUTED | NF_OPTIONAL;
                expect_p(CCA, P_RBRACK, "']'");
                e = m;
                continue;
            }
            if (cc->tok.type != T_NAME) { cc_error(cc, line, "expected a name after '.'"); break; }
            uint32_t m = node(cc, N_MEMBER, line);
            cc->nodes[m].a = e;
            cc->nodes[m].s = cc->tok.str;
            cc->nodes[m].op = opt ? NF_OPTIONAL : 0;
            lx(CCA);
            e = m;
        } else if (is_p(cc, P_LBRACK)) {
            lx(CCA);
            uint32_t m = node(cc, N_MEMBER, line);
            cc->nodes[m].a = e;
            cc->nodes[m].b = p_expr(CCA, 0);
            cc->nodes[m].op = NF_COMPUTED;
            expect_p(CCA, P_RBRACK, "']'");
            e = m;
        } else if (is_p(cc, P_LPAREN)) {
            uint32_t n = node(cc, N_CALL, line);
            cc->nodes[n].a = e;
            e = p_args(CCA, n);
        } else if (cc->tok.type == T_TEMPLATE) {
            /* a tagged template: the tag called with the strings (op 1: one frozen array
               for this place in the source, its raw strings beside) and the substitutions */
            uint32_t t = p_template(CCA);
            cc->nodes[t].op = 1;
            uint32_t n = node(cc, N_CALL, line);
            cc->nodes[n].a = e;
            size_t mark = len(cc->tmp);
            push_back(cc->tmp, t);
            for (uint32_t i = 1; i < cc->nodes[t].b; i += 2) push_back(cc->tmp, cc->lists[cc->nodes[t].a + i]);
            list_take(cc, mark, &cc->nodes[n].b, &cc->nodes[n].c);
            e = n;
        } else break;
    }
    if (chain) {
        uint32_t c = node(cc, N_CHAIN, cc->nodes[e].line);
        cc->nodes[c].a = e;
        e = c;
    }
    return e;
}

static uint32_t p_postfix(CC) {
    uint32_t e = p_call(CCA);
    if ((is_p(cc, P_INC) || is_p(cc, P_DEC)) && !cc->tok.nl) {
        uint32_t n = node(cc, N_UPDATE, cc->tok.line);
        cc->nodes[n].op = (uint16_t)cc->tok.op;
        cc->nodes[n].a = e;
        lx(CCA);
        return n;
    }
    return e;
}

static uint32_t p_unary_in(CC) {
    uint32_t line = cc->tok.line;
    if (cc->tok.type == T_PUNCT) {
        uint32_t op = cc->tok.op;
        if (op == P_INC || op == P_DEC) {
            lx(CCA);
            uint32_t n = node(cc, N_UPDATE, line);
            cc->nodes[n].op = (uint16_t)op;
            cc->nodes[n].d = NF_PREFIX;
            cc->nodes[n].a = p_unary(CCA);
            return n;
        }
        if (op == P_PLUS || op == P_MINUS || op == P_BANG || op == P_TILDE) {
            lx(CCA);
            uint32_t n = node(cc, N_UNARY, line);
            cc->nodes[n].op = (uint16_t)op;
            cc->nodes[n].a = p_unary(CCA);
            return n;
        }
    }
    if (is_kw(cc, K_TYPEOF) || is_kw(cc, K_VOID) || is_kw(cc, K_DELETE)) {
        uint32_t which = is_kw(cc, K_TYPEOF) ? 1000 : is_kw(cc, K_VOID) ? 1001 : 1002;
        lx(CCA);
        uint32_t n = node(cc, N_UNARY, line);
        cc->nodes[n].op = (uint16_t)which;
        cc->nodes[n].a = p_unary(CCA);
        return n;
    }
    if (is_kw(cc, K_AWAIT) && in_async(cc)) {
        /* await in an async function (outside one, a name) */
        if (in_async(cc) == 2) { cc_error(cc, line, "await in an async function's parameters"); return node(cc, N_HOLE, line); }
        lx(CCA);
        uint32_t n = node(cc, N_AWAIT, line);
        cc->nodes[n].a = p_unary(CCA);
        return n;
    }
    uint32_t e = p_postfix(CCA);
    if (is_p(cc, P_POW)) {
        lx(CCA);
        uint32_t n = node(cc, N_BINARY, line);
        cc->nodes[n].op = P_POW;
        cc->nodes[n].a = e;
        cc->nodes[n].b = p_unary(CCA);
        return n;
    }
    return e;
}

static int binop_prec(js_cc *cc, int no_in, uint32_t *op) {
    if (cc->tok.type == T_NAME && !cc->tok.aux) {
        if (cc->tok.str == cc->kw[K_INSTANCEOF]) { *op = P_INSTANCEOF; return 8; }
        if (cc->tok.str == cc->kw[K_IN] && !no_in) { *op = P_IN; return 8; }
        return 0;
    }
    if (cc->tok.type != T_PUNCT) return 0;
    *op = cc->tok.op;
    switch (cc->tok.op) {
    case P_NULLISH: return 1;
    case P_OR: return 2;
    case P_AND: return 3;
    case P_PIPE: return 4;
    case P_CARET: return 5;
    case P_AMP: return 6;
    case P_EQ: case P_NE: case P_SEQ: case P_SNE: return 7;
    case P_LT: case P_GT: case P_LE: case P_GE: return 8;
    case P_SHL: case P_SAR: case P_SHR: return 9;
    case P_PLUS: case P_MINUS: return 10;
    case P_STAR: case P_SLASH: case P_PERCENT: return 11;
    default: return 0;
    }
}

static uint32_t p_binary(CC, int min_prec, int no_in) {
    uint32_t left = p_unary(CCA);
    for (;;) {
        uint32_t op = 0;
        int prec = binop_prec(cc, no_in, &op);
        if (!prec || prec < min_prec || cc->err) break;
        uint32_t line = cc->tok.line;
        lx(CCA);
        uint32_t right = p_binary(CCA, prec + 1, no_in);
        uint32_t n = node(cc, op == P_AND || op == P_OR || op == P_NULLISH ? N_LOGICAL : N_BINARY, line);
        cc->nodes[n].op = (uint16_t)op;
        cc->nodes[n].a = left;
        cc->nodes[n].b = right;
        left = n;
    }
    return left;
}

static uint32_t p_cond(CC, int no_in) {
    uint32_t c = p_binary(CCA, 1, no_in);
    if (is_p(cc, P_QUESTION)) {
        uint32_t n = node(cc, N_COND, cc->tok.line);
        lx(CCA);
        cc->nodes[n].a = c;
        cc->nodes[n].b = p_assign(CCA, 0);
        expect_p(CCA, P_COLON, "':' in a conditional expression");
        cc->nodes[n].c = p_assign(CCA, no_in);
        return n;
    }
    return c;
}

static int is_assign_op(uint32_t op) {
    return op == P_ASSIGN || op == P_ADD_ASSIGN || op == P_SUB_ASSIGN || op == P_MUL_ASSIGN || op == P_DIV_ASSIGN || op == P_MOD_ASSIGN ||
           op == P_POW_ASSIGN || op == P_SHL_ASSIGN || op == P_SAR_ASSIGN || op == P_SHR_ASSIGN || op == P_BAND_ASSIGN ||
           op == P_BOR_ASSIGN || op == P_BXOR_ASSIGN || op == P_LAND_ASSIGN || op == P_LOR_ASSIGN || op == P_NULLISH_ASSIGN;
}

/* the parser's recursion bounded: past JS_NEST_MAX levels of expressions or
   statements a SyntaxError, so neither the C stack nor the value stack's margin
   (JS_STACK_MARGIN) can be outrun */
#define JS_NEST_MAX 400
static uint32_t p_assign_in(CC, int no_in);
static uint32_t p_unary_in(CC);
static uint32_t p_stmt_in(CC);
static int nest_in(js_cc *cc) {
    if (++cc->depth <= JS_NEST_MAX) return 1;
    cc_error(cc, cc->tok.line, "the code is nested more than 400 levels deep");
    cc->depth--;
    return 0;
}
static uint32_t p_assign(CC, int no_in) {
    if (!nest_in(cc)) return 0;
    uint32_t r = p_assign_in(CCA, no_in);
    cc->depth--;
    return r;
}
static uint32_t p_unary(CC) {
    if (!nest_in(cc)) return 0;
    uint32_t r = p_unary_in(CCA);
    cc->depth--;
    return r;
}
static uint32_t p_stmt(CC) {
    if (!nest_in(cc)) return 0;
    uint32_t r = p_stmt_in(CCA);
    cc->depth--;
    return r;
}

/* a yield's operand left out: what follows cannot begin one */
static int yield_bare(js_cc *cc) {
    if (cc->tok.nl || cc->tok.type == T_EOF) return 1;
    if (cc->tok.type == T_NAME) return !cc->tok.aux && (cc->tok.str == cc->kw[K_IN] || cc->tok.str == cc->kw[K_OF]);
    if (cc->tok.type != T_PUNCT) return 0;
    uint32_t op = cc->tok.op;
    return op == P_RPAREN || op == P_RBRACK || op == P_RBRACE || op == P_COMMA || op == P_SEMI || op == P_COLON || op == P_QUESTION;
}

static uint32_t p_assign_in(CC, int no_in) {
    if (is_kw(cc, K_YIELD) && in_generator(cc)) {
        uint32_t line = cc->tok.line;
        if (in_generator(cc) == 2) { cc_error(cc, line, "yield in a generator's parameters"); return node(cc, N_HOLE, line); }
        lx(CCA);
        uint32_t n = node(cc, N_YIELD, line);
        if (!cc->tok.nl && is_p(cc, P_STAR)) { lx(CCA); cc->nodes[n].op = 1; cc->nodes[n].a = p_assign(CCA, no_in); }
        else if (!yield_bare(cc)) cc->nodes[n].a = p_assign(CCA, no_in);
        return n;
    }
    uint32_t left = p_cond(CCA, no_in);
    if (cc->tok.type == T_PUNCT && is_assign_op(cc->tok.op)) {
        uint32_t op = cc->tok.op, line = cc->tok.line;
        lx(CCA);
        uint32_t n = node(cc, N_ASSIGN, line);
        cc->nodes[n].op = (uint16_t)op;
        uint32_t lk = cc->nodes[left].k;
        if (op == P_ASSIGN && (lk == N_ARRAY || lk == N_OBJECT)) left = to_pattern(CCA, left);
        else if (lk != N_IDENT && lk != N_MEMBER) cc_error(cc, line, "invalid assignment target");
        cc->nodes[n].a = left;
        cc->nodes[n].b = p_assign(CCA, no_in);
        return n;
    }
    return left;
}

static uint32_t p_expr(CC, int no_in) {
    uint32_t e = p_assign(CCA, no_in);
    while (is_p(cc, P_COMMA) && !cc->err) {
        uint32_t n = node(cc, N_SEQ, cc->tok.line);
        lx(CCA);
        cc->nodes[n].a = e;
        cc->nodes[n].b = p_assign(CCA, no_in);
        e = n;
    }
    return e;
}

/* ---- classes ---- */

static uint32_t p_class(CC, int is_decl) {
    uint32_t line = cc->tok.line;
    lx(CCA);
    uint32_t name = 0;
    if (cc->tok.type == T_NAME && !is_kw(cc, K_EXTENDS)) { name = cc->tok.str; lx(CCA); }
    else if (is_decl) cc_error(cc, line, "a class declaration needs a name");
    jclass k;
    memset(&k, 0, sizeof k);
    k.name = name;
    k.line = line;
    if (is_decl && name) k.decl = declare(CCA, name, D_CLASS, line);
    if (is_kw(cc, K_EXTENDS)) { lx(CCA); k.extends = p_call(CCA); }
    push_back(cc->classes, k);
    uint32_t ci = (uint32_t)(len(cc->classes) - 1);
    expect_p(CCA, P_LBRACE, "'{' before a class's body");
    uint32_t saved_in = cc->in_class;
    cc->in_class = 1;
    size_t mark = len(cc->tmp);
    uint32_t fields_fn = 0;
    uint32_t nfields = 0;
    while (!is_p(cc, P_RBRACE) && !cc->err) {
        if (is_p(cc, P_SEMI)) { lx(CCA); continue; }
        uint32_t mline = cc->tok.line, mstart = cc->tok.start;
        uint32_t p = node(cc, N_PROP, mline);
        uint32_t flags = 0;
        if (is_kw(cc, K_STATIC)) {
            jtok t0 = cc->tok, p0 = cc->prev;
            uint32_t pos0 = cc->pos, line0 = cc->line;
            lx(CCA);
            if (is_p(cc, P_LPAREN) || is_p(cc, P_ASSIGN) || is_p(cc, P_SEMI) || is_p(cc, P_RBRACE)) { cc->tok = t0; cc->prev = p0; cc->pos = pos0; cc->line = line0; }
            else flags |= NF_STATIC;
            if (is_p(cc, P_LBRACE)) { cc_error(cc, mline, "static blocks are not supported"); break; }
        }
        uint32_t kind = PK_METHOD;
        if (is_kw(cc, K_GET) || is_kw(cc, K_SET)) {
            uint32_t which = is_kw(cc, K_GET) ? PK_GET : PK_SET;
            jtok t0 = cc->tok, p0 = cc->prev;
            uint32_t pos0 = cc->pos, line0 = cc->line;
            lx(CCA);
            if (!is_p(cc, P_LPAREN) && !is_p(cc, P_ASSIGN) && !is_p(cc, P_SEMI) && !is_p(cc, P_RBRACE)) kind = which;
            else { cc->tok = t0; cc->prev = p0; cc->pos = pos0; cc->line = line0; }
        }
        /* *m() a generator method; async m() an async one (async alone a member's name) */
        uint32_t mfl = 0;
        if (kind == PK_METHOD && is_p(cc, P_STAR)) { lx(CCA); mfl = JF_GENERATOR; }
        else if (kind == PK_METHOD && is_kw(cc, K_ASYNC)) {
            jtok t0 = cc->tok, p0 = cc->prev;
            uint32_t pos0 = cc->pos, line0 = cc->line;
            lx(CCA);
            if (!cc->tok.nl && !is_p(cc, P_LPAREN) && !is_p(cc, P_ASSIGN) && !is_p(cc, P_SEMI) && !is_p(cc, P_RBRACE)) {
                if (is_p(cc, P_STAR)) { cc_error(cc, mline, "async generator methods are not supported"); break; }
                mfl = JF_ASYNC;
            } else { cc->tok = t0; cc->prev = p0; cc->pos = pos0; cc->line = line0; }
        }
        int computed;
        uint32_t key = p_prop_key(CCA, &computed);
        cc->nodes[p].a = key;
        cc->nodes[p].d = flags | (computed ? NF_COMPUTED : 0);
        if (mfl && !is_p(cc, P_LPAREN)) { cc_error(cc, mline, "expected '(' after a generator's or an async method's name"); break; }
        if (is_p(cc, P_LPAREN)) {
            int ctor = !computed && !(flags & NF_STATIC) && kind == PK_METHOD && cc->nodes[key].s == cc->kw[K_CONSTRUCTOR];
            uint32_t ff = JF_METHOD | JF_STRICT | (kind == PK_GET ? JF_GETTER : 0) | mfl;
            if (ctor && mfl) { cc_error(cc, mline, "a class's constructor cannot be a generator or async"); break; }
            if (ctor) ff = JF_CLASS_CTOR | JF_STRICT | (cc->classes[ci].extends ? JF_DERIVED : 0);
            uint32_t f = p_function(CCA, computed ? 0 : cc->nodes[key].s, ff, 0, mline, mstart);
            if (ctor) { cc->classes[ci].ctor = cc->nodes[f].a; cc->fns[cc->nodes[f].a].name = name; cc->fns[cc->nodes[f].a].cls = ci + 1; continue; }
            cc->nodes[p].op = (uint16_t)kind;
            cc->nodes[p].b = f;
        } else {
            /* a field: its initializer is a function of its own, run on each new instance */
            cc->nodes[p].op = PK_FIELD;
            if (!(flags & NF_STATIC)) {
                if (!fields_fn) {
                    fields_fn = fn_new(cc, 0, JF_METHOD | JF_STRICT | FN_FIELDS, mline, mstart);
                    uint32_t saved_fn = cc->cur_fn, saved_scope = cc->cur_scope;
                    cc->cur_fn = fields_fn;
                    cc->fns[fields_fn].scope = scope_new(cc, SC_FUNC);
                    cc->fns[fields_fn].cls = ci + 1;
                    cc->cur_fn = saved_fn;
                    cc->cur_scope = saved_scope;
                }
                if (is_p(cc, P_ASSIGN)) {
                    lx(CCA);
                    uint32_t saved_fn = cc->cur_fn, saved_scope = cc->cur_scope;
                    cc->cur_fn = fields_fn;
                    cc->cur_scope = cc->fns[fields_fn].scope;
                    cc->nodes[p].b = p_assign(CCA, 0);
                    cc->cur_fn = saved_fn;
                    cc->cur_scope = saved_scope;
                }
                nfields++;
            } else if (is_p(cc, P_ASSIGN)) {
                lx(CCA);
                cc->nodes[p].b = p_assign(CCA, 0);
            }
            semi(CCA);
        }
        push_back(cc->tmp, p);
    }
    expect_p(CCA, P_RBRACE, "'}' after a class's body");
    cc->in_class = saved_in;
    list_take(cc, mark, &cc->classes[ci].members, &cc->classes[ci].nmembers);
    cc->classes[ci].fields_fn = fields_fn;
    cc->classes[ci].nfields = nfields;
    if (!cc->classes[ci].ctor) {
        /* the constructor not written: an empty one, or super(...args) */
        uint32_t f = fn_new(cc, name, JF_CLASS_CTOR | JF_STRICT | FN_DEFAULT_CTOR | (cc->classes[ci].extends ? JF_DERIVED | JF_HAS_REST : 0), line, 0);
        uint32_t saved_fn = cc->cur_fn, saved_scope = cc->cur_scope;
        cc->cur_fn = f;
        cc->fns[f].scope = scope_new(cc, SC_FUNC);
        cc->fns[f].cls = ci + 1;
        cc->cur_fn = saved_fn;
        cc->cur_scope = saved_scope;
        cc->classes[ci].ctor = f;
    }
    uint32_t n = node(cc, is_decl ? N_CLASSDECL : N_CLASS, line);
    cc->nodes[n].a = ci;
    return n;
}

/* ---- statements ---- */

static uint32_t p_var(CC, uint32_t kind, int no_in, int need_init) {
    uint32_t n = node(cc, N_VAR, cc->tok.line);
    cc->nodes[n].op = (uint16_t)kind;
    lx(CCA);
    size_t mark = len(cc->tmp);
    for (;;) {
        uint32_t d = node(cc, N_DECL, cc->tok.line);
        cc->nodes[d].a = p_binding(CCA, kind);
        if (is_p(cc, P_ASSIGN)) { lx(CCA); cc->nodes[d].b = p_assign(CCA, no_in); }
        else if (need_init && (kind == D_CONST || cc->nodes[cc->nodes[d].a].k != N_IDENT)) cc_error(cc, cc->tok.line, "a const or a pattern needs an initializer");
        push_back(cc->tmp, d);
        if (!is_p(cc, P_COMMA) || cc->err) break;
        lx(CCA);
    }
    list_take(cc, mark, &cc->nodes[n].a, &cc->nodes[n].b);
    return n;
}

static uint32_t p_block(CC) {
    uint32_t n = node(cc, N_BLOCK, cc->tok.line);
    expect_p(CCA, P_LBRACE, "'{'");
    uint32_t saved = cc->cur_scope;
    cc->cur_scope = scope_new(cc, SC_BLOCK);
    cc->nodes[n].c = cc->cur_scope;
    { uint32_t so, sn; p_statements(CCA, &so, &sn); cc->nodes[n].a = so; cc->nodes[n].b = sn; }
    expect_p(CCA, P_RBRACE, "'}'");
    cc->cur_scope = saved;
    return n;
}

/* a function declaration, its function keyword read (and async before it: flags) */
static uint32_t p_func_decl(CC, uint32_t flags, uint32_t line, uint32_t start) {
    if (is_p(cc, P_STAR)) {
        if (flags & JF_ASYNC) { cc_error(cc, line, "async generators are not supported"); return node(cc, N_EMPTY, line); }
        lx(CCA);
        flags |= JF_GENERATOR;
    }
    if (cc->tok.type != T_NAME) { cc_error(cc, line, "a function declaration needs a name"); return node(cc, N_EMPTY, line); }
    uint32_t name = cc->tok.str;
    lx(CCA);
    uint32_t d = declare(CCA, name, D_FUNC, line);
    uint32_t f = p_function(CCA, name, flags, 1, line, start);
    cc->decls[d].init = cc->nodes[f].a + 1;
    uint32_t n = node(cc, N_FUNCDECL, line);
    cc->nodes[n].a = cc->nodes[f].a;
    return n;
}

static int is_decl_start(js_cc *cc) {
    return is_kw(cc, K_VAR) || is_kw(cc, K_CONST) || is_kw(cc, K_LET);
}

static uint32_t p_stmt_in(CC) {
    uint32_t line = cc->tok.line, start = cc->tok.start;
    if (cc->err) return node(cc, N_EMPTY, line);
    if (is_p(cc, P_LBRACE)) return p_block(CCA);
    if (is_p(cc, P_SEMI)) { lx(CCA); return node(cc, N_EMPTY, line); }
    if (cc->tok.type == T_NAME && !cc->tok.aux) {
        uint32_t a = cc->tok.str;
        if (a == cc->kw[K_VAR] || a == cc->kw[K_CONST] || a == cc->kw[K_LET]) {
            uint32_t kind = a == cc->kw[K_VAR] ? D_VAR : a == cc->kw[K_CONST] ? D_CONST : D_LET;
            uint32_t n = p_var(CCA, kind, 0, 1);
            semi(CCA);
            return n;
        }
        if (a == cc->kw[K_FUNCTION]) { lx(CCA); return p_func_decl(CCA, 0, line, start); }
        if (a == cc->kw[K_ASYNC]) {
            jtok t0 = cc->tok, p0 = cc->prev;
            uint32_t pos0 = cc->pos, line0 = cc->line;
            lx(CCA);
            if (!cc->tok.nl && is_kw(cc, K_FUNCTION)) { lx(CCA); return p_func_decl(CCA, JF_ASYNC, line, start); }
            cc->tok = t0; cc->prev = p0; cc->pos = pos0; cc->line = line0;
        }
        if (a == cc->kw[K_CLASS]) return p_class(CCA, 1);
        if (a == cc->kw[K_IF]) {
            lx(CCA);
            uint32_t n = node(cc, N_IF, line);
            expect_p(CCA, P_LPAREN, "'(' after if");
            cc->nodes[n].a = p_expr(CCA, 0);
            expect_p(CCA, P_RPAREN, "')'");
            cc->nodes[n].b = p_stmt(CCA);
            if (is_kw(cc, K_ELSE)) { lx(CCA); cc->nodes[n].c = p_stmt(CCA); }
            return n;
        }
        if (a == cc->kw[K_WHILE]) {
            lx(CCA);
            uint32_t n = node(cc, N_WHILE, line);
            expect_p(CCA, P_LPAREN, "'(' after while");
            cc->nodes[n].a = p_expr(CCA, 0);
            expect_p(CCA, P_RPAREN, "')'");
            cc->nodes[n].b = p_stmt(CCA);
            return n;
        }
        if (a == cc->kw[K_DO]) {
            lx(CCA);
            uint32_t n = node(cc, N_DO, line);
            cc->nodes[n].a = p_stmt(CCA);
            if (!is_kw(cc, K_WHILE)) cc_error(cc, cc->tok.line, "expected while after do's body");
            lx(CCA);
            expect_p(CCA, P_LPAREN, "'('");
            cc->nodes[n].b = p_expr(CCA, 0);
            expect_p(CCA, P_RPAREN, "')'");
            if (is_p(cc, P_SEMI)) lx(CCA);
            return n;
        }
        if (a == cc->kw[K_FOR]) {
            lx(CCA);
            if (is_kw(cc, K_AWAIT)) { cc_error(cc, line, "for await is not supported"); return node(cc, N_EMPTY, line); }
            expect_p(CCA, P_LPAREN, "'(' after for");
            uint32_t saved = cc->cur_scope;
            uint32_t sc = scope_new(cc, SC_BLOCK);
            cc->cur_scope = sc;
            uint32_t init = 0;
            if (is_decl_start(cc)) {
                uint32_t kind = is_kw(cc, K_VAR) ? D_VAR : is_kw(cc, K_CONST) ? D_CONST : D_LET;
                init = p_var(CCA, kind, 1, 0);
            } else if (!is_p(cc, P_SEMI)) init = p_expr(CCA, 1);
            if (is_kw(cc, K_OF) || is_kw(cc, K_IN)) {
                uint32_t n = node(cc, N_FORIN, line);
                cc->nodes[n].op = is_kw(cc, K_OF) ? 1 : 0;
                lx(CCA);
                if (cc->nodes[init].k != N_VAR) init = to_pattern(CCA, init);
                cc->nodes[n].a = init;
                /* the value iterated is read outside the loop's bindings */
                cc->cur_scope = saved;
                cc->nodes[n].b = cc->nodes[n].op ? p_assign(CCA, 0) : p_expr(CCA, 0);
                cc->cur_scope = sc;
                expect_p(CCA, P_RPAREN, "')'");
                cc->nodes[n].c = p_stmt(CCA);
                cc->nodes[n].s = sc;
                cc->cur_scope = saved;
                return n;
            }
            uint32_t n = node(cc, N_FOR, line);
            cc->nodes[n].a = init;
            expect_p(CCA, P_SEMI, "';' in a for");
            if (!is_p(cc, P_SEMI)) cc->nodes[n].b = p_expr(CCA, 0);
            expect_p(CCA, P_SEMI, "';' in a for");
            if (!is_p(cc, P_RPAREN)) cc->nodes[n].c = p_expr(CCA, 0);
            expect_p(CCA, P_RPAREN, "')'");
            cc->nodes[n].d = p_stmt(CCA);
            cc->nodes[n].s = sc;
            cc->cur_scope = saved;
            return n;
        }
        if (a == cc->kw[K_RETURN]) {
            lx(CCA);
            uint32_t n = node(cc, N_RETURN, line);
            if (!is_p(cc, P_SEMI) && !is_p(cc, P_RBRACE) && cc->tok.type != T_EOF && !cc->tok.nl) cc->nodes[n].a = p_expr(CCA, 0);
            semi(CCA);
            return n;
        }
        if (a == cc->kw[K_BREAK] || a == cc->kw[K_CONTINUE]) {
            lx(CCA);
            uint32_t n = node(cc, a == cc->kw[K_BREAK] ? N_BREAK : N_CONTINUE, line);
            if (cc->tok.type == T_NAME && !cc->tok.nl) { cc->nodes[n].s = cc->tok.str; lx(CCA); }
            semi(CCA);
            return n;
        }
        if (a == cc->kw[K_THROW]) {
            lx(CCA);
            if (cc->tok.nl) cc_error(cc, line, "a line break after throw");
            uint32_t n = node(cc, N_THROW, line);
            cc->nodes[n].a = p_expr(CCA, 0);
            semi(CCA);
            return n;
        }
        if (a == cc->kw[K_TRY]) {
            lx(CCA);
            uint32_t n = node(cc, N_TRY, line);
            cc->nodes[n].a = p_block(CCA);
            if (is_kw(cc, K_CATCH)) {
                lx(CCA);
                uint32_t saved = cc->cur_scope;
                uint32_t sc = scope_new(cc, SC_BLOCK);
                cc->cur_scope = sc;
                if (is_p(cc, P_LPAREN)) {
                    lx(CCA);
                    cc->nodes[n].b = p_binding(CCA, D_LET);
                    expect_p(CCA, P_RPAREN, "')'");
                }
                cc->nodes[n].c = p_block(CCA);
                cc->nodes[n].s = sc;
                cc->cur_scope = saved;
            }
            if (is_kw(cc, K_FINALLY)) { lx(CCA); cc->nodes[n].d = p_block(CCA); }
            if (!cc->nodes[n].c && !cc->nodes[n].d) cc_error(cc, line, "a try needs a catch or a finally");
            return n;
        }
        if (a == cc->kw[K_SWITCH]) {
            lx(CCA);
            uint32_t n = node(cc, N_SWITCH, line);
            expect_p(CCA, P_LPAREN, "'(' after switch");
            cc->nodes[n].a = p_expr(CCA, 0);
            expect_p(CCA, P_RPAREN, "')'");
            expect_p(CCA, P_LBRACE, "'{'");
            uint32_t saved = cc->cur_scope;
            cc->cur_scope = scope_new(cc, SC_BLOCK);
            cc->nodes[n].s = cc->cur_scope;
            size_t mark = len(cc->tmp);
            while (!is_p(cc, P_RBRACE) && !cc->err) {
                uint32_t cs = node(cc, N_CASE, cc->tok.line);
                if (is_kw(cc, K_CASE)) { lx(CCA); cc->nodes[cs].a = p_expr(CCA, 0); }
                else if (is_kw(cc, K_DEFAULT)) { lx(CCA); cc->nodes[cs].d = 1; }
                else { cc_error(cc, cc->tok.line, "expected case or default"); break; }
                expect_p(CCA, P_COLON, "':'");
                size_t m2 = len(cc->tmp);
                while (!is_kw(cc, K_CASE) && !is_kw(cc, K_DEFAULT) && !is_p(cc, P_RBRACE) && !cc->err) push_back(cc->tmp, p_stmt(CCA));
                list_take(cc, m2, &cc->nodes[cs].b, &cc->nodes[cs].c);
                push_back(cc->tmp, cs);
            }
            expect_p(CCA, P_RBRACE, "'}'");
            list_take(cc, mark, &cc->nodes[n].b, &cc->nodes[n].c);
            cc->cur_scope = saved;
            return n;
        }
        if (a == cc->kw[K_DEBUGGER]) { lx(CCA); semi(CCA); return node(cc, N_EMPTY, line); }
        if (a == cc->kw[K_WITH]) { cc_error(cc, line, "with statements are not supported"); return node(cc, N_EMPTY, line); }
        if (a == cc->kw[K_IMPORT] || a == cc->kw[K_EXPORT]) { cc_error(cc, line, "modules (import and export) are not supported"); return node(cc, N_EMPTY, line); }
        /* a label */
        jtok t0 = cc->tok, p0 = cc->prev;
        uint32_t pos0 = cc->pos, line0 = cc->line;
        lx(CCA);
        if (is_p(cc, P_COLON)) {
            lx(CCA);
            uint32_t n = node(cc, N_LABEL, line);
            cc->nodes[n].s = a;
            cc->nodes[n].a = p_stmt(CCA);
            return n;
        }
        cc->tok = t0; cc->prev = p0; cc->pos = pos0; cc->line = line0;
    }
    uint32_t n = node(cc, N_EXPR, line);
    cc->nodes[n].a = p_expr(CCA, 0);
    semi(CCA);
    return n;
}

/* ---- the scopes resolved ---- */

/* a function's arguments binding, made at its first use */
static uint32_t args_decl(js_cc *cc, uint32_t f) {
    if (cc->fns[f].args_decl) return cc->fns[f].args_decl;
    uint32_t sc = cc->fns[f].scope;
    jdecl d;
    memset(&d, 0, sizeof d);
    d.scope = sc;
    d.atom = cc->kw[K_ARGUMENTS];
    d.kind = D_ARGS;
    d.fn = f;
    push_back(cc->decls, d);
    uint32_t id = (uint32_t)(len(cc->decls) - 1);
    if (!cc->scopes[sc].first) cc->scopes[sc].first = id;
    else {
        uint32_t x = cc->scopes[sc].first;
        while (cc->decls[x].next) x = cc->decls[x].next;
        cc->decls[x].next = id;
    }
    cc->fns[f].args_decl = id;
    cc->fns[f].flags |= JF_ARGUMENTS;
    return id;
}

static void resolve(js_cc *cc) {
    for (size_t r = 1; r < len(cc->refs); r++) {
        jref ref = cc->refs[r];
        uint32_t s = ref.scope, found = 0;
        for (;;) {
            for (uint32_t d = cc->scopes[s].first; d; d = cc->decls[d].next)
                if (cc->decls[d].atom == ref.atom) { found = d; break; }
            if (found) break;
            if (cc->scopes[s].kind == SC_FUNC && ref.atom == cc->kw[K_ARGUMENTS]) {
                uint32_t f = cc->scopes[s].fn;
                if (!(cc->fns[f].flags & (JF_ARROW | FN_FIELDS))) { found = args_decl(cc, f); break; }
            }
            if (cc->scopes[s].kind == SC_SCRIPT || s == 0) break;
            s = cc->scopes[s].parent;
        }
        cc->refs[r].decl = found;
        if (!found) continue;
        uint32_t ds = cc->decls[found].scope;
        if (cc->scopes[ds].kind == SC_SCRIPT) continue;
        if (cc->decls[found].fn != ref.fn) {
            if (cc->decls[found].kind == D_SELF && !cc->decls[found].captured) {
                /* a function's own name, captured: kept in its body's environment */
                uint32_t f = cc->decls[found].fn;
                cc->decls[found].scope = cc->fns[f].scope;
            }
            cc->decls[found].captured = 1;
            cc->scopes[cc->decls[found].scope].has_env = 1;
        }
    }
}

/* ---- the code ---- */

static void emit(js_vm *vm, uint32_t b) { push_back(vm->code, (uint8_t)b); }
static void emit16(js_vm *vm, uint32_t v) { emit(vm, v & 255); emit(vm, v >> 8 & 255); }
static void emit32(js_vm *vm, uint32_t v) { emit(vm, v & 255); emit(vm, v >> 8 & 255); emit(vm, v >> 16 & 255); emit(vm, v >> 24 & 255); }
static uint32_t here(js_vm *vm) { return (uint32_t)len(vm->code); }
static uint32_t rd32c(js_vm *vm, uint32_t at) {
    return (uint32_t)vm->code[at] | (uint32_t)vm->code[at + 1] << 8 | (uint32_t)vm->code[at + 2] << 16 | (uint32_t)vm->code[at + 3] << 24;
}
static void wr32c(js_vm *vm, uint32_t at, uint32_t v) {
    vm->code[at] = (uint8_t)(v & 255);
    vm->code[at + 1] = (uint8_t)(v >> 8 & 255);
    vm->code[at + 2] = (uint8_t)(v >> 16 & 255);
    vm->code[at + 3] = (uint8_t)(v >> 24 & 255);
}
static void patch_to(js_vm *vm, uint32_t at, uint32_t target) { wr32c(vm, at, (uint32_t)((int32_t)target - (int32_t)(at + 4))); }
static uint32_t jump_fwd(js_vm *vm, uint32_t op) { emit(vm, op); uint32_t at = here(vm); emit32(vm, 0); return at; }
static void jump_to(js_vm *vm, uint32_t op, uint32_t target) { emit(vm, op); uint32_t at = here(vm); emit32(vm, 0); patch_to(vm, at, target); }
/* a chain of jumps to a place not yet known: each operand holds the chain's next */
static uint32_t chain_add(js_vm *vm, uint32_t op, uint32_t head) { emit(vm, op); uint32_t at = here(vm); emit32(vm, head); return at; }
static void chain_end(js_vm *vm, uint32_t head, uint32_t target) {
    while (head) { uint32_t next = rd32c(vm, head); patch_to(vm, head, target); head = next; }
}
static uint32_t new_ic(js_vm *vm) { js_ic c = { 0, 0, 0, 0 }; push_back(vm->ics, c); return (uint32_t)(len(vm->ics) - 1); }
static void emit_atom_ic(js_vm *vm, uint32_t op, uint32_t atom) { emit(vm, op); emit32(vm, atom); emit32(vm, new_ic(vm)); }
static void emit_num(js_vm *vm, double d) {
    if (d >= -2147483648.0 && d <= 2147483647.0 && d == (double)(int32_t)d && !(d == 0 && signbit(d))) { emit(vm, OP_INT); emit32(vm, (uint32_t)(int32_t)d); return; }
    push_back(vm->consts, jv_num(d));
    emit(vm, OP_CONST);
    emit32(vm, (uint32_t)(len(vm->consts) - 1));
}
static uint32_t new_slot(js_cc *cc) { return cc->nslots++; }

static void gen_expr(CC, uint32_t n);
static void gen_stmt(CC, uint32_t n);
static void gen_pattern(CC, uint32_t p, int init);

static int is_lexical(uint32_t kind) { return kind == D_LET || kind == D_CONST || kind == D_CLASS; }
static int is_script_decl(js_cc *cc, uint32_t d) { return d && cc->scopes[cc->decls[d].scope].kind == SC_SCRIPT; }

/* the environments between a scope and a declaration's: how deep its cell is */
static uint32_t env_depth(js_cc *cc, uint32_t from, uint32_t to) {
    uint32_t depth = 0;
    for (uint32_t s = from; s != to; s = cc->scopes[s].parent) {
        if (cc->scopes[s].has_env) depth++;
        if (s == 0) break;
    }
    return depth;
}

/* a name read: its declaration's place, from the scope the reference stands in */
static void gen_get(CC, uint32_t atom, uint32_t d, uint32_t scope) {
    if (!d || is_script_decl(cc, d)) { emit_atom_ic(vm, OP_GET_GLOBAL, atom); return; }
    jdecl x = cc->decls[d];
    if (x.kind == D_SELF && !x.captured) { emit(vm, OP_CALLEE); return; }
    if (x.captured) {
        emit(vm, is_lexical(x.kind) ? OP_GET_ENV_CHK : OP_GET_ENV);
        emit(vm, env_depth(cc, scope, x.scope));
        emit16(vm, x.envslot);
        if (is_lexical(x.kind)) emit32(vm, atom);
        return;
    }
    if (is_lexical(x.kind)) { emit(vm, OP_GET_LOC_CHK); emit16(vm, x.slot); emit32(vm, atom); return; }
    emit(vm, OP_GET_LOC);
    emit16(vm, x.slot);
}

/* the value on top stored in a name: kept (keep) or taken; init: its declaration's
   own initialization, which a const and a let not yet set allow */
static void gen_put(CC, uint32_t atom, uint32_t d, uint32_t scope, int keep, int init) {
    if (!d || is_script_decl(cc, d)) {
        if (d && init && is_lexical(cc->decls[d].kind)) {
            if (keep) emit(vm, OP_DUP);
            emit(vm, OP_INIT_LEX);
            emit32(vm, atom);
            return;
        }
        if (d && !init && cc->decls[d].kind == D_CONST) { emit(vm, OP_THROW_CONST); emit32(vm, atom); return; }
        emit_atom_ic(vm, keep ? OP_SET_GLOBAL : OP_PUT_GLOBAL, atom);
        return;
    }
    jdecl x = cc->decls[d];
    if (!init && x.kind == D_CONST) { emit(vm, OP_THROW_CONST); emit32(vm, atom); return; }
    if (x.kind == D_SELF && !x.captured) { if (!keep) emit(vm, OP_POP); return; }
    if (x.captured) {
        emit(vm, keep ? OP_SET_ENV : OP_PUT_ENV);
        emit(vm, env_depth(cc, scope, x.scope));
        emit16(vm, x.envslot);
        return;
    }
    emit(vm, keep ? OP_SET_LOC : OP_PUT_LOC);
    emit16(vm, x.slot);
}

static void gen_ident_get(CC, uint32_t n) {
    uint32_t r = cc->nodes[n].a;
    gen_get(CCA, cc->nodes[n].s, cc->refs[r].decl, cc->refs[r].scope);
}
static void gen_ident_put(CC, uint32_t n, int keep, int init) {
    uint32_t r = cc->nodes[n].a;
    gen_put(CCA, cc->nodes[n].s, cc->refs[r].decl, cc->refs[r].scope, keep, init);
}

/* a declaration's storage set at a scope's entry: a let's hole, a hoisted function */
static void gen_scope_decls(CC, uint32_t sc) {
    for (uint32_t d = cc->scopes[sc].first; d; d = cc->decls[d].next) {
        jdecl x = cc->decls[d];
        if (x.fn != cc->gen_fn && cc->scopes[sc].kind != SC_SCRIPT) continue;
        if (is_lexical(x.kind) && !x.captured) { emit(vm, OP_HOLE); emit(vm, OP_PUT_LOC); emit16(vm, x.slot); }
        if (x.kind == D_FUNC && x.init) {
            emit(vm, OP_CLOSURE);
            emit32(vm, cc->fns[x.init - 1].id);
            gen_put(CCA, x.atom, d, sc, 0, 1);
        }
    }
}

static void ctx_push(js_cc *cc, uint32_t kind, uint32_t nd, uint32_t label) {
    jctx c;
    memset(&c, 0, sizeof c);
    c.kind = kind;
    c.node = nd;
    c.label = label;
    push_back(cc->ctx, c);
}

static void gen_block_enter(CC, uint32_t sc) {
    if (cc->scopes[sc].has_env) {
        emit(vm, OP_ENTER_ENV);
        emit16(vm, cc->scopes[sc].nenv);
        ctx_push(cc, CX_ENV, 0, 0);
    }
    gen_scope_decls(CCA, sc);
}
static void gen_block_leave(CC, uint32_t sc) {
    if (cc->scopes[sc].has_env) {
        emit(vm, OP_LEAVE_ENV);
        pop_back(cc->ctx);
    }
}

/* a context of this kind below index i (a return's way out passes it) */
static int ctx_below(js_cc *cc, size_t i, uint32_t kind) {
    for (size_t j = 0; j < i; j++) if (cc->ctx[j].kind == kind) return 1;
    return 0;
}

/* the code that leaves the contexts above depth: environments left, try handlers
   dropped, finally blocks run, iterators popped and a for-of's closed. A return leaves
   only what what runs on its way out needs: an environment for a finally block below
   it, a for-in's iterator for a for-of's below (its RETURN drops the frame) */
static void unwind_to(CC, size_t depth, int for_return) {
    for (size_t i = len(cc->ctx); i > depth; i--) {
        jctx c = cc->ctx[i - 1];
        if (c.kind == CX_ENV && (!for_return || ctx_below(cc, i - 1, CX_FINALLY))) emit(vm, OP_LEAVE_ENV);
        else if (c.kind == CX_LOOP && c.label == 1 && (!for_return || ctx_below(cc, i - 1, CX_ITER))) emit(vm, OP_POP);
        else if (c.kind == CX_ITER) { emit(vm, OP_END_TRY); emit(vm, OP_ITER_CLOSE); emit(vm, 0); }
        else if (c.kind == CX_TRY) emit(vm, OP_END_TRY);
        else if (c.kind == CX_FINALLY) {
            emit(vm, OP_END_TRY);
            /* the finally block run here, the contexts above it put aside */
            vector<jctx> saved;
            for (size_t j = i - 1; j < len(cc->ctx); j++) push_back(saved, cc->ctx[j]);
            resize(cc->ctx, i - 1);
            gen_stmt(CCA, c.node);
            for (size_t j = 0; j < len(saved); j++) push_back(cc->ctx, at(saved, j));
        }
    }
}

/* a return here has a way out to run: a finally block, a for-of's iterator to close */
static int has_finally(js_cc *cc) {
    for (size_t i = 0; i < len(cc->ctx); i++) if (cc->ctx[i].kind == CX_FINALLY || cc->ctx[i].kind == CX_ITER) return 1;
    return 0;
}

/* the loops' iterators on the stack here, above the frame's slots: a statement's
   stack, which a generator resumed to return is cut back to */
static uint32_t stack_iters(js_cc *cc) {
    uint32_t n = 0;
    for (size_t i = 0; i < len(cc->ctx); i++) if (cc->ctx[i].kind == CX_LOOP && cc->ctx[i].label) n++;
    return n;
}

/* the value on top returned, at a statement's stack: the way out run first */
static void gen_return_top(CC) {
    if (has_finally(cc)) {
        uint32_t tmp = new_slot(cc);
        emit(vm, OP_PUT_LOC);
        emit16(vm, tmp);
        unwind_to(CCA, 0, 1);
        emit(vm, OP_GET_LOC);
        emit16(vm, tmp);
    }
    emit(vm, OP_RETURN);
}

static uint32_t binop_of(uint32_t p) {
    switch (p) {
    case P_PLUS: case P_ADD_ASSIGN: return OP_ADD;
    case P_MINUS: case P_SUB_ASSIGN: return OP_SUB;
    case P_STAR: case P_MUL_ASSIGN: return OP_MUL;
    case P_SLASH: case P_DIV_ASSIGN: return OP_DIV;
    case P_PERCENT: case P_MOD_ASSIGN: return OP_MOD;
    case P_POW: case P_POW_ASSIGN: return OP_POW;
    case P_SHL: case P_SHL_ASSIGN: return OP_SHL;
    case P_SAR: case P_SAR_ASSIGN: return OP_SAR;
    case P_SHR: case P_SHR_ASSIGN: return OP_SHR;
    case P_AMP: case P_BAND_ASSIGN: return OP_BAND;
    case P_PIPE: case P_BOR_ASSIGN: return OP_BOR;
    case P_CARET: case P_BXOR_ASSIGN: return OP_BXOR;
    case P_EQ: return OP_EQ;
    case P_NE: return OP_NE;
    case P_SEQ: return OP_SEQ;
    case P_SNE: return OP_SNE;
    case P_LT: return OP_LT;
    case P_GT: return OP_GT;
    case P_LE: return OP_LE;
    case P_GE: return OP_GE;
    case P_INSTANCEOF: return OP_INSTANCEOF;
    case P_IN: return OP_IN;
    default: return OP_NOP;
    }
}

/* a name that is a local's plain slot (read by GET_LOC, no check): the slot plus one, else 0 */
static uint32_t local_slot1(js_cc *cc, uint32_t n) {
    if (cc->nodes[n].k != N_IDENT) return 0;
    uint32_t d = cc->refs[cc->nodes[n].a].decl;
    if (!d || is_script_decl(cc, d)) return 0;
    jdecl x = cc->decls[d];
    if (x.captured || is_lexical(x.kind) || x.kind == D_SELF) return 0;
    return x.slot + 1;
}
/* an integer literal's value (in *k): 1 if the node is one */
static int int_literal(js_cc *cc, uint32_t n, int32_t *k) {
    jn r = cc->nodes[n];
    if (r.k != N_NUM || r.op != 0) return 0;
    if (!(r.num >= -2147483648.0 && r.num <= 2147483647.0 && r.num == (double)(int32_t)r.num) || (r.num == 0 && signbit(r.num))) return 0;
    *k = (int32_t)r.num;
    return 1;
}

/* the right operand and its operator: an integer literal folded into the operator
   (ADD_I SUB_I MUL_I MOD_I: one instruction, the number its operand) */
static void gen_rhs_op(CC, uint32_t op, uint32_t rhs) {
    int32_t k = 0;
    if ((op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_MOD) && int_literal(cc, rhs, &k)) {
        emit(vm, op == OP_ADD ? OP_ADD_I : op == OP_SUB ? OP_SUB_I : op == OP_MUL ? OP_MUL_I : OP_MOD_I);
        emit32(vm, (uint32_t)k);
        return;
    }
    gen_expr(CCA, rhs);
    emit(vm, op);
}
/* a binary operator's operands and itself: a local with an integer or another local
   one instruction */
static void gen_binary(CC, uint32_t op, uint32_t lhs, uint32_t rhs) {
    uint32_t l = local_slot1(cc, lhs);
    int32_t k = 0;
    if (l && (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_MOD) && int_literal(cc, rhs, &k)) {
        emit(vm, op == OP_ADD ? OP_ADD_LI : op == OP_SUB ? OP_SUB_LI : op == OP_MUL ? OP_MUL_LI : OP_MOD_LI);
        emit16(vm, l - 1);
        emit32(vm, (uint32_t)k);
        return;
    }
    uint32_t r = l ? local_slot1(cc, rhs) : 0;
    if (r && (op == OP_ADD || op == OP_SUB || op == OP_MUL)) {
        emit(vm, op == OP_ADD ? OP_ADD_LL : op == OP_SUB ? OP_SUB_LL : OP_MUL_LL);
        emit16(vm, l - 1);
        emit16(vm, r - 1);
        return;
    }
    gen_expr(CCA, lhs);
    gen_rhs_op(CCA, op, rhs);
}

/* an anonymous function or class given the name it is bound to */
static void name_fn(js_cc *cc, uint32_t value, uint32_t atom) {
    if (!value || !atom) return;
    if (cc->nodes[value].k == N_FUNC && !cc->fns[cc->nodes[value].a].name) cc->fns[cc->nodes[value].a].name = atom;
}

/* the value on top, if undefined, replaced by the default */
static void gen_default(CC, uint32_t def) {
    if (!def) return;
    emit(vm, OP_DUP);
    emit(vm, OP_UNDEF);
    emit(vm, OP_SEQ);
    uint32_t skip = jump_fwd(vm, OP_JF);
    emit(vm, OP_POP);
    gen_expr(CCA, def);
    patch_to(vm, skip, here(vm));
}

/* the arguments of a call, then the call: argc values or an array of them */
static int has_spread(js_cc *cc, uint32_t off, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (cc->nodes[cc->lists[off + i]].k == N_SPREAD) return 1;
    return 0;
}
static void gen_list_array(CC, uint32_t off, uint32_t n) {
    emit(vm, OP_ARRAY);
    emit32(vm, 0);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t e = cc->lists[off + i];
        if (cc->nodes[e].k == N_SPREAD) { gen_expr(CCA, cc->nodes[e].a); emit(vm, OP_SPREAD); }
        else if (cc->nodes[e].k == N_HOLE) emit(vm, OP_APPEND_HOLE);
        else { gen_expr(CCA, e); emit(vm, OP_APPEND); }
    }
}
static void gen_args_call(CC, uint32_t off, uint32_t n, uint32_t op, uint32_t op_arr) {
    /* spread, or more than 64 values (JS_STACK_MARGIN): the arguments an array */
    if (has_spread(cc, off, n) || n > 64) { gen_list_array(CCA, off, n); emit(vm, op_arr); return; }
    for (uint32_t i = 0; i < n; i++) gen_expr(CCA, cc->lists[off + i]);
    emit(vm, op);
    emit16(vm, n);
}

static void gen_member(CC, uint32_t n, int keep_obj) {
    jn m = cc->nodes[n];
    gen_expr(CCA, m.a);
    if (m.op & NF_OPTIONAL) cc->chain = chain_add(vm, OP_JNULLISH, cc->chain);
    if (m.op & NF_COMPUTED) { gen_expr(CCA, m.b); emit(vm, keep_obj ? OP_GET_ELEM2 : OP_GET_ELEM); }
    else emit_atom_ic(vm, keep_obj ? OP_GET_PROP2 : OP_GET_PROP, m.s);
}

static void gen_call(CC, uint32_t n) {
    jn c = cc->nodes[n];
    uint32_t callee = c.a;
    uint32_t ck = cc->nodes[callee].k;
    if (ck == N_MEMBER) gen_member(CCA, callee, 1);
    else if (ck == N_SUPER_MEMBER) {
        emit(vm, OP_THIS);
        if (cc->nodes[callee].op & NF_COMPUTED) { gen_expr(CCA, cc->nodes[callee].b); emit(vm, OP_GET_SUPER_ELEM); }
        else { emit(vm, OP_GET_SUPER); emit32(vm, cc->nodes[callee].s); }
    } else if (!(c.op & NF_OPTIONAL) && !has_spread(cc, c.b, c.c) && c.c <= 64) {
        /* a plain call: no this pushed, the callee's own */
        gen_expr(CCA, callee);
        gen_args_call(CCA, c.b, c.c, OP_CALL0, OP_CALL_ARR);
        return;
    } else {
        emit(vm, OP_UNDEF);
        gen_expr(CCA, callee);
    }
    if (c.op & NF_OPTIONAL) cc->chain = chain_add(vm, OP_JNULLISH2, cc->chain);
    gen_args_call(CCA, c.b, c.c, OP_CALL, OP_CALL_ARR);
}

static void gen_object(CC, uint32_t n) {
    emit(vm, OP_OBJECT);
    uint32_t off = cc->nodes[n].a, cnt = cc->nodes[n].b;
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t p = cc->lists[off + i];
        jn pr = cc->nodes[p];
        int computed = pr.d & NF_COMPUTED;
        if (pr.op == PK_SPREAD) { gen_expr(CCA, pr.b); emit(vm, OP_COPY_PROPS); continue; }
        if (pr.op == PK_COVER) { cc_error(cc, pr.line, "an object literal's '=' belongs in a destructuring pattern"); return; }
        if (!computed) name_fn(cc, pr.b, cc->nodes[pr.a].s);
        if (pr.op == PK_GET || pr.op == PK_SET) {
            if (computed) gen_expr(CCA, pr.a);
            gen_expr(CCA, pr.b);
            if (computed) emit(vm, pr.op == PK_GET ? OP_DEF_GET_ELEM : OP_DEF_SET_ELEM);
            else { emit(vm, pr.op == PK_GET ? OP_DEF_GET : OP_DEF_SET); emit32(vm, cc->nodes[pr.a].s); }
            continue;
        }
        if (computed) { gen_expr(CCA, pr.a); gen_expr(CCA, pr.b); emit(vm, OP_DEF_ELEM); }
        else { gen_expr(CCA, pr.b); emit(vm, OP_DEF_PROP); emit32(vm, cc->nodes[pr.a].s); }
    }
}

static void gen_class(CC, uint32_t ci) {
    jclass k = cc->classes[ci];
    if (k.extends) gen_expr(CCA, k.extends);
    else emit(vm, OP_HOLE);
    emit(vm, OP_CLASS);
    emit32(vm, cc->fns[k.ctor].id);
    emit32(vm, k.name);
    for (uint32_t i = 0; i < k.nmembers; i++) {
        uint32_t p = cc->lists[k.members + i];
        jn pr = cc->nodes[p];
        int is_static = pr.d & NF_STATIC, computed = pr.d & NF_COMPUTED;
        if (pr.op == PK_FIELD && !is_static) continue;
        if (is_static) emit(vm, OP_SWAP);
        if (pr.op == PK_FIELD) {
            if (computed) gen_expr(CCA, pr.a);
            if (pr.b) gen_expr(CCA, pr.b);
            else emit(vm, OP_UNDEF);
            if (computed) emit(vm, OP_DEF_ELEM);
            else { emit(vm, OP_DEF_PROP); emit32(vm, cc->nodes[pr.a].s); }
        } else {
            if (computed) gen_expr(CCA, pr.a);
            gen_expr(CCA, pr.b);
            uint32_t op;
            if (pr.op == PK_GET) op = computed ? OP_DEF_GET_ELEM : OP_DEF_GET;
            else if (pr.op == PK_SET) op = computed ? OP_DEF_SET_ELEM : OP_DEF_SET;
            else op = computed ? OP_DEF_METHOD_ELEM : OP_DEF_METHOD;
            emit(vm, op);
            if (!computed) emit32(vm, cc->nodes[pr.a].s);
        }
        if (is_static) emit(vm, OP_SWAP);
    }
    emit(vm, OP_POP);
    if (k.fields_fn) {
        emit(vm, OP_CLOSURE);
        emit32(vm, cc->fns[k.fields_fn].id);
        emit(vm, OP_SET_FIELDS);
    }
}

/* the target of an assignment or a declaration, the value on top taken */
static void gen_pattern(CC, uint32_t p, int init) {
    jn t = cc->nodes[p];
    if (t.k == N_IDENT) { gen_ident_put(CCA, p, 0, init); return; }
    if (t.k == N_MEMBER) {
        /* v -> o v (or o k v) -> v -> nothing */
        gen_expr(CCA, t.a);
        if (t.op & NF_COMPUTED) {
            gen_expr(CCA, t.b);
            emit(vm, OP_ROT3);
            emit(vm, OP_ROT3);
            emit(vm, OP_PUT_ELEM);
        } else {
            emit(vm, OP_SWAP);
            emit_atom_ic(vm, OP_PUT_PROP, t.s);
        }
        emit(vm, OP_POP);
        return;
    }
    if (t.k == N_PELEM) {
        gen_default(CCA, t.b);
        gen_pattern(CCA, t.a, init);
        return;
    }
    if (t.k == N_PATTERN_OBJ) {
        for (uint32_t i = 0; i < t.b; i++) {
            uint32_t e = cc->lists[t.a + i];
            jn el = cc->nodes[e];
            if (el.op & NF_REST) {
                emit(vm, OP_DUP);
                emit(vm, OP_OBJECT);
                emit(vm, OP_SWAP);
                emit(vm, OP_COPY_PROPS);
                for (uint32_t j = 0; j < i; j++) {
                    jn prev = cc->nodes[cc->lists[t.a + j]];
                    if (prev.op & NF_COMPUTED) continue;
                    emit(vm, OP_DUP);
                    emit(vm, OP_DEL_PROP);
                    emit32(vm, cc->nodes[prev.c].s);
                    emit(vm, OP_POP);
                }
                gen_pattern(CCA, el.a, init);
                continue;
            }
            emit(vm, OP_DUP);
            if (el.op & NF_COMPUTED) { gen_expr(CCA, el.c); emit(vm, OP_GET_ELEM); }
            else emit_atom_ic(vm, OP_GET_PROP, cc->nodes[el.c].s);
            gen_default(CCA, el.b);
            gen_pattern(CCA, el.a, init);
        }
        emit(vm, OP_POP);
        return;
    }
    if (t.k == N_PATTERN_ARR) {
        /* any iterable read as the array of its values the pattern takes */
        int rest = 0;
        for (uint32_t i = 0; i < t.b; i++) if (cc->nodes[cc->lists[t.a + i]].op & NF_REST) rest = 1;
        emit(vm, OP_DESTRUCT_ARR);
        emit16(vm, rest || t.b >= 0xFFFF ? 0xFFFF : t.b);
        for (uint32_t i = 0; i < t.b; i++) {
            uint32_t e = cc->lists[t.a + i];
            jn el = cc->nodes[e];
            if (!el.a) continue;
            emit(vm, OP_DUP);
            if (el.op & NF_REST) {
                emit_atom_ic(vm, OP_GET_PROP2, cc->kw[K_SLICE]);
                emit_num(vm, (double)i);
                emit(vm, OP_CALL);
                emit16(vm, 1);
            } else {
                emit_num(vm, (double)i);
                emit(vm, OP_GET_ELEM);
                gen_default(CCA, el.b);
            }
            gen_pattern(CCA, el.a, init);
        }
        emit(vm, OP_POP);
        return;
    }
    cc_error(cc, t.line, "invalid destructuring target");
}

static void gen_assign(CC, uint32_t n) {
    jn as = cc->nodes[n];
    uint32_t op = as.op, t = as.a;
    jn tg = cc->nodes[t];
    if (op == P_ASSIGN) {
        if (tg.k == N_PATTERN_OBJ || tg.k == N_PATTERN_ARR) { gen_expr(CCA, as.b); emit(vm, OP_DUP); gen_pattern(CCA, t, 0); return; }
        if (tg.k == N_IDENT) { name_fn(cc, as.b, tg.s); gen_expr(CCA, as.b); gen_ident_put(CCA, t, 1, 0); return; }
        gen_expr(CCA, tg.a);
        if (tg.op & NF_COMPUTED) { gen_expr(CCA, tg.b); gen_expr(CCA, as.b); emit(vm, OP_PUT_ELEM); }
        else { gen_expr(CCA, as.b); emit_atom_ic(vm, OP_PUT_PROP, tg.s); }
        return;
    }
    int logical = op == P_LAND_ASSIGN || op == P_LOR_ASSIGN || op == P_NULLISH_ASSIGN;
    uint32_t jop = op == P_LAND_ASSIGN ? OP_JF_KEEP : op == P_LOR_ASSIGN ? OP_JT_KEEP : OP_JNN_KEEP;
    if (tg.k == N_IDENT) {
        gen_ident_get(CCA, t);
        if (logical) {
            uint32_t end = jump_fwd(vm, jop);
            gen_expr(CCA, as.b);
            gen_ident_put(CCA, t, 1, 0);
            patch_to(vm, end, here(vm));
            return;
        }
        gen_rhs_op(CCA, binop_of(op), as.b);
        gen_ident_put(CCA, t, 1, 0);
        return;
    }
    if (tg.k != N_MEMBER) { cc_error(cc, as.line, "invalid assignment target"); return; }
    int computed = tg.op & NF_COMPUTED;
    gen_expr(CCA, tg.a);
    if (computed) { gen_expr(CCA, tg.b); emit(vm, OP_DUP2); emit(vm, OP_GET_ELEM); }
    else { emit(vm, OP_DUP); emit_atom_ic(vm, OP_GET_PROP, tg.s); }
    if (logical) {
        uint32_t keep = jump_fwd(vm, jop);
        gen_expr(CCA, as.b);
        if (computed) emit(vm, OP_PUT_ELEM);
        else emit_atom_ic(vm, OP_PUT_PROP, tg.s);
        uint32_t end = jump_fwd(vm, OP_JMP);
        patch_to(vm, keep, here(vm));
        if (computed) { emit(vm, OP_ROT3); emit(vm, OP_POP); emit(vm, OP_POP); }
        else { emit(vm, OP_SWAP); emit(vm, OP_POP); }
        patch_to(vm, end, here(vm));
        return;
    }
    gen_rhs_op(CCA, binop_of(op), as.b);
    if (computed) emit(vm, OP_PUT_ELEM);
    else emit_atom_ic(vm, OP_PUT_PROP, tg.s);
}

static void gen_update(CC, uint32_t n, int force_prefix) {
    jn u = cc->nodes[n];
    uint32_t t = u.a, step = u.op == P_INC ? OP_INC : OP_DEC;
    int prefix = (u.d & NF_PREFIX) || force_prefix;
    jn tg = cc->nodes[t];
    if (tg.k == N_IDENT) {
        gen_ident_get(CCA, t);
        if (prefix) { emit(vm, step); gen_ident_put(CCA, t, 1, 0); return; }
        emit(vm, OP_TONUM);
        emit(vm, OP_DUP);
        emit(vm, step);
        gen_ident_put(CCA, t, 0, 0);
        return;
    }
    if (tg.k != N_MEMBER) { cc_error(cc, u.line, "invalid target of ++ or --"); return; }
    int computed = tg.op & NF_COMPUTED;
    gen_expr(CCA, tg.a);
    if (computed) { gen_expr(CCA, tg.b); emit(vm, OP_DUP2); emit(vm, OP_GET_ELEM); }
    else { emit(vm, OP_DUP); emit_atom_ic(vm, OP_GET_PROP, tg.s); }
    if (prefix) {
        emit(vm, step);
        if (computed) emit(vm, OP_PUT_ELEM);
        else emit_atom_ic(vm, OP_PUT_PROP, tg.s);
        return;
    }
    emit(vm, OP_TONUM);
    emit(vm, OP_DUP);
    emit(vm, computed ? OP_ROT4 : OP_ROT3);
    emit(vm, step);
    if (computed) emit(vm, OP_PUT_ELEM);
    else emit_atom_ic(vm, OP_PUT_PROP, tg.s);
    emit(vm, OP_POP);
}

static void gen_expr(CC, uint32_t n) {
    if (cc->err) return;
    jn e = cc->nodes[n];
    switch (e.k) {
    case N_NUM:
        if (e.op == 1) emit(vm, OP_NULL);
        else if (e.op == 2) emit(vm, OP_TRUE);
        else if (e.op == 3) emit(vm, OP_FALSE);
        else emit_num(vm, e.num);
        return;
    case N_STR: emit(vm, OP_STR); emit32(vm, e.s); return;
    case N_TEMPLATE: {
        if (e.op == 1) {
            /* a tag's strings: made once, here, and the same array each time */
            uint32_t cooked = js_array_new(vm, 0), raw = js_array_new(vm, 0);
            for (uint32_t i = 0; i < e.b; i += 2) {
                jn part = cc->nodes[cc->lists[e.a + i]];
                js_array_push(vm, cooked, jv_str(part.s));
                js_array_push(vm, raw, jv_str(part.b));
            }
            js_define(vm, cooked, js_atom_ascii(vm, "raw"), jv_obj(raw), JS_SHAPE_HIDDEN);
            vm->objs[raw].flags = (uint16_t)((vm->objs[raw].flags | JO_FROZEN) & ~JO_EXTENSIBLE);
            vm->objs[cooked].flags = (uint16_t)((vm->objs[cooked].flags | JO_FROZEN) & ~JO_EXTENSIBLE);
            push_back(vm->consts, jv_obj(cooked));
            emit(vm, OP_CONST);
            emit32(vm, (uint32_t)(len(vm->consts) - 1));
            return;
        }
        for (uint32_t i = 0; i < e.b; i++) {
            uint32_t part = cc->lists[e.a + i];
            if (cc->nodes[part].k == N_STR) {
                if (i > 0 && vm->strs[cc->nodes[part].s].len == 0) continue;
                emit(vm, OP_STR);
                emit32(vm, cc->nodes[part].s);
            } else { gen_expr(CCA, part); emit(vm, OP_TOSTR); }
            if (i > 0) emit(vm, OP_ADD);
        }
        return;
    }
    case N_REGEX:
        emit_atom_ic(vm, OP_GET_GLOBAL, js_atom_ascii(vm, "RegExp"));
        emit(vm, OP_STR);
        emit32(vm, e.s);
        emit(vm, OP_STR);
        emit32(vm, e.a);
        emit(vm, OP_NEW);
        emit16(vm, 2);
        return;
    case N_IDENT: gen_ident_get(CCA, n); return;
    case N_THIS: emit(vm, OP_THIS); return;
    case N_NEW_TARGET: emit(vm, OP_NEW_TARGET); return;
    case N_SUPER_MEMBER:
        if (e.op & NF_COMPUTED) { gen_expr(CCA, e.b); emit(vm, OP_GET_SUPER_ELEM); }
        else { emit(vm, OP_GET_SUPER); emit32(vm, e.s); }
        return;
    case N_SUPER_CALL:
        gen_args_call(CCA, e.b, e.c, OP_SUPER_CALL, OP_SUPER_CALL_ARR);
        emit(vm, OP_INIT_FIELDS);
        return;
    case N_ARRAY: {
        int simple = 1;
        for (uint32_t i = 0; i < e.b; i++) {
            uint32_t k = cc->nodes[cc->lists[e.a + i]].k;
            if (k == N_SPREAD || k == N_HOLE) simple = 0;
        }
        if (simple && e.b < 64) {
            for (uint32_t i = 0; i < e.b; i++) gen_expr(CCA, cc->lists[e.a + i]);
            emit(vm, OP_ARRAY);
            emit32(vm, e.b);
        } else gen_list_array(CCA, e.a, e.b);
        return;
    }
    case N_OBJECT: gen_object(CCA, n); return;
    case N_FUNC: emit(vm, OP_CLOSURE); emit32(vm, cc->fns[e.a].id); return;
    case N_CLASS: gen_class(CCA, e.a); return;
    case N_UNARY: {
        if (e.op == 1000) {
            jn a = cc->nodes[e.a];
            if (a.k == N_IDENT && (!cc->refs[a.a].decl || is_script_decl(cc, cc->refs[a.a].decl))) { emit(vm, OP_TYPEOF_GLOBAL); emit32(vm, a.s); return; }
            gen_expr(CCA, e.a);
            emit(vm, OP_TYPEOF);
            return;
        }
        if (e.op == 1001) { gen_expr(CCA, e.a); emit(vm, OP_POP); emit(vm, OP_UNDEF); return; }
        if (e.op == 1002) {
            jn a = cc->nodes[e.a];
            if (a.k == N_MEMBER) {
                gen_expr(CCA, a.a);
                if (a.op & NF_COMPUTED) { gen_expr(CCA, a.b); emit(vm, OP_DEL_ELEM); }
                else { emit(vm, OP_DEL_PROP); emit32(vm, a.s); }
                return;
            }
            if (a.k != N_IDENT) { gen_expr(CCA, e.a); emit(vm, OP_POP); }
            emit(vm, a.k == N_IDENT ? OP_FALSE : OP_TRUE);
            return;
        }
        gen_expr(CCA, e.a);
        emit(vm, e.op == P_MINUS ? OP_NEG : e.op == P_PLUS ? OP_PLUS : e.op == P_BANG ? OP_NOT : OP_BNOT);
        return;
    }
    case N_UPDATE: gen_update(CCA, n, 0); return;
    case N_BINARY: gen_binary(CCA, binop_of(e.op), e.a, e.b); return;
    case N_LOGICAL: {
        gen_expr(CCA, e.a);
        uint32_t end = jump_fwd(vm, e.op == P_AND ? OP_JF_KEEP : e.op == P_OR ? OP_JT_KEEP : OP_JNN_KEEP);
        gen_expr(CCA, e.b);
        patch_to(vm, end, here(vm));
        return;
    }
    case N_COND: {
        uint32_t els = gen_test_jump(CCA, e.a, 0);
        gen_expr(CCA, e.b);
        uint32_t end = jump_fwd(vm, OP_JMP);
        patch_to(vm, els, here(vm));
        gen_expr(CCA, e.c);
        patch_to(vm, end, here(vm));
        return;
    }
    case N_ASSIGN: gen_assign(CCA, n); return;
    case N_SEQ: gen_expr(CCA, e.a); emit(vm, OP_POP); gen_expr(CCA, e.b); return;
    case N_MEMBER: gen_member(CCA, n, 0); return;
    case N_CALL: gen_call(CCA, n); return;
    case N_NEW:
        gen_expr(CCA, e.a);
        gen_args_call(CCA, e.b, e.c, OP_NEW, OP_NEW_ARR);
        return;
    case N_CHAIN: {
        uint32_t saved = cc->chain;
        cc->chain = 0;
        gen_expr(CCA, e.a);
        chain_end(vm, cc->chain, here(vm));
        cc->chain = saved;
        return;
    }
    case N_YIELD: {
        /* resumed: its value (next), an exception at it (throw), or a return from it
           (return), the stack cut to its statement's and the way out run */
        if (e.op == 1) {
            /* yield*: each step the inner iterator's, sent what this one was sent */
            gen_expr(CCA, e.a);
            emit(vm, OP_GET_ITER);
            emit(vm, OP_UNDEF);
            emit_num(vm, 0);
            uint32_t loop = here(vm);
            emit(vm, OP_YIELD_STAR);
            uint32_t at_done = here(vm);
            emit32(vm, 0);
            uint32_t at_ret = here(vm);
            emit32(vm, 0);
            emit16(vm, stack_iters(cc));
            emit(vm, OP_YIELD);
            jump_to(vm, OP_JMP, loop);
            patch_to(vm, at_ret, here(vm));
            gen_return_top(CCA);
            patch_to(vm, at_done, here(vm));
            return;
        }
        if (e.a) gen_expr(CCA, e.a);
        else emit(vm, OP_UNDEF);
        emit(vm, OP_YIELD);
        uint32_t r = jump_fwd(vm, OP_GEN_RESUME);
        emit16(vm, stack_iters(cc));
        uint32_t after = jump_fwd(vm, OP_JMP);
        patch_to(vm, r, here(vm));
        gen_return_top(CCA);
        patch_to(vm, after, here(vm));
        return;
    }
    case N_AWAIT:
        gen_expr(CCA, e.a);
        emit(vm, OP_AWAIT);
        emit(vm, OP_GEN_RESUME);
        emit32(vm, 0);
        emit16(vm, 0);
        return;
    case N_SPREAD: cc_error(cc, e.line, "'...' only spreads in a call, an array or an object"); return;
    case N_HOLE: emit(vm, OP_UNDEF); return;
    case N_PATTERN_OBJ: case N_PATTERN_ARR: cc_error(cc, e.line, "a destructuring pattern where a value should be"); return;
    default: cc_error(cc, e.line, "an expression the compiler does not know"); return;
    }
}


/* an expression whose value is not used: a statement's, a for's update */
static void gen_void(CC, uint32_t n) {
    if (cc->err) return;
    jn e = cc->nodes[n];
    if (e.k == N_UPDATE) {
        jn tg = cc->nodes[e.a];
        if (tg.k == N_IDENT) {
            uint32_t d = cc->refs[tg.a].decl;
            if (d && !is_script_decl(cc, d) && !cc->decls[d].captured && cc->decls[d].kind != D_CONST && cc->decls[d].kind != D_SELF &&
                cc->decls[d].kind != D_CLASS) {
                emit(vm, e.op == P_INC ? OP_INC_LOC : OP_DEC_LOC);
                emit16(vm, cc->decls[d].slot);
                return;
            }
        }
        gen_update(CCA, n, 1);
        emit(vm, OP_POP);
        return;
    }
    if (e.k == N_ASSIGN && e.op == P_ASSIGN && cc->nodes[e.a].k == N_IDENT) {
        name_fn(cc, e.b, cc->nodes[e.a].s);
        gen_expr(CCA, e.b);
        gen_ident_put(CCA, e.a, 0, 0);
        return;
    }
    if (e.k == N_ASSIGN && cc->nodes[e.a].k == N_IDENT && binop_of(e.op) != OP_NOP && e.op != P_LAND_ASSIGN && e.op != P_LOR_ASSIGN &&
        e.op != P_NULLISH_ASSIGN) {
        gen_ident_get(CCA, e.a);
        gen_rhs_op(CCA, binop_of(e.op), e.b);
        gen_ident_put(CCA, e.a, 0, 0);
        return;
    }
    if (e.k == N_SEQ) { gen_void(CCA, e.a); gen_void(CCA, e.b); return; }
    gen_expr(CCA, n);
    emit(vm, OP_POP);
}

/* a comparison's jump, by LT LE GT GE SEQ SNE EQ NE: the relations jump when they
   hold or when they do not (NaN neither); an equality's negation is the other one */
static const uint8_t cmp_when_true[8] = { OP_JLT, OP_JLE, OP_JGT, OP_JGE, OP_JSEQ, OP_JSNE, OP_JEQ, OP_JNE };
static const uint8_t cmp_when_false[8] = { OP_JNLT, OP_JNLE, OP_JNGT, OP_JNGE, OP_JSNE, OP_JSEQ, OP_JNE, OP_JEQ };

/* a condition that jumps when its truth is when: the jump's operand, to be patched
   (a comparison's jump is one instruction) */
static uint32_t gen_test_jump(CC, uint32_t n, int when) {
    jn e = cc->nodes[n];
    if (e.k == N_BINARY) {
        int cmp = e.op == P_LT ? 0 : e.op == P_LE ? 1 : e.op == P_GT ? 2 : e.op == P_GE ? 3 : e.op == P_SEQ ? 4 : e.op == P_SNE ? 5 :
                  e.op == P_EQ ? 6 : e.op == P_NE ? 7 : -1;
        if (cmp >= 0) {
            uint32_t l = cmp < 4 ? local_slot1(cc, e.a) : 0;
            int32_t k = 0;
            if (l && int_literal(cc, e.b, &k)) {
                /* a local and an integer: their comparison and its jump one instruction */
                emit(vm, (uint32_t)(when ? cmp_when_true[cmp] : cmp_when_false[cmp]) - OP_JLT + OP_JLT_LI);
                emit16(vm, l - 1);
                emit32(vm, (uint32_t)k);
                uint32_t at = here(vm);
                emit32(vm, 0);
                return at;
            }
            gen_expr(CCA, e.a);
            gen_expr(CCA, e.b);
            return jump_fwd(vm, when ? cmp_when_true[cmp] : cmp_when_false[cmp]);
        }
    }
    if (e.k == N_UNARY && e.op == P_BANG) return gen_test_jump(CCA, e.a, !when);
    gen_expr(CCA, n);
    return jump_fwd(vm, when ? OP_JT : OP_JF);
}

/* break or continue: the context it goes to, the contexts on the way left */
static void gen_jump(CC, uint32_t n, int is_break) {
    uint32_t label = cc->nodes[n].s;
    size_t t = len(cc->ctx);
    int found = 0;
    while (t > 0) {
        jctx c = cc->ctx[t - 1];
        if (label) {
            if (c.kind == CX_LABEL && c.label == label) {
                if (is_break) { found = 1; break; }
                /* continue label: the loop the label names, just inside it (a for-of's
                   handler between them) */
                size_t u = t;
                if (u < len(cc->ctx) && cc->ctx[u].kind == CX_ITER) u++;
                if (u < len(cc->ctx) && cc->ctx[u].kind == CX_LOOP) { t = u + 1; found = 1; break; }
                cc_error(cc, cc->nodes[n].line, "continue's label names no loop");
                return;
            }
        } else if (c.kind == CX_LOOP || (is_break && c.kind == CX_SWITCH)) { found = 1; break; }
        t--;
    }
    if (!found) { cc_error(cc, cc->nodes[n].line, is_break ? "break outside a loop or a switch" : "continue outside a loop"); return; }
    unwind_to(CCA, t, 0);
    jctx c = cc->ctx[t - 1];
    if (is_break) {
        if (c.kind == CX_LOOP && c.label == 1) emit(vm, OP_POP);
        else if (c.kind == CX_LOOP && c.label == 2) { emit(vm, OP_END_TRY); emit(vm, OP_ITER_CLOSE); emit(vm, 0); }
        cc->ctx[t - 1].brk = chain_add(vm, OP_JMP, cc->ctx[t - 1].brk);
    } else if (c.has_cont) jump_to(vm, OP_JMP, c.cont_pc);
    else cc->ctx[t - 1].cont = chain_add(vm, OP_JMP, cc->ctx[t - 1].cont);
}

static void gen_statements(CC, uint32_t off, uint32_t n) {
    for (uint32_t i = 0; i < n && !cc->err; i++) gen_stmt(CCA, cc->lists[off + i]);
}

static void gen_var(CC, uint32_t n) {
    jn v = cc->nodes[n];
    for (uint32_t i = 0; i < v.b; i++) {
        jn d = cc->nodes[cc->lists[v.a + i]];
        if (d.b) {
            if (cc->nodes[d.a].k == N_IDENT) name_fn(cc, d.b, cc->nodes[d.a].s);
            gen_expr(CCA, d.b);
            gen_pattern(CCA, d.a, 1);
        } else if (v.op != D_VAR) {
            emit(vm, OP_UNDEF);
            gen_pattern(CCA, d.a, 1);
        }
    }
}

static void gen_try(CC, uint32_t n) {
    jn t = cc->nodes[n];
    if (!t.d) {
        uint32_t h = jump_fwd(vm, OP_TRY);
        ctx_push(cc, CX_TRY, 0, 0);
        gen_stmt(CCA, t.a);
        pop_back(cc->ctx);
        emit(vm, OP_END_TRY);
        uint32_t end = jump_fwd(vm, OP_JMP);
        patch_to(vm, h, here(vm));
        gen_block_enter(CCA, t.s);
        if (t.b) gen_pattern(CCA, t.b, 1);
        else emit(vm, OP_POP);
        gen_stmt(CCA, t.c);
        gen_block_leave(CCA, t.s);
        patch_to(vm, end, here(vm));
        return;
    }
    uint32_t tmp = new_slot(cc);
    uint32_t h1 = jump_fwd(vm, OP_TRY);
    ctx_push(cc, CX_FINALLY, t.d, 0);
    gen_stmt(CCA, t.a);
    pop_back(cc->ctx);
    emit(vm, OP_END_TRY);
    uint32_t fin = jump_fwd(vm, OP_JMP);
    uint32_t h2 = 0;
    patch_to(vm, h1, here(vm));
    if (t.c) {
        h2 = jump_fwd(vm, OP_TRY);
        ctx_push(cc, CX_FINALLY, t.d, 0);
        gen_block_enter(CCA, t.s);
        if (t.b) gen_pattern(CCA, t.b, 1);
        else emit(vm, OP_POP);
        gen_stmt(CCA, t.c);
        gen_block_leave(CCA, t.s);
        pop_back(cc->ctx);
        emit(vm, OP_END_TRY);
        patch_to(vm, fin, here(vm));
        gen_stmt(CCA, t.d);
        uint32_t end = jump_fwd(vm, OP_JMP);
        patch_to(vm, h2, here(vm));
        emit(vm, OP_PUT_LOC);
        emit16(vm, tmp);
        gen_stmt(CCA, t.d);
        emit(vm, OP_GET_LOC);
        emit16(vm, tmp);
        emit(vm, OP_THROW);
        patch_to(vm, end, here(vm));
        return;
    }
    /* try / finally: the exception kept while the finally block runs, then thrown again */
    emit(vm, OP_PUT_LOC);
    emit16(vm, tmp);
    gen_stmt(CCA, t.d);
    emit(vm, OP_GET_LOC);
    emit16(vm, tmp);
    emit(vm, OP_THROW);
    patch_to(vm, fin, here(vm));
    gen_stmt(CCA, t.d);
}

static void gen_loop_ctx(js_cc *cc, uint32_t label_iter, uint32_t cont_pc, int has_cont) {
    jctx c;
    memset(&c, 0, sizeof c);
    c.kind = CX_LOOP;
    c.label = label_iter;
    c.cont_pc = cont_pc;
    c.has_cont = (uint32_t)has_cont;
    push_back(cc->ctx, c);
}

/* return c ? a : b: each branch its own return, no jump between them */
static void gen_return_value(CC, uint32_t v) {
    jn e = cc->nodes[v];
    if (e.k == N_COND) {
        uint32_t els = gen_test_jump(CCA, e.a, 0);
        gen_return_value(CCA, e.b);
        patch_to(vm, els, here(vm));
        gen_return_value(CCA, e.c);
        return;
    }
    uint32_t l = local_slot1(cc, v);
    if (l) { emit(vm, OP_RETURN_LOC); emit16(vm, l - 1); return; }
    gen_expr(CCA, v);
    emit(vm, OP_RETURN);
}

static void gen_stmt(CC, uint32_t n) {
    if (cc->err) return;
    jn s = cc->nodes[n];
    switch (s.k) {
    case N_EMPTY: case N_FUNCDECL: return;
    case N_EXPR: gen_void(CCA, s.a); return;
    case N_VAR: gen_var(CCA, n); return;
    case N_CLASSDECL: {
        jclass k = cc->classes[s.a];
        gen_class(CCA, s.a);
        if (k.decl) gen_put(CCA, k.name, k.decl, cc->decls[k.decl].scope, 0, 1);
        else emit(vm, OP_POP);
        return;
    }
    case N_BLOCK: gen_block_enter(CCA, s.c); gen_statements(CCA, s.a, s.b); gen_block_leave(CCA, s.c); return;
    case N_IF: {
        uint32_t els = gen_test_jump(CCA, s.a, 0);
        gen_stmt(CCA, s.b);
        if (s.c) {
            uint32_t end = jump_fwd(vm, OP_JMP);
            patch_to(vm, els, here(vm));
            gen_stmt(CCA, s.c);
            patch_to(vm, end, here(vm));
        } else patch_to(vm, els, here(vm));
        return;
    }
    case N_WHILE: {
        /* the test after the body, entered by a jump: one jump an iteration, the test's */
        int forever = cc->nodes[s.a].k == N_NUM && cc->nodes[s.a].op == 2;
        uint32_t to_test = forever ? 0 : jump_fwd(vm, OP_JMP);
        uint32_t top = here(vm);
        gen_loop_ctx(cc, 0, 0, 0);
        size_t me = len(cc->ctx) - 1;
        gen_stmt(CCA, s.b);
        chain_end(vm, cc->ctx[me].cont, here(vm));
        if (forever) jump_to(vm, OP_JMP, top);
        else {
            patch_to(vm, to_test, here(vm));
            patch_to(vm, gen_test_jump(CCA, s.a, 1), top);
        }
        chain_end(vm, cc->ctx[me].brk, here(vm));
        pop_back(cc->ctx);
        return;
    }
    case N_DO: {
        uint32_t top = here(vm);
        gen_loop_ctx(cc, 0, 0, 0);
        size_t me = len(cc->ctx) - 1;
        gen_stmt(CCA, s.a);
        chain_end(vm, cc->ctx[me].cont, here(vm));
        uint32_t back = gen_test_jump(CCA, s.b, 1);
        patch_to(vm, back, top);
        chain_end(vm, cc->ctx[me].brk, here(vm));
        pop_back(cc->ctx);
        return;
    }
    case N_FOR: {
        uint32_t sc = s.s;
        gen_block_enter(CCA, sc);
        if (s.a) {
            if (cc->nodes[s.a].k == N_VAR) gen_var(CCA, s.a);
            else gen_void(CCA, s.a);
        }
        /* the test after the body and the update, entered by a jump */
        uint32_t to_test = s.b ? jump_fwd(vm, OP_JMP) : 0;
        uint32_t top = here(vm);
        gen_loop_ctx(cc, 0, 0, 0);
        size_t me = len(cc->ctx) - 1;
        gen_stmt(CCA, s.d);
        chain_end(vm, cc->ctx[me].cont, here(vm));
        /* a closure of the body keeps its own iteration's bindings */
        if (cc->scopes[sc].has_env) emit(vm, OP_COPY_ENV);
        if (s.c) gen_void(CCA, s.c);
        if (s.b) {
            patch_to(vm, to_test, here(vm));
            patch_to(vm, gen_test_jump(CCA, s.b, 1), top);
        } else jump_to(vm, OP_JMP, top);
        chain_end(vm, cc->ctx[me].brk, here(vm));
        pop_back(cc->ctx);
        gen_block_leave(CCA, sc);
        return;
    }
    case N_FORIN: {
        /* a for-of's iterator closed when the loop is left early: a break, a return or a
           continue past it (unwind_to), an exception (its handler, which loses an error
           the closing throws, and throws the exception on) */
        uint32_t sc = s.s;
        gen_expr(CCA, s.b);
        emit(vm, s.op ? OP_ITER_OF : OP_ITER_IN);
        uint32_t h = 0;
        if (s.op) { h = jump_fwd(vm, OP_TRY); ctx_push(cc, CX_ITER, 0, 0); }
        uint32_t top = here(vm);
        gen_loop_ctx(cc, s.op ? 2 : 1, top, 1);
        size_t me = len(cc->ctx) - 1;
        uint32_t done = jump_fwd(vm, OP_ITER_NEXT);
        gen_block_enter(CCA, sc);
        jn left = cc->nodes[s.a];
        if (left.k == N_VAR) gen_pattern(CCA, cc->nodes[cc->lists[left.a]].a, 1);
        else gen_pattern(CCA, s.a, 0);
        gen_stmt(CCA, s.c);
        gen_block_leave(CCA, sc);
        jump_to(vm, OP_JMP, top);
        patch_to(vm, done, here(vm));
        if (s.op) emit(vm, OP_END_TRY);
        chain_end(vm, cc->ctx[me].brk, here(vm));
        pop_back(cc->ctx);
        if (s.op) {
            pop_back(cc->ctx);
            uint32_t end = jump_fwd(vm, OP_JMP);
            patch_to(vm, h, here(vm));
            emit(vm, OP_SWAP);
            emit(vm, OP_ITER_CLOSE);
            emit(vm, 1);
            emit(vm, OP_THROW);
            patch_to(vm, end, here(vm));
        }
        return;
    }
    case N_RETURN: {
        if (s.a && !has_finally(cc) && (cc->nodes[s.a].k == N_COND || local_slot1(cc, s.a))) { gen_return_value(CCA, s.a); return; }
        if (s.a) gen_expr(CCA, s.a);
        else emit(vm, OP_UNDEF);
        gen_return_top(CCA);
        return;
    }
    case N_BREAK: gen_jump(CCA, n, 1); return;
    case N_CONTINUE: gen_jump(CCA, n, 0); return;
    case N_THROW: gen_expr(CCA, s.a); emit(vm, OP_THROW); return;
    case N_TRY: gen_try(CCA, n); return;
    case N_LABEL: {
        ctx_push(cc, CX_LABEL, 0, s.s);
        size_t me = len(cc->ctx) - 1;
        gen_stmt(CCA, s.a);
        chain_end(vm, cc->ctx[me].brk, here(vm));
        pop_back(cc->ctx);
        return;
    }
    case N_SWITCH: {
        uint32_t tmp = new_slot(cc);
        gen_expr(CCA, s.a);
        emit(vm, OP_PUT_LOC);
        emit16(vm, tmp);
        gen_block_enter(CCA, s.s);
        ctx_push(cc, CX_SWITCH, 0, 0);
        size_t me = len(cc->ctx) - 1;
        size_t mark = len(cc->tmp);
        uint32_t deflt = 0;
        for (uint32_t i = 0; i < s.c; i++) {
            jn cs = cc->nodes[cc->lists[s.b + i]];
            if (cs.d) { deflt = i + 1; push_back(cc->tmp, 0); continue; }
            emit(vm, OP_GET_LOC);
            emit16(vm, tmp);
            gen_expr(CCA, cs.a);
            emit(vm, OP_SEQ);
            push_back(cc->tmp, jump_fwd(vm, OP_JT));
        }
        uint32_t to_default = jump_fwd(vm, OP_JMP);
        for (uint32_t i = 0; i < s.c; i++) {
            jn cs = cc->nodes[cc->lists[s.b + i]];
            uint32_t at_pc = here(vm);
            if (cs.d) patch_to(vm, to_default, at_pc);
            else patch_to(vm, at(cc->tmp, mark + i), at_pc);
            gen_statements(CCA, cs.b, cs.c);
        }
        if (!deflt) patch_to(vm, to_default, here(vm));
        resize(cc->tmp, mark);
        chain_end(vm, cc->ctx[me].brk, here(vm));
        pop_back(cc->ctx);
        gen_block_leave(CCA, s.s);
        return;
    }
    default: cc_error(cc, s.line, "a statement the compiler does not know"); return;
    }
}

/* the slots and cells of a function's declarations */
static void assign_slots(js_cc *cc, uint32_t f) {
    cc->nslots = cc->fns[f].nparams;
    for (size_t d = 1; d < len(cc->decls); d++) {
        jdecl x = cc->decls[d];
        if (x.fn != f || cc->scopes[x.scope].kind == SC_SCRIPT) continue;
        if (x.kind == D_SELF && !x.captured) continue;
        if (x.kind == D_PARAM && x.param) cc->decls[d].slot = x.param - 1;
        else if (!x.captured) cc->decls[d].slot = cc->nslots++;
        if (x.captured) cc->decls[d].envslot = cc->scopes[x.scope].nenv++;
    }
}

static void gen_function(CC, uint32_t f) {
    jfn F = cc->fns[f];
    uint32_t id = F.id;
    vm->funcs[id].code = here(vm);
    cc->gen_fn = f;
    clear(cc->ctx);
    assign_slots(cc, f);
    uint32_t fs = F.scope;
    if (cc->scopes[fs].kind == SC_SCRIPT) {
        /* a script: its top-level names are the page's globals */
        for (uint32_t d = cc->scopes[fs].first; d; d = cc->decls[d].next) {
            jdecl x = cc->decls[d];
            if (x.kind == D_VAR) { emit(vm, OP_DEF_VAR); emit32(vm, x.atom); }
            else if (is_lexical(x.kind)) { emit(vm, OP_DEF_LEX); emit32(vm, x.atom); }
        }
        for (uint32_t d = cc->scopes[fs].first; d; d = cc->decls[d].next) {
            jdecl x = cc->decls[d];
            if (x.kind == D_FUNC && x.init) {
                emit(vm, OP_CLOSURE);
                emit32(vm, cc->fns[x.init - 1].id);
                emit(vm, OP_DEF_FUNC);
                emit32(vm, x.atom);
            }
        }
        jn b = cc->nodes[F.body];
        gen_statements(CCA, b.a, b.b);
        emit(vm, OP_RETURN_UNDEF);
    } else {
        /* an async function's promise is made before its parameters are (their errors reject it) */
        if (F.flags & JF_ASYNC) emit(vm, OP_ASYNC_START);
        if (cc->scopes[fs].has_env) { emit(vm, OP_ENTER_ENV); emit16(vm, cc->scopes[fs].nenv); }
        for (uint32_t d = cc->scopes[fs].first; d; d = cc->decls[d].next) {
            jdecl x = cc->decls[d];
            if (x.kind == D_PARAM && x.captured && x.param) {
                emit(vm, OP_GET_LOC);
                emit16(vm, x.param - 1);
                emit(vm, OP_PUT_ENV);
                emit(vm, 0);
                emit16(vm, x.envslot);
            } else if (x.kind == D_VAR && x.captured) {
                emit(vm, OP_UNDEF);
                emit(vm, OP_PUT_ENV);
                emit(vm, 0);
                emit16(vm, x.envslot);
            } else if (x.kind == D_ARGS) {
                emit(vm, OP_ARGUMENTS);
                gen_put(CCA, x.atom, d, fs, 0, 1);
            }
        }
        for (size_t d = 1; d < len(cc->decls); d++)
            if (cc->decls[d].kind == D_SELF && cc->decls[d].fn == f && cc->decls[d].captured) {
                emit(vm, OP_CALLEE);
                emit(vm, OP_PUT_ENV);
                emit(vm, 0);
                emit16(vm, cc->decls[d].envslot);
            }
        /* the parameters' defaults, patterns and rest */
        for (uint32_t i = 0; i < F.nparams; i++) {
            jn p = cc->nodes[cc->lists[F.params + i]];
            jn t = cc->nodes[p.a];
            if (p.op & NF_REST) { emit(vm, OP_REST); emit16(vm, i); gen_pattern(CCA, p.a, 1); continue; }
            int simple = t.k == N_IDENT && !p.b;
            if (simple) continue;
            emit(vm, OP_GET_LOC);
            emit16(vm, i);
            gen_default(CCA, p.b);
            gen_pattern(CCA, p.a, 1);
        }
        gen_scope_decls(CCA, fs);
        /* a generator's call ends here, its object returned: the body runs at next() */
        if (F.flags & JF_GENERATOR) emit(vm, OP_GEN_START);
        if ((F.flags & JF_CLASS_CTOR) && !(F.flags & JF_DERIVED)) emit(vm, OP_INIT_FIELDS);
        if (F.flags & FN_EXPR) { gen_expr(CCA, F.body); emit(vm, OP_RETURN); }
        else if (F.flags & FN_FIELDS) {
            jclass k = cc->classes[F.cls - 1];
            for (uint32_t i = 0; i < k.nmembers; i++) {
                jn pr = cc->nodes[cc->lists[k.members + i]];
                if (pr.op != PK_FIELD || (pr.d & NF_STATIC)) continue;
                emit(vm, OP_THIS);
                if (pr.d & NF_COMPUTED) gen_expr(CCA, pr.a);
                if (pr.b) gen_expr(CCA, pr.b);
                else emit(vm, OP_UNDEF);
                if (pr.d & NF_COMPUTED) emit(vm, OP_DEF_ELEM);
                else { emit(vm, OP_DEF_PROP); emit32(vm, cc->nodes[pr.a].s); }
                emit(vm, OP_POP);
            }
            emit(vm, OP_RETURN_UNDEF);
        } else if (F.flags & FN_DEFAULT_CTOR) {
            if (F.flags & JF_DERIVED) {
                emit(vm, OP_REST);
                emit16(vm, 0);
                emit(vm, OP_SUPER_CALL_ARR);
                emit(vm, OP_INIT_FIELDS);
                emit(vm, OP_POP);
            }
            emit(vm, OP_RETURN_UNDEF);
        } else {
            jn b = cc->nodes[F.body];
            gen_statements(CCA, b.a, b.b);
            emit(vm, OP_RETURN_UNDEF);
        }
    }
    vm->funcs[id].nslots = cc->nslots;
    vm->funcs[id].code_len = here(vm) - vm->funcs[id].code;
}

int js_compile(js_vm *vm, const char *src, size_t n, uint32_t script, int as_function_body, uint32_t *func, vector<char> *err) {
    js_cc ccs;
    memset(&ccs, 0, sizeof ccs);
    defer(ccs) { JS_CC_DROP(ccs); }
    js_cc *cc = &ccs;
    uint32_t srcn = (uint32_t)n;
    jn n0;
    memset(&n0, 0, sizeof n0);
    push_back(cc->nodes, n0);
    push_back(cc->lists, 0);
    jdecl d0;
    memset(&d0, 0, sizeof d0);
    push_back(cc->decls, d0);
    jref r0 = { 0, 0, 0, 0 };
    push_back(cc->refs, r0);
    for (int k = 0; k < KW_N; k++) cc->kw[k] = js_atom_ascii(vm, js_kw_names[k]);
    cc->script = script;
    /* the script: function 0 in scope 0 (a handler's body: a function of event) */
    jscope s0 = { 0, 0, as_function_body ? SC_FUNC : SC_SCRIPT, 0, 0, 0 };
    push_back(cc->scopes, s0);
    jfn f0;
    memset(&f0, 0, sizeof f0);
    f0.flags = 0;
    push_back(cc->fns, f0);
    cc->cur_fn = 0;
    cc->cur_scope = 0;
    cc->line = 1;
    cc->tok.type = T_EOF;
    if (as_function_body) {
        uint32_t ev = js_atom_ascii(vm, "event");
        uint32_t d = declare(vm, src, srcn, cc, ev, D_PARAM, 1);
        cc->decls[d].param = 1;
        uint32_t p = node(cc, N_PARAM, 1);
        cc->nodes[p].a = ident_node(vm, src, srcn, cc, ev, 1);
        size_t mark = len(cc->tmp);
        push_back(cc->tmp, p);
        list_take(cc, mark, &cc->fns[0].params, &cc->fns[0].nparams);
        cc->fns[0].nformal = 1;
    }
    lx(vm, src, srcn, cc);
    uint32_t body = node(cc, N_BLOCK, 1);
    {
        uint32_t so = 0, sn = 0;
        p_body(vm, src, srcn, cc, 0, &so, &sn);
        if (!cc->err && cc->tok.type != T_EOF) cc_error(cc, cc->tok.line, "unexpected text after the script's end");
        cc->nodes[body].a = so;
        cc->nodes[body].b = sn;
    }
    cc->fns[0].body = body;
    cc->nodes[body].c = 0;
    if (!cc->err) {
        resolve(cc);
        /* every function's record, then each one's code */
        for (size_t f = 0; f < len(cc->fns); f++) {
            js_func jf;
            memset(&jf, 0, sizeof jf);
            jfn F = cc->fns[f];
            jf.nparams = F.nparams;
            jf.name = F.name;
            jf.flags = F.flags & 0xFFFF;
            jf.line = F.line;
            jf.script = script;
            jf.src_off = F.src_start;
            jf.src_len = F.src_end > F.src_start ? F.src_end - F.src_start : 0;
            jf.arity = F.nformal;
            push_back(vm->funcs, jf);
            cc->fns[f].id = (uint32_t)(len(vm->funcs) - 1);
        }
        for (size_t f = 0; f < len(cc->fns) && !cc->err; f++) gen_function(vm, src, srcn, cc, (uint32_t)f);
    }
    if (cc->err) {
        char m[320];
        int k = snprintf(m, sizeof m, "SyntaxError: %s (line %u)", cc->errmsg, cc->err_line);
        if (k > 0) append_range(err, m, (size_t)k < sizeof m ? (size_t)k : sizeof m - 1);
        return 0;
    }
    *func = cc->fns[0].id;
    if (getenv("AMB_JS_DUMP")) {
        /* the functions and their code, for the engine's own debugging */
        for (size_t f = 0; f < len(cc->fns); f++) {
            js_func jf = vm->funcs[cc->fns[f].id];
            fprintf(stderr, "func %u (ast %zu): code %u+%u params %u slots %u flags %x\n", cc->fns[f].id, f, jf.code, jf.code_len, jf.nparams, jf.nslots, jf.flags);
            for (uint32_t pc = jf.code; pc < jf.code + jf.code_len; pc++) fprintf(stderr, " %u", (unsigned)vm->code[pc]);
            fprintf(stderr, "\n");
        }
    }
    return 1;
}
