#!/bin/sh
# tests/integ/cssref.sh -- computed values against Chromium's: each fixture page whose
# values Chromium gave (tests/pages/NAME.chromium written by tools/bench/cssref.js),
# loaded by Ambitious, each element with an id and a data-check its computed value of each
# property named there held to Chromium's (tools/cssref.py: the same text, or lengths
# within 0.02 px). The values that differ are printed; then how many agree.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-cssref.XXXXXX")
export AMB_WORK=$work
for ref in "$AMB_ROOT"/tests/pages/*.chromium; do
  head -1 "$ref" | grep -q 'tools/bench/cssref.js' || continue
  name=$(basename "$ref" .chromium)
  sh "$AMB_ROOT/bin/ambitious" query "$AMB_ROOT/tests/pages/$name.html" "dom_attr(N, id, I), dom_attr(N, 'data-check', _), css_computed(N, P, V, _)" > "$work/$name.amb" 2>&1
  python3 -I "$AMB_ROOT/tools/cssref.py" "$work/$name.amb" "$ref"
done
rm -rf "$work"
