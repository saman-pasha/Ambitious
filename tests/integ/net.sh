#!/bin/sh
# tests/integ/net.sh -- a page of many parts served over HTTP by a local server
# (tests/net/serve.py, tests/site/): loaded with its stylesheets (one moved twice,
# one gone, one chunked, one redirecting forever), its script, and its images (one
# from another origin, one a data: URL, one a file the page may not read). Then its
# dependency graph, origins, styles and failures asked of the reasoner, and the page
# drawn. The server's port is written PORT.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-net.XXXXXX")
python3 "$AMB_ROOT/tests/net/serve.py" "$AMB_ROOT/tests/site" > "$work/port" 2> "$work/server.err" &
server=$!
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do [ -s "$work/port" ] && break; sleep 0.25; done
port=$(head -1 "$work/port")
url="http://127.0.0.1:$port/index.html"
export AMB_WORK=$work
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | sed "s/:$port/:PORT/g"; }
amb load "$url"
echo "-- what the page needs:"
amb query "$url" "net_page(P), net_depends(P, R), net_resource(R, U, K, O, S, _, _)"
echo "-- from another origin than the page's:"
amb query "$url" "net_cross_origin(_, R), net_resource(R, U, _, _, _, _, _), net_origin(R, O)"
echo "-- the stylesheet that moved, and where to:"
amb query "$url" "net_redirects(R, Hops), net_resource(R, U, _, ok, _, _, _), net_final_url(R, F)"
echo "-- why the lead is navy, and its span green and bold:"
amb query "$url" "member(C, [lead, chunked]), dom_attr(N, class, C), member(P, [color, font_weight]), css_explain(N, P, X)"
echo "-- the images, as laid out:"
amb query "$url" "dom_element(N, img), layout_box_of(N, B), layout_box(B, _, _, _, _, W, H)"
echo "-- the reasoner's diagnostics:"
amb advise "$url"
echo "-- drawn:"
amb render "$url" "$work/page.png"
kill "$server" 2> /dev/null
wait "$server" 2> /dev/null
rm -rf "$work"
