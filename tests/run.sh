#!/bin/sh
# tests/run.sh SUITE -- one gate of `make test', run under time caps.
#
#   gen      the generated C text (tools/amb ctext) and the reasoning schema of
#            each contract, against tests/golden/
#   exec     each tests/exec/NAME.c built with the modules, run, its output
#            compared with NAME.expect; under valgrind too where it is installed
#   safe     each tests/safe/NAME.c must be REFUSED by cicilang, with the
#            diagnostic NAME.expect names
#   reason   each tests/reason/NAME.pl run by cocolog, its output against NAME.expect
#   integ    each tests/integ/NAME.sh run, its output against NAME.expect
#   js       each tests/js/NAME.js run by the engine (ambitious js), its output
#            against NAME.expect, which V8 wrote (node NAME.js > NAME.expect)
#
# A RED line names what differs; the exit status is 1 on any RED.
AMB_ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$AMB_ROOT/toolchain/env.sh"
cd "$AMB_ROOT" || exit 1
B=$AMB_ROOT/build
OUT=$B/test-out
mkdir -p "$OUT"
red=0
pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; red=1; }
CICILANG_FLAGS=$AMB_CICILANG_FLAGS

case "$1" in
gen)
  for c in contracts/*.pl; do
    m=$(basename "$c" .pl)
    for kind in ctext schema; do
      want=tests/golden/$m.$kind
      got=$OUT/$m.$kind
      sh tools/amb "$kind" "$m" > "$got" 2>&1
      if [ ! -f "$want" ]; then fail "$m $kind: no golden at $want (make golden writes it)"
      elif cmp -s "$want" "$got"; then pass "$m $kind matches its golden"
      else fail "$m $kind differs from its golden:"; diff "$want" "$got" | head -20; fi
    done
  done ;;
exec)
  for t in tests/exec/*.c; do [ -e "$t" ] || continue
    n=$(basename "$t" .c)
    if ! timeout 300 "$AMB_CICILANG" $CICILANG_FLAGS "$t" $AMB_OBJS -o "$B/test-$n" > "$OUT/$n.build" 2>&1 < /dev/null; then
      fail "$n does not build:"; head -5 "$OUT/$n.build"; continue
    fi
    timeout 120 "$B/test-$n" > "$OUT/$n.out" 2>&1 < /dev/null
    if cmp -s "tests/exec/$n.expect" "$OUT/$n.out"; then pass "$n prints its .expect"
    else fail "$n differs from its .expect:"; diff "tests/exec/$n.expect" "$OUT/$n.out" | head -20; fi
    if command -v valgrind > /dev/null 2>&1; then
      if timeout 300 valgrind -q --leak-check=full --errors-for-leak-kinds=all --error-exitcode=9 "$B/test-$n" > /dev/null 2> "$OUT/$n.vg" < /dev/null; then
        pass "$n under valgrind: no error, no leak"
      else fail "$n under valgrind:"; head -10 "$OUT/$n.vg"; fi
    fi
  done ;;
safe)
  for t in tests/safe/*.c; do [ -e "$t" ] || continue
    n=$(basename "$t" .c)
    want=$(cat "tests/safe/$n.expect")
    if timeout 300 "$AMB_CICILANG" $CICILANG_FLAGS -fsyntax-only "$t" > "$OUT/$n.safe" 2>&1 < /dev/null; then
      fail "$n was accepted: it must be refused with '$want'"
    elif grep -qF -- "$want" "$OUT/$n.safe"; then pass "$n refused: $want"
    else fail "$n refused, but not with '$want':"; head -5 "$OUT/$n.safe"; fi
  done ;;
reason)
  for t in tests/reason/*.pl; do [ -e "$t" ] || continue
    n=$(basename "$t" .pl)
    timeout 120 "$AMB_COCOLOG" -s "$t" > "$OUT/$n.reason" 2>&1 < /dev/null
    if cmp -s "tests/reason/$n.expect" "$OUT/$n.reason"; then pass "$n prints its .expect"
    else fail "$n differs from its .expect:"; diff "tests/reason/$n.expect" "$OUT/$n.reason" | head -20; fi
  done ;;
html)
  if ! timeout 300 "$AMB_CICILANG" $CICILANG_FLAGS -O2 tests/html/html5lib.c $AMB_OBJS -o "$B/test-html5lib" > "$OUT/html5lib.build" 2>&1 < /dev/null; then
    fail "the html5lib runner does not build:"; head -5 "$OUT/html5lib.build"
  else
    timeout 300 "$B/test-html5lib" tests/html/basic.dat > "$OUT/basic.html" 2>&1 < /dev/null
    if cmp -s tests/html/basic.expect "$OUT/basic.html"; then pass "the HTML fixtures: $(tail -1 "$OUT/basic.html")"
    else fail "the HTML fixtures differ from tests/html/basic.expect:"; diff tests/html/basic.expect "$OUT/basic.html" | head -20; fi
    if [ -d "$AMB_HTML5LIB" ]; then
      timeout 600 "$B/test-html5lib" "$AMB_HTML5LIB"/*.dat > "$OUT/html5lib.txt" 2>&1 < /dev/null
      if cmp -s tests/html/html5lib.expect "$OUT/html5lib.txt"; then pass "html5lib: $(tail -1 "$OUT/html5lib.txt")"
      else fail "html5lib's results differ from tests/html/html5lib.expect (a change, better or worse, is recorded there):"; diff tests/html/html5lib.expect "$OUT/html5lib.txt" | head -20; fi
    else
      printf 'SKIP html5lib: no tests at %s (toolchain/bootstrap.sh checks them out; or set AMB_HTML5LIB)\n' "$AMB_HTML5LIB"
    fi
  fi ;;
integ)
  for t in tests/integ/*.sh; do [ -e "$t" ] || continue
    n=$(basename "$t" .sh)
    timeout 600 sh "$t" > "$OUT/$n.integ" 2>&1 < /dev/null
    if cmp -s "tests/integ/$n.expect" "$OUT/$n.integ"; then pass "$n prints its .expect"
    else fail "$n differs from its .expect:"; diff "tests/integ/$n.expect" "$OUT/$n.integ" | head -20; fi
  done ;;
js)
  for t in tests/js/*.js; do [ -e "$t" ] || continue
    n=$(basename "$t" .js)
    timeout 120 "$B/ambitious" js "$t" > "$OUT/$n.js.out" 2>&1 < /dev/null
    if cmp -s "tests/js/$n.expect" "$OUT/$n.js.out"; then pass "$n prints what V8 prints"
    else fail "$n differs from V8's output:"; diff "tests/js/$n.expect" "$OUT/$n.js.out" | head -20; fi
  done ;;
*) echo "usage: tests/run.sh gen|exec|safe|reason|integ|js" >&2; exit 2 ;;
esac
if [ "$red" -eq 0 ]; then echo "GREEN: $1"; else echo "RED: $1"; fi
exit "$red"
