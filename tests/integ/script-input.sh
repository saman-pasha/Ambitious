#!/bin/sh
# tests/integ/script-input.sh -- a user's actions at a page (src/script/input.c): the
# pointer, keys, focus and forms' submissions, each page's console against Chromium's
# line for line (tests/pages/NAME.chromium, written by tools/bench/script-references.sh,
# the actions both are given in NAME.actions):
#   events.html: a click's pointer and mouse events (where the pointer comes from, the
#     boundary events, its coordinates from the layout), focus moved by a press, typing
#     into a field, Backspace, Tab, Enter on a link (its fragment's popstate and
#     hashchange), a double click, a key cancelled, focus() and blur();
#   keys.html: the sequential focus order (positive tabindex first), Shift+Tab, Space and
#     Enter on a box and a button, a textarea's line break, Control+a, a label's click,
#     focus() of what takes none;
#   form.html, served by tests/integ/script-serve.py, once for each line of form.actions:
#     the console and the requests the server had (a submit button's click, a submitter's
#     formaction and formmethod, submit() and requestSubmit(), GET, multipart and
#     text/plain, a submission cancelled, constraint validation, formdata's listener,
#     Enter's implicit submission, a second submission in place of the first, a link,
#     the page's windows-1252 in what it sends).
# Then the facts the reasoner has of the navigations and the fields they sent, and its
# diagnostics: a form sent to another origin over http:, a password over http: and in a
# GET's URL, a key listener no key reaches. The served pages' port is written PORT.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-sinput.XXXXXX")
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
export AMB_WORK=$work
pages="$AMB_ROOT/tests/pages"
python3 "$AMB_ROOT/tests/integ/script-serve.py" --log "$work/req.log" "$pages" > "$work/port" 2> "$work/serve.err" &
pid=$!
i=0
while [ ! -s "$work/port" ] && [ $i -lt 40 ]; do sleep 0.25; i=$((i + 1)); done
port=$(head -1 "$work/port")
norm() { sed -e "s|$AMB_ROOT|ROOT|g" -e "s/:$port\b/:PORT/g" -e 's/----[A-Za-z]*FormBoundary[A-Za-z0-9]*/BOUNDARY/g'; }
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" < /dev/null 2>&1 | norm; }
# a page's console after the actions, as Chromium's is written: its messages, then its
# uncaught errors (the actions, then the page, are the arguments)
console() {
  amb "$@" > "$work/s.txt"
  sed -n -e 's/^console [a-z]* *\[[0-9]*\] //p' "$work/s.txt"
  sed -n -e 's/^error *[a-z]* *\[[0-9]*\] /error: /p' "$work/s.txt"
}
same() {
  if cmp -s "$1" "$2"; then echo "the same $(wc -l < "$2" | tr -d ' ') lines"
  else diff "$1" "$2"; fi
}
for p in events keys; do
  echo "-- the $p page's console after its actions, against Chromium's:"
  eval "set -- $(cat "$pages/$p.actions")"
  console "$@" script "$pages/$p.html" > "$work/c.txt"
  same "$pages/$p.chromium" "$work/c.txt"
done
echo "-- the form's submissions, each one's console and requests, against Chromium's:"
url="http://127.0.0.1:$port/form.html"
: > "$work/form.txt"
while read -r q acts; do
  : > "$work/req.log"
  echo "== $q $acts" >> "$work/form.txt"
  u=$url
  [ "$q" != - ] && u="$url?$q"
  eval "set -- $acts"
  console "$@" script "$u" >> "$work/form.txt"
  grep -v 'favicon\|^GET /form.html' "$work/req.log" | norm >> "$work/form.txt"
done < "$pages/form.actions"
same "$pages/form.chromium" "$work/form.txt"
echo "-- the navigations, as the reasoner has them: the form's, the field names it sent:"
amb --click '#go' query "$url" "script_navigation(N, C, M, U), script_navigation_form(N, F, S), dom_attr(F, id, Fi), dom_attr(S, id, Si), script_navigation_status(N, St)"
amb --click '#go' query "$url" "script_form_field(N, Name, _)"
echo "-- a planned one that another took the place of, and a link's:"
amb --click '#run' query "$url?twice" "script_navigation(N, C, M, U), ( script_navigation_status(N, St) -> true ; St = none )"
amb --click '#away' query "$url" "script_navigation(N, C, M, U), script_navigation_status(N, St)"
echo "-- what the reasoner says of them:"
amb --click '#go' advise "$url?cross" | grep -E '^amb_diag\(form_'
amb --click '#go' advise "$url?pwget" | grep -E '^amb_diag\(form_'
amb advise "$pages/keys.html" | grep -E '^amb_diag\(key_'
kill $pid
rm -rf "$work"
