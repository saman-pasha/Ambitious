#!/bin/sh
# tools/bench/script-references.sh -- the Chromium references of the script module's
# fixture pages (Phase 12h), written again: each tests/pages/NAME.chromium (the console,
# by tools/bench/console.js for a page read from its file, by tools/bench/console-actions.js
# for one served by tests/integ/script-serve.py or one a user acts on, the actions it is
# given after it: NAME.actions), and each NAME.serialized (the document after its scripts,
# by tools/bench/serialize.js). A served page's port is written PORT. form.chromium holds,
# for each line of form.actions (the page's query, then its actions), the console and the
# requests the server had after the page's own, a form's multipart boundary written
# BOUNDARY. Review the diff before keeping it.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
pages=$AMB_ROOT/tests/pages
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-refs.XXXXXX")
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
for p in lists geometry thrash inserted inserted-order write-async observer observer-unsettled; do
  node "$AMB_ROOT/tools/bench/console.js" "$pages/$p.html" > "$pages/$p.chromium" && echo "wrote $p.chromium"
done
for p in write-dom; do
  node "$AMB_ROOT/tools/bench/serialize.js" "$pages/$p.html" > "$pages/$p.serialized" && echo "wrote $p.serialized"
done
for p in events keys; do
  eval "set -- $(cat "$pages/$p.actions")"
  node "$AMB_ROOT/tools/bench/console-actions.js" "$pages/$p.html" "$@" < /dev/null > "$pages/$p.chromium" && echo "wrote $p.chromium"
done
python3 "$AMB_ROOT/tests/integ/script-serve.py" --log "$work/req.log" "$pages" > "$work/port" 2> "$work/serve.err" &
pid=$!
i=0
while [ ! -s "$work/port" ] && [ $i -lt 40 ]; do sleep 0.25; i=$((i + 1)); done
port=$(head -1 "$work/port")
served() { name=$1; shift; node "$AMB_ROOT/tools/bench/console-actions.js" "http://127.0.0.1:$port/$name.html" "$@" | sed "s/:$port\b/:PORT/g" > "$pages/$name.chromium" && echo "wrote $name.chromium"; }
served fetch
norm() { sed -e "s/:$port\b/:PORT/g" -e 's/----[A-Za-z]*FormBoundary[A-Za-z0-9]*/BOUNDARY/g'; }
: > "$work/form.txt"
while read -r q acts; do
  : > "$work/req.log"
  echo "== $q $acts" >> "$work/form.txt"
  url="http://127.0.0.1:$port/form.html"
  [ "$q" != - ] && url="$url?$q"
  eval "set -- $acts"
  node "$AMB_ROOT/tools/bench/console-actions.js" "$url" "$@" < /dev/null | norm >> "$work/form.txt"
  grep -v 'favicon\|^GET /form.html' "$work/req.log" | norm >> "$work/form.txt"
done < "$pages/form.actions"
cp "$work/form.txt" "$pages/form.chromium" && echo "wrote form.chromium"
kill $pid
rm -rf "$work"
