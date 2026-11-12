/* src/rt/amb_prefs.h -- the reader's preferences that change how a page is drawn, as
   the profile keeps them (prolog/amb/profile.pl: amb_profile_pref/2, the keeper's
   prefs) and the shell hands each stage (src/shell/main.c: page_cascade, page_layout,
   page_paint). A struct of numbers, no pointer, so a session keeps it as a value.

   What each is to do in the stages, once they take it:
     font_size       the base font size, CSS px: the initial font-size (medium) the root
                     inherits and rem resolves against, as Chromium's font size setting;
     color_scheme    prefers-color-scheme in media queries, and the canvas's and the
                     default text's colours (Canvas, CanvasText) where a page opts in by
                     color-scheme;
     reduced_motion  prefers-reduced-motion in media queries;
     zoom            CSS px to device px: the layout viewport is the window divided by
                     it, and the painter draws each CSS px zoom device px wide. */
#ifndef AMBITIOUS_AMB_PREFS_H
#define AMBITIOUS_AMB_PREFS_H
#include <stdlib.h>
#include <string.h>

#define AMB_SCHEME_LIGHT 0
#define AMB_SCHEME_DARK 1

typedef struct amb_prefs {
    double font_size;        /* px: 16 */
    double zoom;             /* a factor: 1 */
    int color_scheme;        /* AMB_SCHEME_LIGHT */
    int reduced_motion;      /* 0: no-preference; 1: reduce */
    int from_profile;        /* 1 when a profile gave them, 0 the defaults */
} amb_prefs;

static inline void amb_prefs_default(amb_prefs *p) {
    p->font_size = 16;
    p->zoom = 1;
    p->color_scheme = AMB_SCHEME_LIGHT;
    p->reduced_motion = 0;
    p->from_profile = 0;
}

/* one preference as the keeper writes it, pref(Key, Value), into p: 1 when it is one */
static inline int amb_prefs_line(amb_prefs *p, const char *line) {
    if (strncmp(line, "pref(", 5) != 0) return 0;
    const char *k = line + 5;
    const char *v = strchr(k, ',');
    if (!v) return 0;
    v++;
    while (*v == ' ') v++;
    if (strncmp(k, "font_size,", 10) == 0) p->font_size = atof(v);
    else if (strncmp(k, "zoom,", 5) == 0) p->zoom = atof(v);
    else if (strncmp(k, "color_scheme,", 13) == 0) p->color_scheme = strncmp(v, "dark", 4) == 0 ? AMB_SCHEME_DARK : AMB_SCHEME_LIGHT;
    else if (strncmp(k, "reduced_motion,", 15) == 0) p->reduced_motion = strncmp(v, "reduce", 6) == 0;
    else return 0;
    return 1;
}
#endif
