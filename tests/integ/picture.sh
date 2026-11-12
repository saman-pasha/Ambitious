#!/bin/sh
# tests/integ/picture.sh -- <picture>'s <source>s (tests/net/site/p/page.html): an img in a
# picture takes its image from the first source before it whose media query list holds in
# the 800x600 viewport, whose type the painter decodes and whose srcset has a candidate
# (by the source's own sizes); else from its own srcset and src. Each choice is a fact
# (net_picture_source, beside net_srcset's descriptor and density). Then each img's image,
# its natural size and its border box against Chromium's for the same page
# (tests/net/picture.chromium, which `node tests/net/picture.js tests/net/site/p/page.html`
# writes), img by img.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-picture.XXXXXX")
export AMB_WORK=$work
page=$AMB_ROOT/tests/net/site/p/page.html
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1; }
# the Key = value pairs of a query's answers (split at ", " outside a quoted atom), the
# keys named, a row each, separated by `|'
row() {
  awk -v keys="$1" -v q="'" '
    BEGIN { nk = split(keys, k, " ") }
    {
      n = 0; inq = 0; cur = ""
      for (i = 1; i <= length($0); i++) {
        c = substr($0, i, 1)
        if (inq) {
          if (c == "\\") { cur = cur substr($0, i + 1, 1); i++; continue }
          if (c == q) { inq = 0; continue }
          cur = cur c; continue
        }
        if (c == q) { inq = 1; continue }
        if (c == "," && substr($0, i + 1, 1) == " ") { part[++n] = cur; cur = ""; i++; continue }
        cur = cur c
      }
      part[++n] = cur
      split("", v)
      for (i = 1; i <= n; i++) { e = index(part[i], " = "); if (e) v[substr(part[i], 1, e - 1)] = substr(part[i], e + 3) }
      line = ""
      for (j = 1; j <= nk; j++) line = line (j > 1 ? "|" : "") v[k[j]]
      print line
    }'
}
echo "== the source each img took (its media and type), or its own srcset and src (img):"
amb query "$page" "dom_attr(N, id, Id), net_asked_by(R, _, N), ( net_picture_source(R, S) -> ( dom_attr(S, media, M) -> true ; M = '-' ), ( dom_attr(S, type, T) -> true ; T = '-' ) ; M = img, T = img )" |
  row "Id M T"
echo "-- and the candidate it chose there: its descriptor, density and source size, of how many:"
amb query "$page" "dom_attr(N, id, Id), net_asked_by(R, _, N), net_srcset(R, D, X, S, C)" | row "Id D X S C"
echo "== each img's image, natural size and border box, against Chromium's (tests/net/picture.chromium):"
amb query "$page" "dom_attr(N, id, Id), net_asked_by(R, _, N), net_resource(R, U, image, _, _, _, _), net_image_size(R, W, H), layout_box_of(N, B), layout_border_box(B, X, Y, BW, BH)" |
  row "Id U W H X Y BW BH" | awk -F'|' '{ n = split($2, p, "/"); printf "%s %s %dx%d %s,%s %sx%s\n", $1, p[n], $3, $4, $5 + 0, $6 + 0, $7 + 0, $8 + 0 }' > "$work/picture.got"
if diff "$AMB_ROOT/tests/net/picture.chromium" "$work/picture.got"; then echo "the same, img by img ($(wc -l < "$work/picture.got" | tr -d ' ') of them)"; fi
rm -rf "$work"
