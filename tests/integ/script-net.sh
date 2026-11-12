#!/bin/sh
# tests/integ/script-net.sh -- what a page's scripts reach of the network, and the scripts
# they insert and write. The pages' consoles must be Chromium's line for line, and a
# written page's document Chromium's byte for byte (tests/pages/NAME.chromium and
# NAME.serialized, written by tools/bench/script-references.sh):
#   fetch.html, served by tests/integ/script-serve.py: fetch with Request, Response and
#     Headers, XMLHttpRequest's events, a 404 and a 500, a redirect, another origin's
#     answer blocked (and a redirect to it), no-cors, a data: URL, POST and PUT bodies
#     the server echoes, a synchronous XHR, an abort; Chromium's console lines for each
#     refusal and HTTP error;
#   inserted.html: document.write while the parse's scripts run (a tag one write opens
#     another closes; a written inline script at once, an external one after its
#     writer), scripts a script inserts (an inline one at once, an external one later
#     with its load event, a missing one's error), innerHTML's, a clone's and a data
#     block's that do not run, one given its text after its insertion, then a write
#     after load that makes a new document;
#   inserted-order.html: sync, defer and async scripts and an inline defer, in order;
#   write-async.html: an async script's write, ignored with Chromium's warning;
#   write-dom.html: the document the writes and insertions make.
# Then the facts the reasoner has of them: each request and how it ended, what the server
# was sent, the scripts inserted and written, the writes; and its diagnostics.
# The served pages' port is written PORT, paths ROOT.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-snet.XXXXXX")
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
export AMB_WORK=$work
pages="$AMB_ROOT/tests/pages"
python3 "$AMB_ROOT/tests/integ/script-serve.py" --log "$work/req.log" "$pages" > "$work/port" 2> "$work/serve.err" &
pid=$!
i=0
while [ ! -s "$work/port" ] && [ $i -lt 40 ]; do sleep 0.25; i=$((i + 1)); done
port=$(head -1 "$work/port")
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | sed -e "s|$AMB_ROOT|ROOT|g" -e "s/:$port\b/:PORT/g"; }
# a page's console as Chromium's is written: its messages, then its uncaught errors
console() { amb script "$1" | sed -n -e 's/^console [a-z]* *\[[0-9]*\] //p' > "$work/c.txt"; amb script "$1" | sed -n -e 's/^error *[a-z]* *\[[0-9]*\] /error: /p' >> "$work/c.txt"; }
same() {
  if cmp -s "$1" "$2"; then echo "the same $(wc -l < "$2" | tr -d ' ') lines"
  else diff "$1" "$2"; fi
}
url="http://127.0.0.1:$port/fetch.html"
echo "-- the fetch page's console, against Chromium's:"
console "$url"
same "$pages/fetch.chromium" "$work/c.txt"
echo "-- its requests that did not end well, as the reasoner has them:"
amb query "$url" "script_request(Q, A, M, U, O), O \\== ok"
amb query "$url" "aggregate_all(count, script_request(_, fetch, _, _, _), F), aggregate_all(count, script_request(_, xhr, _, _, _), X), aggregate_all(count, script_request_cross(_), C)"
echo "-- what the server was sent:"
grep -E '^(POST|PUT)' "$work/req.log" | LC_ALL=C sort -u
echo "-- what the reasoner says of them:"
amb advise "$url" | grep -E '^amb_diag\(request_' | LC_ALL=C sort
for i in inserted inserted-order write-async; do
  echo "-- the $i page's console, against Chromium's:"
  console "$pages/$i.html"
  same "$pages/$i.chromium" "$work/c.txt"
done
echo "-- the written page's document, against Chromium's:"
amb parse "$pages/write-dom.html" | sed '$d' > "$work/ser.txt"
if cmp -s "$pages/write-dom.serialized" "$work/ser.txt"; then echo "the same $(wc -c < "$work/ser.txt" | tr -d ' ') bytes"
else diff "$pages/write-dom.serialized" "$work/ser.txt"; fi
echo "-- the scripts inserted and written, and the writes:"
amb query "$pages/inserted.html" "script_inserted(R, How), script_run(R, _, S, O)"
amb query "$pages/inserted.html" "script_write(W, R, When, Bytes)"
amb query "$pages/write-async.html" "script_write(W, R, When, Bytes)"
echo "-- what the reasoner says of them:"
amb advise "$pages/inserted.html" | grep -E '^amb_diag\((write|script_failed)'
amb advise "$pages/write-async.html" | grep -E '^amb_diag\(write'
kill $pid
rm -rf "$work"
