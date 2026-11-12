/* tests/exec/quirks_mode.c -- the document's mode, as the tree builder decides it from the
   DOCTYPE (HTML 13.2.6.4.1): each line of tests/exec/quirks_mode.txt (a DOCTYPE, what comes
   before one, or nothing) parsed as a document's start, and the mode kept on its document
   printed with the line. quirks_mode.expect is Chromium's mode for each line
   (tools/bench/doctypes.js), but for the four lines whose system identifier is empty: the
   HTML standard takes an empty system identifier for one that is there, so HTML 4.01's
   transitional and frameset identifiers put the document in limited-quirks mode, where
   Blink takes it for a missing one and gives quirks mode. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);

/* the probe tools/bench/doctypes.js puts after each line */
static const char probe[] = "<div id=quirks-probe><span style=\"display:inline-block;width:1px;height:1px\"></span></div>";

static void mode_of(const char *line, size_t n) {
    vector<char> src;
    append_range(src, line, n);
    append_range(src, probe, sizeof probe - 1);
    dom_document d;
    html_parser p;
    dom_init(&d, 1);
    html_init(&p, 1);
    html_parse(&p, &d, data(src), len(src));
    printf("%s\t%.*s\n", dom_document_mode_name(d.node[1].mode), (int)n, line);
}

int main(void) {
    own FILE *f = fopen("tests/exec/quirks_mode.txt", "rb");
    if (!f) { printf("no tests/exec/quirks_mode.txt\n"); return 1; }
    defer(f) { fclose(f); }
    vector<char> all;
    char buf[4096];
    for (;;) {
        size_t k = fread(buf, 1, sizeof buf, f);
        if (k == 0) break;
        append_range(all, buf, k);
    }
    size_t i = 0, n = len(all);
    /* a line each; the last newline ends the last line */
    while (i < n) {
        size_t e = i;
        while (e < n && all[e] != '\n') e++;
        mode_of(data(all) + i, e - i);
        i = e + 1;
    }
    return 0;
}
