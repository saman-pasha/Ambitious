#!/bin/sh
# tools/bench/net-load.sh [N] [BEFORE] -- `ambitious load` of tests/net/site/h2/page.html (a
# page and thirty parts) over https from tests/net/h2serve.js, N runs of each (10 by
# default), each a process of its own, the runs interleaved: over HTTP/2 (one connection, the
# parts concurrent streams) and over HTTP/1.1 kept alive (the server with --h1: six
# connections at most); with BEFORE, a build/ambitious made before Phase 12q (it offers
# http/1.1 alone, a connection and a TLS handshake a request, one after another), that
# binary against the HTTP/2 server too. Prints each one's min, median and mean wall time, and
# how many connections each server saw a load. docs/performance.md has the numbers.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export AMB_ROOT
. "$AMB_ROOT/toolchain/env.sh"
n=${1:-10}
before=$2
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-netload.XXXXXX")
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
export AMB_WORK=$work
sh "$AMB_ROOT/tests/net/certs.sh" "$work/ca" > /dev/null 2>&1
export SSL_CERT_FILE=$work/ca/ca.crt
serve() {   # serve NAME OPTIONS...: the server started, its port echoed
  name=$1
  shift
  node "$AMB_ROOT/tests/net/h2serve.js" --tls "$work/ca/srv.crt" "$work/ca/srv.key" "$@" "$AMB_ROOT/tests/net/site" "$AMB_ROOT/tests/site" \
    > "$work/$name.port" 2> /dev/null &
  echo $! >> "$work/pids"
  i=0
  while [ ! -s "$work/$name.port" ] && [ $i -lt 40 ]; do sleep 0.25; i=$((i + 1)); done
  head -1 "$work/$name.port"
}
h2=$(serve h2 --log "$work/h2.log")
h1=$(serve h1 --h1 --log "$work/h1.log")
run() {   # run LABEL BINARY URL: one load, its milliseconds appended to $work/LABEL
  a=$(date +%s%N)
  "$2" load "$3" > /dev/null 2>&1
  b=$(date +%s%N)
  echo $(((b - a) / 1000000)) >> "$work/$1"
}
labels="h2 h1"
[ -n "$before" ] && labels="before h2 h1"
# a load of each first, not counted
for l in $labels; do
  case $l in before) b=$before u=$h2 ;; h2) b=$AMB_ROOT/build/ambitious u=$h2 ;; h1) b=$AMB_ROOT/build/ambitious u=$h1 ;; esac
  "$b" load "https://localhost:$u/h2/page.html" > /dev/null 2>&1
done
: > "$work/h2.log"
: > "$work/h1.log"
k=0
while [ $k -lt "$n" ]; do
  for l in $labels; do
    case $l in before) b=$before u=$h2 ;; h2) b=$AMB_ROOT/build/ambitious u=$h2 ;; h1) b=$AMB_ROOT/build/ambitious u=$h1 ;; esac
    run "$l" "$b" "https://localhost:$u/h2/page.html"
  done
  k=$((k + 1))
done
for l in $labels; do
  sort -n "$work/$l" | awk -v l="$l" '{ v[NR] = $1; s += $1 }
    END { printf "%-7s min %d ms, median %d ms, mean %.1f ms over %d loads\n", l, v[1], NR % 2 ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2, s / NR, NR }'
done
per() { awk -v n="$n" -v p="$1" '$1 == "conn" && NF == 3 && $3 == p { c++ } END { printf "%.1f", c / n }' "$2"; }
echo "connections a load: over HTTP/2 $(per h2 "$work/h2.log"), over HTTP/1.1 kept alive $(per http/1.1 "$work/h1.log")"
[ -n "$before" ] && echo "connections a load of the binary before: $(per http/1.1 "$work/h2.log") (http/1.1, the HTTP/2 server's)"
for p in $(cat "$work/pids"); do kill "$p" 2> /dev/null; done
rm -rf "$work"
