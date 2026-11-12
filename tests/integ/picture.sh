#!/bin/sh
# tests/integ/picture.sh -- <picture>'s <source>s (tests/net/site/p/page.html): an img in a
# picture takes its image from the first source before it whose media query list holds in
# the 800x600 viewport, whose type the painter decodes and whose srcset has a candidate
# (by the source's own sizes); else from its own srcset and src. Each choice is a fact
# (net_picture_source, beside net_srcset's descriptor and density). Then each img's image,
# its natural size and its border box against Chromium's for the same page
# (tests/net/picture.chromium, which `node tests/net/picture.js tests/net/site/p/page.html`
# writes), img by img. Then a source's width and height as its img's (tests/net/site/p/dims.html,
# against tests/net/picture-dims.chromium); and images chosen in the profile's viewport, at the
# device pixel ratio its zoom makes, in its colour scheme and motion, a font-relative length
# of sizes in its font size (tests/net/site/e/page.html under five settings, against
# tests/net/env.chromium, which `node tests/net/env.js tests/net/site/e/page.html` writes).
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
# boxes ARGS: each img's image, natural size and border box, as tests/net/picture.js writes them
boxes() {
  amb "$@" "dom_attr(N, id, Id), net_asked_by(R, _, N), net_resource(R, U, image, _, _, _, _), net_image_size(R, W, H), layout_box_of(N, B), layout_border_box(B, X, Y, BW, BH)" |
    row "Id U W H X Y BW BH" | awk -F'|' '{ n = split($2, p, "/"); printf "%s %s %dx%d %s,%s %sx%s\n", $1, p[n], $3, $4, $5 + 0, $6 + 0, $7 + 0, $8 + 0 }'
}
# agree GOT REF [SECTION]: each img of REF's lines (those under "== SECTION" when one is named)
# against GOT's: the image and its natural size the same, the box within a tenth of a px
# (Chromium lays out in 64ths of a px, and at a zoom of 2.5 its image boxes come out up to
# 0.08 px short of their natural sizes)
agree() {
  awk -v want="$3" '
    FNR == NR { got[$1] = $0; next }
    /^== / { sect = substr($0, 4); next }
    sect != want { next }
    {
      total++
      if (!($1 in got)) { printf "%s#%s has no image\n", (want != "" ? want ": " : ""), $1; next }
      split(got[$1], g, " ")
      split($4, cp, ","); split(g[4], gp, ","); split($5, cs, "x"); split(g[5], gs, "x")
      d = 0
      for (i = 1; i <= 2; i++) {
        x = cp[i] - gp[i]; if (x < 0) x = -x; if (x > d) d = x
        x = cs[i] - gs[i]; if (x < 0) x = -x; if (x > d) d = x
      }
      if ($2 == g[2] && $3 == g[3] && d <= 0.1) { same++; next }
      printf "%sAmbitious %s; Chromium %s\n", (want != "" ? want ": " : ""), got[$1], $0
    }
    END { printf "%s%d of %d images the same\n", (want != "" ? want ": " : ""), same, total }' "$1" "$2"
}
echo "== a source's width and height, its img's when it takes that source (tests/net/site/p/dims.html), against Chromium's (tests/net/picture-dims.chromium):"
boxes query "$AMB_ROOT/tests/net/site/p/dims.html" > "$work/dims.got"
agree "$work/dims.got" "$AMB_ROOT/tests/net/picture-dims.chromium"
echo "-- the declarations that sized them: the source's attributes, or the img's own"
amb query "$AMB_ROOT/tests/net/site/p/dims.html" "member(Id, [both, wonly, nottaken]), dom_attr(N, id, Id), member(P, [width, height]), css_explain(N, P, X)" | sed 's/, N = n([0-9,]*)//'
echo "== images chosen in the profile's viewport, zoom, colour scheme, motion and font size (tests/net/site/e/page.html), against Chromium's under the same settings (tests/net/env.chromium):"
prof=$work/profile
for s in "defaults:16:light:no_preference:1" "zoom 2:16:light:no_preference:2" "zoom 2.5:16:light:no_preference:2.5" "dark, reduced motion:16:dark:reduce:1" "font 20:20:light:no_preference:1"; do
  name=${s%%:*}
  set -- $(echo "${s#*:}" | tr ':' ' ')
  amb --profile "$prof" prefer font_size "$1" > /dev/null
  amb --profile "$prof" prefer color_scheme "$2" > /dev/null
  amb --profile "$prof" prefer reduced_motion "$3" > /dev/null
  amb --profile "$prof" prefer zoom "$4" > /dev/null
  boxes --profile "$prof" query "$AMB_ROOT/tests/net/site/e/page.html" > "$work/env.got"
  agree "$work/env.got" "$AMB_ROOT/tests/net/env.chromium" "$name"
done
rm -rf "$work"
