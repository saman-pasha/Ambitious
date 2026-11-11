#!/bin/sh
# tests/integ/session.sh -- a reasoner kept for a session (Phase 12i, 1). `loop -` reads
# a session's commands; one reasoner holds the page's snapshot for all of them, given
# the first snapshot whole and each one after as a delta, the facts retracted and
# added. The page is tests/integ/session/cart.html, whose button's click removes the
# first item, puts a new one first and one on sale at the end, and changes a count and
# a title; the second click's nodes take the first's freed slots, so facts come back
# between kept ones and predicates are rebuilt in order. After the clicks, every answer
# the session's reasoner gives is held to a fresh reasoner's (the reasoner's own
# command, cocolog over prolog/amb/reason.pl) over the whole snapshot, line for line:
# queries whose answers come in the facts' order, the advice, a proof, the check, and
# every fact the reasoner holds, predicate by predicate in the order it holds them.
# Paths are written ROOT.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
. "$AMB_ROOT/toolchain/env.sh"
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-session.XXXXXX")
export AMB_WORK=$work/session
page=$AMB_ROOT/tests/integ/session/cart.html
fresh() { timeout 120 "$AMB_COCOLOG" -s "$AMB_ROOT/prolog/amb/reason.pl" -- "$@" < /dev/null 2>&1; }
ITEMS="dom_element(L, li), dom_text_content(L, T)"
TITLE="dom_attr(N, title, T)"
STYLE="dom_element(L, li), css_computed(L, color, V, H)"
BOXES="dom_element(L, li), layout_box_of(L, B), layout_border_box(B, X, Y, W, H)"
NAMES="a11y_node(A, E, R, Nm, F), dom_element(E, li)"
WHY="dom_inside(T, U), dom_element(U, ul), dom_text(T, _)"
ALL="member(M, [dom, html, css, layout, net, a11y, script]), amb_reason_schema_decl(M, fact(F/K, _)), functor(G, F, K), call(G)"
sh "$AMB_ROOT/bin/ambitious" loop - > "$work/out" 2>&1 <<EOF
open $page
query $ITEMS
advise
click #more
query $ITEMS
click #more
echo -- after the clicks
query $ITEMS
query $TITLE
query $STYLE
query $BOXES
query $NAMES
why $WHY
advise
check
query $ALL
echo -- end
snapshot $work/after.pl
reasoner
quit
EOF
echo "exit $?" >> "$work/out"
echo "-- the session (every fact it holds left out):"
sed -e "s|$AMB_ROOT|ROOT|g" -e "s|$work|WORK|g" "$work/out" | awk '/^> query member/ { skip = 1; print; next } /^> / { skip = 0 } !skip'
echo "-- the same requests asked of a fresh reasoner over the whole snapshot after the clicks:"
{
  echo "-- after the clicks"
  for q in "$ITEMS" "$TITLE" "$STYLE" "$BOXES" "$NAMES"; do echo "> query $q"; fresh query "$work/after.pl" "$q"; done
  echo "> why $WHY"; fresh why "$work/after.pl" "$WHY"
  echo "> advise"; fresh advise "$work/after.pl"
  echo "> check"; fresh check "$work/after.pl"
  echo "> query $ALL"; fresh query "$work/after.pl" "$ALL"
  echo "> echo -- end"; echo "-- end"
} > "$work/fresh"
sed -n '/^-- after the clicks$/,/^-- end$/p' "$work/out" > "$work/kept"
if cmp -s "$work/kept" "$work/fresh"; then
  echo "the same $(wc -l < "$work/kept" | tr -d ' ') lines, $(grep -c '^M = ' "$work/kept") of them the facts held, in order"
else
  diff "$work/fresh" "$work/kept" | head -20
fi
rm -rf "$work"
