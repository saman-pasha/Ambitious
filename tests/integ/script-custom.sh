#!/bin/sh
# tests/integ/script-custom.sh -- custom elements and shadow trees (src/script/custom.c,
# the DOM's attach_shadow). The pages' consoles must be Chromium's line for line
# (tests/pages/NAME.chromium, from tools/bench/console.js):
#   custom.html: define()'s checks, get and whenDefined, the parsed elements upgraded at
#     the definition with their observed attributes and connectedCallback, createElement and
#     new, an element made before its definition upgraded on insertion and by upgrade(),
#     attributeChangedCallback, disconnectedCallback, a move, innerHTML's elements, nested
#     elements' order, one in a shadow tree, a constructor that throws;
#   shadow.html: attachShadow's checks, open and closed roots, a root's own tree, slots and
#     their assigned nodes, assignedSlot, events retargeted and their composed paths.
# Then the facts the reasoner has of the definitions and the shadow trees, and its
# diagnostic of an element of a custom element's name no script defined.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-scustom.XXXXXX")
export AMB_WORK=$work
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" < /dev/null 2>&1 | sed "s|$AMB_ROOT|ROOT|g"; }
pages="$AMB_ROOT/tests/pages"
# a page's console as Chromium's is written: its messages, then its uncaught errors
console() { amb script "$1" > "$work/s.txt"; sed -n -e 's/^console [a-z]* *\[[0-9]*\] //p' "$work/s.txt"; sed -n -e 's/^error *[a-z]* *\[[0-9]*\] /error: /p' "$work/s.txt"; }
for p in custom shadow; do
  echo "-- the $p page's console, against Chromium's:"
  console "$pages/$p.html" > "$work/c.txt"
  if cmp -s "$pages/$p.chromium" "$work/c.txt"; then echo "the same $(wc -l < "$work/c.txt" | tr -d ' ') lines"
  else diff "$pages/$p.chromium" "$work/c.txt"; fi
done
echo "-- the definitions: their names, what they observe, the elements upgraded, their callbacks:"
amb query "$pages/custom.html" "script_definition(D, Name, R), script_definition_observes(D, O), script_definition_upgraded(D, U), ( script_defines_connected(D) -> C = y ; C = n ), ( script_defines_attribute_changed(D) -> A = y ; A = n )"
echo "-- the shadow roots, each its tree's own:"
amb query "$pages/shadow.html" "dom_shadow_root(H, R, M), dom_attr(H, id, Id), dom_first_child(R, F), dom_element(F, E)"
amb query "$pages/custom.html" "dom_shadow_root(H, R, M), dom_first_child(R, F), dom_attr(F, id, Id)"
echo "-- what the reasoner says of them:"
amb advise "$pages/custom.html" | grep -E '^amb_diag\(custom_'
amb advise "$pages/shadow.html" | grep -E '^amb_diag\(custom_'
rm -rf "$work"
