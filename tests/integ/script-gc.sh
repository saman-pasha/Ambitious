#!/bin/sh
# tests/integ/script-gc.sh -- the collector for a page's scripts (src/js/gc.c), its roots the
# values the host holds (src/script/script.h says which). Each gate of the pages' scripts
# again, with AMB_GC_STRESS=1: the collector runs at every moment it may (each script's
# end, each timer's and host task's, and a running script's loops' turns each 256
# allocations), so a value the host holds but gives no root for is
# collected while the host still names it, and a console or a fact changes. Each gate's
# output must be its .expect's, as without the stress (the pages' consoles are Chromium's
# line for line). Then gc-host.html, each of whose values the host alone holds when a
# later task uses it (an observer, a custom element's class and callback, whenDefined's
# promises, a CustomEvent's detail, a Response's Headers, Headers and FormData lists by
# their objects, an XMLHttpRequest, prototypes whose globals are gone, a history state):
# its console against Chromium's (tests/pages/gc-host.chromium, from tools/bench/console.js),
# without the stress and with it. Then the pages tests/integ/script-serve.py serves, with
# the stress, under valgrind's memcheck where it is installed (as the exec gate runs its
# programs): each run's exit status, 0 when it is clean (valgrind's errors exit 9).
# tests/exec/script_gc runs every page read from its file so, under valgrind too.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-sgc.XXXXXX")
export AMB_WORK=$work
echo "-- each gate of the pages' scripts, a collection at every moment between tasks, against its .expect:"
for g in script script-cssom script-custom script-input script-modules script-net script-observe script-view script-window foreign; do
  AMB_GC_STRESS=1 sh "$AMB_ROOT/tests/integ/$g.sh" > "$work/$g.out" 2>&1 < /dev/null
  if cmp -s "$AMB_ROOT/tests/integ/$g.expect" "$work/$g.out"; then echo "$g: as its .expect"
  else echo "$g differs from its .expect:"; diff "$AMB_ROOT/tests/integ/$g.expect" "$work/$g.out" | head -20; fi
done
page="$AMB_ROOT/tests/pages/gc-host.html"
# the page's console as Chromium's is written: its messages, then its uncaught errors
console() {
  sh "$AMB_ROOT/bin/ambitious" script "$page" < /dev/null > "$work/s.txt" 2>&1
  sed -n -e 's/^console [a-z]* *\[[0-9]*\] //p' "$work/s.txt"
  sed -n -e 's/^error *[a-z]* *\[[0-9]*\] /error: /p' "$work/s.txt"
}
same() {
  if cmp -s "$1" "$2"; then echo "the same $(wc -l < "$2" | tr -d ' ') lines"
  else diff "$1" "$2"; fi
}
echo "-- the values the host alone holds (gc-host.html), against Chromium's console:"
console > "$work/c.txt"
same "$AMB_ROOT/tests/pages/gc-host.chromium" "$work/c.txt"
echo "-- and with a collection at every moment between tasks:"
AMB_GC_STRESS=1
export AMB_GC_STRESS
console > "$work/c.txt"
same "$AMB_ROOT/tests/pages/gc-host.chromium" "$work/c.txt"
echo "-- the collections that made, one at each script's end, each task's and each timer's, and one at its loops' turns each 256 allocations:"
AMB_GC_DEBUG=1 sh "$AMB_ROOT/bin/ambitious" script "$page" < /dev/null 2>&1 >/dev/null | grep -c '^gc [0-9]*:'
unset AMB_GC_STRESS
echo "-- and without the stress, none (the page makes less than the 32 MB a first collection waits for):"
AMB_GC_DEBUG=1 sh "$AMB_ROOT/bin/ambitious" script "$page" < /dev/null 2>&1 >/dev/null | grep -c '^gc [0-9]*:'
echo "-- the served pages with the stress, under valgrind where it is installed: each run's exit status:"
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
pages="$AMB_ROOT/tests/pages"
python3 "$AMB_ROOT/tests/integ/script-serve.py" "$pages" > "$work/port" 2> "$work/serve.err" &
pid=$!
i=0
while [ ! -s "$work/port" ] && [ $i -lt 240 ]; do sleep 0.25; i=$((i + 1)); done
port=$(head -1 "$work/port")
. "$AMB_ROOT/toolchain/env.sh"
vg=""
command -v valgrind > /dev/null 2>&1 && vg="valgrind -q --error-exitcode=9"
served() {   # its name, then the command's arguments
  name=$1
  shift
  AMB_GC_STRESS=1 timeout 600 $vg "$AMB_ROOT/build/ambitious" "$@" > /dev/null 2> "$work/$name.vg" < /dev/null
  rc=$?
  echo "$name: exit $rc"
  [ "$rc" -ne 0 ] && head -20 "$work/$name.vg"
}
for p in fetch window modules modules-cross; do served "$p" script "http://127.0.0.1:$port/$p.html"; done
served "form, sent" --click '#go' script "http://127.0.0.1:$port/form.html"
served "form, multipart" --click '#run' script "http://127.0.0.1:$port/form.html?multipart"
served "form, formdata" --click '#go' script "http://127.0.0.1:$port/form.html?formdata"
kill $pid
rm -rf "$work"
