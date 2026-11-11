#!/bin/sh
# tests/integ/a11y-faults.sh -- the faults the accessibility rules find in states, properties,
# ranges, positions and hidden content, on a page with one of each (tests/pages/a11y-faults.html),
# each diagnostic naming its node and its rule: an aria-* state or property a role does not
# support, a name on a role that takes none, an aria-* attribute WAI-ARIA does not define, a value
# outside an attribute's values, a range widget without the value its role needs or with one
# outside its range, a position outside its set, what takes focus inside aria-hidden content, a
# live region inside hidden content. Then the six kinds axe-core has a rule for, held to what
# axe-core finds on the page (tests/pages/a11y-faults.axe, from tools/bench/axe.js): each
# diagnostic as the axe rule it is and the element it names (the aria-hidden root for focus
# inside it, as axe names it). Then two of the faults proved. Paths are written ROOT.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-a11y-faults.XXXXXX")
export AMB_WORK=$work
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | sed "s|$AMB_ROOT|ROOT|g"; }
page="$AMB_ROOT/tests/pages/a11y-faults.html"
amb advise "$page"
echo "-- the faults axe-core has rules for, against what axe-core finds:"
amb query "$page" "member(K-R, [unsupported_attribute-'aria-allowed-attr', prohibited_attribute-'aria-prohibited-attr', unknown_attribute-'aria-valid-attr', bad_value-'aria-valid-attr-value', range_missing_value-'aria-required-attr', hidden_focusable-'aria-hidden-focus']), a11y_diag(K, S, _), ( a11y_node(S, N0, _, _, _) -> true ; N0 = S ), ( a11y_hidden_focusable(N0, N) -> true ; N = N0 ), dom_attr(N, id, Id)" |
  sed -n "s/^.* R = '\([a-z-]*\)', .* Id = '\{0,1\}\([^']*\)'\{0,1\}$/\1 #\2/p" | sort -u > "$work/amb.axe"
n=$(wc -l < "$work/amb.axe" | tr -d ' ')
if cmp -s "$AMB_ROOT/tests/pages/a11y-faults.axe" "$work/amb.axe"; then echo "the same $n findings"
else echo "$n findings; where they differ from axe-core's:"; diff "$AMB_ROOT/tests/pages/a11y-faults.axe" "$work/amb.axe"; fi
echo "-- the slider without a value, proved:"
amb why "$page" "a11y_range_missing(A, slider)"
echo "-- the checkbox told it is pressed, proved:"
amb why "$page" "a11y_unsupported_attr(N, 'aria-pressed', Role)"
rm -rf "$work"
