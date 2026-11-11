#!/bin/sh
# tests/net/reference.sh -- Chromium's side of tests/integ/net's cookies, written to
# tests/net/cookies.chromium: the same five visits made in Chromium (tests/net/cookies.js,
# Playwright's headless shell), and the Cookie header each of its requests carried, as
# the same servers logged them (tests/net/canon.sh). The third visit comes after the
# 60 s a cookie lives, so a run takes a minute. Run by hand when the visits change.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-ref.XXXXXX")
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
sh "$AMB_ROOT/tests/net/certs.sh" "$work/ca"
python3 "$AMB_ROOT/tests/net/serve.py" --log "$work/log" "$AMB_ROOT/tests/net/site" "$AMB_ROOT/tests/site" > "$work/http.port" &
p1=$!
python3 "$AMB_ROOT/tests/net/serve.py" --tls "$work/ca/srv.crt" "$work/ca/srv.key" --log "$work/log" "$AMB_ROOT/tests/net/site" "$AMB_ROOT/tests/site" > "$work/tls.port" &
p2=$!
sleep 1
port=$(head -1 "$work/http.port")
tls=$(head -1 "$work/tls.port")
node "$AMB_ROOT/tests/net/cookies.js" "$work/log" "http://localhost:$port/c/home.html" "http://127.0.0.1:$port/c/page.html" \
  "61000+http://127.0.0.1:$port/c/again.html" "https://localhost:$tls/c/secure.html" "http://localhost:$port/c/home.html"
kill $p1 $p2
sed -e "s/:$port\b/:PORT/g" -e "s/:$tls\b/:TLS/g" "$work/log" | sh "$AMB_ROOT/tests/net/canon.sh" > "$AMB_ROOT/tests/net/cookies.chromium"
rm -rf "$work"
cat "$AMB_ROOT/tests/net/cookies.chromium"
