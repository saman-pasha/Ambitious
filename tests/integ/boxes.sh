#!/bin/sh
# tests/integ/boxes.sh -- Ambitious's boxes against Chromium's. Each fixture page whose
# Chromium boxes are kept (tests/pages/NAME.chromium, written by tools/bench/boxes.js:
# getBoundingClientRect() of each element with an id) is laid out by Ambitious in the
# same 800x600 viewport, and each such element's border box, from the layout facts (an
# inline element's from its fragments and its text's), is held to Chromium's within
# 1px. The elements that differ are printed, with by how much each edge does; then how
# many of the page's boxes agree.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-boxes.XXXXXX")
export AMB_WORK=$work
for ref in "$AMB_ROOT"/tests/pages/*.chromium; do
  head -1 "$ref" | grep -q '^# boxes' || continue
  name=$(basename "$ref" .chromium)
  sh "$AMB_ROOT/bin/ambitious" query "$AMB_ROOT/tests/pages/$name.html" \
    "dom_attr(N, id, I), ( layout_box_of(N, B), layout_border_box(B, X, Y, W, H) ; layout_inline_box(N, X, Y, W, H) )" > "$work/amb.txt" 2>&1
  awk -v name="$name" '
    function val(s) { sub(/^[^=]*= */, "", s); gsub(/'"'"'/, "", s); return s }
    FNR == NR {
      n = split($0, f, ", ")
      id = ""; x = y = w = h = ""
      for (i = 1; i <= n; i++) {
        if (f[i] ~ /^I = /) id = val(f[i])
        else if (f[i] ~ /^X = /) x = val(f[i])
        else if (f[i] ~ /^Y = /) y = val(f[i])
        else if (f[i] ~ /^W = /) w = val(f[i])
        else if (f[i] ~ /^H = /) h = val(f[i])
      }
      if (id != "" && !(id in ax)) { ax[id] = x; ay[id] = y; aw[id] = w; ah[id] = h }
      next
    }
    /^#/ { next }
    {
      total++
      id = $1
      if (!(id in ax)) { printf "%s: #%s (%s) has no box; Chromium %s,%s %sx%s\n", name, id, $2, $3, $4, $5, $6; next }
      dx = ax[id] - $3; dy = ay[id] - $4; dw = aw[id] - $5; dh = ah[id] - $6
      worst = 0
      if (dx < 0 ? -dx : dx) worst = dx < 0 ? -dx : dx
      if ((dy < 0 ? -dy : dy) > worst) worst = dy < 0 ? -dy : dy
      if ((dw < 0 ? -dw : dw) > worst) worst = dw < 0 ? -dw : dw
      if ((dh < 0 ? -dh : dh) > worst) worst = dh < 0 ? -dh : dh
      if (worst <= 1) { agree++; next }
      printf "%s: #%s (%s) differs by %.2fpx: x %+.2f, y %+.2f, width %+.2f, height %+.2f (Ambitious %s,%s %sx%s; Chromium %s,%s %sx%s)\n", \
        name, id, $2, worst, dx, dy, dw, dh, ax[id], ay[id], aw[id], ah[id], $3, $4, $5, $6
    }
    END { printf "%s: %d of %d boxes within 1px of Chromium'"'"'s\n", name, agree, total }
  ' "$work/amb.txt" "$ref"
done
rm -rf "$work"
