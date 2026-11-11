#!/bin/sh
# tests/integ/a11y.sh -- a page's accessibility through the developer's view: its
# accessibility tree and every module's diagnostics (ambitious inspect), each
# naming its node and its rule; then why the form's controls have their names, the
# outline, and a proof of the heading that skips a level. Paths are written ROOT.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-a11y.XXXXXX")
page="$AMB_ROOT/tests/pages/a11y.html"
export AMB_WORK=$work
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | sed "s|$AMB_ROOT|ROOT|g"; }
amb inspect "$page"
echo "-- why the controls have their names:"
amb query "$page" "a11y_node(A, _, R, _, _), member(R, [textbox, checkbox, button, radio]), a11y_explain_name(A, Why)"
echo "-- the outline:"
amb query "$page" "a11y_outline(A, Level), a11y_node(A, _, _, Name, _)"
echo "-- the heading that skips a level, proved:"
amb inspect "$page" --why "a11y_heading_jump(A, From, To)" | sed -n '/^== why/,$p'
rm -rf "$work"
