/* src/shell/frame.h -- a page's frames (Phase 12u): each iframe's document, its stores and
   its scripts' VM, beside the page's own. Include it after amb_interface of dom, html, net,
   css, layout, a11y and script, <vector.pl>, src/js/js.h, src/script/script.h and
   src/paint/paint.h.

   A frame is a document of its own, made and run as a page is (src/shell/frame.c): its
   markup from its iframe's srcdoc (about:srcdoc, the parent's base URL, UTF-8, never in
   quirks mode by its DOCTYPE), else its src fetched through the page's loader (a document
   of its own encoding and mode), else about:blank; parsed, its parts fetched by a loader of
   its own, its scripts run in a VM of its own (its realm and window, unless its sandbox
   keeps them off), its cascade and layout run in its iframe's content box as its viewport
   (its own initial containing block, media queries and viewport units), less the classic
   scroll bars its overflowing content makes; and drawn into that content box, its canvas
   transparent unless its colour scheme is not its iframe's. Its own iframes are frames too,
   at any depth, up to SCRIPT_FRAMES a page.

   THE STORES. A store holds no pointer and a pointer is only kept where its target is
   declared, so a page's frames are one struct of arrays the page command declares beside
   its own stores (FRAME_STORES): each frame's host is pointed at its stores and every
   document's VM is entered in the frames' table there. A frame's number k (1 ..) is its
   place in the table; its stores are the arrays' (k - 1)th. */
#ifndef AMB_SHELL_FRAME_H
#define AMB_SHELL_FRAME_H

/* what the shell keeps of a frame beside the script module's table: where its document
   came from (net's frame_source), what became of it (frame_outcome), its record in the
   page's loader, its viewport and its place in its parent's page (its iframe's content box,
   whole px), the scroll bars drawn in it, whether its document is in quirks mode, its
   scroll extent, whether it is in the dark scheme (its root's), whether its iframe's
   scrolling keeps its bars away, whether it was made and laid out at rest; the URL its src
   names (a blocked or lazy frame's, which it shows not) */
typedef struct amb_frame_info {
    uint32_t source, outcome, record, vbar, hbar, quirks, scheme_dark, sbno;
    uint32_t loaded, laid;
    double x, y, w, h, vw, vh, sw, sh;
    char want[2048];
} amb_frame_info;

typedef struct amb_frames {
    dom_document d[SCRIPT_FRAMES];
    html_parser p[SCRIPT_FRAMES];
    css_style st[SCRIPT_FRAMES];
    layout_tree lt[SCRIPT_FRAMES];
    net_loader nt[SCRIPT_FRAMES];
    a11y_tree ax[SCRIPT_FRAMES];
    script_runtime sr[SCRIPT_FRAMES];
    js_vm vm[SCRIPT_FRAMES];
    struct js_host hs[SCRIPT_FRAMES];
    amb_frame_info info[SCRIPT_FRAMES];
    sh_frames tab;
} amb_frames;

/* a page's frames, declared beside its stores (PAGE_STORES: d, nt, sr, vm, hs): each
   frame's host pointed at its stores and its VM at its host, the table holding every
   document's VM (the page's first), every host given the table. The frames' stores are
   dropped at the scope's end, before the page's */
#define FRAME_STORES(fs, vm, hs) amb_frames fs; memset(&(fs), 0, sizeof(fs)); \
    for (uint32_t fk_ = 0; fk_ < SCRIPT_FRAMES; fk_++) { \
        (fs).hs[fk_].d = &(fs).d[fk_]; (fs).hs[fk_].nt = &(fs).nt[fk_]; (fs).hs[fk_].rt = &(fs).sr[fk_]; \
        (fs).hs[fk_].tree_ver = 1; (fs).hs[fk_].attr_ver = 1; (fs).hs[fk_].view_w = 300; (fs).hs[fk_].view_h = 150; \
        (fs).vm[fk_].host = &(fs).hs[fk_]; (fs).tab.vm[fk_ + 1] = &(fs).vm[fk_]; (fs).hs[fk_].fr = &(fs).tab; (fs).hs[fk_].frame = fk_ + 1; \
    } \
    (fs).tab.vm[0] = &(vm); \
    defer(fs) { for (uint32_t fk_ = 0; fk_ < SCRIPT_FRAMES; fk_++) { store_dom_store_tag_drop(&(fs).d[fk_]); store_html_store_tag_drop(&(fs).p[fk_]); \
        store_css_store_tag_drop(&(fs).st[fk_]); store_layout_store_tag_drop(&(fs).lt[fk_]); store_net_store_tag_drop(&(fs).nt[fk_]); \
        store_a11y_store_tag_drop(&(fs).ax[fk_]); store_script_store_tag_drop(&(fs).sr[fk_]); JS_VM_DROP((fs).vm[fk_]); \
        SCRIPT_HOST_DROP((fs).hs[fk_]); } amb_paint_frames_forget(); }

/* the page's iframes made frames, loaded and run, as far as the page's load: called once the
   page's own parse's scripts have run (script_run_parse) and it is styled and laid out (its
   iframes' boxes known). Each frame of the page, in tree order and each one's own inside it,
   is made, its document parsed and its parts fetched, its parse's scripts run, styled and
   laid out in its iframe's content box, its own frames made, then load at its window and at
   its iframe. pr: the reader's preferences (their zoom left out: the page's frames are laid
   out in CSS px); denied: the kinds of part the profile denies the page's site; scripts:
   whether the page's scripts run at all. The page's VM is the table's first */
void amb_frames_load(amb_frames *fs, dom_document *d, layout_tree *lt, net_loader *nt, uint32_t page, double font_size, int dark, int reduced,
                     uint32_t denied, int scripts);
/* each frame styled and laid out again, in tree order, in its iframe's content box as the
   page's layout (lt) and each parent frame's have it now: after the page's timers and a
   user's actions, which may have changed either. Each frame's scroll bars found again */
void amb_frames_layout(amb_frames *fs, layout_tree *lt);
/* each frame drawn, the deepest first, into the paint registry, for the page's drawing to
   place in its iframes; st the page's cascade (its iframes' colour schemes) */
void amb_frames_paint(amb_frames *fs, const css_style *st);
/* the page's frames as the loader's facts (net's frame records), in the page's loader nt */
void amb_frames_record(amb_frames *fs, net_loader *nt);
/* whether the document d has an iframe (a page without one runs as it always has) */
int amb_frames_any(const dom_document *d);
/* each frame's elements with an id and their border boxes in its viewport, a line each:
   the frame's path (its iframes' ids from the page's down, joined by /), the id, the tag,
   x, y, width, height (`ambitious frames`, which tests/integ/frames.sh holds to Chromium's) */
void amb_frames_print_boxes(const amb_frames *fs, const dom_document *top, FILE *out);

#endif
