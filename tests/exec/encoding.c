/* tests/exec/encoding.c -- the WHATWG Encoding standard's decoders and HTML's encoding
   sniffing (src/html/encoding.c) over the cases of tests/encoding/cases.txt: each case
   answered on a line, as tests/encoding/gen.py prints the answer that Python's codecs
   and the standards' own text give (tests/exec/encoding.expect). The cases: every
   label; each single-byte encoding's bytes; every two-byte sequence of each
   multi-byte encoding, by its first byte, and gb18030's four-byte ones; malformed and
   boundary sequences; random bytes; and documents whose encoding is sniffed. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);
#include "../../src/html/encoding.h"
HTML_DECODE_DECL;

static const char *const source_names[] = { "bom", "http", "meta", "xml", "detected", "default" };

static int hex_digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* a field of hex digits as bytes; - is none */
static void unhex(const char *p, size_t n, vector<char> *out) {
    clear(out);
    if (n == 1 && p[0] == '-') return;
    for (size_t i = 0; i + 1 < n; i += 2) push_back(out, (char)(hex_digit((unsigned char)p[i]) * 16 + hex_digit((unsigned char)p[i + 1])));
}

/* the code points of the UTF-8 the decoder wrote */
static void code_points(const vector<char> *u, vector<uint32_t> *cps) {
    clear(cps);
    size_t i = 0, n = len(*u);
    while (i < n) {
        uint32_t b = (unsigned char)at(*u, i), cp = b, k = 0;
        if (b >= 0xF0) { cp = b & 0x07; k = 3; }
        else if (b >= 0xE0) { cp = b & 0x0F; k = 2; }
        else if (b >= 0xC0) { cp = b & 0x1F; k = 1; }
        i++;
        for (uint32_t j = 0; j < k && i < n; j++, i++) cp = (cp << 6) | ((unsigned char)at(*u, i) & 0x3F);
        push_back(cps, cp);
    }
}

/* bytes decoded in enc: their code points, and the errors */
static size_t decode(int enc, const vector<char> *bytes, vector<uint32_t> *cps) {
    vector<char> out;
    size_t errors = html_decode(enc, data(*bytes), len(*bytes), &out);
    code_points(&out, cps);
    return errors;
}

static uint32_t fnv(uint32_t h, const vector<uint32_t> *cps) {
    for (size_t i = 0; i < len(*cps); i++) { h ^= at(*cps, i); h *= 0x01000193u; }
    h ^= 0xFFFFFFFFu;
    return h * 0x01000193u;
}

/* the line's field k (from 0), as a run of the line */
static int field(const char *line, size_t n, int k, size_t *at_out, size_t *len_out) {
    size_t i = 0;
    for (int f = 0;; f++) {
        while (i < n && line[i] == ' ') i++;
        if (i >= n) return 0;
        size_t s = i;
        while (i < n && line[i] != ' ') i++;
        if (f == k) { *at_out = s; *len_out = i - s; return 1; }
    }
}

int main(void) {
    vector<char> all;
    {
        own FILE *f = fopen("tests/encoding/cases.txt", "rb");
        if (!f) { printf("tests/encoding/cases.txt not read\n"); return 1; }
        defer(f) { fclose(f); }
        char buf[8192];
        size_t k;
        while ((k = fread(buf, 1, sizeof buf, f)) > 0) append_range(all, buf, k);
    }
    vector<char> bytes, typ, word;
    vector<uint32_t> cps;
    size_t pos = 0, cases = 0;
    while (pos < len(all)) {
        size_t e = pos;
        while (e < len(all) && at(all, e) != '\n') e++;
        vector<char> line;
        append_range(line, data(all) + pos, e - pos);
        pos = e + 1;
        size_t n = len(line);
        if (n == 0 || at(line, 0) == '#') continue;
        cases++;
        size_t a0, l0, a1, l1, a2, l2, a3, l3;
        field(data(line), n, 0, &a0, &l0);
        printf("%.*s -> ", (int)n, data(line));
        if (l0 == 5 && memcmp(data(line) + a0, "label", 5) == 0) {
            field(data(line), n, 1, &a1, &l1);
            unhex(data(line) + a1, l1, &bytes);
            int enc = html_enc_of_label(data(bytes), len(bytes));
            printf("%s\n", enc < 0 ? "failure" : html_enc_name(enc));
            continue;
        }
        if (l0 == 5 && memcmp(data(line) + a0, "sniff", 5) == 0) {
            field(data(line), n, 1, &a1, &l1);
            field(data(line), n, 2, &a2, &l2);
            field(data(line), n, 3, &a3, &l3);
            unhex(data(line) + a2, l2, &typ);
            unhex(data(line) + a3, l3, &bytes);
            html_sniffed sn;
            html_sniff(data(bytes), len(bytes), data(typ), len(typ), at(line, a1) == '1', &sn);
            printf("%s %s\n", html_enc_name(sn.enc), sn.source >= 0 && sn.source <= 5 ? source_names[sn.source] : "?");
            continue;
        }
        field(data(line), n, 1, &a1, &l1);
        field(data(line), n, 2, &a2, &l2);
        clear(&word);
        append_range(word, data(line) + a1, l1);
        int enc = html_enc_of_label(data(word), len(word));
        vector<char> prefix;
        unhex(data(line) + a2, l2, &prefix);
        if (l0 == 6 && memcmp(data(line) + a0, "decode", 6) == 0) {
            size_t errors = decode(enc, &prefix, &cps);
            if (len(cps) == 0) printf("(nothing)");
            for (size_t i = 0; i < len(cps); i++) printf("%sU+%04X", i ? " " : "", at(cps, i));
            printf(" (%zu error%s)\n", errors, errors == 1 ? "" : "s");
            continue;
        }
        /* pairs: the prefix and each byte; quads: the prefix, 81 to FE, then 30 to 39 */
        int quads = l0 == 5 && memcmp(data(line) + a0, "quads", 5) == 0;
        uint32_t h = 0x811C9DC5u;
        size_t inputs = 0, points = 0, errors = 0;
        for (int b3 = quads ? 0x81 : 0; b3 <= (quads ? 0xFE : 0xFF); b3++)
            for (int b4 = quads ? 0x30 : -1; b4 <= (quads ? 0x39 : -1); b4++) {
                clear(&bytes);
                append_range(bytes, data(prefix), len(prefix));
                push_back(bytes, (char)b3);
                if (b4 >= 0) push_back(bytes, (char)b4);
                errors += decode(enc, &bytes, &cps);
                points += len(cps);
                h = fnv(h, &cps);
                inputs++;
            }
        printf("%zu inputs, %zu code points, %zu errors, fnv1a32 %08x\n", inputs, points, errors, h);
    }
    printf("%zu cases\n", cases);
    return 0;
}
