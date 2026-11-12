#!/bin/sh
# tests/integ/a11y-chromium.sh -- the accessibility trees of the fixture pages held to Chromium's
# own: states and properties (a11y-states), names and descriptions (a11y-names), live regions
# (a11y-live), the rest of 12e (a11y-more), the flat tree's (a11y-shadow, PLAN.md 12z). Each node whose element has an id is a line, as tools/bench/axtree.js writes
# Chromium's tree into tests/pages/NAME.chromium: its role, name, description, level, states and
# properties, values and live region. Ambitious's lines come from the developer's view (ambitious
# inspect), less what Chromium's DevTools protocol does not show: where a name or a value came
# from, a position in a set, aria-current, aria-valuetext, selected=false, a region that is off,
# focusability; a generic node with nothing more to say is no line. A line that differs is
# printed as diff prints it. The differences expected are where Ambitious keeps to HTML-AAM and
# Chromium does not: a disabled fieldset and a disabled optgroup are disabled themselves, not only
# their controls; a figure is named by its figcaption.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-a11y-chromium.XXXXXX")
export AMB_WORK=$work
pages="$AMB_ROOT/tests/pages"
tree() {
  sh "$AMB_ROOT/bin/ambitious" inspect "$1" 2>&1 | sed -n '/^== accessibility tree/,/^== diagnostics/p' | grep '#' |
    sed -e 's/^ *//' -e 's/ ([a-z_]*)//g' -e 's/ posinset=[-0-9]* setsize=[-0-9]*//' -e 's/ current=[a-z]*//' \
        -e 's/ valuetext="[^"]*"//' -e 's/ selected=false//' -e 's/ invalid=grammar/ invalid=true/' \
        -e 's/ invalid=spelling/ invalid=true/' -e 's/ live=off atomic=[a-z]* relevant="[^"]*"//' -e 's/ focusable$//' |
    grep -v '^generic#[^ ]*$'
}
for p in a11y-states a11y-names a11y-live a11y-more a11y-shadow; do
  echo "-- the $p page's tree, against Chromium's:"
  tree "$pages/$p.html" > "$work/tree.txt"
  n=$(wc -l < "$work/tree.txt" | tr -d ' ')
  if cmp -s "$pages/$p.chromium" "$work/tree.txt"; then echo "the same $n lines"
  else echo "$n lines; where they differ from Chromium's:"; diff "$pages/$p.chromium" "$work/tree.txt"; fi
done
rm -rf "$work"
