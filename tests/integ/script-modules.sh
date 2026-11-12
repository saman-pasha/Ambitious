#!/bin/sh
# tests/integ/script-modules.sh -- module scripts (HTML 8.1.5) and import maps, the pages'
# consoles held to Chromium's line for line (tests/pages/NAME.chromium, written by
# tools/bench/script-references.sh):
#   modules.html, served by tests/integ/script-serve.py: an import map with a scope; an
#     inline module importing a mapped name, a relative module, a mapped prefix's module
#     (a namespace import) and a default export; an external one importing the same
#     module by another specifier (evaluated once); import.meta.url; nomodule passed
#     over; the modules run after the parse, in order with a deferred script; a scope's
#     own mapping; a bare specifier no map maps and a module that throws, reported as
#     the scripts' errors; top-level await; an import() from the load event;
#   modules-cross.html: another origin's module refused (no CORS header read, each URL
#     once), a text/plain module refused by the strict MIME check, an async inline
#     module run as soon as it can;
#   modules.html read from its file: each module's fetch refused (a file's origin is
#     opaque), the same lines as Chromium's in any order (its order is the network's).
# Then what the reasoner has of the module scripts' runs. The served pages' port is
# written PORT, the checkout ROOT.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-smod.XXXXXX")
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
export AMB_WORK=$work
pages="$AMB_ROOT/tests/pages"
python3 "$AMB_ROOT/tests/integ/script-serve.py" --log "$work/req.log" "$pages" > "$work/port" 2> "$work/serve.err" &
pid=$!
i=0
while [ ! -s "$work/port" ] && [ $i -lt 240 ]; do sleep 0.25; i=$((i + 1)); done
port=$(head -1 "$work/port")
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | sed -e "s|$AMB_ROOT|ROOT|g" -e "s/:$port\b/:PORT/g"; }
# a page's console as Chromium's is written: its messages, then its uncaught errors
console() { amb script "$1" > "$work/s.txt"; sed -n -e 's/^console [a-z]* *\[[0-9]*\] //p' "$work/s.txt" > "$work/c.txt"; sed -n -e 's/^error *[a-z]* *\[[0-9]*\] /error: /p' "$work/s.txt" >> "$work/c.txt"; }
same() {
  if cmp -s "$1" "$2"; then echo "the same $(wc -l < "$2" | tr -d ' ') lines"
  else diff "$1" "$2"; fi
}
for p in modules modules-cross; do
  echo "-- the $p page's console, against Chromium's:"
  console "http://127.0.0.1:$port/$p.html"
  same "$pages/$p.chromium" "$work/c.txt"
done
echo "-- the modules page read from its file, against Chromium's, in any order:"
console "$pages/modules.html"
LC_ALL=C sort "$work/c.txt" > "$work/sorted.txt"
same "$pages/modules-file.chromium" "$work/sorted.txt"
echo "-- the module scripts' runs, as the reasoner has them:"
amb query "http://127.0.0.1:$port/modules.html" "aggregate_all(count, script_run(_, _, _, ran), Ran), aggregate_all(count, script_run(_, _, _, threw), Threw), aggregate_all(count, script_run(_, _, _, not_run), NotRun)"
amb query "http://127.0.0.1:$port/modules-cross.html" "script_run(R, _, external, O)"
kill $pid
rm -rf "$work"
