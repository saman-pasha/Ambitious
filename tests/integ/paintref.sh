#!/bin/sh
# tests/integ/paintref.sh -- what paint draws against Chromium's screenshots: each fixture
# page whose screenshot is kept (tests/pages/NAME.shot.png, with its regions in
# NAME.regions: tools/bench/shot.js) drawn by Ambitious in the same 800x600 viewport (at
# the zoom NAME.zoom gives, set in a profile of its own, when it has one) and compared
# region by region (tools/pixdiff.py: the share of a region's pixels whose colours differ
# beyond anti-aliasing's tolerance). A region agrees when at most 1% of its pixels differ;
# a region of text (its id text-...) when the share of its ink that is anti-aliased grey
# is within 0.1 of Chromium's (text set at its size, not scaled up). Those that do not
# are printed; then how many of the page's regions agree.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-paintref.XXXXXX")
export AMB_WORK=$work
for shot in "$AMB_ROOT"/tests/pages/*.shot.png; do
  [ -e "$shot" ] || continue
  name=$(basename "$shot" .shot.png)
  prof=
  if [ -e "$AMB_ROOT/tests/pages/$name.zoom" ]; then
    prof="--profile $work/profile-$name"
    sh "$AMB_ROOT/bin/ambitious" $prof prefer zoom "$(cat "$AMB_ROOT/tests/pages/$name.zoom")" > /dev/null 2>&1
  fi
  sh "$AMB_ROOT/bin/ambitious" $prof render "$AMB_ROOT/tests/pages/$name.html" "$work/$name.png" > /dev/null 2>&1
  python3 -I "$AMB_ROOT/tools/pixdiff.py" "$work/$name.png" "$shot" "$AMB_ROOT/tests/pages/$name.regions" |
    awk -v name="$name" '
      $2 == "grey" { total++; d = $3 - $4; if (d < 0) d = -d
        if (d <= 0.1) agree++
        else printf "%s: #%s: %s of its ink is grey, %s of Chromium'"'"'s\n", name, $1, $3, $4
        next }
      { total++; share = $2; sub(/%/, "", share)
        if (share + 0 <= 1.0) agree++
        else printf "%s: #%s: %s of its pixels differ (at most %s)\n", name, $1, $2, $3 }
      END { printf "%s: %d of %d regions agree with Chromium'"'"'s drawing\n", name, agree, total }'
done
rm -rf "$work"
