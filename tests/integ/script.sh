#!/bin/sh
# tests/integ/script.sh -- a page's scripts through the engine and its bindings.
# First the DOM interop pages' consoles, each line as Chromium writes it (their
# expected consoles: tests/pages/NAME.chromium, from tools/bench/console.js). Then
# the counter: what its script did, the button clicked twice and the page queried
# again (the clicks change the DOM through its own operations, so the reasoner sees
# them), and its diagnostics. Then the list a script builds, a timer trims and an
# interval keeps from settling. Paths are written ROOT.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-script.XXXXXX")
export AMB_WORK=$work
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | sed "s|$AMB_ROOT|ROOT|g"; }
pages="$AMB_ROOT/tests/pages"
# a page's console as Chromium's is written: its messages, then its uncaught errors
console() { amb script "$1" | sed -n -e 's/^console [a-z]* *\[[0-9]*\] //p' > "$work/c.txt"; amb script "$1" | sed -n -e 's/^error *[a-z]* *\[[0-9]*\] /error: /p' >> "$work/c.txt"; }
for i in interop interop2; do
  echo "-- the $i page's console, against Chromium's:"
  console "$pages/$i.html"
  if cmp -s "$pages/$i.chromium" "$work/c.txt"; then echo "the same $(wc -l < "$work/c.txt" | tr -d ' ') lines"
  else diff "$pages/$i.chromium" "$work/c.txt"; fi
done
echo "-- the counter:"
amb script "$pages/counter.html"
echo "-- clicked twice:"
amb --click '#inc' --click '#inc' query "$pages/counter.html" "dom_attr(N, id, count), dom_text_content(N, T), dom_attr(N, class, C)"
echo "-- its listeners, and what the reasoner says of them:"
amb query "$pages/counter.html" "script_listens(N, T, R), dom_element(N, E)"
amb advise "$pages/counter.html" | grep '^amb_diag(click'
echo "-- the list:"
amb script "$pages/list.html"
amb query "$pages/list.html" "dom_element(L, li), dom_text_content(L, T)"
amb advise "$pages/list.html" | grep '^amb_diag(unsettled'
echo "-- a click on nothing:"
amb --click '#nope' script "$pages/counter.html" | head -1
rm -rf "$work"
