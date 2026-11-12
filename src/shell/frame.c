/* src/shell/frame.c -- a page's frames (Phase 12u): each iframe's document made, loaded,
   run, styled, laid out and drawn as Chromium makes and draws it. src/shell/frame.h says
   what a frame is and how its stores are kept.

   A FRAME'S DOCUMENT. An iframe with a srcdoc attribute shows an about:srcdoc document of
   its markup (UTF-8, its base URL its parent's, never in quirks mode by its DOCTYPE or its
   lack: HTML 13.2.6.4.1); else one with a src (not about:blank) shows that URL fetched by
   the page's loader as a document (its redirects followed; asked for by the iframe, of the
   page), decoded with its own encoding sniffed and in its own mode; else, or when the src
   names a page that frames its own URL inside itself twice over (Chromium's one level of
   self-reference), about:blank: an empty html, head and body, in quirks mode. A src whose
   fetch fails shows Chromium's error page, which draws nothing and fires no load. A
   frame's parts (its stylesheets, images, scripts) are fetched by a loader of its own.

   ITS SCRIPTS run in its own VM (src/script/frames.c), unless its iframe's sandbox has no
   allow-scripts: then none runs, each refused with Chromium's console message, and its
   noscript content is drawn. A sandbox without allow-same-origin gives the frame an
   opaque origin of its own; a frame inside a sandboxed one is sandboxed as it is.

   THE ORDER, as Chromium's: the page's parse's scripts, its DOMContentLoaded and its async
   scripts; then each of its frames in tree order: its parse's scripts, its own frames,
   load at its window, then load at its iframe; then load at the page's window, then every
   frame's timers and tasks as one event loop.

   ITS VIEWPORT is its iframe's content box (whole px): its initial containing block, the
   width and height media queries and the viewport units read. Content that overflows it
   makes classic scroll bars, as Chromium draws them in a frame (measured in its headless
   shell with its scroll bars shown): 15px, the viewport less them laid out again, a track
   (#fcfcfc; #2c2c2c in the dark scheme) with an arrow button at each end, its arrow a
   triangle of 9 by 5 px (#8b8b8b; #9f9f9f), and a thumb 9px across, its ends round, at the
   track's start (the frame unscrolled) 3px in from the buttons, as long as the track less
   6px times the share of the content the viewport shows, 17px at least; a corner of the
   track's colour where both meet. None when its iframe's scrolling is no, or its root (or
   body, the viewport's overflow) clips its overflow.

   ITS CANVAS is transparent, as Chromium leaves a frame's, unless its root's colour scheme
   is not its iframe's (then the Canvas of its own): it is drawn twice, over black and over
   white, and paint (src/paint/paint.c) draws it into its iframe's content box over what is
   there, so what it leaves transparent shows the page through. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <strings.h>
#include "../net/url.h"
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);
amb_interface(net);
amb_interface(css);
amb_interface(layout);
amb_interface(a11y);
amb_interface(script);

#include "../paint/paint.h"
#include "../css/css.h"
#include "../js/js.h"
#include "../script/script.h"
#include "../script/host.h"
#include "../html/encoding.h"
#include "frame.h"

/* the scroll bars' thickness, a classic one's, in CSS px */
enum { FR_BAR = 15 };

/* ---- the documents ---- */

static int fr_is_iframe(const dom_document *d, uint32_t x) {
    return x && x < d->node_len && d->node[x].live && d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].ns == DOM_NAMESPACE_HTML &&
           names_is(d, d->node[x].name, "iframe", 6);
}
/* x's attribute name: its index, or 0 */
static uint32_t fr_attr(const dom_document *d, uint32_t x, const char *name) {
    uint32_t a = names_find(d, name, strlen(name));
    if (!a) return 0;
    for (uint32_t at_ = d->node[x].first_attr; at_; at_ = d->attr[at_].next) if (d->attr[at_].name == a) return at_;
    return 0;
}
/* an attribute's value, white space trimmed, into out (with a zero after) */
static void fr_attr_text(const dom_document *d, uint32_t at_, vector<char> *out, int trim) {
    amb_span v = d->attr[at_].value;
    const char *t = text_ptr(d, v);
    size_t a = 0, b = v.len;
    if (trim) {
        while (a < b && (t[a] == ' ' || t[a] == '\t' || t[a] == '\n' || t[a] == '\r' || t[a] == '\f')) a++;
        while (b > a && (t[b - 1] == ' ' || t[b - 1] == '\t' || t[b - 1] == '\n' || t[b - 1] == '\r' || t[b - 1] == '\f')) b--;
    }
    append_range(*out, t + a, b - a);
    push_back(*out, 0);
}

/* the next node after x in tree order under the document (not into a template's contents) */
static uint32_t fr_next(const dom_document *d, uint32_t x) {
    if (d->node[x].first_child) return d->node[x].first_child;
    while (x && x != 1 && !d->node[x].next_sibling) x = d->node[x].parent;
    if (!x || x == 1) return 0;
    return d->node[x].next_sibling;
}

int amb_frames_any(const dom_document *d) {
    if (d->node_len < 2) return 0;
    for (uint32_t x = d->node[1].first_child; x; x = fr_next(d, x)) if (fr_is_iframe(d, x)) return 1;
    return 0;
}

/* the content box of x's box in t: 1, and its place and size; 0 when x has none */
static int fr_box(const layout_tree *t, uint32_t x, double *bx, double *by, double *bw, double *bh) {
    for (uint32_t b = 1; b < t->box_len; b++) {
        if (!t->box[b].live || t->box[b].node.index != x || t->box[b].pseudo) continue;
        *bx = t->box[b].x;
        *by = t->box[b].y;
        *bw = t->box[b].w;
        *bh = t->box[b].h;
        return 1;
    }
    return 0;
}

/* how far x's border box's top is above its content box's in t (its border and padding) */
static double plt_border_top(const layout_tree *t, uint32_t x) {
    for (uint32_t b = 1; b < t->box_len; b++)
        if (t->box[b].live && t->box[b].node.index == x && !t->box[b].pseudo) return t->box[b].bt + t->box[b].pt;
    return 0;
}

/* an about:blank document: html, head and body, in quirks mode */
static void fr_blank(dom_document *d) {
    dom_node_h html = dom_create_element(d, "html", 4);
    dom_node_h head = dom_create_element(d, "head", 4);
    dom_node_h body = dom_create_element(d, "body", 4);
    dom_append_child(d, dom_root(d), html);
    dom_append_child(d, html, head);
    dom_append_child(d, html, body);
    dom_set_mode(d, DOM_DOCUMENT_MODE_QUIRKS);
}

/* a URL without its fragment, into out */
static void fr_unfragment(const char *u, char *out, size_t cap) {
    size_t n = 0;
    while (u[n] && u[n] != '#') n++;
    snprintf(out, cap, "%.*s", (int)n, u);
}

/* the document URL of frame k (0 the page's), into out: its docurl when it has one (about:
   srcdoc, about:blank), else its page resource's */
static void fr_doc_url(const amb_frames *fs, const net_loader *nt, uint32_t page, uint32_t k, char *out, size_t cap) {
    out[0] = 0;
    if (!k) {
        if (page && page < nt->resource_len) snprintf(out, cap, "%.*s", (int)nt->resource[page].final_url.len, text_ptr(nt, nt->resource[page].final_url));
        return;
    }
    const struct js_host *h = &fs->hs[k - 1];
    if (len(h->docurl)) { snprintf(out, cap, "%.*s", (int)len(h->docurl), data(h->docurl)); return; }
    const net_loader *fn = &fs->nt[k - 1];
    if (h->page && h->page < fn->resource_len) snprintf(out, cap, "%.*s", (int)fn->resource[h->page].final_url.len, text_ptr(fn, fn->resource[h->page].final_url));
}
/* the base URL of frame k's document (0 the page's): its page resource's final URL, which
   is its parent's base for about:srcdoc and about:blank */
static void fr_base(const amb_frames *fs, const net_loader *nt, uint32_t page, uint32_t k, char *out, size_t cap) {
    out[0] = 0;
    if (!k) {
        if (page && page < nt->resource_len) snprintf(out, cap, "%.*s", (int)nt->resource[page].final_url.len, text_ptr(nt, nt->resource[page].final_url));
        return;
    }
    const net_loader *fn = &fs->nt[k - 1];
    uint32_t pg = fs->hs[k - 1].page;
    if (pg && pg < fn->resource_len) snprintf(out, cap, "%.*s", (int)fn->resource[pg].final_url.len, text_ptr(fn, fn->resource[pg].final_url));
}

/* an origin's key: the one of the frame (or the page) already of the origin text o, or a
   new one; an opaque origin ("null") always a new one */
static uint32_t fr_origin_key(amb_frames *fs, const char *o) {
    uint32_t top = 0;
    for (uint32_t k = 0; k <= fs->tab.n; k++) {
        if (fs->tab.f[k].origin > top) top = fs->tab.f[k].origin;
        if (strcmp(o, "null") != 0 && fs->tab.f[k].origin && strcmp(fs->tab.f[k].origin_text, o) == 0) return fs->tab.f[k].origin;
    }
    return top + 1;
}

/* ---- the cascade, the layout and the scroll bars ---- */

/* the right and bottom edges of what frame k's layout lays out (its scroll extent): its
   boxes' border boxes and its lines' fragments, a box that clips its overflow keeping its
   content's to itself */
static void fr_extent(const layout_tree *t, double *sw, double *sh) {
    uint32_t vp = 0;
    for (uint32_t b = 1; b < t->box_len && !vp; b++) if (t->box[b].live && t->box[b].kind == LAYOUT_BOX_KIND_VIEWPORT) vp = b;
    *sw = vp ? t->box[vp].w : 0;
    *sh = vp ? t->box[vp].h : 0;
    for (uint32_t b = 1; b < t->box_len; b++) {
        if (!t->box[b].live || b == vp) continue;
        /* inside a clipping box (the viewport's root excepted): its overflow is its own */
        int inside = 0;
        for (uint32_t u = t->box[b].parent; u && u != vp; u = t->box[u].parent)
            if (t->box[u].clip && t->box[u].parent != vp) { inside = 1; break; }
        if (inside) continue;
        double r = t->box[b].x + t->box[b].w + t->box[b].pr + t->box[b].br, bt = t->box[b].y + t->box[b].h + t->box[b].pb + t->box[b].bb;
        if (r > *sw) *sw = r;
        if (bt > *sh) *sh = bt;
        for (uint32_t l = t->box[b].first_line; l; l = t->line[l].next)
            for (uint32_t f = t->line[l].first_frag; f; f = t->frag[f].next) {
                if (t->frag[f].x + t->frag[f].w > *sw) *sw = t->frag[f].x + t->frag[f].w;
                if (t->frag[f].y + t->frag[f].h > *sh) *sh = t->frag[f].y + t->frag[f].h;
            }
    }
}

/* whether the root (or the body, whose overflow the viewport takes when the root's is
   visible) of d clips its overflow: overflow hidden, clip or scroll in its style's text */
static int fr_root_clips(const layout_tree *t) {
    uint32_t vp = 0;
    for (uint32_t b = 1; b < t->box_len && !vp; b++) if (t->box[b].live && t->box[b].kind == LAYOUT_BOX_KIND_VIEWPORT) vp = b;
    if (!vp) return 0;
    uint32_t root = t->box[vp].first_child;
    if (root && t->box[root].clip) return 1;
    return 0;
}

/* the iframe's used colour scheme in its document's cascade: 1 dark */
static int fr_scheme_dark(const css_style *st, uint32_t x) {
    for (uint32_t es = 1; es < st->estyle_len; es++) {
        if (st->estyle[es].node.index != x) continue;
        uint32_t c = st->estyle[es].first;
        for (uint32_t e = c; e < st->cval_len && st->cval[e].style == st->cval[c].style; e++)
            if (st->cval[e].prop == CSS_PROP_COLOR_SCHEME) return st->cval[e].color == 0x121212ffu;
        return 0;
    }
    return 0;
}

/* whether a frame's root is in the dark scheme: its viewport's canvas the dark Canvas */
static int fr_root_dark(const layout_tree *t) {
    for (uint32_t b = 1; b < t->box_len; b++)
        if (t->box[b].live && t->box[b].kind == LAYOUT_BOX_KIND_VIEWPORT) return t->box[b].background == 0x121212ffu;
    return 0;
}

/* frame k's cascade's settings: its viewport W x H (what media queries and the viewport
   units read: its scroll bars' room included, as Chromium's), the reader's, and whether its
   scripts run (the scripting media feature) */
static void fr_env(const amb_frames *fs, uint32_t k, double W, double H) {
    const sh_frame *f = &fs->tab.f[k];
    css_set_env(W, H, fs->tab.font_size > 0 ? fs->tab.font_size : 16, fs->tab.dark, fs->tab.reduced);
    css_set_scripting(!(f->sandbox & SH_SANDBOX) || (f->sandbox & SH_SANDBOX_SCRIPTS));
}

/* frame k's scroll bars in its viewport W x H, its document styled in st: laid out (in
   stores of this call's) without the room they take, then again with it, until they hold;
   the scroll extent of the last layout, which the bars found were laid out with */
static void fr_fit(amb_frames *fs, uint32_t k, const css_style *st, double W, double H) {
    uint32_t i = k - 1, id = k + 1;
    amb_frame_info *fi = &fs->info[i];
    int barless = fs->tab.f[k].state == SH_FRAME_FAILED || fi->sbno;
    int vbar = 0, hbar = 0;
    double sw = W, sh = H;
    for (int it = 0; it < 4; it++) {
        double vw = W - (vbar ? FR_BAR : 0), vh = H - (hbar ? FR_BAR : 0);
        if (vw < 0) vw = 0;
        if (vh < 0) vh = 0;
        layout_tree lt;
        layout_init(&lt, id);
        layout_run(&lt, &fs->d[i], st, &fs->nt[i], vw, vh);
        fr_extent(&lt, &sw, &sh);
        if (barless || fr_root_clips(&lt)) { vbar = hbar = 0; break; }
        int nv = floor(sh + 0.5) > H - (hbar ? FR_BAR : 0);
        int nh = floor(sw + 0.5) > W - (nv ? FR_BAR : 0);
        if (nh && !nv) nv = floor(sh + 0.5) > H - FR_BAR;
        if (nv == vbar && nh == hbar) break;
        vbar = nv;
        hbar = nh;
    }
    fi->vbar = (uint32_t)vbar;
    fi->hbar = (uint32_t)hbar;
    fi->w = W;
    fi->h = H;
    fi->sw = sw;
    fi->sh = sh;
    fi->vw = W - (vbar ? FR_BAR : 0);
    fi->vh = H - (hbar ? FR_BAR : 0);
    if (fi->vw < 0) fi->vw = 0;
    if (fi->vh < 0) fi->vh = 0;
}

/* ---- loading ---- */

/* the page's own a frame's load reads, passed along as parameters (a borrow is kept in no
   struct): its document, its loader and its page's record there, the kinds of part its
   profile denies, whether its scripts run */
#define FR_CX dom_document *td, net_loader *tnt, uint32_t page, uint32_t denied, int allowed
#define FR_CXA td, tnt, page, denied, allowed

static void fr_children(amb_frames *fs, FR_CX, uint32_t pk, const layout_tree *plt);

/* the sandbox's flags of an iframe, its parent's (pk) taken in: SH_SANDBOX when either is
   sandboxed, and SH_SANDBOX_SCRIPTS and SH_SANDBOX_SAME_ORIGIN only when both allow them */
static uint32_t fr_sandbox(const amb_frames *fs, uint32_t pk, const dom_document *pd, uint32_t x) {
    uint32_t inherited = pk ? fs->tab.f[pk].sandbox : 0;
    uint32_t a = fr_attr(pd, x, "sandbox");
    if (!a && !(inherited & SH_SANDBOX)) return 0;
    uint32_t allow = SH_SANDBOX_SCRIPTS | SH_SANDBOX_SAME_ORIGIN;
    if (a) {
        allow = 0;
        vector<char> t;
        fr_attr_text(pd, a, &t, 0);
        const char *s = data(t);
        size_t n = strlen(s), p = 0;
        while (p < n) {
            while (p < n && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r' || s[p] == '\f')) p++;
            size_t q = p;
            while (q < n && !(s[q] == ' ' || s[q] == '\t' || s[q] == '\n' || s[q] == '\r' || s[q] == '\f')) q++;
            if (q - p == 13 && strncasecmp(s + p, "allow-scripts", 13) == 0) allow |= SH_SANDBOX_SCRIPTS;
            if (q - p == 17 && strncasecmp(s + p, "allow-same-origin", 17) == 0) allow |= SH_SANDBOX_SAME_ORIGIN;
            p = q;
        }
    }
    if (inherited & SH_SANDBOX) allow &= inherited;
    return SH_SANDBOX | allow;
}

/* how many script elements d has (a sandboxed frame's refused ones) */
static uint32_t fr_scripts_in(const dom_document *d) {
    uint32_t n = 0;
    if (d->node_len < 2) return 0;
    for (uint32_t x = d->node[1].first_child; x; x = fr_next(d, x))
        if (d->node[x].kind == DOM_NODE_KIND_ELEMENT && d->node[x].ns == DOM_NAMESPACE_HTML && names_is(d, d->node[x].name, "script", 6)) n++;
    return n;
}

/* frame k, its iframe x in its parent pk's document pd (laid out in plt): made, loaded,
   run, laid out, its own frames made, its load fired */
static void fr_load(amb_frames *fs, FR_CX, uint32_t k, uint32_t pk, dom_document *pd, const layout_tree *plt, uint32_t x) {
    uint32_t i = k - 1, id = k + 1;
    amb_frame_info *fi = &fs->info[i];
    sh_frame *f = &fs->tab.f[k];
    memset(fi, 0, sizeof *fi);
    f->parent = pk;
    f->iframe = x;
    f->iframe_gen = pd->node[x].gen;
    f->sandbox = fr_sandbox(fs, pk, pd, x);
    uint32_t na = fr_attr(pd, x, "name");
    if (na) {
        vector<char> nm;
        fr_attr_text(pd, na, &nm, 0);
        snprintf(f->name, sizeof f->name, "%s", data(nm));
    }
    uint32_t sc = fr_attr(pd, x, "scrolling");
    if (sc) {
        vector<char> v;
        fr_attr_text(pd, sc, &v, 1);
        fi->sbno = strcasecmp(data(v), "no") == 0 || strcasecmp(data(v), "off") == 0 || strcasecmp(data(v), "noscroll") == 0;
    }
    /* its viewport: its iframe's content box in its parent's layout, whole px */
    double bx = 0, by = 0, bw = 0, bh = 0;
    if (fr_box(plt, x, &bx, &by, &bw, &bh)) { fi->x = floor(bx + 0.5); fi->y = floor(by + 0.5); }
    double W = floor(bw + 0.5), H = floor(bh + 0.5);
    /* its stores */
    dom_init(&fs->d[i], id);
    html_init(&fs->p[i], id);
    css_init(&fs->st[i], id);
    layout_init(&fs->lt[i], id);
    net_init(&fs->nt[i], id);
    a11y_init(&fs->ax[i], id);
    script_init(&fs->sr[i], id);
    fs->hs[i].view_w = W;
    fs->hs[i].view_h = H;
    /* where its document comes from */
    char pbase[8400], url[8400], final[8400], type[512];
    fr_base(fs, tnt, page, pk, pbase, sizeof pbase);
    uint32_t sd = fr_attr(pd, x, "srcdoc"), sa = fr_attr(pd, x, "src");
    vector<char> src;
    if (sa) fr_attr_text(pd, sa, &src, 1);
    int scripts = allowed && (!(f->sandbox & SH_SANDBOX) || (f->sandbox & SH_SANDBOX_SCRIPTS));
    vector<char> body;
    type[0] = 0;
    fi->source = NET_FRAME_SOURCE_BLANK;
    fi->outcome = NET_FRAME_OUTCOME_LOADED;
    snprintf(url, sizeof url, "about:blank");
    snprintf(final, sizeof final, "%s", pbase);
    int srcdoc = 0, is_file = 0;
    uint32_t http_status = 0;
    if (sd) {
        srcdoc = 1;
        fi->source = NET_FRAME_SOURCE_SRCDOC;
        snprintf(url, sizeof url, "about:srcdoc");
        amb_span v = pd->attr[sd].value;
        append_range(body, text_ptr(pd, v), v.len);
        snprintf(type, sizeof type, "text/html;charset=utf-8");
    } else if (sa && strlen(data(src)) && strcasecmp(data(src), "about:blank") != 0) {
        fi->source = NET_FRAME_SOURCE_SRC;
        char abs[8400];
        if (!net_url_resolve(pbase, data(src), abs, sizeof abs)) snprintf(abs, sizeof abs, "%s", data(src));
        snprintf(url, sizeof url, "%s", abs);
        /* a page framing its own URL: once is let be, twice over is blocked (Chromium's
           NavigationRequest::IsSelfReferentialURL), the frame left about:blank */
        char want[8400], have[8400], du[8400];
        fr_unfragment(abs, want, sizeof want);
        uint32_t same = 0;
        for (uint32_t a = pk;; a = fs->tab.f[a].parent) {
            fr_doc_url(fs, tnt, page, a, du, sizeof du);
            fr_unfragment(du, have, sizeof have);
            if (strcmp(have, want) == 0) same++;
            if (!a) break;
        }
        /* loading=lazy, an http: or https: URL, its iframe beyond 4000px of its parent's viewport
           (Chromium's distance for a frame, measured): not loaded at rest, about:blank. A file:
           URL and srcdoc are never lazy (measured) */
        uint32_t la = fr_attr(pd, x, "loading");
        int lazy = 0;
        if (la && same < 2 && (strncmp(abs, "http:", 5) == 0 || strncmp(abs, "https:", 6) == 0)) {
            vector<char> lv;
            fr_attr_text(pd, la, &lv, 1);
            double reach = (pk ? fs->hs[pk - 1].view_h : fs->tab.vm[0]->host->view_h) + 4000;
            lazy = strcasecmp(data(lv), "lazy") == 0 && by - plt_border_top(plt, x) > reach;
        }
        snprintf(fi->want, sizeof fi->want, "%s", abs);
        if (same >= 2) fi->outcome = NET_FRAME_OUTCOME_BLOCKED;
        else if (lazy) fi->outcome = NET_FRAME_OUTCOME_LAZY;
        else {
            net_resource_h rh = net_fetch(tnt, abs, strlen(abs), NET_KIND_DOCUMENT);
            uint32_t r = rh.index;
            fi->record = r;
            tnt->resource[r].from = page;
            tnt->resource[r].elem = node_handle(pd, x);
            net_outcome o = tnt->resource[r].outcome;
            /* an HTTP error's body is the document it shows, as any page's */
            if (o == NET_OUTCOME_OK || (o == NET_OUTCOME_HTTP_ERROR && tnt->resource[r].body.len)) {
                append_range(body, bytes_ptr(tnt, tnt->resource[r].body), tnt->resource[r].body.len);
                amb_span fu = tnt->resource[r].final_url, ty = tnt->resource[r].type, cs = tnt->resource[r].charset;
                snprintf(final, sizeof final, "%.*s", (int)fu.len, text_ptr(tnt, fu));
                snprintf(url, sizeof url, "%s", final);
                if (cs.len) snprintf(type, sizeof type, "%.*s;charset=%.*s", (int)ty.len, text_ptr(tnt, ty), (int)cs.len, text_ptr(tnt, cs));
                else snprintf(type, sizeof type, "%.*s", (int)ty.len, text_ptr(tnt, ty));
                is_file = strncmp(final, "file:", 5) == 0;
            } else fi->outcome = NET_FRAME_OUTCOME_FAILED;
            http_status = o == NET_OUTCOME_HTTP_ERROR ? tnt->resource[r].status : 0;
        }
    }
    /* its document */
    int blank = fi->source == NET_FRAME_SOURCE_BLANK || fi->outcome == NET_FRAME_OUTCOME_BLOCKED || fi->outcome == NET_FRAME_OUTCOME_LAZY;
    if (blank) {
        snprintf(url, sizeof url, "about:blank");
        fr_blank(&fs->d[i]);
    } else if (fi->outcome != NET_FRAME_OUTCOME_FAILED) {
        html_set_scripting(scripts);
        html_set_srcdoc(srcdoc);
        html_parse_bytes(&fs->p[i], &fs->d[i], data(body), len(body), type, strlen(type), is_file);
        html_set_srcdoc(0);
        html_set_scripting(1);
        if (fs->p[i].encoding_len > 1 && scripts) {
            amb_span en = fs->p[i].encoding[fs->p[i].encoding_len - 1].name;
            script_charset(&fs->vm[i], bytes_ptr(&fs->p[i], en), en.len);
        }
    }
    fi->quirks = fs->d[i].node_len > 1 && fs->d[i].node[1].mode == DOM_DOCUMENT_MODE_QUIRKS;
    /* its page record in its own loader: its URL, its base (for about:srcdoc and about:blank
       its parent's), its origin */
    net_resource_h ph = resource_new(&fs->nt[i]);
    uint32_t pr = ph.index;
    net_loader *fn = &fs->nt[i];
    fn->resource[pr].url = text_add(fn, url, strlen(url));
    fn->resource[pr].final_url = text_add(fn, final, strlen(final));
    fn->resource[pr].kind = NET_KIND_DOCUMENT;
    fn->resource[pr].outcome = fi->outcome == NET_FRAME_OUTCOME_FAILED ? NET_OUTCOME_NO_SUCH_FILE : NET_OUTCOME_OK;
    fn->resource[pr].status = 200;
    fn->resource[pr].type = text_add(fn, "text/html", 9);
    fs->hs[i].page = pr;
    if (srcdoc || blank) append_range(fs->hs[i].docurl, url, strlen(url));
    /* its origin: about:srcdoc's and about:blank's its parent's, a URL's its own (a file:'s
       opaque, of its own), a sandbox's without allow-same-origin opaque; an error page's
       none's */
    char org[512];
    if (fi->outcome == NET_FRAME_OUTCOME_FAILED) {
        snprintf(f->origin_text, sizeof f->origin_text, "null");
        f->origin = 0;
    } else if ((f->sandbox & SH_SANDBOX) && !(f->sandbox & SH_SANDBOX_SAME_ORIGIN)) {
        snprintf(f->origin_text, sizeof f->origin_text, "null");
        f->origin = fr_origin_key(fs, "null");
    } else if (srcdoc || blank) {
        snprintf(f->origin_text, sizeof f->origin_text, "%s", fs->tab.f[pk].origin_text);
        f->origin = fs->tab.f[pk].origin;
    } else {
        if (!net_url_origin(final, org, sizeof org)) snprintf(org, sizeof org, "null");
        snprintf(f->origin_text, sizeof f->origin_text, "%s", org);
        f->origin = fr_origin_key(fs, org);
    }
    fn->resource[pr].origin = text_add(fn, f->origin_text, strlen(f->origin_text));
    f->state = fi->outcome == NET_FRAME_OUTCOME_FAILED ? SH_FRAME_FAILED : blank ? SH_FRAME_BLANK : SH_FRAME_LOADED;
    /* named on its parent's window */
    script_frame_made(fs->tab.vm[pk], k);
    /* a document an HTTP error brought: Chromium's console line for it */
    if (http_status) {
        char why[128], m[300];
        script_reason(http_status, why, sizeof why);
        snprintf(m, sizeof m, "Failed to load resource: the server responded with a status of %u (%s)", http_status, why);
        script_console_note(&fs->vm[i], 3, m);
    }
    /* its parts, then its parse's scripts (or the sandbox's refusals) */
    if (f->state != SH_FRAME_FAILED) net_fetch_subresources(fn, &fs->d[i], ph, denied);
    fr_env(fs, k, W, H);
    css_set_scripting(1);
    if (scripts && f->state == SH_FRAME_LOADED) script_run_parse(&fs->vm[i]);
    else if (allowed && f->state == SH_FRAME_LOADED) {
        uint32_t ns = fr_scripts_in(&fs->d[i]);
        char m[700];
        snprintf(m, sizeof m, "Blocked script execution in '%s' because the document's frame is sandboxed and the 'allow-scripts' permission is not set.", url);
        for (uint32_t q = 0; q < ns; q++) script_console_note(&fs->vm[i], 3, m);
    }
    /* styled and laid out in its viewport (its scroll bars found), in stores of this call's:
       its frame's own are made once, when the page is at rest (amb_frames_layout); then its
       own frames made in its iframes' boxes there */
    fi->loaded = 1;
    {
        css_style st;
        css_init(&st, id);
        fr_env(fs, k, W, H);
        css_cascade(&st, &fs->d[i], fn);
        fr_fit(fs, k, &st, W, H);
        layout_tree lt;
        layout_init(&lt, id);
        layout_run(&lt, &fs->d[i], &st, fn, fi->vw, fi->vh);
        css_set_scripting(1);
        fr_children(fs, FR_CXA, k, &lt);
    }
    /* load at its window, then at its iframe (an error page fires none; nor about:blank's,
       whose load Chromium fires as the iframe is inserted, before any script of its parent's
       can listen) */
    fr_env(fs, k, W, H);
    css_set_scripting(1);
    if (f->state == SH_FRAME_LOADED) {
        script_run_load(&fs->vm[i], -1);
        script_frame_loaded(fs->tab.vm[pk], x);
    }
}

/* the iframes of frame pk's document (0 the page's), in tree order, made frames */
static void fr_children(amb_frames *fs, FR_CX, uint32_t pk, const layout_tree *plt) {
    vector<uint32_t> xs;
    if (pk) {
        dom_document *pd = &fs->d[pk - 1];
        if (pd->node_len > 1)
            for (uint32_t x = pd->node[1].first_child; x; x = fr_next(pd, x)) if (fr_is_iframe(pd, x)) push_back(xs, x);
        for (size_t q = 0; q < len(xs); q++) {
            if (fs->tab.n >= SCRIPT_FRAMES) return;
            uint32_t k = ++fs->tab.n;
            fr_load(fs, FR_CXA, k, pk, &fs->d[pk - 1], plt, xs[q]);
        }
        return;
    }
    dom_document *pd = td;
    if (pd->node_len > 1)
        for (uint32_t x = pd->node[1].first_child; x; x = fr_next(pd, x)) if (fr_is_iframe(pd, x)) push_back(xs, x);
    for (size_t q = 0; q < len(xs); q++) {
        if (fs->tab.n >= SCRIPT_FRAMES) return;
        uint32_t k = ++fs->tab.n;
        fr_load(fs, FR_CXA, k, 0, td, plt, xs[q]);
    }
}

void amb_frames_load(amb_frames *fs, dom_document *d, layout_tree *lt, net_loader *nt, uint32_t page, double font_size, int dark, int reduced,
                     uint32_t denied, int scripts) {
    fs->tab.font_size = font_size;
    fs->tab.dark = dark;
    fs->tab.reduced = reduced;
    /* the page's own entry: frame 0, its origin's */
    char url[8400], org[512];
    fr_doc_url(fs, nt, page, 0, url, sizeof url);
    if (!net_url_origin(url, org, sizeof org)) snprintf(org, sizeof org, "null");
    snprintf(fs->tab.f[0].origin_text, sizeof fs->tab.f[0].origin_text, "%s", org);
    fs->tab.f[0].origin = 1;
    fs->tab.f[0].state = SH_FRAME_LOADED;
    fr_children(fs, d, nt, page, denied, scripts, 0, lt);
}

/* ---- laying out again ---- */

void amb_frames_layout(amb_frames *fs, layout_tree *lt) {
    for (uint32_t k = 1; k <= fs->tab.n; k++) {
        uint32_t pk = fs->tab.f[k].parent;
        const layout_tree *plt = pk ? &fs->lt[pk - 1] : lt;
        double bx = 0, by = 0, bw = 0, bh = 0;
        amb_frame_info *fi = &fs->info[k - 1];
        if (!fi->loaded) continue;
        uint32_t x = fs->tab.f[k].iframe;
        int has = fr_box(plt, x, &bx, &by, &bw, &bh);
        fi->x = has ? floor(bx + 0.5) : 0;
        fi->y = has ? floor(by + 0.5) : 0;
        double W = floor(bw + 0.5), H = floor(bh + 0.5);
        fs->hs[k - 1].view_w = W;
        fs->hs[k - 1].view_h = H;
        /* its own stores, made once: its cascade, its bars found, its layout */
        if (fi->laid) continue;
        fr_env(fs, k, W, H);
        css_cascade(&fs->st[k - 1], &fs->d[k - 1], &fs->nt[k - 1]);
        fr_fit(fs, k, &fs->st[k - 1], W, H);
        layout_run(&fs->lt[k - 1], &fs->d[k - 1], &fs->st[k - 1], &fs->nt[k - 1], fi->vw, fi->vh);
        css_set_scripting(1);
        fi->scheme_dark = (uint32_t)fr_root_dark(&fs->lt[k - 1]);
        fi->laid = 1;
    }
}

/* ---- drawing ---- */

/* a pixel of c set to rgb blended with coverage a (0 .. 1) */
static void fr_px(amb_canvas *c, int x, int y, uint32_t rgb, double a) {
    if (x < 0 || y < 0 || (uint32_t)x >= c->w || (uint32_t)y >= c->h || a <= 0) return;
    if (a > 1) a = 1;
    size_t q = ((size_t)y * c->w + (size_t)x) * 3;
    for (int ch = 0; ch < 3; ch++) {
        double v = (double)((rgb >> (16 - 8 * ch)) & 0xff);
        double was = c->px[q + (size_t)ch];
        c->px[q + (size_t)ch] = (uint8_t)floor(was + (v - was) * a + 0.5);
    }
}
static void fr_rect(amb_canvas *c, int x0, int y0, int x1, int y1, uint32_t rgb) {
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) fr_px(c, x, y, rgb, 1);
}
/* a rounded rectangle (x0, y0)-(x1, y1), its corners' radius r, filled with rgb, each
   pixel by the share of it the shape covers (8 by 8 samples) */
static void fr_round(amb_canvas *c, double x0, double y0, double x1, double y1, double r, uint32_t rgb) {
    for (int y = (int)floor(y0); y < (int)ceil(y1); y++)
        for (int x = (int)floor(x0); x < (int)ceil(x1); x++) {
            int in = 0;
            for (int sy = 0; sy < 8; sy++)
                for (int sx = 0; sx < 8; sx++) {
                    double px = x + (sx + 0.5) / 8, py = y + (sy + 0.5) / 8;
                    if (px < x0 || px > x1 || py < y0 || py > y1) continue;
                    double cx = px < x0 + r ? x0 + r : px > x1 - r ? x1 - r : px;
                    double cy = py < y0 + r ? y0 + r : py > y1 - r ? y1 - r : py;
                    if ((px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r) in++;
                }
            fr_px(c, x, y, rgb, in / 64.0);
        }
}
/* an arrow of the scroll bars, in the 15px button at (bx, by): dir 0 up, 1 down, 2 left, 3
   right, as Chromium draws them (measured) */
static void fr_arrow(amb_canvas *c, int bx, int by, int dir, uint32_t rgb) {
    for (int i = 0; i < 5; i++) {
        /* the row (or column) i from the apex: 2i + 1 px */
        if (dir == 0) fr_rect(c, bx + 7 - i, by + 6 + i, bx + 8 + i, by + 7 + i, rgb);
        if (dir == 1) fr_rect(c, bx + 7 - i, by + 7 - i, bx + 8 + i, by + 8 - i, rgb);
        if (dir == 2) fr_rect(c, bx + 5 + i, by + 7 - i, bx + 6 + i, by + 8 + i, rgb);
        if (dir == 3) fr_rect(c, bx + 8 - i, by + 7 - i, bx + 9 - i, by + 8 + i, rgb);
    }
}
/* frame k's scroll bars drawn on c (W x H): the track, its buttons and their arrows, the
   thumb at the start; the corner where both are */
static void fr_bars(amb_canvas *c, const amb_frame_info *fi) {
    int W = (int)fi->w, H = (int)fi->h;
    uint32_t track = fi->scheme_dark ? 0x2c2c2cu : 0xfcfcfcu, ink = fi->scheme_dark ? 0x9f9f9fu : 0x8b8b8bu;
    if (fi->vbar) {
        int x0 = W - FR_BAR, len_ = H - (fi->hbar ? FR_BAR : 0);
        fr_rect(c, x0, 0, W, len_, track);
        if (len_ >= 2 * FR_BAR) {
            fr_arrow(c, x0, 0, 0, ink);
            fr_arrow(c, x0, len_ - FR_BAR, 1, ink);
            double tl = len_ - 2 * FR_BAR, room = tl - 6, vis = fi->vh, tot = fi->sh > 0 ? fi->sh : 1;
            double th = floor(room * vis / tot + 0.5);
            if (th < 17) th = 17;
            if (th <= room) fr_round(c, x0 + 3, FR_BAR + 3, x0 + 12, FR_BAR + 3 + th, 4.5, ink);
        }
    }
    if (fi->hbar) {
        int y0 = H - FR_BAR, len_ = W - (fi->vbar ? FR_BAR : 0);
        fr_rect(c, 0, y0, len_, H, track);
        if (len_ >= 2 * FR_BAR) {
            fr_arrow(c, 0, y0, 2, ink);
            fr_arrow(c, len_ - FR_BAR, y0, 3, ink);
            double tl = len_ - 2 * FR_BAR, room = tl - 6, vis = fi->vw, tot = fi->sw > 0 ? fi->sw : 1;
            double tw = floor(room * vis / tot + 0.5);
            if (tw < 17) tw = 17;
            if (tw <= room) fr_round(c, FR_BAR + 3, y0 + 3, FR_BAR + 3 + tw, y0 + 12, 4.5, ink);
        }
    }
    if (fi->vbar && fi->hbar) fr_rect(c, W - FR_BAR, H - FR_BAR, W, H, track);
}

/* whether a frame's canvas is all its root's (or its body's) background, an opaque one, as
   paint fills it: then nothing of the page shows through it */
static int fr_opaque_root(const layout_tree *t) {
    uint32_t vp = 0;
    for (uint32_t b = 1; b < t->box_len && !vp; b++) if (t->box[b].live && t->box[b].kind == LAYOUT_BOX_KIND_VIEWPORT) vp = b;
    uint32_t root = vp ? t->box[vp].first_child : 0;
    if (!root) return 0;
    if (t->box[root].background & 0xff) return (t->box[root].background & 0xff) == 0xff;
    for (uint32_t ch = t->box[root].first_child; ch; ch = t->box[ch].next)
        if (t->box[ch].kind == LAYOUT_BOX_KIND_BLOCK && (t->box[ch].background & 0xff)) return (t->box[ch].background & 0xff) == 0xff;
    return 0;
}

/* frame k's drawing over the canvas colour under (0xRRGGBBff), at the zoom z: its content
   (its viewport less its bars) then its bars, onto out (W x H) */
static void fr_draw(amb_frames *fs, uint32_t k, uint32_t under, int transparent, amb_canvas *out) {
    uint32_t i = k - 1;
    amb_frame_info *fi = &fs->info[i];
    layout_tree *lt = &fs->lt[i];
    uint32_t vp = 0;
    for (uint32_t b = 1; b < lt->box_len && !vp; b++) if (lt->box[b].live && lt->box[b].kind == LAYOUT_BOX_KIND_VIEWPORT) vp = b;
    uint32_t was = vp ? lt->box[vp].background : 0;
    if (vp && transparent) lt->box[vp].background = under;
    amb_canvas c = {0};
    defer(c) { AMB_CANVAS_DROP(c); }
    if (fs->tab.f[k].state != SH_FRAME_FAILED && fi->vh >= 1 && fi->vw >= 1) amb_paint_view(lt, &fs->nt[i], &c, 0, fi->vh);
    if (vp) lt->box[vp].background = was;
    uint32_t W = (uint32_t)fi->w, H = (uint32_t)fi->h;
    out->w = W;
    out->h = H;
    out->oy = 0;
    out->cx0 = 0;
    out->cy0 = 0;
    out->cx1 = W;
    out->cy1 = H;
    out->zoom = 1;
    clear(out->px);
    resize(&out->px, (size_t)W * H * 3);
    for (size_t q = 0; q < (size_t)W * H; q++) {
        out->px[q * 3] = (uint8_t)(under >> 24);
        out->px[q * 3 + 1] = (uint8_t)(under >> 16);
        out->px[q * 3 + 2] = (uint8_t)(under >> 8);
    }
    for (uint32_t y = 0; y < H && y < c.h; y++)
        for (uint32_t x = 0; x < W && x < c.w && x < (uint32_t)fi->vw; x++) {
            if (y >= (uint32_t)fi->vh) break;
            size_t a = ((size_t)y * c.w + x) * 3, b = ((size_t)y * W + x) * 3;
            out->px[b] = c.px[a];
            out->px[b + 1] = c.px[a + 1];
            out->px[b + 2] = c.px[a + 2];
        }
    fr_bars(out, fi);
}

void amb_frames_paint(amb_frames *fs, const css_style *st) {
    amb_paint_frames_forget();
    /* the deepest first: a frame's drawing holds its own frames' */
    for (uint32_t k = fs->tab.n; k >= 1; k--) {
        amb_frame_info *fi = &fs->info[k - 1];
        if (!fi->laid || fi->w < 1 || fi->h < 1) continue;
        uint32_t pk = fs->tab.f[k].parent;
        uint32_t pdoc = pk + 1;
        /* its canvas transparent unless its scheme is not its iframe's */
        int iframe_dark = fr_scheme_dark(pk ? &fs->st[pk - 1] : st, fs->tab.f[k].iframe);
        int transparent = fs->tab.f[k].state == SH_FRAME_FAILED || iframe_dark == (int)fi->scheme_dark;
        amb_canvas b = {0};
        defer(b) { AMB_CANVAS_DROP(b); }
        amb_canvas w = {0};
        defer(w) { AMB_CANVAS_DROP(w); }
        /* a root (or body) of an opaque background covers the canvas: one drawing is both */
        if (transparent && !fr_opaque_root(&fs->lt[k - 1])) {
            fr_draw(fs, k, 0x000000ffu, 1, &b);
            fr_draw(fs, k, 0xffffffffu, 1, &w);
            amb_paint_frame_put(pdoc, fs->tab.f[k].iframe, &b, &w);
        } else {
            uint32_t canvas = fi->scheme_dark ? 0x121212ffu : 0xffffffffu;
            fr_draw(fs, k, canvas, !transparent ? 0 : 1, &b);
            amb_paint_frame_put(pdoc, fs->tab.f[k].iframe, &b, &b);
        }
    }
}

/* ---- the facts ---- */

void amb_frames_record(amb_frames *fs, net_loader *nt) {
    for (uint32_t k = 1; k <= fs->tab.n; k++) {
        const amb_frame_info *fi = &fs->info[k - 1];
        const sh_frame *f = &fs->tab.f[k];
        net_frame_h h = frame_new(nt);
        uint32_t r = h.index;
        dom_node_h ih = { f->parent + 1, f->iframe, f->iframe_gen };
        nt->frame[r].iframe = ih;
        nt->frame[r].doc = k + 1;
        nt->frame[r].parent = f->parent + 1;
        char url[8400];
        fr_doc_url(fs, nt, 0, k, url, sizeof url);
        if (f->state == SH_FRAME_FAILED && fi->record && fi->record < nt->resource_len) {
            amb_span u = nt->resource[fi->record].url;
            snprintf(url, sizeof url, "%.*s", (int)u.len, text_ptr(nt, u));
        }
        /* a frame not loaded at rest (blocked, lazy): the URL it was to load */
        if (fi->outcome == NET_FRAME_OUTCOME_BLOCKED || fi->outcome == NET_FRAME_OUTCOME_LAZY) snprintf(url, sizeof url, "%s", fi->want);
        nt->frame[r].url = text_add(nt, url, strlen(url));
        nt->frame[r].source = (net_frame_source)fi->source;
        nt->frame[r].outcome = (net_frame_outcome)fi->outcome;
        nt->frame[r].resource = fi->record;
        nt->frame[r].width = fi->vw;
        nt->frame[r].height = fi->vh;
        nt->frame[r].vbar = fi->vbar != 0;
        nt->frame[r].hbar = fi->hbar != 0;
        nt->frame[r].sandboxed = (f->sandbox & SH_SANDBOX) != 0;
        nt->frame[r].scripts = !(f->sandbox & SH_SANDBOX) || (f->sandbox & SH_SANDBOX_SCRIPTS);
        nt->frame[r].same_origin = f->origin && f->origin == fs->tab.f[f->parent].origin;
        nt->frame[r].quirks = fi->quirks != 0;
    }
}

/* ---- each frame's boxes, for the gate ---- */

/* the path of frame k: its iframes' ids from the page's down, joined by / */
static void fr_path(const amb_frames *fs, const dom_document *top, uint32_t k, vector<char> *out) {
    if (!k) return;
    uint32_t pk = fs->tab.f[k].parent;
    fr_path(fs, top, pk, out);
    if (len(*out)) push_back(*out, '/');
    const dom_document *pd = pk ? &fs->d[pk - 1] : top;
    uint32_t x = fs->tab.f[k].iframe, a = x < pd->node_len ? fr_attr(pd, x, "id") : 0;
    if (a) append_range(*out, text_ptr(pd, pd->attr[a].value), pd->attr[a].value.len);
    else push_back(*out, '?');
}

/* whether the node n has a box of its own in t (a block, an atomic inline ...: not an inline
   element's decorations, not a pseudo-element's) */
static int fr_has_box(const layout_tree *t, uint32_t n) {
    for (uint32_t b = 1; b < t->box_len; b++)
        if (t->box[b].live && t->box[b].node.index == n && !t->box[b].pseudo && t->box[b].kind != LAYOUT_BOX_KIND_INLINE &&
            t->box[b].kind != LAYOUT_BOX_KIND_VIEWPORT) return 1;
    return 0;
}
/* whether fragment f is part of the inline element x's box: its own (not an atomic), its
   text's, an atomic child's, or a boxless child element's in turn */
static int fr_inline_part(const dom_document *d, const layout_tree *t, uint32_t f, uint32_t x) {
    uint32_t n = t->frag[f].node.index;
    if (!n || n >= d->node_len) return 0;
    if (n == x) return t->frag[f].kind != LAYOUT_FRAG_KIND_ATOMIC;
    uint32_t c = d->node[n].parent;
    if (t->frag[f].kind != LAYOUT_FRAG_KIND_TEXT && t->frag[f].kind != LAYOUT_FRAG_KIND_ATOMIC) c = n;
    while (c && c != x) {
        if (d->node[c].kind != DOM_NODE_KIND_ELEMENT || fr_has_box(t, c)) return 0;
        c = d->node[c].parent;
    }
    return c == x;
}

void amb_frames_print_boxes(const amb_frames *fs, const dom_document *top, FILE *out) {
    for (uint32_t k = 1; k <= fs->tab.n; k++) {
        const amb_frame_info *fi = &fs->info[k - 1];
        if (!fi->laid) continue;
        vector<char> path;
        fr_path(fs, top, k, &path);
        const dom_document *d = &fs->d[k - 1];
        const layout_tree *t = &fs->lt[k - 1];
        for (uint32_t x = d->node_len > 1 ? d->node[1].first_child : 0; x; x = fr_next(d, x)) {
            if (d->node[x].kind != DOM_NODE_KIND_ELEMENT) continue;
            uint32_t a = fr_attr(d, x, "id");
            if (!a) continue;
            double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            int got = 0;
            for (uint32_t b = 1; b < t->box_len && !got; b++) {
                if (!t->box[b].live || t->box[b].node.index != x || t->box[b].pseudo || t->box[b].kind == LAYOUT_BOX_KIND_INLINE) continue;
                x0 = t->box[b].x - t->box[b].pl - t->box[b].bl;
                y0 = t->box[b].y - t->box[b].pt - t->box[b].bt;
                x1 = t->box[b].x + t->box[b].w + t->box[b].pr + t->box[b].br;
                y1 = t->box[b].y + t->box[b].h + t->box[b].pb + t->box[b].bb;
                got = 1;
            }
            /* an inline element: its fragments' union, as layout_inline_box/5 has it (its own
               fragments, its text's, its atomic children's, and its boxless children's in turn) */
            for (uint32_t f = 1; f < t->frag_len && !got; f++) {
                if (!t->frag[f].live || !fr_inline_part(d, t, f, x)) continue;
                double fx0 = t->frag[f].x, fy0 = t->frag[f].y, fx1 = fx0 + t->frag[f].w, fy1 = fy0 + t->frag[f].h;
                for (uint32_t g = f + 1; g < t->frag_len; g++) {
                    if (!t->frag[g].live || !fr_inline_part(d, t, g, x)) continue;
                    if (t->frag[g].x < fx0) fx0 = t->frag[g].x;
                    if (t->frag[g].y < fy0) fy0 = t->frag[g].y;
                    if (t->frag[g].x + t->frag[g].w > fx1) fx1 = t->frag[g].x + t->frag[g].w;
                    if (t->frag[g].y + t->frag[g].h > fy1) fy1 = t->frag[g].y + t->frag[g].h;
                }
                x0 = fx0;
                y0 = fy0;
                x1 = fx1;
                y1 = fy1;
                got = 1;
            }
            if (!got) continue;
            fprintf(out, "%.*s %.*s %s %g %g %g %g\n", (int)len(path), data(path), (int)d->attr[a].value.len, text_ptr(d, d->attr[a].value),
                    names_ptr(d, d->node[x].name), x0, y0, x1 - x0, y1 - y0);
        }
    }
}
