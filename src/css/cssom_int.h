/* src/css/cssom_int.h -- what src/css/cssom.c (tokens, selectors, media queries, rules) and
   src/css/cssom_decl.c (values and declarations) share: CSS Syntax 3's tokens of a text, and
   the serializers both write with. Include it after src/css/cssom.h. */
#ifndef AMB_CSSOM_INT_H
#define AMB_CSSOM_INT_H

/* a token's type (CSS Syntax 3, 4) */
enum { OT_EOF, OT_WS, OT_IDENT, OT_FUNC, OT_AT, OT_HASH, OT_STR, OT_BADSTR, OT_URL, OT_BADURL, OT_DELIM, OT_NUM, OT_PCT, OT_DIM,
       OT_CDO, OT_CDC, OT_COLON, OT_SEMI, OT_COMMA, OT_LBRACK, OT_RBRACK, OT_LPAREN, OT_RPAREN, OT_LBRACE, OT_RBRACE };
/* a token's flags: a number written as an integer; a hash that would start an identifier; a number
   written with its sign */
enum { OTF_INT = 1, OTF_ID = 2, OTF_SIGN = 4 };
/* a token: its type, its place in the text (s to e), its flags, a number's value; u, a
   dimension's unit's start (the unit runs to e); m, for an opening bracket, parenthesis,
   brace or function, the place of the token that closes it (the tokens' count when none
   does). A function's name runs from s to e - 1; a string's and a URL's value, an ident's
   and a hash's name, are read with their escapes by css_omt_text */
typedef struct css_omt { uint32_t t, s, e, f; double v; uint32_t u, m; } css_omt;

/* the tokens of a text, comments dropped, an EOF token last */
void css_omt_tokens(const char *p, size_t n, vector<css_omt> *out);
/* a token's value (an ident's, a function's or an at-keyword's name, a hash's, a string's, a
   URL's; a dimension's unit), its escapes read, appended to out */
void css_omt_text(const char *p, const css_omt *t, vector<char> *out);
void css_omt_unit(const char *p, const css_omt *t, vector<char> *out);
/* an ident (or a function's name) that is the keyword w, ASCII case-insensitively */
int css_omt_is(const char *p, const css_omt *t, const char *w);
/* the token after i in [i, b) that is not white space (b when none) */
uint32_t css_omt_skip(const vector<css_omt> *tk, uint32_t i, uint32_t b);
/* an identifier, a string and a number as CSSOM serializes them (2.1), appended to out */
void css_omx_ident(const char *s, size_t n, vector<char> *out);
void css_omx_string(const char *s, size_t n, vector<char> *out);
void css_omx_num(double v, vector<char> *out);
/* the tokens [a, b) of p written as they are, white space one space (none at their ends), a
   comment none: a custom property's value, a var()'s */
void css_omx_raw(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int keep_ws, vector<char> *out);
/* the tokens [a, b) as component values serialized: idents and units in lower case where
   lower, numbers, strings and URLs as CSSOM writes them, functions' arguments so too, a
   comma ", " (what a value with no grammar of its own here is written as) */
void css_omx_tokens(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, int lower, vector<char> *out);
/* a media query list's tokens [a, b) as Chromium serializes them, appended to out */
void css_omx_media(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out);
/* a value as a declaration of property prop holds it (src/css/cssom_decl.c): 1 and its text in
   out, 0 when prop does not take it. The tokens [a, b) of p, trimmed */
int css_omv_value(uint32_t prop, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out);
/* a <length> or <percentage>-like value of a media feature or container query, as written there */
void css_omx_feature_value(const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, vector<char> *out);
/* a declaration list's tokens [a, b) of p read into dl, as css_om_parse_decls reads a text */
void css_om_decls_tokens(struct css_om_dl *dl, const char *p, const vector<css_omt> *tk, uint32_t a, uint32_t b, uint32_t kind);
/* the place a descriptor's name has among a block kind's (past CSS_OM_NPROPS), -1 none; a descriptor's name by its place ("" for none) */
int css_om_descriptor(uint32_t kind, const char *name, size_t n);
const char *css_om_desc_name(uint32_t p);
/* an ASCII lower-cased copy of s appended to out */
void css_omx_lower(const char *s, size_t n, vector<char> *out);
/* whether two texts are the same */
int css_omx_eq(const char *a, size_t an, const char *b, size_t bn);
/* whether a text is the keyword w, ASCII case-insensitively */
int css_omx_ieq(const char *a, size_t an, const char *w);

#endif
