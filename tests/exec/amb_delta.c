/* tests/exec/amb_delta.c -- the delta between two snapshots (src/rt/amb_proc.c,
   amb_delta_write), held to the snapshot it leads to. Pairs of snapshots are made at
   random: two sections, five predicates whose lines are interleaved as records
   interleave them, values from a small range so that twins are common; the second of
   each pair has lines retracted, put anywhere (between kept ones too), changed and
   moved. Each delta is applied here, by this test's own reading of the reasoner's
   rules (prolog/amb/reasoner.pl): a -(Fact) retracts the first fact equal to it, a
   +(Fact) of an edit is added at the end, a rebuild is its runs of the old facts and
   its new ones in order. The facts each predicate then holds must be the second
   snapshot's, in its order, and the headers its headers. Then the deltas that cannot
   be: the same text, sections that differ, a module twice, a line that is no fact. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <vector.pl>
#include "../../src/rt/amb_proc.h"

#define NPRED 5
static const char *const pred_name[NPRED] = { "dom_a", "dom_b", "dom_c", "css_x", "css_y" };
static const int pred_sec[NPRED] = { 0, 0, 0, 1, 1 };

/* a fact: its predicate and its two values */
typedef struct fact { uint32_t pred, k, v; } fact;

static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(uint32_t n) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t)(rng % n);
}

/* the predicate a name is, or NPRED */
static uint32_t pred_of(const char *s, size_t n) {
    for (uint32_t p = 0; p < NPRED; p++)
        if (strlen(pred_name[p]) == n && memcmp(pred_name[p], s, n) == 0) return p;
    return NPRED;
}

/* a snapshot's text: each section's header, then its predicates' facts, interleaved at
   random, each predicate's in its order */
static void snapshot_text(const vector<fact> *fs, uint32_t rev, vector<char> *out) {
    clear(*out);
    for (int sec = 0; sec < 2; sec++) {
        char h[128];
        int hn = snprintf(h, sizeof h, "amb_snapshot(%s, 1, %u, 'fp%d').\n", sec ? "css" : "dom", rev + (uint32_t)sec, sec);
        append_range(out, h, (size_t)hn);
        /* the next fact of each predicate of the section, taken at random */
        size_t next[NPRED];
        for (int p = 0; p < NPRED; p++) next[p] = 0;
        for (;;) {
            int left[NPRED], nl = 0;
            for (int p = 0; p < NPRED; p++) {
                if (pred_sec[p] != sec) continue;
                while (next[p] < len(*fs) && at(*fs, next[p]).pred != (uint32_t)p) next[p]++;
                if (next[p] < len(*fs)) left[nl++] = p;
            }
            if (!nl) break;
            int p = left[rnd((uint32_t)nl)];
            fact f = at(*fs, next[p]);
            char l[128];
            int ln = snprintf(l, sizeof l, "%s(n(1,%u,0),v%u).\n", pred_name[p], f.k, f.v);
            append_range(out, l, (size_t)ln);
            next[p]++;
        }
    }
}

/* the second snapshot's facts: the first's, each predicate's run edited at random */
static void mutate(const vector<fact> *a, vector<fact> *b) {
    clear(*b);
    for (uint32_t p = 0; p < NPRED; p++) {
        vector<fact> run;
        for (size_t i = 0; i < len(*a); i++) if (at(*a, i).pred == p) push_back(run, at(*a, i));
        uint32_t edits = rnd(4) == 0 ? 0 : 1 + rnd(5);
        for (uint32_t e = 0; e < edits; e++) {
            uint32_t what = rnd(5);
            fact f = { p, 1 + rnd(12), rnd(3) };
            size_t n = len(run);
            if (what == 0 && n) {                          /* a fact retracted */
                size_t at_ = rnd((uint32_t)n);
                for (size_t i = at_; i + 1 < n; i++) run[i] = at(run, i + 1);
                pop_back(run);
            } else if (what == 1 || n == 0) {              /* a fact put anywhere */
                size_t at_ = rnd((uint32_t)n + 1);
                push_back(run, f);
                for (size_t i = n; i > at_; i--) run[i] = at(run, i - 1);
                run[at_] = f;
            } else if (what == 2) {                        /* one changed in place */
                run[rnd((uint32_t)n)] = f;
            } else if (what == 3 && n > 1) {               /* two swapped */
                size_t i = rnd((uint32_t)n), j = rnd((uint32_t)n);
                fact t = at(run, i);
                run[i] = at(run, j);
                run[j] = t;
            } else push_back(run, f);                      /* one added at the end */
        }
        for (size_t i = 0; i < len(run); i++) push_back(b, at(run, i));
    }
}

/* a fact's line, as the delta writes it: pred(n(1,K,0),vV); s ends with a NUL */
static int read_fact(const char *s, size_t n, fact *f) {
    size_t p = 0;
    while (p < n && s[p] != '(') p++;
    uint32_t pr = pred_of(s, p);
    unsigned k = 0, v = 0;
    if (pr == NPRED || sscanf(s + p, "(n(1,%u,0),v%u", &k, &v) != 2) return 0;
    f->pred = pr;
    f->k = k;
    f->v = v;
    return 1;
}

/* the delta applied to the facts held, the reasoner's way: 1 done, 0 refused */
static int apply(vector<fact> *held, const char *t, size_t n, uint32_t *headers, uint32_t *edits, uint32_t *rebuilds) {
    size_t i = 0;
    int block = 0;                      /* 1 an edit, 2 a rebuild */
    uint32_t bp = 0;
    vector<fact> old;                   /* a rebuild's predicate's facts as held */
    vector<fact> made;                  /* and as it makes them */
    size_t cur = 0;
    *headers = 0;
    while (i < n) {
        size_t e = i;
        while (e < n && t[e] != '\n') e++;
        /* the line, ended by a NUL for sscanf */
        char l[256];
        size_t ln = e - i;
        if (ln >= sizeof l) return 0;
        memcpy(l, t + i, ln);
        l[ln] = 0;
        i = e + 1;
        int ends_block = ln >= 13 && (memcmp(l, "amb_snapshot(", 13) == 0 || memcmp(l, "amb_edit(", 9) == 0 || memcmp(l, "amb_rebuild(", 12) == 0);
        if (ends_block || (ln == 8 && memcmp(l, "amb_end.", 8) == 0)) {
            if (block == 2) {
                /* the predicate's facts made again: the others' kept, its own after them */
                vector<fact> rest;
                for (size_t j = 0; j < len(*held); j++) if (at(*held, j).pred != bp) push_back(rest, at(*held, j));
                clear(*held);
                for (size_t j = 0; j < len(rest); j++) push_back(held, at(rest, j));
                for (size_t j = 0; j < len(made); j++) push_back(held, at(made, j));
            }
            block = 0;
        }
        if (ln == 10 && memcmp(l, "amb_delta.", 10) == 0) continue;
        if (ln == 8 && memcmp(l, "amb_end.", 8) == 0) return 1;
        if (ln > 13 && memcmp(l, "amb_snapshot(", 13) == 0) { (*headers)++; continue; }
        if (ln > 9 && memcmp(l, "amb_edit(", 9) == 0) {
            block = 1;
            bp = pred_of(l + 9, ln - 11);
            (*edits)++;
            continue;
        }
        if (ln > 12 && memcmp(l, "amb_rebuild(", 12) == 0) {
            block = 2;
            bp = pred_of(l + 12, ln - 14);
            (*rebuilds)++;
            clear(old);
            clear(made);
            for (size_t j = 0; j < len(*held); j++) if (at(*held, j).pred == bp) push_back(old, at(*held, j));
            cur = 0;
            continue;
        }
        if (ln > 4 && (l[0] == '-' || l[0] == '+') && l[1] == '(') {
            fact f;
            if (!read_fact(l + 2, ln - 4, &f) || f.pred != bp || !block) return 0;
            if (l[0] == '+' && block == 2) { push_back(made, f); continue; }
            if (l[0] == '+') { push_back(held, f); continue; }
            if (block != 1) return 0;
            /* the first fact held equal to it */
            size_t j = 0;
            while (j < len(*held) && !(at(*held, j).pred == f.pred && at(*held, j).k == f.k && at(*held, j).v == f.v)) j++;
            if (j == len(*held)) return 0;
            for (; j + 1 < len(*held); j++) (*held)[j] = at(*held, j + 1);
            pop_back(*held);
            continue;
        }
        unsigned a = 0, c = 0;
        if (block == 2 && sscanf(l, "amb_keep(%u,%u).", &a, &c) == 2) {
            if (a < cur || c == 0 || (size_t)a + c > len(old)) return 0;
            for (unsigned j = 0; j < c; j++) push_back(made, at(old, a + j));
            cur = a + c;
            continue;
        }
        return 0;
    }
    return 0;
}

/* each predicate's facts the same, in the same order */
static int same_runs(const vector<fact> *a, const vector<fact> *b) {
    for (uint32_t p = 0; p < NPRED; p++) {
        size_t i = 0, j = 0;
        for (;;) {
            while (i < len(*a) && at(*a, i).pred != p) i++;
            while (j < len(*b) && at(*b, j).pred != p) j++;
            if (i == len(*a) || j == len(*b)) { if (i != len(*a) || j != len(*b)) return 0; break; }
            if (at(*a, i).k != at(*b, j).k || at(*a, i).v != at(*b, j).v) return 0;
            i++;
            j++;
        }
    }
    return 1;
}

/* the delta of old and nw, read back */
static int delta_of(const vector<char> *old, const vector<char> *nw, vector<char> *d, amb_delta_stats *st) {
    clear(*d);
    own FILE *f = tmpfile();
    if (!f) return -1;
    defer(f) { fclose(f); }
    int r = amb_delta_write(f, data(*old), len(*old), data(*nw), len(*nw), st);
    rewind(f);
    char buf[4096];
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) append_range(d, buf, k);
    return r;
}

static void text_of(const char *s, vector<char> *out) { clear(*out); append_range(out, s, strlen(s)); }

int main(void) {
    int trials = 400, good = 0, same_text = 0;
    uint64_t retracted = 0, added = 0;
    uint32_t edits = 0, rebuilds = 0, headers_ok = 0;
    for (int t = 0; t < trials; t++) {
        vector<fact> a;
        vector<fact> b;
        uint32_t n = rnd(40);
        for (uint32_t i = 0; i < n; i++) { fact f = { rnd(NPRED), 1 + rnd(12), rnd(3) }; push_back(a, f); }
        mutate(&a, &b);
        vector<char> ta;
        vector<char> tb;
        vector<char> d;
        snapshot_text(&a, 7, &ta);
        snapshot_text(&b, 9, &tb);
        amb_delta_stats st;
        int r = delta_of(&ta, &tb, &d, &st);
        if (r != 1) { printf("trial %d: the delta answered %d\n", t, r); continue; }
        vector<fact> held;
        for (size_t i = 0; i < len(a); i++) push_back(held, at(a, i));
        uint32_t hs = 0, e = 0, rb = 0;
        if (!apply(&held, data(d), len(d), &hs, &e, &rb)) { printf("trial %d: the delta does not apply:\n%.*s", t, (int)len(d), data(d)); continue; }
        if (!same_runs(&held, &b)) { printf("trial %d: the facts held are not the new snapshot's:\n%.*s", t, (int)len(d), data(d)); continue; }
        if (hs == 2) headers_ok++;
        good++;
        edits += e;
        rebuilds += rb;
        retracted += st.retracted;
        added += st.added;
        if (st.edits != e || st.rebuilds != rb) printf("trial %d: the delta's counts are not its blocks\n", t);
    }
    printf("%d random pairs of snapshots: each delta leaves every predicate's facts as the new snapshot's, in order: %d\n", trials, good);
    printf("  their headers, the new snapshot's: %u; %u predicates edited, %u rebuilt; %llu facts retracted, %llu added\n", headers_ok, edits,
           rebuilds, (unsigned long long)retracted, (unsigned long long)added);
    /* the cases with no delta */
    vector<char> x;
    vector<char> y;
    vector<char> d;
    amb_delta_stats st;
    text_of("amb_snapshot(dom, 1, 1, 'f').\ndom_a(n(1,1,0),v1).\n", &x);
    text_of("amb_snapshot(dom, 1, 1, 'f').\ndom_a(n(1,1,0),v1).\n", &y);
    same_text = delta_of(&x, &y, &d, &st);
    printf("the same text: %d (no delta to send)\n", same_text);
    text_of("amb_snapshot(dom, 1, 2, 'f').\ndom_a(n(1,1,0),v1).\namb_snapshot(css, 1, 2, 'f').\n", &y);
    printf("a section more: %d (the whole snapshot is sent)\n", delta_of(&x, &y, &d, &st));
    text_of("amb_snapshot(dom, 2, 1, 'f').\ndom_a(n(2,1,0),v1).\n", &y);
    printf("another document: %d\n", delta_of(&x, &y, &d, &st));
    text_of("amb_snapshot(dom, 1, 1, 'f').\nx.\namb_snapshot(dom, 1, 1, 'f').\nx.\n", &x);
    text_of("amb_snapshot(dom, 1, 1, 'f').\nx.\namb_snapshot(dom, 1, 1, 'f').\ny.\n", &y);
    printf("a module twice: %d\n", delta_of(&x, &y, &d, &st));
    text_of("amb_snapshot(dom, 1, 1, 'f').\ndom_a(n(1,1,0),v1).\n", &x);
    text_of("amb_snapshot(dom, 1, 1, 'f').\nnot a fact\n", &y);
    printf("a line that is no fact: %d\n", delta_of(&x, &y, &d, &st));
    /* a revision alone moved on: the headers, and no predicate */
    text_of("amb_snapshot(dom, 1, 2, 'f').\ndom_a(n(1,1,0),v1).\n", &y);
    int r = delta_of(&x, &y, &d, &st);
    printf("a revision alone: %d, %.*s", r, (int)len(d), data(d));
    return good == trials ? 0 : 1;
}
