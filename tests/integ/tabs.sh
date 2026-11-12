#!/bin/sh
# tests/integ/tabs.sh -- tabs, and sessions restored into them (Phase 12i, 3). A
# session opens three tabs, each its own page and reasoner: one tab goes to a second
# page and is clicked; switching away and back loads its page again with its click
# made again, and its reasoner, which kept its facts, is sent nothing; a history is
# walked back and forward; a tab is closed. `quit' keeps the tabs in the profile (the
# session `last'): their order, the current one, their histories. `loop --restore'
# opens them again, after one tab's page was removed: that tab cannot load it, and the
# profile's rules state the fault. A private session restores no session and stores
# nothing. Forgetting a page takes it from the saved tabs too. AMB_NOW fixes the clock;
# paths are written WORK.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-tabs.XXXXXX")
export AMB_WORK=$work/session AMB_NOW=1700000000
prof=$work/profile
pages=$work/pages
mkdir -p "$pages"
cp "$AMB_ROOT/tests/pages/news.html" "$AMB_ROOT/tests/pages/style.html" "$AMB_ROOT/tests/pages/layout.html" "$AMB_ROOT/tests/pages/logo.png" \
   "$AMB_ROOT/tests/integ/session/cart.html" "$pages/"
amb() { sh "$AMB_ROOT/bin/ambitious" --profile "$prof" "$@" 2>&1 | sed -e "s|$work|WORK|g"; }
files() { find "$prof" -type f | sort | xargs cat | cksum; }
ITEMS="dom_element(L, li), dom_text_content(L, T)"
echo "-- a session of three tabs, each with its page and its reasoner:"
amb loop - <<EOF
open $pages/news.html
open $pages/style.html
go $pages/cart.html
click #more
query $ITEMS
open $pages/layout.html
tabs
tab 1
query dom_element(N, h1), dom_text_content(N, T)
tab 2
echo -- the cart again: its page loaded again, its click made again, its reasoner sent nothing
query $ITEMS
reasoner
history
back
forward
query $ITEMS
close 3
tabs
quit
EOF
echo "-- the session kept in the profile:"
amb profile "amb_profile_session_pages(last, Pages)"
amb profile "amb_profile_current(last, T), amb_profile_tab(last, T, Order, At)"
amb profile "amb_profile_tab(last, T, 2, _), amb_profile_entry(last, T, I, Url, Title)"
echo "-- restored, after the cart's page was removed:"
rm "$pages/cart.html"
amb loop --restore - <<EOF
query $ITEMS
tab 1
query dom_element(N, h1), dom_text_content(N, T)
quit
EOF
echo "-- what the profile says is wrong with the session:"
amb profile "amb_profile_fault(K, S, T)"
echo "-- a private session restores no session and stores nothing:"
before=$(files)
sh "$AMB_ROOT/bin/ambitious" --private loop --restore - 2>&1 <<EOF | sed -e "s|$work|WORK|g"
open $pages/news.html
tabs
save
quit
EOF
[ "$before" = "$(files)" ] && echo "the profile's files are as they were" || echo "the profile CHANGED"
echo "-- forgetting the cart's page takes it from the saved tabs:"
amb forget "file://$pages/cart.html"
amb profile "amb_profile_session_pages(last, Pages)"
amb profile "amb_profile_fault(K, S, T)"
rm -rf "$work"
