#!/bin/sh
# tests/integ/foreign.sh -- foreign content and template contents through the
# engine: a page of SVG (adjusted names, an attribute in the XLink namespace, a
# foreignObject, a CDATA section, a style), MathML (an annotation-xml of HTML),
# a template and a select with a selectedcontent. Its serialization must be
# Chromium's byte for byte (tests/pages/foreign.serialized, from
# tools/bench/serialize.js) and its console Chromium's line for line
# (tests/pages/foreign.chromium, from tools/bench/console.js). Then the snapshot's
# facts of namespaces and template contents, what the cascade makes of foreign
# elements, and what the template's contents are not: fetched, styled, listened
# to, or anything a reference can name. Paths are written ROOT.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-foreign.XXXXXX")
export AMB_WORK=$work
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | sed "s|$AMB_ROOT|ROOT|g"; }
page="$AMB_ROOT/tests/pages/foreign.html"
echo "-- the page parsed, against Chromium's serialization:"
amb parse "$page" > "$work/p.txt"
sed '$d' "$work/p.txt" > "$work/ser.txt"
if cmp -s "$AMB_ROOT/tests/pages/foreign.serialized" "$work/ser.txt"; then echo "the same $(wc -c < "$work/ser.txt" | tr -d ' ') bytes"
else diff "$AMB_ROOT/tests/pages/foreign.serialized" "$work/ser.txt"; fi
tail -1 "$work/p.txt"
echo "-- its console, against Chromium's:"
amb script "$page" | sed -n -e 's/^console [a-z]* *\[[0-9]*\] //p' > "$work/c.txt"
if cmp -s "$AMB_ROOT/tests/pages/foreign.chromium" "$work/c.txt"; then echo "the same $(wc -l < "$work/c.txt" | tr -d ' ') lines"
else diff "$AMB_ROOT/tests/pages/foreign.chromium" "$work/c.txt"; fi
echo "-- the elements of the SVG and MathML namespaces:"
amb query "$page" "dom_namespace(N, NS), dom_element(N, E)"
echo "-- the attributes in a namespace, their names with their prefixes:"
amb query "$page" "dom_attr_namespace(N, A, NS), dom_attr(N, A, V)"
echo "-- the template's contents, a fragment of its own:"
amb query "$page" "dom_template_content(T, F), dom_node(F, K), dom_children(F, Cs)"
amb query "$page" "aggregate_all(count, dom_in_template(_, _), Inside), dom_element(B, body), aggregate_all(count, dom_descendant(_, B), InBody)"
echo "-- what the cascade made of the two titles, the SVG link and the foreignObject (Chromium's values):"
amb query "$page" "dom_element(N, E), memberchk(E, [title, a]), (dom_namespace(N, NS) -> true ; NS = html), css_computed(N, display, D, _), css_computed(N, color, C, _)"
amb query "$page" "dom_element(N, foreignObject), css_computed(N, color, C, _)"
echo "-- nothing of the template's contents styled, fetched or listened to:"
amb query "$page" "dom_in_template(N, _), css_style_of(N, _)"
amb query "$page" "net_resource(R, U, K, O, _, _, _)"
amb query "$page" "dom_in_template(N, _), script_listener(N, T, _)"
echo "-- the references that point into the template, broken:"
amb query "$page" "a11y_broken_ref(N, A, Id), dom_element(N, E)"
rm -rf "$work"
