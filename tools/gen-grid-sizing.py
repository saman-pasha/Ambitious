#!/usr/bin/env python3
"""tools/gen-grid-sizing.py tests/pages/grid-sizing.html -- the grid sizing fixture: each of twenty track lists with
items spanning one to three of its tracks (text, fixed widths, a min-width, overflow: hidden), as a float, an inline-grid,
width: min-content, width: max-content and a block 360px wide, then floats placed by the grid's other means; the page's
boxes are held to Chromium's (tools/bench/boxes.js writes tests/pages/grid-sizing.chromium)"""
import sys

TRACKS = [
    "auto auto",
    "auto auto auto",
    "min-content auto",
    "max-content auto",
    "min-content max-content auto",
    "fit-content(80px) auto",
    "fit-content(150px) fit-content(50px)",
    "1fr auto",
    "1fr 2fr",
    "auto 1fr 1fr",
    "minmax(50px, 1fr) auto",
    "minmax(min-content, 100px) auto",
    "minmax(auto, 60px) minmax(auto, 60px) auto",
    "minmax(max-content, 1fr) 1fr",
    "100px auto",
    "40px auto 40px",
    "repeat(3, minmax(0, 1fr))",
    "auto minmax(0, 200px)",
    "minmax(30px, max-content) fit-content(60px) 1fr",
    "min-content 1fr max-content",
]
TWO = ("auto auto", "min-content auto", "max-content auto", "fit-content(80px) auto", "fit-content(150px) fit-content(50px)", "1fr auto", "1fr 2fr", "minmax(50px, 1fr) auto", "minmax(min-content, 100px) auto", "minmax(max-content, 1fr) 1fr", "100px auto", "auto minmax(0, 200px)")
WORDS = ["alpha", "beta gamma", "delta epsilon zeta", "eta", "theta iota kappa lambda", "mu nu", "xi omicron pi rho sigma tau",
         "upsilon", "phi chi psi omega", "one two three"]
CONTAINERS = ["float", "inline", "minc", "maxc", "block"]


def items_for(case, ncols):
    """a list of (span, start or 0, text or None, style)"""
    out = []
    k = case
    if ncols == 2:
        pats = [[(2, 0), (1, 0), (1, 0)], [(1, 0), (1, 0), (2, 0)], [(1, 2), (1, 1), (2, 1)], [(2, 0), (2, 0), (1, 0), (1, 0)]]
    else:
        pats = [[(2, 0), (1, 0), (3, 0)], [(1, 0), (2, 0), (1, 0), (1, 0), (3, 0)], [(3, 0), (1, 1), (1, 2), (1, 3)],
                [(2, 2), (2, 1), (1, 0), (1, 0)], [(1, 0), (1, 0), (1, 0), (2, 0), (1, 0)]]
    pat = pats[case % len(pats)]
    for i, (span, start) in enumerate(pat):
        w = WORDS[(case * 3 + i * 7) % len(WORDS)]
        style = ""
        if (case + i) % 5 == 3:
            style = "width: 70px"
            w = "fixed"
        elif (case + i) % 7 == 5:
            style = "min-width: 40px"
        elif (case + i) % 11 == 9:
            style = "overflow: hidden"
        out.append((span, start, w, style))
    return out


def main():
    out = []
    out.append("<!DOCTYPE html>")
    out.append("<html><head><title>Grid track sizing</title>")
    out.append("<style>")
    out.append("/* tests/pages/grid-sizing.html (tools/gen-grid-sizing.py: each track list with items spanning one to three of its")
    out.append("   tracks, of text and of fixed widths, some with a min-width or overflow: hidden; each grid as a float, an")
    out.append("   inline-grid, width: min-content, width: max-content, and a block 360px wide) -- the grid container's own")
    out.append("   min- and max-content widths, its items' placements and spans, the fr tracks' shares and the track sizing")
    out.append("   algorithm as Chromium runs it; the boxes held to Chromium's (tests/pages/grid-sizing.chromium) */")
    out.append("body { font: 13px 'DejaVu Sans'; margin: 4px; }")
    out.append(".g { display: grid; background: #eef; margin: 0 0 3px; column-gap: 3px; }")
    out.append(".g > div { background: #cde; }")
    out.append(".float { float: left; clear: left; }")
    out.append(".inline { display: inline-grid; vertical-align: top; }")
    out.append(".minc { width: min-content; }")
    out.append(".maxc { width: max-content; }")
    out.append(".block { width: 360px; }")
    out.append(".case { height: 64px; display: flow-root; }")
    out.append("</style></head><body>")
    n = 0
    for ti, tracks in enumerate(TRACKS):
        ncols = 2 if tracks in TWO else 3
        for ci, cont in enumerate(CONTAINERS):
            case = ti * 5 + ci
            its = items_for(case, ncols)
            gid = "g%d" % n
            gap = "" if case % 3 else "; column-gap: 9px"
            cls = "g " + cont
            out.append('<div class="case"><div class="%s" id="%s" style="grid-template-columns: %s%s">' % (cls, gid, tracks, gap))
            for k, (span, start, text, style) in enumerate(its):
                st = []
                if start:
                    st.append("grid-column: %d / span %d" % (start, span))
                elif span > 1:
                    st.append("grid-column: span %d" % span)
                if style:
                    st.append(style)
                sa = (' style="%s"' % "; ".join(st)) if st else ""
                out.append('<div id="%s-%d"%s>%s</div>' % (gid, k, sa, text))
            out.append("</div></div>")
            n += 1
    # floats placed by the grid's other means: a percentage gap and a percentage track (the width indefinite), automatic
    # repeats, areas, a column flow, named lines, lines from the end and implicit columns
    extra = [
        ('grid-template-columns: auto auto; column-gap: 10%', ['alpha beta', 'gamma']),
        ('grid-template-columns: 30% auto', ['percent', 'auto track']),
        ('grid-template-columns: repeat(auto-fill, 60px)', ['a', 'b', 'c']),
        ('grid-template-columns: repeat(auto-fit, minmax(50px, 1fr))', ['a', 'bb']),
        ("grid-template-areas: 'x y' 'z z'", [('grid-area: x', 'x item'), ('grid-area: y', 'y'), ('grid-area: z', 'z spanning both')]),
        ('grid-auto-flow: column; grid-template-rows: auto auto', ['one', 'two', 'three']),
        ('grid-template-columns: [a] auto [b] auto [c]', [('grid-column: b / c', 'b to c'), ('grid-column: a', 'a')]),
        ('grid-template-columns: auto auto; grid-auto-columns: 40px', [('grid-column: -1', 'neg'), ('grid-column: 4', 'four')]),
    ]
    for k, (style, items) in enumerate(extra):
        gid = "x%d" % k
        out.append('<div class="case"><div class="g float" id="%s" style="%s">' % (gid, style))
        for i, it in enumerate(items):
            if isinstance(it, tuple):
                out.append('<div id="%s-%d" style="%s">%s</div>' % (gid, i, it[0], it[1]))
            else:
                out.append('<div id="%s-%d">%s</div>' % (gid, i, it))
        out.append("</div></div>")
    out.append("</body></html>")
    open(sys.argv[1], "w").write("\n".join(out) + "\n")


main()
