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
for p in news style layout values atrules; do
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
echo "-- the reasoner's diagnostics:"
amb advise "$page"
st=$?
rm -rf "$work"
echo "exit $st"
