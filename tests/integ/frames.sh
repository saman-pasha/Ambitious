#!/bin/sh
# tests/integ/frames.sh -- a page's frames (Phase 12u, src/shell/frame.c, src/script/frames.c)
# against Chromium. Each fixture page whose frames' boxes are kept (tests/pages/NAME.frames,
# written by tools/bench/frameboxes.js: getBoundingClientRect() of each element with an id in
# each frame, in that frame's own viewport, Chromium's scroll bars shown) has each such box held
# to Chromium's within 1px (`ambitious frames`); the elements that differ are printed, then
# how many agree. (The pages' own boxes and their drawing, region by region, are
# tests/integ/boxes.sh's and tests/integ/paintref.sh's.) Then the scripts across frames, their
# consoles against Chromium's line for line: frames-script.html read from its file, and
# frames-http.html served by tests/integ/script-serve.py (http://localhost:PORT another origin
# than the page's). Then the frames as the reasoner has them, and what it says of them.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-frames.XXXXXX")
unset http_proxy HTTP_PROXY https_proxy HTTPS_PROXY no_proxy NO_PROXY
export AMB_WORK=$work
pages=$AMB_ROOT/tests/pages
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" < /dev/null 2>&1 | sed -e "s|$AMB_ROOT|ROOT|g"; }
console() { amb "$@" > "$work/s.txt"; sed -n -e 's/^console [a-z]* *\[[0-9.]*\] //p' "$work/s.txt"; sed -n -e 's/^error *[a-z]* *\[[0-9.]*\] /error: /p' "$work/s.txt"; }
echo "-- each frame's boxes, in its own viewport, against Chromium's:"
for ref in "$pages"/*.frames; do
  name=$(basename "$ref" .frames)
  amb frames "$pages/$name.html" > "$work/amb.txt"
  awk -v name="$name" '
    FNR == NR { if (NF == 7) { k = $1 " " $2; ax[k] = $4; ay[k] = $5; aw[k] = $6; ah[k] = $7 } next }
    /^#/ { next }
    {
      total++
      k = $1 " " $2
      if (!(k in ax)) { printf "%s: %s #%s (%s) has no box; Chromium %s,%s %sx%s\n", name, $1, $2, $3, $4, $5, $6, $7; next }
      dx = ax[k] - $4; dy = ay[k] - $5; dw = aw[k] - $6; dh = ah[k] - $7
      worst = 0
      if ((dx < 0 ? -dx : dx) > worst) worst = dx < 0 ? -dx : dx
      if ((dy < 0 ? -dy : dy) > worst) worst = dy < 0 ? -dy : dy
      if ((dw < 0 ? -dw : dw) > worst) worst = dw < 0 ? -dw : dw
      if ((dh < 0 ? -dh : dh) > worst) worst = dh < 0 ? -dh : dh
      if (worst <= 1) { agree++; next }
      printf "%s: %s #%s (%s) differs by %.2fpx (Ambitious %s,%s %sx%s; Chromium %s,%s %sx%s)\n", name, $1, $2, $3, worst, ax[k], ay[k], aw[k], ah[k], $4, $5, $6, $7
    }
    END { printf "%s: %d of %d boxes in its frames within 1px of Chromium'"'"'s\n", name, agree, total }
  ' "$work/amb.txt" "$ref"
done
echo "-- the scripts across frames (frames-script.html), their console against Chromium's:"
console script "$pages/frames-script.html" > "$work/c.txt"
if cmp -s "$pages/frames-script.chromium" "$work/c.txt"; then echo "the same $(wc -l < "$work/c.txt" | tr -d ' ') lines"
else diff "$pages/frames-script.chromium" "$work/c.txt"; fi
echo "-- a frame's realm through the membrane (frames-realm.html), the console against Chromium's:"
console script "$pages/frames-realm.html" > "$work/r.txt"
if cmp -s "$pages/frames-realm.chromium" "$work/r.txt"; then echo "the same $(wc -l < "$work/r.txt" | tr -d ' ') lines"
else diff "$pages/frames-realm.chromium" "$work/r.txt"; fi
echo "-- frames over HTTP, of two origins (frames-http.html, served), against Chromium's:"
python3 "$AMB_ROOT/tests/integ/script-serve.py" "$pages" > "$work/port" 2> "$work/serve.err" &
pid=$!
i=0
while [ ! -s "$work/port" ] && [ $i -lt 40 ]; do sleep 0.25; i=$((i + 1)); done
port=$(head -1 "$work/port")
console script "http://127.0.0.1:$port/frames-http.html" | sed -e "s/:$port\b/:PORT/g" > "$work/h.txt"
if cmp -s "$pages/frames-http.chromium" "$work/h.txt"; then echo "the same $(wc -l < "$work/h.txt" | tr -d ' ') lines"
else diff "$pages/frames-http.chromium" "$work/h.txt"; fi
echo "-- the frames of the served page as facts: their iframes, URLs, sources and outcomes:"
amb query "http://127.0.0.1:$port/frames-http.html" "net_frame(F, I, D, U, S, O), dom_attr(I, id, Id)" | sed -e "s/:$port\b/:PORT/g"
echo "-- and what the reasoner says of them:"
amb advise "http://127.0.0.1:$port/frames-http.html" | grep -E '^amb_diag\((frame_|failed)' | sed -e "s/:$port\b/:PORT/g"
kill $pid
echo "-- the frames of frames.html: each frame's iframe (by id where its parent is the page), its document, URL, source, outcome:"
amb query "$pages/frames.html" "net_frame(F, I, D, U, S, O), ( dom_attr(I, id, Id) -> true ; Id = nested ), net_frame_parent(F, P)"
echo "-- their viewports, the frames deeper than the page's own, those in quirks mode and those sandboxed:"
amb query "$pages/frames.html" "net_frame_viewport(F, W, H, V, B)"
amb query "$pages/frames.html" "net_frame_depth(F, N), N > 1"
amb query "$pages/frames.html" "net_frame_quirks(F), net_frame(F, I, _, U, _, _)"
amb query "$pages/frames.html" "net_frame_sandbox(F, Scripts, SameOrigin)"
echo "-- the frames that scroll, and their bars:"
amb query "$pages/frames-scroll.html" "net_frame_scrolls(F, Bars), net_frame(F, I, _, _, _, _), dom_attr(I, id, Id)"
echo "-- the frame that frames its own page twice over, and the frame that failed to load, as advise says them:"
amb advise "$pages/frames-deep.html" | grep -E '^amb_diag\(frame_'
amb advise "$pages/frames.html" | grep -E '^amb_diag\((frame_|failed)'
echo "-- why a frame failed:"
amb why "$pages/frames.html" "net_frame_failed(F, Why)"
rm -rf "$work"
