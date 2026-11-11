/* include/amb.pl.h -- the C that include/amb.pl's generated text calls, included
   with the macro file (cicilang includes NAME.pl.h before NAME.pl). The functions
   are src/rt/amb_rt.c's. A generated text is not preprocessed, so it names none
   of these headers' macros: no NULL, no stdout, no EOF. */
#ifndef AMBITIOUS_AMB_PL_H
#define AMBITIOUS_AMB_PL_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* text in a store's byte pool: off and len, never a pointer */
typedef struct amb_span { uint32_t off, len; } amb_span;

/* writing Prolog terms: a quoted atom with every byte escaped that must be, a
   handle F(Doc, Index, Gen), a ref (a handle or none) */
void amb_put_qatom(FILE *out, const char *p, size_t n);
void amb_put_handle(FILE *out, const char *f, uint32_t doc, uint32_t index, uint32_t gen);
void amb_put_ref(FILE *out, const char *f, uint32_t doc, uint32_t index, uint32_t gen);
/* an invariant broken: invariant(Module, Name, Index). on report, if report */
void amb_report(FILE *report, const char *module, const char *what, uint32_t index);
/* a request's outcome: amb_result(Op, Status, Revision). on report, if report */
void amb_result(FILE *report, const char *op, const char *status, uint64_t revision);
/* a name: not empty, no white space and none of < > " ' / = and NUL */
int amb_valid_name(const char *p, size_t n);

/* reading a request: each skips white space first, answers 1 and moves *pos on
   a match, 0 and leaves it on none */
int amb_lex_char(const char *t, size_t n, size_t *pos, char c);
int amb_lex_word(const char *t, size_t n, size_t *pos, const char *w);
int amb_lex_int(const char *t, size_t n, size_t *pos, int64_t *out);
int amb_lex_name(const char *t, size_t n, size_t *pos, char *buf, size_t cap, size_t *len);
int amb_lex_atom(const char *t, size_t n, size_t *pos, char *buf, size_t cap, size_t *len);
int amb_lex_handle(const char *t, size_t n, size_t *pos, const char *f, uint32_t *doc, uint32_t *index, uint32_t *gen);
int amb_lex_end(const char *t, size_t n, size_t *pos);
int amb_name_is(const char *p, size_t n, const char *w);
#endif
