#!/bin/sh
# tests/integ/script-observe.sh -- MutationObserver (src/script/observe.c) over the DOM's
# log of its changes (contracts/dom.pl's mutation event: a child's siblings, an
# attribute's name and old value, a node's old data). The pages' consoles must be
# Chromium's line for line (tests/pages/NAME.chromium, from tools/bench/console.js):
#   observer.html: childList records of each insertion and removal (one call's run of
#     nodes one record: append and prepend of many, replaceChildren, textContent,
#     innerHTML, replaceChild), a move between parents, attributes and characterData
#     with their old values, namespaced attributes, an attributeFilter, observe() again,
#     takeRecords, disconnect, a removed subtree's transient registrations, the
#     notification's place among the microtasks and the observers' order, observe()'s
#     refusals;
#   observer-unsettled.html: an observer an interval keeps busy, one disconnected.
# Then the facts the reasoner has of the observers and what they watch, and its
# diagnostic of an observer never disconnected on a page that never comes to rest.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-sobserve.XXXXXX")
export AMB_WORK=$work
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" < /dev/null 2>&1 | sed "s|$AMB_ROOT|ROOT|g"; }
pages="$AMB_ROOT/tests/pages"
# a page's console as Chromium's is written: its messages, then its uncaught errors
console() { amb script "$1" > "$work/s.txt"; sed -n -e 's/^console [a-z]* *\[[0-9]*\] //p' "$work/s.txt"; sed -n -e 's/^error *[a-z]* *\[[0-9]*\] /error: /p' "$work/s.txt"; }
for p in observer observer-unsettled; do
  echo "-- the $p page's console, against Chromium's:"
  console "$pages/$p.html" > "$work/c.txt"
  if cmp -s "$pages/$p.chromium" "$work/c.txt"; then echo "the same $(wc -l < "$work/c.txt" | tr -d ' ') lines"
  else diff "$pages/$p.chromium" "$work/c.txt"; fi
done
echo "-- the observers, what their callbacks had, and which were disconnected:"
amb query "$pages/observer.html" "script_observer(O, R), script_observer_delivered(O, Records, Calls)"
amb query "$pages/observer.html" "script_observer_disconnected(O)"
echo "-- each observe(): its node, and what it watches there:"
amb query "$pages/observer.html" "script_observation(S, O, T, _), dom_attr(T, id, Id), ( script_observes_children(S) -> C = y ; C = n ), ( script_observes_attributes(S, _) -> A = y ; A = n ), ( script_observes_data(S) -> D = y ; D = n ), ( script_observes_subtree(S) -> Sub = y ; Sub = n ), ( script_observes_old_values(S) -> Old = y ; Old = n )"
amb query "$pages/observer.html" "script_observes_attributes(S, F), F > 0"
echo "-- what the reasoner says of them:"
amb advise "$pages/observer-unsettled.html" | grep -E '^amb_diag\(observer_'
amb advise "$pages/observer.html" | grep -E '^amb_diag\(observer_'
rm -rf "$work"
