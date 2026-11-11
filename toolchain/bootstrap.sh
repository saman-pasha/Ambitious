#!/bin/sh
# toolchain/bootstrap.sh -- the toolchain Ambitious builds on, from nothing to a
# cicilang that compiles a ccllib program, on Debian or Ubuntu:
#
#   sh toolchain/bootstrap.sh                  packages (as root), checkouts, builds
#   NO_PACKAGES=1 sh toolchain/bootstrap.sh    the packages are there already
#   AMB_DEPS=/path sh toolchain/bootstrap.sh   where the four checkouts go
#                                              (default ~/Projects/GitHub)
#
# It does what cocolog's own install/install-linux.sh does, with one difference:
# Cicili's four Lisp systems come from the distribution and from shims, never
# from a Quicklisp download -- cl-ppcre from Debian's cl-ppcre, sha1 and base64
# from cocolog's colab/lisp shims, and str from toolchain/lisp/str, which holds
# the ten functions of cl-str that Cicili calls. Each neighbour is checked out
# at its pin (toolchain/pins) and built by its own build:
#
#   ZiguratIP   make MODE=Release                  (needs Cicili for MVCCS)
#   cocolog     make; make schema; make modules    (needs Cicili and ZiguratIP)
#   cicilang    module/build.sh; module/build-llvm.sh  (needs Cicili, cocolog, LLVM)
#
# Idempotent: a checkout at its pin is kept, a build re-run is incremental.
set -eu
AMB_ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$AMB_ROOT/toolchain/env.sh"
. "$AMB_ROOT/toolchain/pins"
LOG=${LOG:-${TMPDIR:-/tmp}/ambitious-bootstrap}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 2)}
step() { printf '== %s\n' "$*"; }
say()  { printf '   %s\n' "$*"; }
die()  { printf 'BOOTSTRAP RED: %s\n' "$*" >&2; exit 1; }

if [ "${NO_PACKAGES:-0}" != 1 ]; then
  step "packages"
  SUDO=""; [ "$(id -u)" = 0 ] || SUDO=sudo
  command -v apt-get >/dev/null 2>&1 || die "no apt-get: install by hand what the next line names, then NO_PACKAGES=1"
  export DEBIAN_FRONTEND=noninteractive
  $SUDO apt-get -qq update
  $SUDO apt-get -qq install -y build-essential make git curl ca-certificates gnupg sbcl cl-ppcre \
        libtool-bin libssl-dev zlib1g-dev libcurl4-openssl-dev libpcre2-dev \
        clang-18 llvm-18-dev >/dev/null
  say "build-essential make git curl gnupg sbcl cl-ppcre libtool-bin libssl-dev zlib1g-dev libcurl4-openssl-dev libpcre2-dev clang-18 llvm-18-dev"
fi
for t in git make sbcl clang clang++; do command -v $t >/dev/null 2>&1 || die "$t is not on PATH"; done

step "the four checkouts, at their pins, under $AMB_DEPS"
mkdir -p "$AMB_DEPS"
checkout() {   # $1 directory, $2 repository, $3 commit
  if [ ! -d "$1/.git" ]; then
    git init -q "$1" && git -C "$1" remote add origin "$2"
  fi
  if [ "$(git -C "$1" rev-parse HEAD 2>/dev/null)" != "$3" ]; then
    git -C "$1" fetch -q --depth 1 origin "$3" && git -C "$1" -c advice.detachedHead=false checkout -q FETCH_HEAD
  fi
  [ "$(git -C "$1" rev-parse HEAD)" = "$3" ] || die "$1 is not at $3"
  say "$1 at $(git -C "$1" rev-parse --short HEAD)"
}
checkout "$CICILI"    https://github.com/saman-pasha/cicili.git    "$CICILI_COMMIT"
checkout "$ZIGURATIP" https://github.com/saman-pasha/ZiguratIP.git "$ZIGURATIP_COMMIT"
checkout "$COCOLOG"   https://github.com/saman-pasha/cocolog.git   "$COCOLOG_COMMIT"
checkout "$CICILANG"  https://github.com/saman-pasha/cicilang.git  "$CICILANG_COMMIT"
checkout "$AMB_DEPS/html5lib-tests" https://github.com/html5lib/html5lib-tests.git "$HTML5LIB_TESTS_COMMIT"

step "the Lisp systems Cicili is built from, with no download"
mkdir -p "$HOME/common-lisp"
cp -R "$COCOLOG/colab/lisp/sha1" "$COCOLOG/colab/lisp/base64" "$HOME/common-lisp/"
ln -sfn "$AMB_ROOT/toolchain/lisp/str" "$HOME/common-lisp/str"
ln -sfn "$CICILI" "$HOME/common-lisp/cicili"
v=$(cd "$CICILI" && sbcl --script cicili.lisp --version 2>/dev/null | tail -1)
[ "$v" = "$CICILI_VERSION" ] || die "Cicili answers '$v', the pin is $CICILI_VERSION"
sbcl --non-interactive --eval '(require "asdf")' --eval '(asdf:load-system "cicili")' >"$LOG.lisp" 2>&1 \
  || die "SBCL cannot load cicili (whole log: $LOG.lisp)"
say "Cicili $v loads: sha1, base64 (cocolog's shims), str (toolchain/lisp/str), cl-ppcre (Debian)"

step "ZiguratIP (Release)"
mkdir -p "$ZIGURATIP_HOME/data" "$ZIGURATIP_HOME/ld" "$ZIGURATIP_HOME/catalog" "$ZIGURATIP_HOME/log" \
         "$ZIGURATIP_HOME/tmp" "$ZIGURATIP_HOME/obj" "$ZIGURATIP_HOME/lib" "$ZIGURATIP_HOME/bin"
( cd "$ZIGURATIP" && make MODE=Release -j"$JOBS" ) >"$LOG.ziguratip" 2>&1 || true
missing=""
for lib in Core StreamIO Type Library Encoding Compression Cryptography Configuration \
           Threading SocketIO Connector HTTP MVCCS Compiler; do
  [ -f "$ZIGURATIP_HOME/lib/lib$lib.so" ] || missing="$missing lib$lib.so"
done
for b in parsi parsic ziguratip; do [ -x "$ZIGURATIP_HOME/bin/$b" ] || missing="$missing bin/$b"; done
[ -z "$missing" ] || die "ZiguratIP incomplete:$missing (whole log: $LOG.ziguratip)"
say "14 libraries and the executables in $ZIGURATIP_HOME"

step "cocolog"
( cd "$COCOLOG" && PATH="$COCOLOG/tools/cc:$PATH" make ) >"$LOG.cocolog" 2>&1 \
  || die "cocolog's make failed (whole log: $LOG.cocolog)"
( cd "$COCOLOG" && PATH="$COCOLOG/tools/cc:$PATH" make schema ) >"$LOG.schema" 2>&1 \
  || die "cocolog's make schema failed (whole log: $LOG.schema)"
( cd "$COCOLOG" && PATH="$COCOLOG/tools/cc:$PATH" make modules ) >"$LOG.modules" 2>&1 || true
ans=$("$AMB_COCOLOG" query "X is 6*7, write(X), nl" 2>&1 | tr -d '\r')
case "$ans" in *42*) say "cocolog $("$AMB_COCOLOG" --version 2>/dev/null) answers 42" ;; *) die "cocolog does not answer: $ans" ;; esac

step "cicilang's two modules"
( cd "$CICILANG" && sh module/build.sh ) >"$LOG.cicilang" 2>&1 \
  || die "cicilang's module/build.sh failed (whole log: $LOG.cicilang)"
( cd "$CICILANG" && LLVM="/usr/lib/llvm-$LLVM_MAJOR" sh module/build-llvm.sh ) >"$LOG.ccl_llvm" 2>&1 \
  || die "cicilang's module/build-llvm.sh failed (whole log: $LOG.ccl_llvm)"
say "$("$AMB_CICILANG" --version)"

step "check"
sh "$AMB_ROOT/toolchain/check.sh"
