#!/bin/sh
# tools/bench/script-references.sh -- the Chromium references of the script module's
# fixture pages (Phase 12h), written again: each tests/pages/NAME.chromium (the console,
# by tools/bench/console.js for a page read from its file, by tools/bench/console-actions.js
# for one served by tests/integ/script-serve.py, the actions it is given after it), and each
# NAME.serialized (the document after its scripts, by tools/bench/serialize.js). A served
# page's port is written PORT. Review the diff before keeping it.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
pages=$AMB_ROOT/tests/pages
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-refs.XXXXXX")
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
for p in lists geometry thrash inserted inserted-order write-async; do
  node "$AMB_ROOT/tools/bench/console.js" "$pages/$p.html" > "$pages/$p.chromium" && echo "wrote $p.chromium"
done
for p in write-dom; do
  node "$AMB_ROOT/tools/bench/serialize.js" "$pages/$p.html" > "$pages/$p.serialized" && echo "wrote $p.serialized"
done
python3 "$AMB_ROOT/tests/integ/script-serve.py" "$pages" > "$work/port" &
pid=$!
i=0
while [ ! -s "$work/port" ] && [ $i -lt 40 ]; do sleep 0.25; i=$((i + 1)); done
port=$(head -1 "$work/port")
served() { name=$1; shift; node "$AMB_ROOT/tools/bench/console-actions.js" "http://127.0.0.1:$port/$name.html" "$@" | sed "s/:$port\b/:PORT/g" > "$pages/$name.chromium" && echo "wrote $name.chromium"; }
served fetch
kill $pid
rm -rf "$work"
