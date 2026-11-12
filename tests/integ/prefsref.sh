#!/bin/sh
# tests/integ/prefsref.sh -- the reader's preferences against Chromium's: tests/pages/
# prefs.html under each setting tools/bench/prefs.js gave Chromium (the defaults, a
# default font size of 20 px, a dark scheme with reduced motion, a zoom of 2), set here
# in a profile of its own. Each element with an id: its border box held to Chromium's
# within 1px, its computed font-size and colour equal to Chromium's. The elements that
# differ are printed; then how many agree under each setting.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-prefsref.XXXXXX")
export AMB_WORK=$work/session
prof=$work/profile
page=$AMB_ROOT/tests/pages/prefs.html
ref=$AMB_ROOT/tests/pages/prefs.chromium
amb() { sh "$AMB_ROOT/bin/ambitious" --profile "$prof" "$@" 2>&1; }
setting() {
  # the profile's preferences, then the page's ids, boxes, sizes and colours
  amb prefer font_size "$2" > /dev/null; amb prefer color_scheme "$3" > /dev/null
  amb prefer reduced_motion "$4" > /dev/null; amb prefer zoom "$5" > /dev/null
  amb query "$page" "dom_attr(N, id, I), layout_box_of(N, B), layout_border_box(B, X, Y, W, H), css_computed(N, font_size, F, _), css_computed(N, color, C, _)" > "$work/amb.txt"
  awk -v want="$1" '
    function val(s) { sub(/^[^=]*= */, "", s); gsub(/'"'"'/, "", s); gsub(/ /, "", s); return s }
    FNR == NR {
      n = split($0, f, ", ")
      id = ""; x = y = w = h = fs = c = ""
      for (i = 1; i <= n; i++) {
        if (f[i] ~ /^I = /) id = val(f[i])
        else if (f[i] ~ /^X = /) x = val(f[i])
        else if (f[i] ~ /^Y = /) y = val(f[i])
        else if (f[i] ~ /^W = /) w = val(f[i])
        else if (f[i] ~ /^H = /) h = val(f[i])
        else if (f[i] ~ /^F = /) fs = val(f[i])
      }
      c = $0; sub(/.*, C = /, "", c); gsub(/'"'"'/, "", c); gsub(/ /, "", c)
      if (id != "" && !(id in ax)) { ax[id] = x; ay[id] = y; aw[id] = w; ah[id] = h; af[id] = fs; ac[id] = c }
      next
    }
    /^== / { sect = substr($0, 4); next }
    /^#/ || sect != want { next }
    {
      total++
      id = $1
      if (!(id in ax)) { printf "%s: #%s has no box\n", want, id; next }
      dx = ax[id] - $2; dy = ay[id] - $3; dw = aw[id] - $4; dh = ah[id] - $5
      worst = 0
      if ((dx < 0 ? -dx : dx) > worst) worst = dx < 0 ? -dx : dx
      if ((dy < 0 ? -dy : dy) > worst) worst = dy < 0 ? -dy : dy
      if ((dw < 0 ? -dw : dw) > worst) worst = dw < 0 ? -dw : dw
      if ((dh < 0 ? -dh : dh) > worst) worst = dh < 0 ? -dh : dh
      if (worst <= 1 && af[id] == $6 && ac[id] == $7) { agree++; next }
      printf "%s: #%s: Ambitious %s,%s %sx%s %s %s; Chromium %s,%s %sx%s %s %s\n", want, id, ax[id], ay[id], aw[id], ah[id], af[id], ac[id], $2, $3, $4, $5, $6, $7
    }
    END { printf "%s: %d of %d elements agree with Chromium\n", want, agree, total }
  ' "$work/amb.txt" "$ref"
}
setting "defaults" 16 light no_preference 1
setting "font 20" 20 light no_preference 1
setting "dark, reduced motion" 16 dark reduce 1
setting "zoom 2" 16 light no_preference 2
rm -rf "$work"
