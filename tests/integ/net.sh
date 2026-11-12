#!/bin/sh
# tests/integ/net.sh -- the loader against local servers (tests/net/serve.py) and a
# local proxy (tests/net/proxy.py).
#
# http: a page of many parts (tests/site/): loaded with its stylesheets (one moved
# twice, one gone, one chunked, one redirecting forever), its script, and its images
# (one from another origin, one a data: URL, one a file the page may not read). Then
# its dependency graph, origins, styles and failures asked of the reasoner, and the
# page drawn.
#
# https: (tests/net/site/) from servers whose authority and certificate the openssl
# CLI makes here, the certificate naming localhost and no address: a page and its
# parts over TLS 1.3, an image from a server held to TLS 1.2, one from the address
# the certificate does not name, one over TLS from a server that speaks none; then
# a store that lacks the test's authority.
#
# Proxies: https_proxy's CONNECT tunnels, the hosts no_proxy reaches directly, a
# tunnel the proxy refuses, http_proxy's requests in absolute form with credentials.
#
# Cookies (tests/net/site/c/, each response's Set-Cookie lines in FILE.headers): five
# runs of one profile, the clock set by AMB_NOW, each page's cookies as the reasoner
# sees them (never a value) and its refusals. Persistent cookies last from one run to
# the next in the profile's jar file, session cookies end with their run; a Max-Age
# runs out and another deletes; Path and Domain match; SameSite holds back a cross-
# site part's cookies, as Lax by default; Secure ones go over TLS alone. The Cookie
# header each request carried, as the server logged it, against Chromium's for the
# same visits (tests/net/reference.sh). A private run leaves the jar as it was.
#
# Content codings (tests/net/site/z/), the server's done by Python's gzip and zlib: a
# page and its parts in gzip, deflate, deflate with no zlib header, gzip then deflate;
# a br that is no Brotli stream (the server sends the file as it is) and a gzip body
# whose CRC-32 is wrong, each failing with its reason. Each decoded size is its file's;
# the decoded sheets style the page and the decoded PNG is laid out at its size. Every
# request asked for gzip, deflate, br and zstd, as Chromium asks a loopback host (br and
# zstd themselves: tests/integ/net-h2).
#
# The HTTP cache (tests/net/site/k/): three runs of a profile 61 s apart, from a server
# whose Date stays put, a part for each of max-age, no-cache with an ETag, a max-age
# that runs out and a 304 that lengthens it, no-store, private, Expires, a
# Last-Modified revalidated, a heuristic lifetime, and an ETag that changes. What the
# cache did for each part, and which requests reached the server, against Chromium's
# over its own cache for the same visits. AMB_CACHE names a cache of its own; a
# private run neither uses nor changes one.
#
# srcset and sizes (tests/net/site/s/): fourteen imgs, x and w descriptors, sizes
# with media conditions, em and calc(), src as the 1x, densities above and below 1, a
# bad descriptor, a comma in a URL, w and x together. Each choice is a fact; the
# candidate and its density-corrected size are Chromium's, image by image
# (tests/net/srcset.chromium), and layout draws them at that size.
#
# IPv6: an image from an IPv6 literal, written back compressed and dialled through
# getaddrinfo: loaded from a server on ::1 where the machine has one, else refused for
# its address family. The line says which held, so either machine prints the same.
#
# The ports are written by name: PORT, HTTP2, TLS, TLS12, PROXY, CODED, CACHE, V6.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-net.XXXXXX")
# the environment's proxies are not the tests': each test names its own
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
export AMB_WORK=$work
site=$AMB_ROOT/tests/site
nsite=$AMB_ROOT/tests/net/site
# serve NAME PROGRAM ARGS...: a server started, its port in $work/NAME.port and echoed
serve() {
  name=$1
  shift
  python3 "$@" > "$work/$name.port" 2> "$work/$name.err" &
  echo $! >> "$work/pids"
  i=0
  while [ ! -s "$work/$name.port" ] && [ $i -lt 40 ]; do sleep 0.25; i=$((i + 1)); done
  head -1 "$work/$name.port"
}
port=$(serve http "$AMB_ROOT/tests/net/serve.py" --log "$work/req.log" "$site" "$nsite")
names() { sed -e "s/:$port\b/:PORT/g" -e "s/:${http2:-x}\b/:HTTP2/g" -e "s/:${tls:-x}\b/:TLS/g" -e "s/:${tls12:-x}\b/:TLS12/g" -e "s/:${proxy:-x}\b/:PROXY/g"; }
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | names; }

echo "== http:"
url="http://127.0.0.1:$port/index.html"
amb load "$url"
echo "-- what the page needs:"
amb query "$url" "net_page(P), net_depends(P, R), net_resource(R, U, K, O, S, _, _)"
echo "-- from another origin than the page's:"
amb query "$url" "net_cross_origin(_, R), net_resource(R, U, _, _, _, _, _), net_origin(R, O)"
echo "-- the stylesheet that moved, and where to:"
amb query "$url" "net_redirects(R, Hops), net_resource(R, U, _, ok, _, _, _), net_final_url(R, F)"
echo "-- why each failure failed, in words:"
amb query "$url" "net_failure(R, Why), net_resource(R, U, _, O, _, _, _)"
echo "-- why the lead is navy, and its span green and bold:"
amb query "$url" "member(C, [lead, chunked]), dom_attr(N, class, C), member(P, [color, font_weight]), css_explain(N, P, X)"
echo "-- the images, as laid out:"
amb query "$url" "dom_element(N, img), layout_box_of(N, B), layout_box(B, _, _, _, _, W, H)"
echo "-- the reasoner's diagnostics:"
amb advise "$url"
echo "-- drawn:"
amb render "$url" "$work/page.png"

echo "== https:"
ca=$work/ca
sh "$AMB_ROOT/tests/net/certs.sh" "$ca"
tls12=$(serve tls12 "$AMB_ROOT/tests/net/serve.py" --tls "$ca/srv.crt" "$ca/srv.key" --tls12 "$nsite" "$site")
tls=$(serve tls "$AMB_ROOT/tests/net/serve.py" --tls "$ca/srv.crt" "$ca/srv.key" --sub "@TLS12@=$tls12" --sub "@HTTP@=$port" --log "$work/req.log" \
  "$nsite" "$site")
http2=$(serve http2 "$AMB_ROOT/tests/net/serve.py" --sub "@TLS@=$tls" "$nsite" "$site")
export SSL_CERT_FILE=$ca/ca.crt
turl="https://localhost:$tls/tls.html"
amb load "$turl"
echo "-- each TLS session: its version, and the protocol ALPN chose:"
amb query "$turl" "net_tls(R, V, _, A), net_resource(R, U, _, _, _, _, _)"
echo "-- why the others failed:"
amb query "$turl" "net_failure(R, Why), net_resource(R, U, _, O, _, _, _)"
echo "-- the heading's colour, from the stylesheet over TLS:"
amb query "$turl" "dom_attr(N, class, secure), css_explain(N, color, X)"
echo "-- the reasoner's diagnostics:"
amb advise "$turl"
echo "-- a store that lacks the test's authority (the system's):"
SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt amb load "$turl"
eurl="http://localhost:$http2/elsewhere.html"
SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt amb query "$eurl" "net_failure(R, Why), net_resource(R, U, _, O, _, _, _)"

echo "== proxies:"
proxy=$(serve proxy "$AMB_ROOT/tests/net/proxy.py" "$work/proxy.log" --deny blocked.test)
echo "-- https_proxy: every https: fetch through a tunnel"
https_proxy=http://127.0.0.1:$proxy amb query "$turl" "net_proxy(R, P), net_resource(R, U, _, O, _, _, _)"
echo "-- what the proxy was asked (sorted: the parts' tunnels are opened at once; the page's, kept alive, carries a part):"
names < "$work/proxy.log" | sort
: > "$work/proxy.log"
echo "-- no_proxy=localhost: localhost reached directly, the address through the proxy"
https_proxy=http://127.0.0.1:$proxy no_proxy=localhost amb load "$turl" > /dev/null
names < "$work/proxy.log"
: > "$work/proxy.log"
echo "-- a tunnel the proxy refuses:"
https_proxy=http://127.0.0.1:$proxy amb query "$eurl" "net_failure(R, Why), net_resource(R, U, _, O, _, _, _)"
names < "$work/proxy.log"
: > "$work/proxy.log"
echo "-- http_proxy: the page asked for in absolute form, with the proxy's credentials"
http_proxy=http://amb:s%40fe@127.0.0.1:$proxy amb query "$eurl" "net_proxy(R, P), net_resource(R, U, _, O, _, _, _)"
names < "$work/proxy.log"

echo "== cookies, kept in the profile's directory from one run to the next:"
# visit N TIME URL GOAL: run N of a profile at the clock's TIME, its requests logged
# under `-- N URL'; what the reasoner says of its cookies, and GOAL's answers
prof=$work/profile
: > "$work/req.log"
visit() {
  echo "-- $1 $3" >> "$work/req.log"
  echo "-- $1. $3" | names
  AMB_NOW=$2 sh "$AMB_ROOT/bin/ambitious" --profile "$prof" inspect "$3" --query "$4" 2>&1 | names |
    sed -n -e '/^  amb_diag(\(cookie_refused\|third_party_cookie\),/p' -e '/^== query/,$p' | sed '/^== query/d'
}
cookie="net_cookie(C, N, D, P, S), net_cookie_flags(C, HO, Sec, Http, SS)"
visit 1 1700000000 "http://localhost:$port/c/home.html" "$cookie"
visit 2 1700000010 "http://127.0.0.1:$port/c/page.html" "$cookie"
echo "-- the page's snapshot holds no cookie's value: $(grep -c -e '-7f3a' -e '-5d21' "$work/page.pl") lines with one"
visit 3 1700000100 "http://127.0.0.1:$port/c/again.html" "net_cookie(C, N, D, P, S)"
visit 4 1700000200 "https://localhost:$tls/c/secure.html" "$cookie"
visit 5 1700000300 "http://localhost:$port/c/home.html" "net_cookies_sent(R, Ns)"
echo "-- the jar's file in the profile: each persistent cookie's domain, path, expiry and name"
grep -v '^#' "$prof/cookies" | cut -f 1,3,7,9
echo "-- what the server was sent, against Chromium's (tests/net/cookies.chromium), which takes loopback http: as secure:"
names < "$work/req.log" | sh "$AMB_ROOT/tests/net/canon.sh" > "$work/cookies.got"
cat "$work/cookies.got"
diff "$AMB_ROOT/tests/net/cookies.chromium" "$work/cookies.got"
echo "-- a private session keeps its cookies to itself: the jar's file is as it was"
before=$(cksum < "$prof/cookies")
AMB_NOW=1700000400 sh "$AMB_ROOT/bin/ambitious" --profile "$prof" --private load "http://127.0.0.1:$port/c/page.html" > /dev/null 2>&1
[ "$before" = "$(cksum < "$prof/cookies")" ] && echo "the same" || echo "CHANGED"

echo "== content codings (tests/net/site/z/), done by Python's gzip and zlib:"
coded=$(serve coded "$AMB_ROOT/tests/net/serve.py" --log-accept --log "$work/coded.log" "$nsite" "$site")
zurl="http://127.0.0.1:$coded/z/page.html"
zamb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | names | sed "s/:$coded\b/:CODED/g"; }
zamb load "$zurl"
echo "-- each coded body, its coding, and its size as sent and as decoded:"
zamb query "$zurl" "net_coding(R, C, E), net_resource(R, U, _, _, _, _, S)"
echo "-- the sizes of the files they were made from:"
for f in page.html gz.css df.css raw.css both.css gz.png; do echo "$f $(wc -c < "$nsite/z/$f" | tr -d ' ')"; done
echo "-- what did not decode, and why:"
zamb advise "$zurl" | grep '^amb_diag(failed'
echo "-- the decoded sheets' colours, and the decoded image's size as laid out:"
zamb query "$zurl" "member(C, [gz, df, raw, both, br]), dom_attr(N, class, C), css_explain(N, color, X)"
zamb query "$zurl" "dom_element(N, img), layout_box_of(N, B), layout_box(B, _, _, _, _, W, H)"
echo "-- the codings each request asked for:"
sed 's/^.* | accept-encoding: /accept-encoding: /' "$work/coded.log" | sort | uniq -c | sed 's/^ *//'

echo "== the HTTP cache, in the profile's directory:"
# a server whose clock stays at 1700000000 (its responses' Date); its changed.css from a
# root of the test's own, written anew between the first run and the second
mkdir -p "$work/kroot/k"
printf '.changed { color: red }\n' > "$work/kroot/k/changed.css"
printf 'Cache-Control: no-cache\nETag: auto\n' > "$work/kroot/k/changed.css.headers"
cache=$(serve cache "$AMB_ROOT/tests/net/serve.py" --date 1700000000 --log "$work/cache.log" "$work/kroot" "$nsite" "$site")
kurl="http://127.0.0.1:$cache/k/page.html"
kprof=$work/kprofile
kq="net_cache(R, How, Age, Life), net_resource(R, U, _, _, S, _, _)"
kamb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | names | sed "s/:$cache\b/:CACHE/g"; }
for run in 1 2 3; do
  echo "-- $run" >> "$work/cache.log"
  now=$((1700000000 + 61 * (run - 1)))
  echo "-- $run. at $now: each part, what the cache did, the age and lifetime it went by, the status"
  AMB_NOW=$now kamb --profile "$kprof" query "$kurl" "$kq"
  [ $run = 1 ] && printf '.changed { color: blue }\n' > "$work/kroot/k/changed.css"
done
echo "-- 4" >> "$work/cache.log"
echo "-- the parts that must be asked about each time, as the reasoner says:"
AMB_NOW=1700000183 kamb --profile "$kprof" advise "$kurl" | grep '^amb_diag(no_lifetime'
echo "-- what reached the server in the three runs, against Chromium's for the same visits (tests/net/cache.chromium):"
sed -e '/^-- 4/,$d' -e "s/:$cache\b/:CACHE/g" "$work/cache.log" | sh "$AMB_ROOT/tests/net/canon.sh" > "$work/cache.got"
cat "$work/cache.got"
diff "$AMB_ROOT/tests/net/cache.chromium" "$work/cache.got" && echo "the same, request by request"
echo "-- AMB_CACHE, a cache of its own with no profile: every part stored again"
AMB_CACHE=$work/other AMB_NOW=1700000200 kamb query "$kurl" "net_cache(R, stored, _, _), net_resource(R, U, _, _, _, _, _)" | wc -l
echo "$(ls "$work/other" | wc -l) entries"
echo "-- a private session caches nothing, and leaves the cache as it was:"
before=$(cat "$kprof"/cache/* | cksum)
AMB_NOW=1700000300 kamb --profile "$kprof" --private query "$kurl" "net_cache(R, How, _, _)"
[ "$before" = "$(cat "$kprof"/cache/* | cksum)" ] && echo "the same" || echo "CHANGED"

echo "== srcset and sizes, in an 800x600 viewport at 1x (tests/net/site/s/page.html):"
surl="http://127.0.0.1:$port/s/page.html"
echo "-- each img's choice: its descriptor, its density, the source size, and how many candidates there were:"
amb query "$surl" "dom_attr(N, id, Id), net_asked_by(R, _, N), net_srcset(R, D, X, S, C)" | sed 's/^N = n([0-9,]*), //'
echo "-- each img's candidate and its size, against Chromium's (tests/net/srcset.chromium):"
amb query "$surl" "dom_attr(N, id, Id), net_asked_by(R, _, N), net_resource(R, U, image, _, _, _, _), net_image_size(R, W, H)" |
  sed -e "s/^N = [^,]*([^)]*), Id = \([^,]*\), R = [^,]*([^)]*), U = '[^']*\/\([^/']*\)', W = \([0-9]*\), H = \([0-9]*\)$/\1 \2 \3x\4/" > "$work/srcset.got"
diff "$AMB_ROOT/tests/net/srcset.chromium" "$work/srcset.got" && echo "the same, image by image ($(wc -l < "$work/srcset.got" | tr -d ' ') of them)"
echo "-- laid out at those sizes, and what the reasoner says of the one below 1x:"
amb query "$surl" "member(Id, [narrow, below, mixed]), dom_attr(N, id, Id), layout_box_of(N, B), layout_box(B, _, _, _, _, W, H)" | sed 's/, N = n([0-9,]*), B = bx([0-9,]*)//'
amb advise "$surl" | grep '^amb_diag(low_density'

echo "== IPv6 literals: an image from http://[0:0:0:0:0:0:0:1]:V6/ (tests/net/site/v6.html)"
# a server on ::1 where the machine has it; else port 9 of ::1, which the machine cannot dial
if python3 -c 'import socket; socket.socket(socket.AF_INET6).bind(("::1", 0))' 2> /dev/null; then
  v6=$(serve v6 "$AMB_ROOT/tests/net/serve.py" --bind ::1 "$site")
  want=ok
else
  v6=9
  want=refused
fi
v6page=$(serve v6page "$AMB_ROOT/tests/net/serve.py" --sub "@V6@=$v6" "$nsite" "$site")
got=$(sh "$AMB_ROOT/bin/ambitious" query "http://127.0.0.1:$v6page/v6.html" "net_resource(R, U, image, O, _, _, _), (net_failure(R, W) -> true ; W = none)" 2>&1)
echo "-- its URL written back compressed, as the URL standard writes it:"
echo "$got" | sed -n "s/.*U = '\([^']*\)'.*/\1/p" | sed "s/:$v6\//:V6\//"
echo "-- dialled through getaddrinfo: loaded where the machine has ::1, else refused for its address family"
case "$want:$got" in
  ok:*"U = 'http://[::1]:$v6/logo.png', O = ok"*) echo "right for this machine" ;;
  refused:*"U = 'http://[::1]:9/logo.png', O = refused, W = '::1 port 9: "*) echo "right for this machine" ;;
  *) echo "WRONG for this machine (want $want): $got" ;;
esac

for p in $(cat "$work/pids"); do kill "$p" 2> /dev/null; done
wait 2> /dev/null
rm -rf "$work"
