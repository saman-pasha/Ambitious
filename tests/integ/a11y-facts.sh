#!/bin/sh
# tests/integ/a11y-facts.sh -- the accessibility facts Chromium's DevTools protocol does not show,
# asked of the fixture pages: each item's position in its set (WAI-ARIA 1.2: counted among its
# set, a named radio's among its name's in its form, or as aria-posinset and aria-setsize say),
# levels, aria-current, aria-valuetext, the values of ranges and where they came from, fields'
# values and placeholders; why a state and a description are what they are; the live region each
# node is inside; the hidden subtrees with what in them takes focus or is a live region; and the
# elements that are no node but carry aria-* attributes. Paths are written ROOT.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-a11y-facts.XXXXXX")
export AMB_WORK=$work
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | sed "s|$AMB_ROOT|ROOT|g"; }
states="$AMB_ROOT/tests/pages/a11y-states.html"
names="$AMB_ROOT/tests/pages/a11y-names.html"
live="$AMB_ROOT/tests/pages/a11y-live.html"
faults="$AMB_ROOT/tests/pages/a11y-faults.html"
echo "-- positions in their sets:"
amb query "$states" "a11y_by_id(Id, A), a11y_position(A, P, S)"
echo "-- levels:"
amb query "$states" "a11y_by_id(Id, A), a11y_level(A, L)"
echo "-- aria-current and aria-valuetext:"
amb query "$states" "a11y_by_id(Id, A), ( a11y_state(A, current, V) ; a11y_valuetext(A, V) )"
echo "-- ranges, and where each value came from:"
amb query "$states" "a11y_by_id(Id, A), a11y_valuenow(A, Now, From), ( a11y_valuemin(A, Min) -> true ; Min = none ), ( a11y_valuemax(A, Max) -> true ; Max = none )"
echo "-- fields' values and placeholders:"
amb query "$states" "a11y_by_id(Id, A), ( a11y_value(A, V), K = value ; a11y_placeholder(A, V), K = placeholder )"
echo "-- why the disabled are disabled:"
amb query "$states" "a11y_by_id(Id, A), a11y_explain_state(A, disabled, Why)"
echo "-- why the fruit select's options are chosen or not:"
amb query "$states" "member(Id, ['f-none', 'f-lemon', 'f-apple', 'f-pear']), a11y_by_id(Id, A), a11y_explain_state(A, S, Why)"
echo "-- why the descriptions:"
amb query "$names" "a11y_by_id(Id, A), a11y_explain_description(A, Why)"
echo "-- the live region each node is inside:"
amb query "$live" "a11y_by_id(Id, A), a11y_live_inside(A, P), a11y_live_region(A, R), a11y_by_id(In, R)"
echo "-- hidden subtrees, and what in them takes focus or is a live region:"
amb query "$live" "a11y_hidden(N, How), dom_attr(N, id, Id)"
amb query "$live" "( a11y_hidden_focusable(N, R), W = focusable ; a11y_hidden_live(N, R), W = live_region ), dom_attr(N, id, Id), dom_element(R, E)"
echo "-- elements that are no node, kept for their aria-* attributes:"
amb query "$faults" "a11y_no_node(N, Role), dom_attr(N, id, Id)"
rm -rf "$work"
