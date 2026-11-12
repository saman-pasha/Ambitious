#!/bin/sh
# tests/net/reference.sh [cookies] [cache] [srcset] [picture] [h2] -- Chromium's side of
# tests/integ/net, tests/integ/net-h2 and tests/integ/picture, made with Playwright's
# headless shell against the same servers, and written where those tests compare
# themselves with it (each server's log as tests/net/canon.sh, or for h2
# tests/net/h2canon.sh, reads it):
#
#   cookies  tests/net/cookies.chromium: the five visits of tests/net/cookies.js, and
#            the Cookie header each request carried
#   cache    tests/net/cache.chromium: the three visits of tests/net/cache.js over one
#            HTTP cache, and which requests reached the server, with their validators
#   srcset   tests/net/srcset.chromium: the candidate each img of tests/net/site/s/
#            page.html takes in an 800x600 viewport at 1x (tests/net/srcset.js)
#   picture  tests/net/picture.chromium: the image each img of tests/net/site/p/page.html
#            takes from its picture's sources, its natural size and its box
#            (tests/net/picture.js), the page read as a file
#   h2       tests/net/h2.chromium: one visit of tests/net/site/h2/page.html, thirty parts
#            over HTTP/2 from tests/net/h2serve.js (tests/net/cookies.js makes it), and
#            each request's HEADERS priority and header block as the server decoded them.
#            The script's frame may carry MEDIUM's weight (220) where its field says LOW,
#            when Blink raises the preloaded script before its stream opens: a race (one
#            run of three here); keep a run whose weight is its field's (183)
#
# Each of the first two waits out a lifetime of 60 s between two visits, so a run takes
# a minute or two. Run by hand when the visits change; all five by default.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-ref.XXXXXX")
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
what=${*:-cookies cache srcset picture h2}
serve() {   # serve ARGS...: tests/net/serve.py started, its port echoed
  n=$(($(cat "$work/pids" 2> /dev/null | wc -l) + 1))
  python3 "$AMB_ROOT/tests/net/serve.py" "$@" > "$work/$n.port" &
  echo $! >> "$work/pids"
  sleep 1
  head -1 "$work/$n.port"
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
  srcset)
    sport=$(serve "$AMB_ROOT/tests/net/site" "$AMB_ROOT/tests/site")
    node "$AMB_ROOT/tests/net/srcset.js" "http://127.0.0.1:$sport/s/page.html" > "$AMB_ROOT/tests/net/srcset.chromium"
    cat "$AMB_ROOT/tests/net/srcset.chromium" ;;
  picture)
    node "$AMB_ROOT/tests/net/picture.js" "$AMB_ROOT/tests/net/site/p/page.html" > "$AMB_ROOT/tests/net/picture.chromium"
    cat "$AMB_ROOT/tests/net/picture.chromium" ;;
  h2)
    [ -f "$work/ca/srv.crt" ] || sh "$AMB_ROOT/tests/net/certs.sh" "$work/ca"
    node "$AMB_ROOT/tests/net/h2serve.js" --tls "$work/ca/srv.crt" "$work/ca/srv.key" --hold 30 --log "$work/h2.log" --log-headers --log-frames \
      "$AMB_ROOT/tests/net/site" "$AMB_ROOT/tests/site" > "$work/h2.port" &
    echo $! >> "$work/pids"
    i=0
    while [ ! -s "$work/h2.port" ] && [ $i -lt 40 ]; do sleep 0.25; i=$((i + 1)); done
    hport=$(head -1 "$work/h2.port")
    node "$AMB_ROOT/tests/net/cookies.js" "$work/h2.log" "https://localhost:$hport/h2/page.html"
    sed -e "s/:$hport\b/:H2/g" "$work/h2.log" | sh "$AMB_ROOT/tests/net/h2canon.sh" > "$AMB_ROOT/tests/net/h2.chromium"
    cat "$AMB_ROOT/tests/net/h2.chromium" ;;
  esac
done
for p in $(cat "$work/pids" 2> /dev/null); do kill "$p" 2> /dev/null; done
rm -rf "$work"
