/* tests/exec/counter_styles.c -- counter styles (CSS Counter Styles 3) as Chromium writes them: each predefined
   style at values from 0 to 123456789 and below 0, as counter() writes it; and this test's own @counter-style rules (each
   system, prefix and suffix, range, pad, negative, fallback, extends, invalid rules and names), as counter() writes them and
   as a marker. The .expect is Chromium's text, read from its accessibility tree (a ::before's counter(), a list item's
   ListMarker) over pages of the same styles and values. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(net);
amb_interface(css);
#include "../../src/css/css.h"

/* the test's own counter styles, one rule a line */
static const char *const sheet[] = {
    "@counter-style cyc { system: cyclic; symbols: \"\\25C6\" \"\\25C7\" \"\\25CB\"; prefix: \"(\"; suffix: \") \"; }\n",
    "@counter-style fix { system: fixed; symbols: A B C D; }\n",
    "@counter-style fix5 { system: fixed 5; symbols: \"v\" \"vi\" \"vii\"; fallback: lower-roman; }\n",
    "@counter-style sym { system: symbolic; symbols: \"*\" \"\\2020\" \"\\2021\"; }\n",
    "@counter-style alpha2 { system: alphabetic; symbols: x y; }\n",
    "@counter-style bin { system: numeric; symbols: \"0\" \"1\"; suffix: \"b \"; }\n",
    "@counter-style hex { system: numeric; symbols: \"0\" \"1\" \"2\" \"3\" \"4\" \"5\" \"6\" \"7\" \"8\" \"9\" \"A\" \"B\" \"C\" \"D\" \"E\" \"F\"; prefix: \"0x\"; }\n",
    "@counter-style add { system: additive; additive-symbols: 10 \"X\", 5 \"V\", 1 \"I\"; }\n",
    "@counter-style addz { system: additive; additive-symbols: 3 \"c\", 2 \"b\", 0 \"zero\"; }\n",
    "@counter-style ext { system: extends lower-roman; prefix: \"[\"; suffix: \"] \"; }\n",
    "@counter-style rng { system: numeric; symbols: \"0\" \"1\" \"2\" \"3\" \"4\" \"5\" \"6\" \"7\" \"8\" \"9\"; range: 2 4, 10 12; fallback: upper-alpha; }\n",
    "@counter-style padded { system: extends decimal; pad: 4 \"0\"; }\n",
    "@counter-style neg { system: numeric; symbols: \"0\" \"1\" \"2\" \"3\" \"4\" \"5\" \"6\" \"7\" \"8\" \"9\"; negative: \"(\" \")\"; }\n",
    "@counter-style negp { system: extends decimal; negative: \"\\2212\"; pad: 3 \"\\B7\"; }\n",
    "@counter-style badsym { system: alphabetic; symbols: \"a\"; }\n",
    "@counter-style noext { system: extends nothing-here; suffix: \"! \"; }\n",
    "@counter-style cyc2 { system: extends cyc; }\n",
    "@counter-style lower-roman { system: extends upper-roman; prefix: \"<\"; }\n",
    "@counter-style rngr { system: cyclic; symbols: a b c; range: infinite 0; }\n",
    "@counter-style fbloop1 { system: fixed; symbols: q; fallback: fbloop2; }\n",
    "@counter-style fbloop2 { system: fixed; symbols: r; fallback: fbloop1; }\n",
    "@counter-style idents { system: cyclic; symbols: \\2605 \\2606; suffix: \" \"; }\n",
    "@counter-style addbig { system: additive; additive-symbols: 1000 \"M\", 1 \"I\"; }\n",
    "@counter-style padneg { system: extends decimal; pad: 3 \"0\"; negative: \"-\"; }\n",
    "@counter-style dflt { system: cyclic; symbols: o; }\n",
    "@counter-style nosys { symbols: \"\\2022\" \"\\25E6\"; }\n",
    "@counter-style badadd { system: additive; additive-symbols: 1 \"a\", 5 \"b\"; }\n",
    "@counter-style lastwins { system: cyclic; symbols: first; }\n",
    "@counter-style lastwins { system: cyclic; symbols: second; }\n",
    "@counter-style Upper { system: cyclic; symbols: U; }\n",
    "@counter-style disc { system: cyclic; symbols: nope; }\n",
    "@counter-style extdisc { system: extends disc; suffix: \"> \"; }\n",
    "@counter-style kor { system: extends korean-hangul-formal; suffix: \": \"; }\n",
};
static const char *const predefined[] = {
    "decimal", "decimal-leading-zero", "arabic-indic", "armenian", "upper-armenian", "lower-armenian",
    "bengali", "cambodian", "khmer", "cjk-decimal", "devanagari", "georgian",
    "gujarati", "gurmukhi", "hebrew", "kannada", "lao", "malayalam",
    "mongolian", "myanmar", "oriya", "persian", "lower-roman", "upper-roman",
    "tamil", "telugu", "thai", "tibetan", "lower-alpha", "lower-latin",
    "upper-alpha", "upper-latin", "lower-greek", "hiragana", "hiragana-iroha", "katakana",
    "katakana-iroha", "disc", "circle", "square", "disclosure-open", "disclosure-closed",
    "cjk-earthly-branch", "cjk-heavenly-stem", "japanese-informal", "japanese-formal", "korean-hangul-formal", "korean-hanja-informal",
    "korean-hanja-formal", "simp-chinese-informal", "simp-chinese-formal", "trad-chinese-informal", "trad-chinese-formal", "cjk-ideographic",
    "ethiopic-numeric", "ethiopic-halehame", "ethiopic-halehame-am", "ethiopic-halehame-ti-er", "ethiopic-halehame-ti-et", "hangul",
    "hangul-consonant", "urdu",
};
static const long pre_values[] = { 0, 1, 2, 3, 4, 5, 9, 10, 11, 12, 15, 16, 19, 20, 21, 24, 25, 26, 27, 28, 49, 50, 99, 100, 101, 110, 111, 999, 1000, 1001, 1234, 3999, 4000, 4999, 5000, 9999, 10000, 10001, 12345, 19999, 20000, 99999, 100000, 123456, 1000000, 100000000, 123456789, -1, -5, -12, -1234 };
static const char *const mine[] = {
    "cyc", "fix", "fix5", "sym", "alpha2", "bin",
    "hex", "add", "addz", "ext", "rng", "padded",
    "neg", "negp", "badsym", "noext", "cyc2", "lower-roman",
    "rngr", "fbloop1", "fbloop2", "idents", "addbig", "padneg",
    "dflt", "nosys", "badadd", "lastwins", "Upper", "upper",
    "disc", "extdisc", "kor", "Decimal", "LOWER-ALPHA", "none",
    "unknown",
};
static const long mine_values[] = { -12, -5, -1, 0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 26, 27, 100, 1000, 12345 };

static void put(const char *kind, const char *name, long v, const vector<char> *t) {
    printf("%s %s %ld \"%.*s\"\n", kind, name, v, (int)len(*t), data(*t));
}

int main(void) {
    css_style st;
    css_init(&st, 1);
    vector<char> src;
    for (size_t i = 0; i < sizeof sheet / sizeof sheet[0]; i++) append_range(src, sheet[i], strlen(sheet[i]));
    css_parse_sheet(&st, CSS_ORIGIN_AUTHOR, data(src), len(src));
    /* the predefined styles, in a store of no rules (this test's sheet redefines lower-roman) */
    css_style plain;
    css_init(&plain, 1);
    for (size_t i = 0; i < sizeof predefined / sizeof predefined[0]; i++)
        for (size_t k = 0; k < sizeof pre_values / sizeof pre_values[0]; k++) {
            vector<char> t;
            css_counter_text(&plain, predefined[i], strlen(predefined[i]), pre_values[k], 0, &t);
            put("c", predefined[i], pre_values[k], &t);
        }
    for (int marker = 0; marker < 2; marker++)
        for (size_t i = 0; i < sizeof mine / sizeof mine[0]; i++)
            for (size_t k = 0; k < sizeof mine_values / sizeof mine_values[0]; k++) {
                vector<char> t;
                css_counter_text(&st, mine[i], strlen(mine[i]), mine_values[k], marker, &t);
                put(marker ? "m" : "c", mine[i], mine_values[k], &t);
            }
    return 0;
}
