/* tests/html/html5lib.c -- the html5lib tree-construction tests through the HTML
   module: each case of each .dat file parsed (as a document, or as a fragment in
   its context), the DOM dumped in the tests' own format and compared with the
   case's #document. Prints a line a file and the totals; -v prints each failing
   case. Cases that need what is not done -- scripting on, a context in another
   namespace -- are skipped and counted. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);

static int read_all(const char *path, vector<char> *out) {
    own FILE *f = fopen(path, "rb");
    if (!f) return 0;
    defer(f) { fclose(f); }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) append_range(out, buf, n);
    return 1;
}

static void put(vector<char> *o, const char *s) { append_range(o, s, strlen(s)); }
static void indent(vector<char> *o, int depth) {
    put(o, "| ");
    for (int i = 0; i < depth; i++) put(o, "  ");
}

/* a node and what is under it, in the tests' format */
static void dump(const dom_document *d, uint32_t x, int depth, vector<char> *o) {
    dom_node r = d->node[x];
    if (r.kind == DOM_NODE_KIND_ELEMENT) {
        indent(o, depth);
        put(o, "<");
        append_range(o, names_ptr(d, r.name), names_len(d, r.name));
        put(o, ">\n");
        /* the attributes, sorted by name */
        vector<uint32_t> as;
        for (uint32_t a = r.first_attr; a; a = d->attr[a].next) push_back(as, a);
        for (size_t i = 1; i < len(as); i++)
            for (size_t j = i; j > 0; j--) {
                uint32_t x1 = at(as, j - 1), x2 = at(as, j);
                size_t l1 = names_len(d, d->attr[x1].name), l2 = names_len(d, d->attr[x2].name);
                int c = memcmp(names_ptr(d, d->attr[x1].name), names_ptr(d, d->attr[x2].name), l1 < l2 ? l1 : l2);
                if (c > 0 || (c == 0 && l1 > l2)) { as[j - 1] = x2; as[j] = x1; } else break;
            }
        for (size_t i = 0; i < len(as); i++) {
            uint32_t a = at(as, i);
            indent(o, depth + 1);
            append_range(o, names_ptr(d, d->attr[a].name), names_len(d, d->attr[a].name));
            put(o, "=\"");
            append_range(o, text_ptr(d, d->attr[a].value), d->attr[a].value.len);
            put(o, "\"\n");
        }
    } else if (r.kind == DOM_NODE_KIND_TEXT) {
        indent(o, depth);
        put(o, "\"");
        append_range(o, text_ptr(d, r.data), r.data.len);
        put(o, "\"\n");
    } else if (r.kind == DOM_NODE_KIND_COMMENT) {
        indent(o, depth);
        put(o, "<!-- ");
        append_range(o, text_ptr(d, r.data), r.data.len);
        put(o, " -->\n");
    } else if (r.kind == DOM_NODE_KIND_DOCTYPE) {
        indent(o, depth);
        put(o, "<!DOCTYPE ");
        append_range(o, names_ptr(d, r.name), names_len(d, r.name));
        if (r.doctype_ids) {
            put(o, " \"");
            append_range(o, text_ptr(d, r.public_id), r.public_id.len);
            put(o, "\" \"");
            append_range(o, text_ptr(d, r.system_id), r.system_id.len);
            put(o, "\"");
        }
        put(o, ">\n");
    }
    for (uint32_t ch = r.first_child; ch; ch = d->node[ch].next_sibling) dump(d, ch, depth + (r.kind == DOM_NODE_KIND_DOCUMENT ? 0 : 1), o);
}

static int starts(const char *s, size_t n, const char *w) { size_t k = strlen(w); return n >= k && memcmp(s, w, k) == 0; }

/* one case: 1 passed, 0 failed, -1 skipped */
static int run_case(const char *dat, size_t dat_len, const char *ctx, size_t ctx_len, int fragment,
                    const char *want, size_t want_len, int verbose, const char *file, int no) {
    dom_document d;
    html_parser p;
    dom_init(&d, 1);
    html_init(&p, 1);
    if (fragment) html_parse_fragment(&p, &d, ctx, ctx_len, dat, dat_len);
    else html_parse(&p, &d, dat, dat_len);
    vector<char> got;
    dump(&d, 1, 0, &got);
    while (len(got) && at(got, len(got) - 1) == '\n') pop_back(got);
    int ok = len(got) == want_len && memcmp(data(got), want, want_len) == 0;
    if (!ok && verbose) {
        printf("== FAIL %s #%d%s%.*s\n-- data:\n%.*s\n-- want:\n%.*s\n-- got:\n%.*s\n", file, no, fragment ? " fragment " : "",
               fragment ? (int)ctx_len : 0, ctx, (int)dat_len, dat, (int)want_len, want, (int)len(got), data(got));
    }
    return ok;
}

int main(int argc, char **argv) {
    int verbose = 0, pass = 0, fail = 0, skip = 0;
    for (int ai = 1; ai < argc; ai++) {
        if (strcmp(argv[ai], "-v") == 0) { verbose = 1; continue; }
        vector<char> text;
        if (!read_all(argv[ai], &text)) { printf("cannot read %s\n", argv[ai]); return 2; }
        push_back(text, '\n');
        const char *base = strrchr(argv[ai], '/') ? strrchr(argv[ai], '/') + 1 : argv[ai];
        /* the lines, by offsets into text */
        vector<size_t> ls, le;
        size_t st = 0;
        for (size_t i = 0; i < len(text); i++) if (at(text, i) == '\n') { push_back(ls, st); push_back(le, i); st = i + 1; }
        int fpass = 0, ffail = 0, fskip = 0, no = 0;
        size_t li = 0, nl = len(ls);
        while (li < nl) {
            const char *t = data(text);
            if (!(le[li] - ls[li] == 5 && memcmp(t + ls[li], "#data", 5) == 0)) { li++; continue; }
            no++;
            /* #data, up to the next section */
            vector<char> dat, ctx, want;
            int fragment = 0, script_on = 0, section = 1, first_data = 1, first_doc = 1;
            li++;
            while (li < nl) {
                size_t a = ls[li], b = le[li];
                if (b - a == 5 && memcmp(t + a, "#data", 5) == 0) break;
                if (section != 3 && t[a] == '#') {
                    if (starts(t + a, b - a, "#errors") || starts(t + a, b - a, "#new-errors")) section = 2;
                    else if (starts(t + a, b - a, "#document-fragment")) section = 4;
                    else if (starts(t + a, b - a, "#script-on")) { script_on = 1; section = 2; }
                    else if (starts(t + a, b - a, "#script-off")) section = 2;
                    else if (starts(t + a, b - a, "#document")) section = 3;
                    li++;
                    continue;
                }
                if (section == 1) { if (!first_data) push_back(dat, '\n'); append_range(dat, t + a, b - a); first_data = 0; }
                else if (section == 4) { append_range(ctx, t + a, b - a); fragment = 1; section = 2; }
                else if (section == 3) { if (!first_doc) push_back(want, '\n'); append_range(want, t + a, b - a); first_doc = 0; }
                li++;
            }
            while (len(want) && at(want, len(want) - 1) == '\n') pop_back(want);
            int ns_ctx = fragment && memchr(data(ctx), ' ', len(ctx)) != 0;
            if (script_on || ns_ctx) { fskip++; continue; }
            int r = run_case(data(dat), len(dat), data(ctx), len(ctx), fragment, data(want), len(want), verbose, base, no);
            if (r) fpass++; else ffail++;
        }
        printf("%-44s %4d passed %4d failed %4d skipped\n", base, fpass, ffail, fskip);
        pass += fpass; fail += ffail; skip += fskip;
    }
    printf("html5lib tree construction: %d passed, %d failed, %d skipped -- %.1f%% of the cases run\n",
           pass, fail, skip, pass + fail ? 100.0 * pass / (pass + fail) : 0.0);
    return 0;
}
