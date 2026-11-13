/* tests/exec/cssom_text.c -- the CSS Object Model's parsers and serializers (src/css/cssom.c,
   src/css/cssom_decl.c) against Chromium's text, line for line.

   Each line of tests/exec/cssom/cases.txt read: "sheet: CSS" a sheet's rules, each one's cssText
   on a line of its own; "rule: CSS" one rule inserted into an empty sheet (ERR when insertRule
   refuses it); "decl: DECLARATIONS" a style rule's declarations, its cssText and each longhand's
   value and priority. tests/exec/cssom_text.expect is Chromium's output of the same lines
   (tests/exec/cssom/chromium.js), so a rule's or a value's text that drifts from Chromium's shows
   here; under valgrind too, the model's every record freed. The shorthands the model keeps whole
   (all, grid, grid-template, mask, offset, animation-range: docs/limitations.md) are not among
   the cases. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "../../src/css/cssom.h"

int main(void) {
    own FILE *f = fopen("tests/exec/cssom/cases.txt", "r");
    if (!f) { printf("no tests/exec/cssom/cases.txt\n"); return 1; }
    defer(f) { fclose(f); }
    char line[8192];
    css_om m;
    memset(&m, 0, sizeof m);
    defer(m) { CSS_OM_DROP(m); }
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (!strncmp(line, "sheet: ", 7)) {
            uint32_t s = css_om_sheet_new(&m, CSS_OM_O_STYLE);
            css_om_read(&m, s, line + 7, n - 7, 0);
            printf("%s\n", line);
            for (uint32_t i = 0; i < css_om_count(&m, s, 0); i++) {
                vector<char> t;
                css_om_rule_text(&m, css_om_at(&m, s, 0, i), &t);
                push_back(t, 0);
                printf("  %u: %s\n", i, data(t));
            }
        } else if (!strncmp(line, "rule: ", 6)) {
            uint32_t s = css_om_sheet_new(&m, CSS_OM_O_STYLE);
            uint32_t r = 0;
            int e = css_om_insert(&m, s, 0, 0, line + 6, n - 6, 0, &r);
            vector<char> t;
            if (!e) css_om_rule_text(&m, r, &t);
            push_back(t, 0);
            printf("%s -> %s %s\n", line, e ? "ERR" : "0", data(t));
        } else if (!strncmp(line, "decl: ", 6)) {
            css_om_dl dl;
            memset(&dl, 0, sizeof dl);
            css_om_parse_decls(&dl, line + 6, n - 6, CSS_OM_B_STYLE);
            vector<char> t;
            css_om_decls_text(&dl, CSS_OM_B_STYLE, &t);
            push_back(t, 0);
            printf("%s -> \"%s\" [", line, data(t));
            for (uint32_t i = 0; i < css_om_decl_count(&dl); i++) {
                vector<char> nm, v;
                css_om_decl_name(&dl, i, &nm);
                css_om_get(&dl, CSS_OM_B_STYLE, data(nm), len(nm), &v);
                int imp = css_om_important(&dl, CSS_OM_B_STYLE, data(nm), len(nm));
                push_back(nm, 0);
                push_back(v, 0);
                printf("%s%s=%s%s", i ? "; " : "", data(nm), data(v), imp ? "!" : "");
            }
            printf("]\n");
            CSS_OM_DL_DROP(dl);
        }
    }
    return 0;
}
