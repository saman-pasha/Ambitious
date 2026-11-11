#!/bin/sh
# tests/net/reference.sh [cookies] [cache] -- Chromium's side of tests/integ/net, made
# with Playwright's headless shell against the same servers, and written where that
# test compares itself with it (each server's log as tests/net/canon.sh reads it):
#
#   cookies  tests/net/cookies.chromium: the five visits of tests/net/cookies.js, and
#            the Cookie header each request carried
#   cache    tests/net/cache.chromium: the three visits of tests/net/cache.js over one
#            HTTP cache, and which requests reached the server, with their validators
#
# Each waits out a lifetime of 60 s between two visits, so a run takes a minute or
# two. Run by hand when the visits change. Both, by default.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-ref.XXXXXX")
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
what=${*:-cookies cache}
serve() {   # serve NAME ARGS...: tests/net/serve.py started, its port echoed
  python3 "$AMB_ROOT/tests/net/serve.py" "$@" > "$work/$1.port.tmp" &
  echo $! >> "$work/pids"
  sleep 1
  head -1 "$work/$1.port.tmp"
}
for w in $what; do
  case $w in
  cookies)
    sh "$AMB_ROOT/tests/net/certs.sh" "$work/ca"
    port=$(serve --log "$work/cookies.log" "$AMB_ROOT/tests/site" "$AMB_ROOT/tests/net/site")
    tls=$(serve --tls "$work/ca/srv.crt" "$work/ca/srv.key" --log "$work/cookies.log" "$AMB_ROOT/tests/net/site" "$AMB_ROOT/tests/site")
    node "$AMB_ROOT/tests/net/cookies.js" "$work/cookies.log" "http://localhost:$port/c/home.html" "http://127.0.0.1:$port/c/page.html" \
      "61000+http://127.0.0.1:$port/c/again.html" "https://localhost:$tls/c/secure.html" "http://localhost:$port/c/home.html"
    sed -e "s/:$port\b/:PORT/g" -e "s/:$tls\b/:TLS/g" "$work/cookies.log" | sh "$AMB_ROOT/tests/net/canon.sh" > "$AMB_ROOT/tests/net/cookies.chromium"
    cat "$AMB_ROOT/tests/net/cookies.chromium" ;;
  cache)
    mkdir -p "$work/kroot/k"
    printf '.changed { color: red }\n' > "$work/kroot/k/changed.css"
    printf 'Cache-Control: no-cache\nETag: auto\n' > "$work/kroot/k/changed.css.headers"
    kport=$(serve --log "$work/cache.log" "$work/kroot" "$AMB_ROOT/tests/net/site" "$AMB_ROOT/tests/site")
    node "$AMB_ROOT/tests/net/cache.js" "$work/cache.log" "$work/chromium" 61000 "http://127.0.0.1:$kport/k/page.html" "$work/kroot/k/changed.css"
    sed -e "s/:$kport\b/:CACHE/g" "$work/cache.log" | sh "$AMB_ROOT/tests/net/canon.sh" > "$AMB_ROOT/tests/net/cache.chromium"
    cat "$AMB_ROOT/tests/net/cache.chromium" ;;
  esac
done
for p in $(cat "$work/pids"); do kill "$p" 2> /dev/null; done
rm -rf "$work"
