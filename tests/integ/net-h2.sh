#!/bin/sh
# tests/integ/net-h2.sh -- HTTP/2 over TLS, HTTP/1.1 kept alive, br and zstd: the loader
# against node servers (tests/net/h2serve.js) with the test's authority (tests/net/certs.sh).
#
# HTTP/2 (tests/net/site/h2/page.html): a page of thirty parts -- three sheets (one in br,
# one in zstd), twenty-six images, a script -- over one connection, the parts concurrent
# streams (the server holds its answers until thirty are open at once). The server's log of
# connections and streams; the client's first frames as the server's front read them
# (Chromium's SETTINGS and connection window); each request's HEADERS frame (its priority as
# Chromium sets it, its block's size) and its priority field; HPACK's dynamic table in the
# server's decoder; each request's header block as the server decoded it, against Chromium's
# for the same visit (tests/net/h2.chromium, made by tests/net/reference.sh h2); the facts
# (ALPN's h2, the connection and the streams); how it drew, against the same page over
# HTTP/1.1.
#
# Then over HTTP/2: a server that sends GOAWAY at stream 7 (the streams after it, not
# processed, go again on a new connection); one that lets four streams be open at once; one
# that resets a stream with INTERNAL_ERROR (it fails) and refuses one with REFUSED_STREAM (it
# goes again), and never answers another (the read's ten seconds); a server that refuses h2
# (http/1.1 by ALPN, six connections kept alive); redirects from HTTP/2 to HTTP/1.1 and back;
# a CONNECT proxy, HTTP/2 inside its tunnel; a body over 32 MB; a file: URL a page over
# HTTP/2 asks for; cookies and the cache, two runs of a profile; br and zstd bodies (node's
# zlib) and broken ones. Over HTTP/1.1 with no TLS: a page of twelve parts over six
# connections at most, kept alive, from the server's log; a connection the server closes at
# its second request (the request goes once again, on a new connection); the fields a host
# that is not potentially trustworthy is sent.
#
# The ports are written by name: H2, H1, GOAWAY, FOUR, RESET, PROXY, BIG, CACHE, PLAIN, DROP,
# FAR.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-h2.XXXXXX")
# the environment's proxies are not the tests': the one test of a proxy names its own
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
export AMB_WORK=$work
nsite=$AMB_ROOT/tests/net/site
site=$AMB_ROOT/tests/site
ca=$work/ca
sh "$AMB_ROOT/tests/net/certs.sh" "$ca"
export SSL_CERT_FILE=$ca/ca.crt
tls="--tls $ca/srv.crt $ca/srv.key"
# serve NAME OPTIONS...: tests/net/h2serve.js over the two sites, its port in $work/NAME.port and echoed
serve() {
  name=$1
  shift
  node "$AMB_ROOT/tests/net/h2serve.js" "$@" "$nsite" "$site" > "$work/$name.port" 2> "$work/$name.err" &
  echo $! >> "$work/pids"
  i=0
  while [ ! -s "$work/$name.port" ] && [ $i -lt 40 ]; do sleep 0.25; i=$((i + 1)); done
  head -1 "$work/$name.port"
}
# pyserve NAME PROGRAM ARGS...: a Python server, as serve
pyserve() {
  name=$1
  shift
  python3 "$@" > "$work/$name.port" 2> "$work/$name.err" &
  echo $! >> "$work/pids"
  i=0
  while [ ! -s "$work/$name.port" ] && [ $i -lt 40 ]; do sleep 0.25; i=$((i + 1)); done
  head -1 "$work/$name.port"
}
h2=$(serve h2 $tls --hold 30 --log "$work/h2.log" --log-headers --log-frames --log-hpack --sub-file "@H1@=$work/h1.port")
h1=$(serve h1 $tls --h1 --log "$work/h1.log" --sub-file "@H2@=$work/h2.port")
names() {
  sed -e "s/:$h2\b/:H2/g" -e "s/:$h1\b/:H1/g" -e "s/:${goaway:-x}\b/:GOAWAY/g" -e "s/:${reset:-x}\b/:RESET/g" -e "s/:${proxy:-x}\b/:PROXY/g" \
    -e "s/:${four:-x}\b/:FOUR/g" -e "s/:${big:-x}\b/:BIG/g" -e "s/:${cache:-x}\b/:CACHE/g" -e "s/:${plain:-x}\b/:PLAIN/g" -e "s/:${drop:-x}\b/:DROP/g" -e "s/:${far:-x}\b/:FAR/g"
}
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | names; }
# conns LOG: each connection of a server's log, its protocol and its streams (or requests),
# and over h2 the most streams open on it at once
conns() {
  awk '/^conn [0-9]+ (h2|http\/1\.1)$/ { p[$2] = $3; n[$2] = 0; m[$2] = 0; order[++k] = $2 }
       /^conn [0-9]+ stream [0-9]+ [A-Z]+ [^ ]+, [0-9]+ open$/ { n[$2]++; if ($7 + 0 > m[$2]) m[$2] = $7 + 0 }
       /^conn [0-9]+ request [0-9]+ [A-Z]+ [^ ]+$/ { n[$2]++ }
       END { for (i = 1; i <= k; i++) { c = order[i]; s = "connection " c ", " p[c] ": " n[c] (p[c] == "h2" ? " streams" : " requests")
             if (p[c] == "h2") s = s ", at most " m[c] " open at once"
             print s } }' "$1"
}
# the connections and requests of an HTTP/1.1 server's log, together
kept() { conns "$1" | awk '{ n++; r += $4 } END { print n " connections, " r " requests" }'; }
# carried LOG PATH: whether the HTTP/1.1 connection that carried PATH carried parts after it
carried() {
  c=$(awk -v p="$2" '$1 == "conn" && $3 == "request" && $6 == p { print $2; exit }' "$1")
  n=$(awk -v c="$c" '$1 == "conn" && $2 == c && $3 == "request" && $5 ~ /^[A-Z]+$/' "$1" | wc -l)
  [ "$n" -gt 1 ] && echo "the page's connection carried parts after it" || echo "the page's connection carried the page alone"
}

echo "== HTTP/2: a page of thirty parts over one connection (tests/net/site/h2/page.html)"
url="https://localhost:$h2/h2/page.html"
echo "-- 1 https://localhost:H2/h2/page.html" > "$work/h2.log"
amb load "$url"
echo "-- the server's log: each connection, its streams"
conns "$work/h2.log"
echo "-- the client's first frames, as the server's front read them: Chromium's SETTINGS and window"
grep -m 4 -E '^conn 1 > (the preface|SETTINGS|WINDOW_UPDATE)' "$work/h2.log"
echo "-- each request's HEADERS frame (its priority as Chromium sets it, its block's size), and its priority field"
awk '/^conn 1 > HEADERS stream / { s = $6; sub(/,$/, "", s); h = $0; sub(/^conn 1 > HEADERS stream [0-9]+, /, "", h); hf[s] = h; next }
     /^conn 1 stream [0-9]+ [A-Z]+ [^ ]+, [0-9]+ open$/ { s = $4; p = $6; sub(/,$/, "", p); path[s] = p; order[++n] = s; cur = s; next }
     /^conn / { cur = ""; next }
     /^  priority: / { if (cur != "") { v = $0; sub(/^  priority: /, "", v); pf[cur] = v } }
     END { for (i = 1; i <= n; i++) { s = order[i]; print "stream " s " " path[s] ": " hf[s] "; priority " pf[s] } }' "$work/h2.log"
echo "-- HPACK's dynamic table in the server's decoder: after the page's block, the first part's, the last part's"
awk '/^conn 1 stream [0-9]+ hpack table / { t[++k] = $NF } END { print t[1] " bytes, " t[2] " bytes, " t[k] " bytes" }' "$work/h2.log"
echo "-- each request's header block as the server decoded it, against Chromium's for the same visit (tests/net/h2.chromium)"
names < "$work/h2.log" | sh "$AMB_ROOT/tests/net/h2canon.sh" > "$work/h2.visit"
if diff "$AMB_ROOT/tests/net/h2.chromium" "$work/h2.visit" > "$work/h2.diff"; then
  echo "the same, field by field: $(grep -c '^GET' "$work/h2.visit") requests"
else
  cat "$work/h2.diff"
fi
echo "-- the facts: the parts over TLS 1.3 and the protocol ALPN chose, on the page's connection, a stream each"
amb query "$url" "aggregate_all(count, net_tls(_, 'TLSv1.3', _, h2), H2), aggregate_all(count, net_http(_, h2, 1, _, _), OnOne), aggregate_all(count, net_http(_, h2, 1, _, true), Reused)"
echo "-- the last stream, the thirty-first, as the reasoner has it"
amb query "$url" "net_http(R, h2, 1, 61, Reused), net_resource(R, U, K, _, _, _, _)"
echo "-- the sheets' colours (b.css came in br, c.css in zstd)"
amb query "$url" "member(C, [a, b, c]), dom_attr(N, class, C), css_explain(N, color, X)"
echo "-- drawn over HTTP/2, and over HTTP/1.1 from the server that refuses h2"
a=$(amb render "$url" "$work/h2.png")
b=$(amb render "https://localhost:$h1/h2/page.html" "$work/h1.png")
echo "$a"
[ "$a" = "$b" ] && echo "the same over HTTP/1.1" || echo "NOT the same over HTTP/1.1: $b"

echo "== a server that sends GOAWAY at stream 7: the streams after it, not processed, asked again on a new connection"
goaway=$(serve goaway $tls --goaway 7 --log "$work/goaway.log")
amb query "https://localhost:$goaway/h2/page.html" "aggregate_all(count, net_resource(_, _, _, ok, 200, _, _), Ok), aggregate_all(count, net_http(_, h2, 1, _, _), First), aggregate_all(count, net_http(_, h2, 2, _, _), Again)"
echo "-- the server's log"
conns "$work/goaway.log" | sed 's/, at most .*//'
grep -E 'GOAWAY|refused' "$work/goaway.log" | sed 's/^conn \([0-9]*\) stream [0-9]* [A-Z]* [^ ]*, refused/conn \1: a stream refused/' | uniq -c | sed 's/^ *//'

echo "== a server that lets four streams be open at once (SETTINGS_MAX_CONCURRENT_STREAMS): the parts go four at a time"
four=$(serve four $tls --max-streams 4 --log "$work/four.log")
amb query "https://localhost:$four/h2/page.html" "aggregate_all(count, net_resource(_, _, _, ok, 200, _, _), Ok), aggregate_all(count, net_http(_, h2, 1, _, _), OnOne)"
conns "$work/four.log"

echo "== a server that resets a stream with INTERNAL_ERROR and refuses one with REFUSED_STREAM (tests/net/site/h2/s.html)"
reset=$(serve reset $tls --reset /h2/reset.css INTERNAL_ERROR --reset /h2/refused.png REFUSED_STREAM --stall /h2/stall.css --log "$work/reset.log")
amb query "https://localhost:$reset/h2/s.html" "net_resource(R, U, _, O, S, _, _), (net_failure(R, Why) -> true ; Why = none)"
echo "-- the server's log: the stream reset, the one refused and asked again"
grep -E '^conn [0-9]+ stream ' "$work/reset.log" | sed 's/, [0-9]* open$//'
echo "-- a stream the server never answers (tests/net/site/h2/t.html): the read's ten seconds"
amb query "https://localhost:$reset/h2/t.html" "net_resource(R, U, _, O, S, _, _), (net_failure(R, Why) -> true ; Why = none)"

echo "== a server that refuses h2: http/1.1 by ALPN, its connections kept alive"
: > "$work/h1.log"
amb query "https://localhost:$h1/h2/page.html" "aggregate_all(count, net_tls(_, _, _, 'http/1.1'), Http1), aggregate_all(count, net_http(_, http1, _, 0, true), Reused), aggregate_all(count, net_http(_, http1, 7, _, _), OnASeventh)"
echo "-- the server's log"
kept "$work/h1.log"
carried "$work/h1.log" /h2/page.html

echo "== redirects from HTTP/2 to HTTP/1.1 and back (tests/net/site/h2/r.html)"
amb load "https://localhost:$h2/h2/r.html"
amb query "https://localhost:$h2/h2/r.html" "net_http(R, P, _, _, _), net_resource(R, U, _, _, _, _, _), net_final_url(R, F)"
amb query "https://localhost:$h2/h2/r.html" "dom_attr(N, class, moved), css_explain(N, color, X)"

echo "== a CONNECT proxy: HTTP/2 inside its tunnel, one tunnel for the page and its parts"
proxy=$(pyserve proxy "$AMB_ROOT/tests/net/proxy.py" "$work/proxy.log")
https_proxy=http://127.0.0.1:$proxy amb query "$url" "aggregate_all(count, net_proxy(_, _), Proxied), aggregate_all(count, net_tls(_, _, _, h2), H2), aggregate_all(count, net_http(_, h2, 1, _, _), OnOne)"
echo "-- what the proxy was asked"
names < "$work/proxy.log"

echo "== the bounds over HTTP/2: a body over 32 MB (its stream's window opened as it came), a file: URL a page over HTTP/2 asks for"
mkdir "$work/big"
head -c 34000000 /dev/zero > "$work/big/big.bin"
printf '<!DOCTYPE html>\n<title>A part over 32 MB</title>\n<img src="big.bin" alt="34 MB">\n' > "$work/big/big.html"
big=$(serve big $tls "$work/big")
amb query "https://localhost:$big/big.html" "net_resource(R, U, _, O, S, _, _), (net_failure(R, Why) -> true ; Why = none)"
amb query "https://localhost:$h2/h2/f.html" "net_resource(R, U, _, O, S, _, _), (net_failure(R, Why) -> true ; Why = none)"

echo "== cookies and the cache over HTTP/2 (tests/net/site/h2/c.html): two runs of a profile 61 s apart, the server's Date fixed"
cache=$(serve cache $tls --date 1700000000 --log "$work/cache.log" --log-headers)
curl=https://localhost:$cache/h2/c.html
prof=$work/profile
for run in 1 2; do
  now=$((1700000000 + 61 * (run - 1)))
  echo "-- run $run"
  echo "-- $run" >> "$work/cache.log"
  AMB_NOW=$now amb --profile "$prof" query "$curl" "net_cache(R, How, Age, Life), net_resource(R, U, _, _, S, _, _)"
done
echo "-- what each run's requests carried, as the server decoded them: the cookies (a field each, as Chromium splits them) and the validators"
grep -E '^-- [0-9]$|^conn [0-9]+ stream [0-9]+ [A-Z]+ |^  (cookie|if-none-match|if-modified-since):' "$work/cache.log" |
  sed -e 's/^conn [0-9]* stream [0-9]* //' -e 's/, [0-9]* open$//'

echo "== br and zstd (tests/net/site/h2/z.html), each body coded by node's zlib"
zurl="https://localhost:$h2/h2/z.html"
amb load "$zurl"
echo "-- each coded body: its coding, its size as sent and as decoded"
amb query "$zurl" "net_coding(R, C, E), net_resource(R, U, _, O, _, _, S)"
echo "-- the files they were made from"
for f in br.css zstd.css both.css brbad.css zbad.css zdict.css br.png zstd.png; do echo "$f $(wc -c < "$nsite/h2/$f" | tr -d ' ')"; done
echo "-- what did not decode, and why"
amb query "$zurl" "net_failure(R, Why), net_resource(R, U, _, O, _, _, _)"
echo "-- the decoded sheets' colours, the decoded images' sizes as laid out"
amb query "$zurl" "member(C, [br, zstd, both, brbad]), dom_attr(N, class, C), css_explain(N, color, X)"
amb query "$zurl" "dom_element(N, img), layout_box_of(N, B), layout_box(B, _, _, _, _, W, H)"

echo "== HTTP/1.1 with no TLS, kept alive (tests/net/site/h2/k.html: a page and twelve parts)"
plain=$(serve plain --plain --log "$work/plain.log" --log-headers)
amb query "http://127.0.0.1:$plain/h2/k.html" "aggregate_all(count, net_http(_, http1, _, 0, _), Http1), aggregate_all(count, net_http(_, http1, _, 0, true), Reused), aggregate_all(count, net_http(_, http1, 7, _, _), OnASeventh)"
echo "-- the server's log"
kept "$work/plain.log"
carried "$work/plain.log" /h2/k.html
echo "-- the page's request: a loopback host is potentially trustworthy (br and zstd, client hints, fetch metadata)"
awk '/^conn 1 request 1 / { p = 1; print; next } /^conn / { p = 0 } p' "$work/plain.log" | names
echo "-- a connection the server closes at its second request, unanswered: the request goes once again, on a new connection"
drop=$(serve drop --plain --drop-second --log "$work/drop.log")
amb query "http://127.0.0.1:$drop/h2/one.html" "net_resource(R, U, _, O, S, _, _), net_http(R, P, C, _, Reused)"
grep -v '^  ' "$work/drop.log"
echo "-- a host that is not potentially trustworthy (this machine's address that is not loopback's): gzip and deflate alone, no client hints, no fetch metadata"
addr=$(python3 -c 'import socket; s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.connect(("192.0.2.200", 9)); print(s.getsockname()[0])' 2> /dev/null)
case "$addr" in
  "" | 127.*) echo "this machine has no address but loopback's: not tried" ;;
  *)
    far=$(serve far --plain --bind "$addr" --log "$work/far.log" --log-headers)
    sh "$AMB_ROOT/bin/ambitious" load "http://$addr:$far/h2/one.html" > /dev/null 2>&1
    awk '/^conn 1 request 1 / { p = 1; next } /^conn / { p = 0 } p' "$work/far.log" | sed -e "s/$addr:$far/ADDRESS:FAR/" ;;
esac

for p in $(cat "$work/pids"); do kill "$p" 2> /dev/null; done
wait 2> /dev/null
rm -rf "$work"
