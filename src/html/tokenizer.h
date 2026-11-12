/* src/html/tokenizer.h -- the tokenizer's place in its input between the runs
   that the tree builder (src/html/tree.c) drives, included after the HTML
   module's interface.

   The HTML standard has the tree builder decide two things the tokenizer then
   reads by: the state after the start tag of an element whose text may be raw
   (title, textarea, style, xmp, iframe, noembed, noframes, script, plaintext:
   in HTML content the tree builder switches the tokenizer, in foreign content
   it does not), and whether <![CDATA[ opens a CDATA section (only when the
   adjusted current node is foreign) or a bogus comment. A driven run stops at
   either point, with every token before it in the table; the tree builder
   builds up to there, answers, and the next run goes on. A run that is not
   driven decides alone, as the tree builder would for HTML content. */
#ifndef AMBITIOUS_HTML_TOKENIZER_H
#define AMBITIOUS_HTML_TOKENIZER_H

/* the scripting flag every parse runs with (src/html/tokenizer.c; set by html_set_scripting) */
extern int html_scripting_flag;

/* the states a tree builder starts the tokenizer in or switches it to */
enum { HTML_TZ_DATA, HTML_TZ_RCDATA, HTML_TZ_RAWTEXT, HTML_TZ_SCRIPT, HTML_TZ_PLAINTEXT, HTML_TZ_MARKUP };
/* why a run stopped: the input's end, a start tag's state to choose, a CDATA section to allow or not */
enum { HTML_TZ_END, HTML_TZ_TAG, HTML_TZ_CDATA };

typedef struct html_tz {
    size_t i;               /* the offset of the input where the next run goes on ... */
    uint32_t line, col;     /* ... as a line and a column */
    uint32_t tline, tcol;   /* where the markup declaration a run stopped at began */
    int state;              /* the state the next run starts in (HTML_TZ_*) */
    int stop;               /* why the last run stopped */
    int driven;             /* a tree builder answers at the stops */
    int cdata;              /* the answer at HTML_TZ_CDATA: 1 a CDATA section, 0 a bogus comment */
    uint32_t last_start;    /* the last start tag's token, for an appropriate end tag; 0 none */
} html_tz;

/* the place before the input's first byte, in a state */
void html_tz_start(html_tz *z, int state, int driven);
/* src with its newlines normalized (13.2.3.5: CR LF and CR become LF) into out,
   which has room for n bytes; the length written */
size_t html_normalize_newlines(const char *src, size_t n, char *out);
/* tokens from the normalized input in[0..n) appended to the token table, from z
   on, up to the input's end or a driven stop */
html_status html_tokenize_run(html_parser *p, const char *in, size_t n, html_tz *z);
#endif
