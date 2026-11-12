#!/bin/sh
# tests/integ/script-view.sh -- what a page's scripts read of its rendering, a cascade and
# a layout run when they ask: getComputedStyle's resolved values, getBoundingClientRect,
# getClientRects, offset* and client*. The geometry page's console must be Chromium's
# line for line (tests/pages/geometry.chromium, from tools/bench/console.js): sizes,
# margins, paddings and borders of blocks, inline blocks, cells and a list, colours and
# fonts, an element not rendered, a detached one, and every value read again after a
# style, a class, an insertion and a style sheet changed the page. Then the layouts the
# scripts forced, as facts, and the reasoner's word on the one that read between writes.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-view.XXXXXX")
export AMB_WORK=$work
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | sed "s|$AMB_ROOT|ROOT|g"; }
pages="$AMB_ROOT/tests/pages"
# a page's console as Chromium's is written: its messages, then its uncaught errors
console() { amb script "$1" | sed -n -e 's/^console [a-z]* *\[[0-9]*\] //p' > "$work/c.txt"; amb script "$1" | sed -n -e 's/^error *[a-z]* *\[[0-9]*\] /error: /p' >> "$work/c.txt"; }
for i in geometry thrash; do
  echo "-- the $i page's console, against Chromium's:"
  console "$pages/$i.html"
  if cmp -s "$pages/$i.chromium" "$work/c.txt"; then echo "the same $(wc -l < "$work/c.txt" | tr -d ' ') lines"
  else diff "$pages/$i.chromium" "$work/c.txt"; fi
done
echo "-- the cascades and layouts each script's reads forced:"
amb query "$pages/geometry.html" "script_forced_layout(R, N)"
amb query "$pages/thrash.html" "script_forced_layout(R, N)"
echo "-- what the reasoner says of them:"
amb advise "$pages/thrash.html" | grep '^amb_diag(layout_thrash'
amb advise "$pages/geometry.html" | grep -c '^amb_diag(layout_thrash'
rm -rf "$work"
