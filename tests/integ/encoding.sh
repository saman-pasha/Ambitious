#!/bin/sh
# tests/integ/encoding.sh -- pages in the encodings of the WHATWG Encoding standard
# (tests/encoding/pages.txt names them; tests/encoding/pages.py writes them): each
# page's encoding and where it came from, as the reasoner has them from the snapshot,
# and its body's text, each held to what Chromium makes of the same bytes
# (tests/pages/NAME.chromium, from tests/encoding/pages.js). Then the diagnostics of
# the pages that declare no encoding, or whose bytes are not their encoding's.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-encoding.XXXXXX")
export AMB_WORK=$work
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1; }
where() { case "$1" in data:*) echo "$1" ;; *) echo "$AMB_ROOT/$1" ;; esac; }
grep -v '^#' "$AMB_ROOT/tests/encoding/pages.txt" > "$work/pages"
while read -r name page; do
  echo "-- $name"
  amb query "$(where "$page")" "html_encoding(E, S), html_decoding(A, N), dom_element(B, body), dom_text_content(B, T), atom_codes(T, C)" > "$work/q"
  sed 's/, B = n([0-9,]*), T = .*//' "$work/q"
  sed 's/.*, B = n([0-9,]*), \(T = .*\), C = \[[0-9,]*\]$/\1/' "$work/q"
  enc=$(sed "s/^E = '\{0,1\}\([^',]*\)'\{0,1\}, S = .*/\1/" "$work/q")
  codes=$(sed 's/.*, C = \(\[[0-9,]*\]\)$/\1/' "$work/q")
  ref="$AMB_ROOT/tests/pages/$name.chromium"
  if [ "$enc" = "$(sed -n 1p "$ref")" ]; then echo "the encoding is Chromium's"; else echo "the encoding is not Chromium's: $(sed -n 1p "$ref")"; fi
  k=$(echo "$codes" | tr ',' '\n' | wc -l | tr -d ' ')
  if [ "$codes" = "$(sed -n 2p "$ref")" ]; then echo "the text is Chromium's: $k code point$( [ "$k" = 1 ] || echo s)"
  else echo "the text is not Chromium's:"; sed -n 2p "$ref"; echo "$codes"; fi
done < "$work/pages"
echo "-- the diagnostics: what declares no encoding, what is malformed"
for name in enc-windows-1252 enc-utf-8-undeclared enc-utf-8-errors enc-replacement enc-shift_jis enc-http-koi8-r; do
  page=$(grep "^$name " "$work/pages" | cut -d' ' -f2)
  printf '%s: ' "$name"
  amb advise "$(where "$page")" > "$work/a"
  grep -E '^amb_diag\((undeclared_encoding|malformed_bytes|replaced_encoding),' "$work/a" || echo "none of the encoding's"
done
rm -rf "$work"
