/* src/css/cssom.h -- the CSS Object Model's sheets, rules and declarations as Chromium has
   them (W3C CSSOM 1; src/css/cssom.c), which a page's scripts reach through src/script/cssom.c;
   and what the cascade (src/css/css.c) is told of the sheets the scripts changed.

   THE MODEL. One a page's host (src/script/script.h embeds it): its sheets (a style
   element's, a link's, an imported one, a constructed one), each sheet's rules, a list of
   records linked in their order (a grouping rule's own rules under it the same way), each a
   rule of one of Chromium's classes with its texts as Chromium serializes them; a rule's
   declarations a block, a span of declaration records, each a longhand (a shorthand is
   kept as its longhands, as Chromium's property sets keep it) or a custom property, its
   value serialized. Every text is a span of the model's text, which grows only: a change
   appends what it makes. Records are numbered from 1 (0 is none); a record never moves.

   A header's prototypes come before its structs with vector members, and the structs with
   none before those (toolchain/REPORT.md, findings 22 and 23); enum constants, not
   #defines, size and flag what the units share (findings 24 and 25). */
#ifndef AMB_CSSOM_H
#define AMB_CSSOM_H
#include <stdint.h>
#include <stddef.h>
#include <vector.pl>

/* a sheet's origin */
enum { CSS_OM_O_STYLE = 1, CSS_OM_O_LINK, CSS_OM_O_IMPORT, CSS_OM_O_CONSTRUCTED };
/* a rule's kind: Chromium's classes (their type attribute is CSS_OM_TYPE's) */
enum {
    CSS_OM_STYLE = 1, CSS_OM_IMPORT, CSS_OM_MEDIA, CSS_OM_FONT_FACE, CSS_OM_PAGE, CSS_OM_KEYFRAMES, CSS_OM_KEYFRAME,
    CSS_OM_MARGIN, CSS_OM_NAMESPACE, CSS_OM_COUNTER_STYLE, CSS_OM_SUPPORTS, CSS_OM_FONT_FEATURE_VALUES, CSS_OM_CONTAINER,
    CSS_OM_LAYER_BLOCK, CSS_OM_LAYER_STATEMENT, CSS_OM_PROPERTY, CSS_OM_SCOPE, CSS_OM_STARTING_STYLE,
    CSS_OM_FONT_PALETTE_VALUES, CSS_OM_VIEW_TRANSITION, CSS_OM_POSITION_TRY, CSS_OM_NESTED_DECLARATIONS, CSS_OM_KINDS
};
/* a declaration's flags: !important; filled in by its shorthand, which did not name it; its
   value its shorthand's, waiting for a var() (sh names the shorthand) */
enum { CSS_OM_IMPORTANT = 1, CSS_OM_IMPLICIT = 2, CSS_OM_PENDING = 4 };
/* a custom property's number (its name in the declaration's own span) */
enum { CSS_OM_CUSTOM = 0xFFFF };
/* what a sheet's text is read as: a constructed sheet's (its @import rules refused, each noted
   in warn), one a link loaded */
enum { CSS_OM_READ_CONSTRUCTED = 1 };
/* why an insertion was refused (CSSOM 6.4.1 insert a CSS rule, Chromium's checks) */
enum { CSS_OM_E_SYNTAX = 1, CSS_OM_E_INDEX, CSS_OM_E_HIERARCHY, CSS_OM_E_STATE, CSS_OM_E_IMPORT_GROUP, CSS_OM_E_NAMESPACE_GROUP,
       CSS_OM_E_CONSTRUCTED_IMPORT };
/* what a block is the declarations of: a style rule's (a keyframe's, a nested declarations
   rule's) style, an at-rule's descriptors (@font-face, @page, @property, @counter-style, ...) */
enum { CSS_OM_B_STYLE = 1, CSS_OM_B_FONT_FACE, CSS_OM_B_PAGE, CSS_OM_B_PROPERTY, CSS_OM_B_COUNTER, CSS_OM_B_PALETTE,
       CSS_OM_B_VIEW, CSS_OM_B_TRY, CSS_OM_B_KEYFRAME, CSS_OM_B_FEATURE };
/* what the cascade is told of a sheet (css_om_reg_*): disabled; its media changed (the text
   given); its rules changed (their text given) */
enum { CSS_OMR_DISABLED = 1, CSS_OMR_MEDIA = 2, CSS_OMR_TEXT = 4 };

/* a sheet. owner: its style element's or link's NODE and that node's generation, 0 for none
   (a constructed or imported sheet, or one its owner let go); tree: the root of its owner's
   tree when it was made (1 the document, else a shadow root); import_rule and parent: an
   imported sheet's @import rule and the sheet that rule is in; its rules (first, last,
   count), read from its text (src) at their first use (parsed); disabled; changed: a script
   changed its rules (the cascade reads them from then on), media_set its media; clean: its
   rules may be read (an origin-clean sheet: Chromium's is a file: page's own style element's,
   not a link's); its media list, href, title and base URL as texts; sig, its owner's children
   as they were when it was made (src/script/cssom.c: a change of them makes a new sheet); its
   wrappers (the script host's ROOTs: the sheet, its CSSRuleList, its MediaList) */
typedef struct css_om_sheet {
    uint32_t live, origin, owner, ogen;
    uint32_t tree, import_rule, parent, flags;
    uint32_t first, last, count, parsed;
    uint32_t disabled, changed, media_set, clean;
    uint32_t moff, mlen, hoff, hlen;
    uint32_t toff, tlen, boff, blen;
    uint32_t soff, slen, owner_kind, spare;
    uint64_t sig;
    uint32_t obj, rules_obj, media_obj, spare2;
} css_om_sheet;

/* a rule: its kind, its sheet (0 once it is taken out of one) and the rule it is in (0 its
   sheet's top level); its own rules (first, last, count) and its siblings (next, prev); its
   declarations (block, 0 none); sub, an @import's sheet; its texts by kind (a style rule's
   selector list; an @media's media list; @supports' and @container's condition, @container's
   name; a layer's name or names; @import's URL as written (a), its href resolved (b), its
   layer (c, flags 1 a layer at all) and supports (d), its media in its sheet's own;
   @namespace's prefix and URI; @keyframes' name; a keyframe's key list; @page's selector;
   @property's and @counter-style's name; @scope's start and end; @font-feature-values'
   families); its wrappers (ROOTs: the rule, its CSSRuleList, its MediaList, its style) */
typedef struct css_om_rule {
    uint32_t live, kind, sheet, parent;
    uint32_t first, last, next, prev;
    uint32_t count, block, sub, flags;
    uint32_t aoff, alen, boff, blen;
    uint32_t coff, clen, doff, dlen;
    uint32_t obj, rules_obj, media_obj, style_obj;
} css_om_rule;

/* a declaration: its property (its number in src/css/cssom_props.h's table, a descriptor's past
   it, CSS_OM_CUSTOM for a custom property), its flags, a pending one's shorthand; a custom
   property's name; its value, serialized (a pending one's: its shorthand's whole value) */
typedef struct css_om_decl {
    uint32_t prop, flags, sh, spare;
    uint32_t noff, nlen, voff, vlen;
} css_om_decl;

/* a block of declarations: a span of the model's decls, of what kind (CSS_OM_B_*), the rule
   whose it is, its CSSStyleDeclaration (a ROOT) */
typedef struct css_om_block { uint32_t first, n, kind, rule, obj, spare; } css_om_block;

/* an element's inline style as the CSSOM holds it (Chromium's inline style's own declarations,
   in their order): its element's NODE and generation, its block (0 before it is read), its
   CSSStyleDeclaration (a ROOT while the element lives), and the style attribute's text the
   block was read from or last written as, by its length and FNV-1a hash: the block is the
   attribute's while the attribute is that text, else it is read anew */
typedef struct css_om_inline { uint32_t node, gen, block, obj; uint32_t tlen, spare; uint64_t sig; } css_om_inline;

struct css_om;
struct css_om_dl;

/* ---- properties ---- */
/* a property's number by its name (ASCII case-insensitive; an alias's own): -1 when Chromium has
   none; CSS_OM_CUSTOM for a custom property (--name) */
int css_om_prop(const char *name, size_t n);
/* a property's name by its number (a descriptor's too); "" for none */
const char *css_om_prop_name(uint32_t p);
/* the property an alias is of (its own number for a property that is none) */
uint32_t css_om_unalias(uint32_t p);
/* whether a property is a shorthand; its longhands' count, and its i-th */
int css_om_is_shorthand(uint32_t p);
uint32_t css_om_longhands(uint32_t p);
uint32_t css_om_longhand(uint32_t p, uint32_t i);

/* ---- sheets and rules ---- */
/* a sheet made (its record's place), of an origin */
uint32_t css_om_sheet_new(struct css_om *m, uint32_t origin);
/* a sheet's rules read from text (its rules before dropped): the @import rules a constructed
   sheet refused, how many */
uint32_t css_om_read(struct css_om *m, uint32_t sheet, const char *t, size_t n, uint32_t mode);
/* a rule's text inserted in the list of a sheet (parent 0) or of a grouping rule at index:
   0 and the rule's place in *made, or why it was refused (CSS_OM_E_*); a keyframe for
   @keyframes' appendRule (parent a @keyframes rule: at its end) */
int css_om_insert(struct css_om *m, uint32_t sheet, uint32_t parent, uint32_t index, const char *t, size_t n, uint32_t mode, uint32_t *made);
/* the rule at index of a list taken out of it: 0, or CSS_OM_E_INDEX */
int css_om_remove(struct css_om *m, uint32_t sheet, uint32_t parent, uint32_t index);
/* a list's length and its rule at index (0 for none) */
uint32_t css_om_count(const struct css_om *m, uint32_t sheet, uint32_t parent);
uint32_t css_om_at(const struct css_om *m, uint32_t sheet, uint32_t parent, uint32_t index);
/* a rule's cssText, appended to out; a sheet's rules' text, each rule's on its line (what the
   cascade reads of a sheet a script changed) */
void css_om_rule_text(const struct css_om *m, uint32_t r, vector<char> *out);
void css_om_sheet_text(const struct css_om *m, uint32_t s, vector<char> *out);
/* a text of the model: the span (off, n) appended to out */
void css_om_span(const struct css_om *m, uint32_t off, uint32_t n, vector<char> *out);
/* text appended to the model's: its offset */
uint32_t css_om_put(struct css_om *m, const char *t, size_t n);

/* ---- a rule's parts, set by a script: 1 done, 0 refused (the text is no such part) ---- */
/* a style rule's (or @page's) selector list */
int css_om_set_selector(struct css_om *m, uint32_t r, const char *t, size_t n);
/* a keyframe's key list */
int css_om_set_keys(struct css_om *m, uint32_t r, const char *t, size_t n);
/* @keyframes' name (a script may give any) */
void css_om_set_name(struct css_om *m, uint32_t r, const char *t, size_t n);
/* a @counter-style's descriptor of a name (name, system, symbols ...) */
int css_om_set_counter(struct css_om *m, uint32_t r, const char *desc, const char *t, size_t n);
/* @keyframes' keyframe whose keys are t's (the last), 0 for none */
uint32_t css_om_find_key(const struct css_om *m, uint32_t r, const char *t, size_t n);

/* ---- media lists (Media Queries 5, CSSOM 4.1) ---- */
/* a media query list's text as Chromium serializes it, appended to out */
void css_om_media_text(const char *t, size_t n, vector<char> *out);
/* its queries' count, and the i-th's text into out (cleared first) */
uint32_t css_om_media_count(const char *t, size_t n);
void css_om_media_item(const char *t, size_t n, uint32_t i, vector<char> *out);
/* appendMedium and deleteMedium: the list's text with the medium given added (none when it is
   in it already, or no query), or taken out (0: it was not in it), into out */
void css_om_media_append(const char *t, size_t n, const char *q, size_t qn, vector<char> *out);
int css_om_media_delete(const char *t, size_t n, const char *q, size_t qn, vector<char> *out);

/* ---- declarations (CSSOM 6.6, as Chromium's property sets keep them) ---- */
/* a declaration list's text read into dl (emptied first), as declarations of a block's kind */
void css_om_parse_decls(struct css_om_dl *dl, const char *t, size_t n, uint32_t kind);
/* a block of the model read into dl (emptied first), and dl stored as that block */
void css_om_block_load(const struct css_om *m, uint32_t b, struct css_om_dl *dl);
void css_om_block_store(struct css_om *m, uint32_t b, const struct css_om_dl *dl);
/* a block made from dl: its place */
uint32_t css_om_block_new(struct css_om *m, uint32_t kind, uint32_t rule, const struct css_om_dl *dl);
/* the declarations' text (cssText), appended to out */
void css_om_decls_text(const struct css_om_dl *dl, uint32_t kind, vector<char> *out);
/* a property's value by its name (a shorthand's serialized from its longhands; "" for none),
   appended to out; its priority ("important" or ""); whether dl holds it */
void css_om_get(const struct css_om_dl *dl, uint32_t kind, const char *name, size_t n, vector<char> *out);
int css_om_important(const struct css_om_dl *dl, uint32_t kind, const char *name, size_t n);
/* setProperty: 1 when dl changed, 0 when the value was refused or the name is none (an
   empty value removes); removeProperty: the value it had into old, 1 when it had one */
int css_om_set(struct css_om_dl *dl, uint32_t kind, const char *name, size_t n, const char *v, size_t vn, int important);
int css_om_unset(struct css_om_dl *dl, uint32_t kind, const char *name, size_t n, vector<char> *old);
/* the declarations' count (CSSOM's length) and the i-th's name */
uint32_t css_om_decl_count(const struct css_om_dl *dl);
void css_om_decl_name(const struct css_om_dl *dl, uint32_t i, vector<char> *out);

/* ---- what the cascade is told (src/script/cssom.c tells it; src/css/css.c reads it) ----
   For each document (its store's address), the sheets of its trees a script changed: an owned
   sheet by its owner (a NODE and that node's generation), with CSS_OMR_* flags, its media and
   its rules' text; each tree's adopted sheets, in order (tree: 1 the document's, else the
   shadow root's NODE), each with its flags, media, base URL and text. Cleared for a document
   when its host is made and dropped, and when the host tells it anew */
void css_om_reg_clear(const void *doc);
void css_om_reg_owned(const void *doc, uint32_t node, uint32_t gen, uint32_t flags, const char *media, size_t mn, const char *text, size_t n);
void css_om_reg_adopted(const void *doc, uint32_t tree, uint32_t flags, const char *media, size_t mn, const char *base, size_t bn,
                        const char *text, size_t n);
/* whether anything was told of doc */
int css_om_reg_any(const void *doc);
/* an owned sheet's entry for its owner: 1 and its flags, media and text into the vectors (each
   cleared first), 0 for none */
int css_om_reg_owner(const void *doc, uint32_t node, uint32_t gen, uint32_t *flags, vector<char> *media, vector<char> *text);
/* a tree's adopted sheets' count, and the i-th's flags, media, base and text */
uint32_t css_om_reg_adopted_count(const void *doc, uint32_t tree);
void css_om_reg_adopted_at(const void *doc, uint32_t tree, uint32_t i, uint32_t *flags, vector<char> *media, vector<char> *base,
                           vector<char> *text);

/* a declaration list apart from the model (an element's style attribute's, a block's as it is
   changed): its declarations and their texts. A local one is zeroed first and dropped by hand
   (CSS_OM_DL_DROP): a struct's vectors are not autodropped */
typedef struct css_om_dl {
    vector<css_om_decl> d;
    vector<char> t;
} css_om_dl;

/* the model, one a page's host. adopted: each tree's adopted sheets, three numbers a sheet
   (the tree: 1 the document, else a shadow root's NODE; that node's generation; the sheet);
   trees: each tree's objects, five numbers a tree (its root NODE, its generation, its
   StyleSheetList, its adoptedStyleSheets array and the array behind it: ROOTs); protos: the
   script host's prototypes, by class; aux: the script host's own. inl: the elements' inline
   styles, inl_at: each element's place in inl plus one, by its NODE. dom_rev and log_pos: the
   DOM's revision and its mutation log's length when the sheets were last checked against
   it; rev, the model's revision, which each change moves; told, the revision the cascade
   was last told; on, a script reached CSSOM; warn, a read's @import rules refused */
typedef struct css_om {
    vector<css_om_sheet> sheets;
    vector<css_om_rule> rules;
    vector<css_om_decl> decls;
    vector<css_om_block> blocks;
    vector<char> text;
    vector<uint32_t> adopted;
    vector<uint32_t> trees;
    vector<uint32_t> protos;
    vector<uint32_t> aux;
    vector<css_om_inline> inl;
    vector<uint32_t> inl_at;
    uint64_t dom_rev;
    uint32_t log_pos, rev, told, on;
    uint32_t warn, spare1, spare2, spare3;
} css_om;
#define CSS_OM_DL_DROP(x) vector_css_om_decl_drop(&(x).d); vector_char_drop(&(x).t)
#define CSS_OM_DROP(x) vector_css_om_sheet_drop(&(x).sheets); vector_css_om_rule_drop(&(x).rules); \
    vector_css_om_decl_drop(&(x).decls); vector_css_om_block_drop(&(x).blocks); vector_char_drop(&(x).text); \
    vector_uint32_t_drop(&(x).adopted); vector_uint32_t_drop(&(x).trees); vector_uint32_t_drop(&(x).protos); \
    vector_uint32_t_drop(&(x).aux); vector_css_om_inline_drop(&(x).inl); vector_uint32_t_drop(&(x).inl_at)

#endif
