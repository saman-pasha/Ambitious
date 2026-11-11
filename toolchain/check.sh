#!/bin/sh
# toolchain/check.sh -- is this box's toolchain the one Ambitious was verified on?
#
#   sh toolchain/check.sh
#
# Each neighbour's checkout and version against toolchain/pins, then what
# Ambitious needs of them, each run under a time cap: cocolog answers, cicilang
# compiles and runs a ccllib program in memory (--no-kb, as every Ambitious
# build does), and the contract tool validates a contract. Exit 1 on any RED;
# a checkout off its pin is reported and is not RED (cocolog moves on its
# owner's rule: the pin moves when the gates are run over the new commit).
AMB_ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$AMB_ROOT/toolchain/env.sh"
. "$AMB_ROOT/toolchain/pins"
red=0
ok()   { printf 'ok       %-10s %s\n' "$1" "$2"; }
off()  { printf 'DIFFERS  %-10s %s\n' "$1" "$2"; }
bad()  { printf 'RED      %-10s %s\n' "$1" "$2"; red=1; }

pin() {   # $1 name, $2 checkout, $3 pinned commit
  if [ ! -d "$2/.git" ]; then bad "$1" "no checkout at $2 (set AMB_DEPS or $1)"; return; fi
  h=$(git -C "$2" rev-parse HEAD 2>/dev/null)
  case "$h" in "$3"*) ok "$1" "$2 at ${h%"${h#???????}"}" ;; *) off "$1" "$2 is at ${h%"${h#???????}"}, the pin is ${3%"${3#???????}"}" ;; esac
}
pin cicili    "$CICILI"    "$CICILI_COMMIT"
pin ziguratip "$ZIGURATIP" "$ZIGURATIP_COMMIT"
pin cocolog   "$COCOLOG"   "$COCOLOG_COMMIT"
pin cicilang  "$CICILANG"  "$CICILANG_COMMIT"

for lib in Core MVCCS; do
  [ -f "$ZIGURATIP_HOME/lib/lib$lib.so" ] || bad ziguratip "no lib$lib.so in $ZIGURATIP_HOME/lib (toolchain/bootstrap.sh builds it)"
done

if [ -x "$AMB_COCOLOG" ]; then
  v=$(timeout 30 "$AMB_COCOLOG" --version < /dev/null 2>/dev/null)
  a=$(timeout 30 "$AMB_COCOLOG" query "X is 6*7, write(X), nl" < /dev/null 2>&1 | head -1)
  case "$v" in *"$COCOLOG_VERSION"*) ok cocolog "$v" ;; *) off cocolog "$v, the pin is $COCOLOG_VERSION" ;; esac
  [ "$a" = 42 ] && ok cocolog "answers 42" || bad cocolog "does not answer: $a"
else
  bad cocolog "no binary at $AMB_COCOLOG"
fi

if [ -x "$AMB_CICILANG" ]; then
  v=$(timeout 60 "$AMB_CICILANG" --version < /dev/null 2>/dev/null)
  case "$v" in *"cicilang $CICILANG_VERSION"*"LLVM $LLVM_MAJOR"*) ok cicilang "$v" ;; "") bad cicilang "no answer to --version (are library/cicilang.so and ccl_llvm.so built?)" ;; *) off cicilang "$v" ;; esac
  t=$(mktemp -d "${TMPDIR:-/tmp}/ambcheck.XXXXXX")
  cat > "$t/smoke.c" <<'EOF'
#include <stdio.h>
#include <vector.pl>
int main(void) {
    vector<int> v;
    for (int i = 1; i <= 4; i++) push_back(v, i * i);
    printf("ccllib %zu %d\n", len(v), back(v));
    return 0;
}
EOF
  if timeout 120 "$AMB_CICILANG" --no-kb "$t/smoke.c" -o "$t/smoke" < /dev/null > "$t/build.log" 2>&1; then
    out=$("$t/smoke")
    [ "$out" = "ccllib 4 16" ] && ok cicilang "builds and runs a ccllib program (--no-kb)" || bad cicilang "the ccllib program printed: $out"
  else
    bad cicilang "cannot build a ccllib program: $(head -3 "$t/build.log" | tr '\n' ' ')"
  fi
  rm -rf "$t"
else
  bad cicilang "no command at $AMB_CICILANG"
fi

if [ -x "$AMB_COCOLOG" ]; then
  r=$(timeout 60 "$AMB_COCOLOG" -s "$AMB_ROOT/prolog/amb/tool.pl" -- validate dom < /dev/null 2>&1 | tail -1)
  case "$r" in *": ok "*) ok contracts "the DOM contract validates" ;; *) bad contracts "$r" ;; esac
fi

if [ "$red" -eq 0 ]; then echo "GREEN: the toolchain"; else echo "RED: the toolchain"; fi
exit "$red"
