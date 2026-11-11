#!/bin/sh
# tests/integ/page.sh -- an HTML page through the engine and the reasoner: parsed
# (the tree, its parse errors, the invariants), asked across the DOM's and the
# parse's facts, advised, and the advice applied by the engine.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-page.XXXXXX")
page="$AMB_ROOT/tests/pages/news.html"
export AMB_WORK=$work
sh "$AMB_ROOT/bin/ambitious" parse "$page"
echo "-- the bold runs and their text:"
sh "$AMB_ROOT/bin/ambitious" query "$page" "dom_element(N, b), dom_text_content(N, T)"
echo "-- the parse errors by kind:"
sh "$AMB_ROOT/bin/ambitious" query "$page" "html_error_count(C, N)"
echo "-- why the stray text is in the body, not the table:"
sh "$AMB_ROOT/bin/ambitious" why "$page" "dom_text(X, T), sub_atom(T, _, _, _, stray), dom_parent(X, P), dom_element(P, E)"
echo "-- advice, applied:"
sh "$AMB_ROOT/bin/ambitious" advise "$page" --apply
st=$?
rm -rf "$work"
echo "exit $st"
