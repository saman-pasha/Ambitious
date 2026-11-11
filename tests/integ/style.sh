#!/bin/sh
# tests/integ/style.sh -- a page's style through the engine and the reasoner: each
# element's computed values; why the paragraphs have their colours; what lost the
# intro's colour, and at which step of the cascade; a proof; the presentational
# hint; and the reasoner's diagnostics of the page (contrast, unused rules).
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-style.XXXXXX")
page="$AMB_ROOT/tests/pages/style.html"
amb() { sh "$AMB_ROOT/bin/ambitious" "$@"; }
export AMB_WORK=$work
amb style "$page"
echo "-- why each paragraph has its colour:"
amb query "$page" "dom_element(N, p), css_explain(N, color, X)"
echo "-- what lost the intro's colour, and at which step:"
amb query "$page" "dom_attr(N, id, intro), css_lost(N, color, D, Why), css_decl(D, _, _, Written, _)"
echo "-- the small print's size, proved:"
amb why "$page" "dom_element(N, small), css_explain(N, font_size, X)"
echo "-- the font element's colour and size, from its attributes:"
amb query "$page" "dom_element(N, font), member(P, [color, font_size]), css_explain(N, P, X)"
echo "-- the reasoner's diagnostics:"
amb advise "$page"
st=$?
rm -rf "$work"
echo "exit $st"
