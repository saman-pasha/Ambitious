#!/bin/sh
# tests/integ/prefs.sh -- the preferences that change how pages are drawn (Phase 12i,
# 4): base font size, colour scheme, reduced motion, zoom. The profile keeps them, each
# checked (font_size 6 to 72 px, color_scheme light or dark, reduced_motion
# no_preference or reduce, zoom 0.25 to 5) and each with its default; its rules state
# what is wrong with them: text drawn under 9 px, and a value kept from before values
# were checked (written here straight into the store). The shell reads them for every
# page command and session and hands them to the stages: the cascade reads the default
# font size, the colour scheme and reduced motion, layout the viewport the zoom leaves,
# paint draws zoomed (tests/integ/prefsref.sh holds them to Chromium's). Paths are
# written WORK.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
. "$AMB_ROOT/toolchain/env.sh"
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-prefs.XXXXXX")
export AMB_WORK=$work/session
prof=$work/profile
amb() { sh "$AMB_ROOT/bin/ambitious" --profile "$prof" "$@" 2>&1 | sed -e "s|$work|WORK|g"; }
echo "-- with no profile there are none to keep:"
sh "$AMB_ROOT/bin/ambitious" prefs 2>&1
echo "-- the defaults, then the reader's, each checked:"
amb prefs
amb prefer font_size 20
amb prefer color_scheme dark
amb prefer reduced_motion reduce
amb prefer zoom 1.5
amb prefer font_size 200
amb prefer color_scheme blue
amb prefer zoom 0
amb prefs
amb profile "amb_profile_fault(K, S, T)"
echo "-- text drawn under 9 px:"
amb prefer font_size 6
amb prefer zoom 0.5
amb profile "amb_profile_text_px(Px), amb_profile_fault(K, S, T)"
echo "-- a value kept from before values were checked:"
amb prefer font_size 18
amb prefer zoom 1
timeout 120 "$AMB_COCOLOG" --embed "$prof" query "retractall(amb_profile_pref(zoom, _)), assertz(amb_profile_pref(zoom, 9))" > /dev/null 2>&1 < /dev/null
amb profile "amb_profile_fault(K, S, T)"
amb prefs
echo "-- what the shell hands the stages, in a session and in a private one:"
amb loop - <<EOF
prefs
quit
EOF
sh "$AMB_ROOT/bin/ambitious" --private loop - 2>&1 <<EOF
prefs
quit
EOF
echo "-- a page drawn with the reader's preferences and with none:"
amb render "$AMB_ROOT/tests/pages/prefs.html" "$work/with.png" | sed 's/^/with:    /'
sh "$AMB_ROOT/bin/ambitious" render "$AMB_ROOT/tests/pages/prefs.html" "$work/none.png" 2>&1 | sed 's/^/without: /'
echo "-- zoomed 2: laid out in a 400 x 300 px viewport, drawn twice as large:"
amb prefer zoom 2 > /dev/null
amb render "$AMB_ROOT/tests/pages/prefs.html" "$work/zoom.png"
rm -rf "$work"
