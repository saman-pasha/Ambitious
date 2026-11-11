#!/bin/sh
# tests/integ/a11y.sh -- a page's accessibility through the developer's view: its
# accessibility tree (each node's role, element id, name and where it came from, level,
# position, states) and every module's diagnostics (ambitious inspect), each naming its
# node and its rule; then why the form's controls have their names, the outline, and a
# proof of the heading that skips a level. The states, names, descriptions and live
# regions of other fixtures are held to Chromium's in a11y-chromium.sh, what Chromium does
# not show in a11y-facts.sh, their faults in a11y-faults.sh. Paths are written ROOT.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-a11y.XXXXXX")
page="$AMB_ROOT/tests/pages/a11y.html"
export AMB_WORK=$work
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | sed "s|$AMB_ROOT|ROOT|g"; }
amb inspect "$page"
echo "-- why the controls have their names:"
amb query "$page" "a11y_node(A, _, R, _, _), member(R, [textbox, searchbox, checkbox, button, radio]), a11y_explain_name(A, Why)"
echo "-- the outline:"
amb query "$page" "a11y_outline(A, Level), a11y_node(A, _, _, Name, _)"
echo "-- the heading that skips a level, proved:"
amb inspect "$page" --why "a11y_heading_jump(A, From, To)" | sed -n '/^== why/,$p'
rm -rf "$work"
