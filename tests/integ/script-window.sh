#!/bin/sh
# tests/integ/script-window.sh -- the window's own (src/script/window.c): window.html, served
# by tests/integ/script-serve.py, its console against Chromium's line for line
# (tests/pages/window.chromium, written by tools/bench/script-references.sh): screen and the
# window's sizes, navigator, matchMedia, history (pushState, replaceState, their refusals,
# back and forward with popstate), localStorage and sessionStorage. Then localStorage kept
# in a profile between runs, and not by a private session; the facts the reasoner has of
# history's navigations and of the storage writes (keys, never values), and its diagnostic
# of a credential kept in Web Storage. The served page's port is written PORT.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-swindow.XXXXXX")
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
export AMB_WORK=$work
pages="$AMB_ROOT/tests/pages"
python3 "$AMB_ROOT/tests/integ/script-serve.py" "$pages" > "$work/port" 2> "$work/serve.err" &
pid=$!
i=0
while [ ! -s "$work/port" ] && [ $i -lt 40 ]; do sleep 0.25; i=$((i + 1)); done
port=$(head -1 "$work/port")
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" < /dev/null 2>&1 | sed -e "s|$AMB_ROOT|ROOT|g" -e "s/:$port\b/:PORT/g"; }
console() { amb "$@" > "$work/s.txt"; sed -n -e 's/^console [a-z]* *\[[0-9]*\] //p' "$work/s.txt"; sed -n -e 's/^error *[a-z]* *\[[0-9]*\] /error: /p' "$work/s.txt"; }
url="http://127.0.0.1:$port/window.html"
echo "-- the window page's console, against Chromium's:"
console script "$url" > "$work/c.txt"
if cmp -s "$pages/window.chromium" "$work/c.txt"; then echo "the same $(wc -l < "$work/c.txt" | tr -d ' ') lines"
else diff "$pages/window.chromium" "$work/c.txt"; fi
echo "-- localStorage kept in a profile, twice; then a private session's:"
mkdir -p "$work/profile"
console --profile "$work/profile" script "$pages/storage-keep.html"
console --profile "$work/profile" script "$pages/storage-keep.html"
console --private --profile "$work/profile" script "$pages/storage-keep.html"
echo "-- history's navigations, as the reasoner has them:"
amb query "$url" "script_navigation(N, history, M, U)"
echo "-- the storage writes: their areas, what they did, their keys (no value):"
amb query "$url" "script_storage_write(S, A, Op, K)"
echo "-- what the reasoner says of them:"
amb advise "$url" | grep -E '^amb_diag\(storage_'
kill $pid
rm -rf "$work"
