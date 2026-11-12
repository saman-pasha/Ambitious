#!/bin/sh
# tests/integ/render.sh -- pages laid out and drawn: each fixture page rendered in an
# 800x600 viewport to a PNG -- its size, what it holds, its pixels' checksum, which
# tests/golden/render/NAME.png shows (make golden writes them) -- and then the
# reasoner over the layout's facts: why boxes are as wide as they are, what is at a
# point, and what overflows its container.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-render.XXXXXX")
export AMB_WORK=$work
amb() { sh "$AMB_ROOT/bin/ambitious" "$@"; }
for p in news style layout values atrules selectors floats position sizing flex grid text; do
  printf '%s: ' "$p"
  amb render "$AMB_ROOT/tests/pages/$p.html" "$work/$p.png"
done
page=$AMB_ROOT/tests/pages/layout.html
echo "-- why the boxes are as wide as they are:"
amb query "$page" "member(E, [div, h2, table, span]), dom_element(N, E), layout_box_of(N, B), layout_explain_width(B, Why)"
echo "-- what is at (150, 240), and at (400, 120):"
amb query "$page" "member(X-Y, [150-240, 400-120]), layout_at(X, Y, N), dom_element(N, E)"
echo "-- the lines the justified paragraph's text is on:"
amb query "$page" "layout_text(_, L, _, T, X, _, W, _), sub_atom(T, 0, _, _, 'This'), layout_line(L, _, _, Y, _, H)"
echo "-- the text the pseudo-elements gave (counters, quotes, attr()), and a ::before's own box:"
sel=$AMB_ROOT/tests/pages/selectors.html
amb query "$sel" "layout_generated(_, N, T), dom_element(N, E)"
amb query "$sel" "dom_attr(N, id, bg), layout_pseudo_box_of(N, P, B), layout_border_box(B, X, Y, W, H)"
echo "-- where boxes are and why: floated, offset, in a containing block, fixed; their paint order's keys:"
pos=$AMB_ROOT/tests/pages/position.html
amb query "$pos" "member(I, [r1, a1, a2, a3, a4, s1, fx]), dom_attr(N, id, I), layout_box_of(N, B), layout_explain_position(B, Why)"
amb query "$pos" "dom_attr(N, id, a1), layout_box_of(N, B), layout_paint_order(B, K)"
amb query "$AMB_ROOT/tests/pages/floats.html" "member(I, [f1, f2, i1]), dom_attr(N, id, I), layout_box_of(N, B), layout_explain_position(B, Why)"
echo "-- the lines of a hyphenated paragraph, and right-to-left text in its order:"
txt=$AMB_ROOT/tests/pages/text.html
amb query "$txt" "dom_attr(P, id, a), dom_parent(T, P), layout_text(_, _, T, Text, X, Y, _, _)"
amb query "$txt" "dom_attr(N, id, r), layout_box_of(N, B), layout_line(L, B, _, _, _, _), layout_text(_, L, _, Text, X, _, _, _)"
echo "-- how flex and grid items got their widths:"
amb query "$AMB_ROOT/tests/pages/flex.html" "member(I, [g2, s1]), dom_attr(N, id, I), layout_box_of(N, B), layout_explain_width(B, Why)"
amb query "$AMB_ROOT/tests/pages/grid.html" "member(I, [a2, b1]), dom_attr(N, id, I), layout_box_of(N, B), layout_explain_width(B, Why)"
echo "-- the reasoner's diagnostics:"
amb advise "$page"
st=$?
rm -rf "$work"
echo "exit $st"
