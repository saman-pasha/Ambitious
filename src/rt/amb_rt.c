/* src/rt/amb_rt.c -- the runtime of include/amb.pl.h: what the generated code
   calls to write Prolog terms and to read requests. Plain C over borrows only:
   no function here keeps a pointer it is given, or allocates. */
#include <amb.pl.h>

/* ---- writing ------------------------------------------------------------- */

/* A quoted atom that reads back as the same bytes: ' and \ escaped, the
   control characters by name or in hex; bytes from 0x80 up are UTF-8 and go
   through as they are. */
void amb_put_qatom(FILE *out, const char *p, size_t n) {
    fputc('\'', out);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c == '\'') fputs("\\'", out);
        else if (c == '\\') fputs("\\\\", out);
        else if (c == '\n') fputs("\\n", out);
        else if (c == '\t') fputs("\\t", out);
        else if (c == '\r') fputs("\\r", out);
        else if (c < 0x20 || c == 0x7f) fprintf(out, "\\x%x\\", (unsigned)c);
        else fputc((int)c, out);
    }
    fputc('\'', out);
}

void amb_put_handle(FILE *out, const char *f, uint32_t doc, uint32_t index, uint32_t gen) {
    fprintf(out, "%s(%u,%u,%u)", f, (unsigned)doc, (unsigned)index, (unsigned)gen);
}

void amb_put_f64(FILE *out, double v) {
    if (v != v || v > 1e300 || v < -1e300) v = 0;
    if (v == 0) v = 0;    /* no -0 */
    fprintf(out, "%.15g", v);
}

void amb_put_ref(FILE *out, const char *f, uint32_t doc, uint32_t index, uint32_t gen) {
    if (index == 0) fputs("none", out);
    else amb_put_handle(out, f, doc, index, gen);
}

void amb_report(FILE *report, const char *module, const char *what, uint32_t index) {
    if (report) fprintf(report, "invariant(%s, %s, %u).\n", module, what, (unsigned)index);
}

void amb_result(FILE *report, const char *op, const char *status, uint64_t revision) {
    if (report) {
        fputs("amb_result(", report);
        amb_put_qatom(report, op, strlen(op));
        fprintf(report, ", %s, %llu).\n", status, (unsigned long long)revision);
    }
}

int amb_valid_name(const char *p, size_t n) {
    if (n == 0) return 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c == 0 || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') return 0;
        if (c == '/' || c == '>') return 0;
    }
    return 1;
}

/* ---- reading a request ----------------------------------------------------
   Each takes the text, its length and a position, skips white space, and
   answers 1 with the position moved past what it read, or 0 with it left. */

static size_t amb_skip(const char *t, size_t n, size_t i) {
    while (i < n && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) i++;
    return i;
}

int amb_lex_char(const char *t, size_t n, size_t *pos, char c) {
    size_t i = amb_skip(t, n, *pos);
    if (i < n && t[i] == c) { *pos = i + 1; return 1; }
    return 0;
}

int amb_lex_end(const char *t, size_t n, size_t *pos) {
    size_t i = amb_skip(t, n, *pos);
    if (i == n) { *pos = i; return 1; }
    return 0;
}

static int amb_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

int amb_lex_word(const char *t, size_t n, size_t *pos, const char *w) {
    size_t i = amb_skip(t, n, *pos), k = 0;
    while (w[k] && i + k < n && t[i + k] == w[k]) k++;
    if (w[k] != 0) return 0;
    if (i + k < n && amb_alnum(t[i + k])) return 0;
    *pos = i + k;
    return 1;
}

int amb_lex_int(const char *t, size_t n, size_t *pos, int64_t *out) {
    size_t i = amb_skip(t, n, *pos);
    int neg = 0;
    if (i < n && t[i] == '-') { neg = 1; i++; }
    if (i >= n || t[i] < '0' || t[i] > '9') return 0;
    int64_t v = 0;
    while (i < n && t[i] >= '0' && t[i] <= '9') {
        if (v > (INT64_MAX - (t[i] - '0')) / 10) return 0;
        v = v * 10 + (t[i] - '0');
        i++;
    }
    *out = neg ? -v : v;
    *pos = i;
    return 1;
}

/* an unquoted atom: a lower-case letter, then letters, digits and _ */
int amb_lex_name(const char *t, size_t n, size_t *pos, char *buf, size_t cap, size_t *len) {
    size_t i = amb_skip(t, n, *pos), k = 0;
    if (cap == 0 || i >= n || t[i] < 'a' || t[i] > 'z') return 0;
    while (i < n && amb_alnum(t[i])) {
        if (k + 1 >= cap) return 0;
        buf[k++] = t[i++];
    }
    buf[k] = 0;
    *len = k;
    *pos = i;
    return 1;
}

static int amb_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* an atom, quoted (its escapes decoded) or unquoted, into buf (NUL after it) */
int amb_lex_atom(const char *t, size_t n, size_t *pos, char *buf, size_t cap, size_t *len) {
    size_t i = amb_skip(t, n, *pos), k = 0;
    if (cap == 0) return 0;
    if (i < n && t[i] != '\'') return amb_lex_name(t, n, pos, buf, cap, len);
    if (i >= n) return 0;
    i++;
    while (i < n && t[i] != '\'') {
        char c = t[i++];
        if (c == '\\') {
            if (i >= n) return 0;
            char e = t[i++];
            if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else if (e == 'r') c = '\r';
            else if (e == '\\' || e == '\'' || e == '"') c = e;
            else if (e == 'x') {
                int v = 0, any = 0;
                while (i < n && amb_hexval(t[i]) >= 0) { v = v * 16 + amb_hexval(t[i]); i++; any = 1; if (v > 255) return 0; }
                if (!any || i >= n || t[i] != '\\') return 0;
                i++;
                c = (char)v;
            } else return 0;
        }
        if (k + 1 >= cap) return 0;
        buf[k++] = c;
    }
    if (i >= n) return 0;
    buf[k] = 0;
    *len = k;
    *pos = i + 1;
    return 1;
}

/* F(Doc, Index, Gen) */
int amb_lex_handle(const char *t, size_t n, size_t *pos, const char *f, uint32_t *doc, uint32_t *index, uint32_t *gen) {
    size_t i = *pos;
    int64_t a = 0, b = 0, c = 0;
    if (!amb_lex_word(t, n, &i, f) || !amb_lex_char(t, n, &i, '(')) return 0;
    if (!amb_lex_int(t, n, &i, &a) || !amb_lex_char(t, n, &i, ',')) return 0;
    if (!amb_lex_int(t, n, &i, &b) || !amb_lex_char(t, n, &i, ',')) return 0;
    if (!amb_lex_int(t, n, &i, &c) || !amb_lex_char(t, n, &i, ')')) return 0;
    if (a < 0 || b < 0 || c < 0 || a > UINT32_MAX || b > UINT32_MAX || c > UINT32_MAX) return 0;
    *doc = (uint32_t)a;
    *index = (uint32_t)b;
    *gen = (uint32_t)c;
    *pos = i;
    return 1;
}

int amb_name_is(const char *p, size_t n, const char *w) {
    size_t k = 0;
    while (k < n && w[k] && p[k] == w[k]) k++;
    return k == n && w[k] == 0;
}
