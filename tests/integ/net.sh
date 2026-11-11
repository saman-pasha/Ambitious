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
# The ports are written by name: PORT, HTTP2, TLS, TLS12, PROXY.
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
port=$(serve http "$AMB_ROOT/tests/net/serve.py" "$site")
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
mkdir -p "$ca"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -keyout "$ca/ca.key" -out "$ca/ca.crt" -days 2 \
  -subj "/CN=Ambitious test authority" -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign" 2> /dev/null
openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -keyout "$ca/srv.key" -out "$ca/srv.csr" -subj "/CN=localhost" 2> /dev/null
printf 'subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\n' > "$ca/srv.ext"
openssl x509 -req -in "$ca/srv.csr" -CA "$ca/ca.crt" -CAkey "$ca/ca.key" -CAcreateserial -out "$ca/srv.crt" -days 2 -extfile "$ca/srv.ext" 2> /dev/null
tls12=$(serve tls12 "$AMB_ROOT/tests/net/serve.py" --tls "$ca/srv.crt" "$ca/srv.key" --tls12 "$nsite" "$site")
tls=$(serve tls "$AMB_ROOT/tests/net/serve.py" --tls "$ca/srv.crt" "$ca/srv.key" --sub "@TLS12@=$tls12" --sub "@HTTP@=$port" "$nsite" "$site")
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
echo "-- what the proxy was asked:"
names < "$work/proxy.log"
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

for p in $(cat "$work/pids"); do kill "$p" 2> /dev/null; done
wait 2> /dev/null
rm -rf "$work"
