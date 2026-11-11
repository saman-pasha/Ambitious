#!/bin/sh
# tests/integ/profile.sh -- the profile, a cocolog store in ZiguratIP: what one
# process writes, the next reads; a private session writes nothing; forgetting
# removes what it names and no more; retention deletes visits older than it keeps;
# a site's permission keeps a kind of resource from being fetched; the reasoner,
# over a page's snapshot, cannot see the profile; a query cannot change it.
# AMB_NOW fixes the clock. Paths are written ROOT.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-profile.XXXXXX")
export AMB_WORK=$work/session
mkdir -p "$AMB_WORK"
prof=$work/profile
pages=$AMB_ROOT/tests/pages
amb() { sh "$AMB_ROOT/bin/ambitious" --profile "$prof" "$@" 2>&1 | sed "s|$AMB_ROOT|ROOT|g"; }
files() { find "$prof" -type f | sort | xargs cat | cksum; }
echo "-- three pages loaded, by three processes; a bookmark, by a fourth:"
AMB_NOW=1700000000 amb load "$pages/news.html" > /dev/null
AMB_NOW=1700003600 amb load "$pages/style.html" > /dev/null
AMB_NOW=1700007200 amb load "$pages/news.html" > /dev/null
AMB_NOW=1700007300 amb bookmark "file://$pages/style.html" "Styles, kept"
echo "-- read by the next:"
amb history
amb bookmarks
amb profile "amb_profile_most_visited(U, N)"
echo "-- a private session writes nothing:"
before=$(files)
AMB_NOW=1700008000 sh "$AMB_ROOT/bin/ambitious" --private load "$pages/layout.html" > /dev/null 2>&1
[ "$before" = "$(files)" ] && echo "the profile's files are as they were" || echo "the profile CHANGED"
echo "visits of the page loaded privately: $(amb history | grep -c layout.html)"
echo "-- a site's permission keeps its images from being fetched:"
amb permit file image deny
AMB_NOW=1700009000 amb load "$pages/news.html" | grep image
echo "-- forgetting removes what it names, and no more:"
amb forget "file://$pages/news.html"
amb history
amb bookmarks
echo "-- visits older than the retention are deleted when the profile is opened:"
AMB_NOW=1700180000 amb load "$pages/layout.html" > /dev/null
# the clock stays where it is from here: retention is applied at each opening
export AMB_NOW=1700185000
amb retain 1
amb history
echo "-- the reasoner, over a page's snapshot, cannot see the profile:"
amb query "$pages/style.html" "catch(amb_profile_visit(U, _, _), _, fail)"
echo "-- a query cannot change the profile:"
amb profile "retractall(amb_profile_bookmark(_, _, _))"
amb bookmarks
rm -rf "$work"
