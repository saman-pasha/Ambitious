#!/bin/sh
# tests/net/reference.sh [cookies] [cache] [srcset] [picture] [h2] [pool] [resume] [dict] [query] -- Chromium's side of
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
#   pool     tests/net/pool.chromium: tests/net/site/t/pool.html over HTTP/2 from h2serve.js at
#            a.localhost, its parts at other hosts of the server's certificate, addresses and
#            ports, visited directly and through tests/net/proxy.py; the connections each
#            server's log shows, each with the requests it carried (tests/net/tlscanon.sh pool).
#            Chromium is launched trusting the test certificates by their keys (tests/net/tls.js)
#   resume   tests/net/res.chromium: tests/net/site/t/res.html (parts at once) and seq.html
#            (requests one after another) over HTTP/1.1, each connection closed after its
#            response, from servers of TLS 1.3, of TLS 1.2 with tickets and of TLS 1.2 with
#            session IDs alone; how many of each server's connections resumed a session
#            (tests/net/tlscanon.sh resume)
#   dict     tests/net/dict.chromium: tests/net/site/dict/page.html then again.html in one browser,
#            over HTTP/2 from h2serve.js: each request's dictionary fields as the server decoded them
#            (tests/net/dictcanon.sh), then what each page's script wrote to the console (what each
#            fetch got, the second page's title and its sheet's colour)
#   query    tests/net/query.chromium: tests/net/site/q/w.html (windows-1252) and s.html (Shift_JIS)
#            from serve.py: each request's target as the server got it (tests/net/canon.sh), its
#            query in the page's encoding or in UTF-8, a fetched sheet's in the sheet's own
#
# Each of the first two waits out a lifetime of 60 s between two visits, so a run takes
# a minute or two. Run by hand when the visits change; all five by default.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-ref.XXXXXX")
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
what=${*:-cookies cache srcset picture h2 pool resume dict query}
# h2s NAME ARGS...: tests/net/h2serve.js started, its port in $work/NAME.port and echoed
h2s() {
  name=$1
  shift
  node "$AMB_ROOT/tests/net/h2serve.js" "$@" > "$work/$name.port" &
  echo $! >> "$work/pids"
  i=0
  while [ ! -s "$work/$name.port" ] && [ $i -lt 40 ]; do sleep 0.25; i=$((i + 1)); done
  head -1 "$work/$name.port"
}
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
  pool)
    [ -f "$work/ca/pool.crt" ] || sh "$AMB_ROOT/tests/net/certs.sh" "$work/ca"
    q=$(h2s q --tls "$work/ca/pool.crt" "$work/ca/pool.key" --log-tls --log "$work/q.log" "$AMB_ROOT/tests/net/site")
    p=$(h2s p --tls "$work/ca/pool.crt" "$work/ca/pool.key" --sni c.localhost "$work/ca/c.crt" "$work/ca/c.key" --bind 0.0.0.0 \
      --log-tls --log "$work/p.log" --misdirect '/top.png?misdirected' --misdirect-always '/top.png?always' --sub-file "@Q@=$work/q.port" \
      "$AMB_ROOT/tests/net/site")
    python3 "$AMB_ROOT/tests/net/proxy.py" "$work/proxy.log" > "$work/proxy.port" &
    echo $! >> "$work/pids"
    sleep 1
    x=$(head -1 "$work/proxy.port")
    node "$AMB_ROOT/tests/net/tls.js" "$work/p.log,$work/q.log" "$(cat "$work/ca/spki")" "https://a.localhost:$p/t/pool.html" \
      "PROXY=$x+https://a.localhost:$p/t/pool.html"
    for s in p q; do sed -e "s/:$p\b/:P/g" -e "s/:$q\b/:Q/g" "$work/$s.log" > "$work/$s.named"; done
    sh "$AMB_ROOT/tests/net/tlscanon.sh" pool "P=$work/p.named" "Q=$work/q.named" > "$AMB_ROOT/tests/net/pool.chromium"
    cat "$AMB_ROOT/tests/net/pool.chromium" ;;
  resume)
    [ -f "$work/ca/pool.crt" ] || sh "$AMB_ROOT/tests/net/certs.sh" "$work/ca"
    : > "$AMB_ROOT/tests/net/res.chromium"
    for tlsv in "TLS 1.3:" "TLS 1.2, tickets:--tls12" "TLS 1.2, session IDs:--tls12 --no-ticket"; do
      opts=${tlsv#*:}
      name=$(echo "${tlsv%%:*}" | tr -cd 'a-zA-Z0-9')
      q=$(h2s "q$name" --tls "$work/ca/pool.crt" "$work/ca/pool.key" --h1 --close $opts --log-tls --log "$work/q$name.log" "$AMB_ROOT/tests/net/site")
      p=$(h2s "p$name" --tls "$work/ca/pool.crt" "$work/ca/pool.key" --h1 --close $opts --log-tls --log "$work/p$name.log" \
        --sub-file "@Q@=$work/q$name.port" "$AMB_ROOT/tests/net/site")
      node "$AMB_ROOT/tests/net/tls.js" "$work/p$name.log,$work/q$name.log" "$(cat "$work/ca/spki")" "https://a.localhost:$p/t/res.html" \
        "https://a.localhost:$p/t/seq.html"
      echo "== ${tlsv%%:*}" >> "$AMB_ROOT/tests/net/res.chromium"
      for s in p q; do sed -e "s/:$p\b/:P/g" -e "s/:$q\b/:Q/g" "$work/$s$name.log" > "$work/$s$name.named"; done
      sh "$AMB_ROOT/tests/net/tlscanon.sh" resume "P=$work/p$name.named" "Q=$work/q$name.named" >> "$AMB_ROOT/tests/net/res.chromium"
    done
    cat "$AMB_ROOT/tests/net/res.chromium" ;;
  dict)
    [ -f "$work/ca/srv.crt" ] || sh "$AMB_ROOT/tests/net/certs.sh" "$work/ca"
    d=$(h2s d --tls "$work/ca/srv.crt" "$work/ca/srv.key" --log-tls --log "$work/d.log" --log-headers "$AMB_ROOT/tests/net/site")
    node "$AMB_ROOT/tests/net/tls.js" "$work/d.log" "$(cat "$work/ca/spki")" "https://localhost:$d/dict/page.html" \
      "+https://localhost:$d/dict/again.html" > "$work/d.console"
    { sed -e "s/:$d\b/:D/g" "$work/d.log" | sh "$AMB_ROOT/tests/net/dictcanon.sh"
      grep '^console: ' "$work/d.console"; } > "$AMB_ROOT/tests/net/dict.chromium"
    cat "$AMB_ROOT/tests/net/dict.chromium" ;;
  query)
    [ -f "$work/ca/srv.crt" ] || sh "$AMB_ROOT/tests/net/certs.sh" "$work/ca"
    qport=$(serve --log "$work/query.log" "$AMB_ROOT/tests/net/site")
    node "$AMB_ROOT/tests/net/tls.js" "$work/query.log" "$(cat "$work/ca/spki")" "http://127.0.0.1:$qport/q/w.html" "http://127.0.0.1:$qport/q/s.html" > /dev/null
    sed -e "s/:$qport\b/:PORT/g" "$work/query.log" | sh "$AMB_ROOT/tests/net/canon.sh" > "$AMB_ROOT/tests/net/query.chromium"
    cat "$AMB_ROOT/tests/net/query.chromium" ;;
  esac
done
for p in $(cat "$work/pids" 2> /dev/null); do kill "$p" 2> /dev/null; done
rm -rf "$work"
