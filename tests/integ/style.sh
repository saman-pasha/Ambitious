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
values="$AMB_ROOT/tests/pages/values.html"
echo "-- custom properties: an element's own and its ancestors', var() replaced:"
amb query "$values" "dom_attr(N, id, e), css_custom(N, Name, V)"
echo "-- widths from var() and calc(), one whose var() is a cycle (its fallback), a border's width under style none:"
amb query "$values" "member(I-P, [a-width, h-width, j-border_right_width, k-border_top_width]), dom_attr(N, id, I), css_explain(N, P, X)"
atrules="$AMB_ROOT/tests/pages/atrules.html"
echo "-- the sheets @import brought (one for print, not read), and the cascade layers in their order:"
amb query "$atrules" "css_imported(S, P, U)" | sed "s|$AMB_ROOT|ROOT|g"
amb query "$atrules" "css_layer(L, N, R)"
echo "-- the web fonts @font-face fetched, and a family resolved to one:"
amb query "$atrules" "css_font_face(F, Fam, U, W, I)" | sed "s|$AMB_ROOT|ROOT|g"
amb query "$atrules" "dom_attr(N, id, m), css_explain(N, font_family, X)"
echo "-- what lost a card's properties: a layer under a later one, under the unlayered rules; order within a layer:"
amb query "$atrules" "dom_attr(N, id, a), member(P, [background_color, color, border_top_color]), css_lost(N, P, D, Why)"
sel="$AMB_ROOT/tests/pages/selectors.html"
echo "-- pseudo-elements' styles: a ::before's colour and content, its own or its element's; the drop cap's size:"
amb query "$sel" "dom_attr(N, id, l1), member(P, [content, font_weight, color]), css_pseudo_computed(N, before, P, V, H)"
amb query "$sel" "dom_attr(N, id, dr), css_pseudo_computed(N, first_letter, font_size, V, H)"
echo "-- the page hovered at #d2's span (--hover): the state, and :has() over it:"
amb --hover '#d2 span' query "$sel" "css_state(K, N), dom_element(N, E)"
amb --hover '#d2 span' query "$sel" "dom_attr(N, id, d2), css_explain(N, background_color, X)"
echo "-- the reasoner's diagnostics:"
amb advise "$page"
st=$?
rm -rf "$work"
echo "exit $st"
