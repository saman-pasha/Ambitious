#!/bin/sh
# tests/integ/schemeref.sh -- color-scheme against Chromium's: tests/pages/scheme.html (a
# root that takes either scheme, elements below that take their own) and scheme-meta.html
# (a page whose meta says dark) under a light preference and a dark one, as
# tools/bench/scheme.js gave Chromium them, set here in a profile of its own. Each element
# with an id: its computed colour, background colour and top border colour equal to
# Chromium's -- but a button's border, which Chromium computes as ButtonBorder and draws
# in its own grey, where Ambitious computes the grey it draws; and the canvas, the
# viewport's background. The elements that differ are printed; then how many agree.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-schemeref.XXXXXX")
export AMB_WORK=$work/session
prof=$work/profile
ref=$AMB_ROOT/tests/pages/scheme.chromium
amb() { sh "$AMB_ROOT/bin/ambitious" --profile "$prof" "$@" 2>&1; }
setting() {
  amb prefer color_scheme "$2" > /dev/null
  page=$AMB_ROOT/tests/pages/$1.html
  {
    amb query "$page" "layout_box(V, _, viewport, _, _, _, _), layout_background(V, X), R is X >> 24, G is (X >> 16) /\\ 255, B is (X >> 8) /\\ 255" |
      sed -n 's/.*R = \([0-9]*\), G = \([0-9]*\), B = \([0-9]*\).*/I = canvas, C = rgb(\1,\2,\3)/p'
    amb query "$page" "dom_attr(N, id, I), css_computed(N, color, C, _), css_computed(N, background_color, B, _), css_computed(N, border_top_color, T, _)"
  } > "$work/amb.txt"
  awk -v want="$1 $2" '
    function val(s) { sub(/^[^=]*= */, "", s); gsub(/'"'"'/, "", s); gsub(/ /, "", s); return s }
    FNR == NR {
      n = split($0, f, ", ")
      id = ""; c = b = t = ""
      for (i = 1; i <= n; i++) {
        if (f[i] ~ /^I = /) id = val(f[i])
        else if (f[i] ~ /^C = /) c = val(f[i])
        else if (f[i] ~ /^B = /) b = val(f[i])
        else if (f[i] ~ /^T = /) t = val(f[i])
      }
      # a colour with its own commas, rejoined: the fields after C, B, T up to the next name
      if (id != "" && !(id in ac)) {
        line = $0
        c = line; sub(/.*, C = /, "", c); sub(/, B = .*/, "", c); sub(/, T = .*/, "", c); gsub(/'"'"'/, "", c); gsub(/ /, "", c)
        if (line ~ /, B = /) { b = line; sub(/.*, B = /, "", b); sub(/, T = .*/, "", b); gsub(/'"'"'/, "", b); gsub(/ /, "", b) }
        if (line ~ /, T = /) { t = line; sub(/.*, T = /, "", t); gsub(/'"'"'/, "", t); gsub(/ /, "", t) }
        ac[id] = c; ab[id] = b; at[id] = t
      }
      next
    }
    /^== / { sect = substr($0, 4); next }
    /^#/ || sect != want { next }
    {
      total++
      id = $1
      if (!(id in ac)) { printf "%s: #%s has no value\n", want, id; next }
      if (id == "canvas") { if (ac[id] == $2) agree++; else printf "%s: the canvas: Ambitious %s; Chromium %s\n", want, ac[id], $2; next }
      border = id ~ /^button/ ? $4 : at[id]
      if (ac[id] == $2 && ab[id] == $3 && border == $4) { agree++; next }
      printf "%s: #%s: Ambitious %s %s %s; Chromium %s %s %s\n", want, id, ac[id], ab[id], at[id], $2, $3, $4
    }
    END { printf "%s: %d of %d agree with Chromium\n", want, agree, total }
  ' "$work/amb.txt" "$ref"
}
setting scheme light
setting scheme dark
setting scheme-meta light
setting scheme-meta dark
rm -rf "$work"
