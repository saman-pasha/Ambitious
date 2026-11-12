#!/bin/sh
# tests/net/dictcanon.sh -- a log of tests/net/h2serve.js --log-headers on stdin, as the dictionary gate
# holds a visit's requests to Chromium's: under each `-- N URL' line, each request's method and path,
# then the fields of its header block that Compression Dictionary Transport sets, in their order:
# accept-encoding, available-dictionary, dictionary-id. The requests are sorted by path (a page's parts
# come in an order of the browser's own); a favicon's is dropped.
tab=$(printf '\t')
awk -v OFS="$tab" '
  /^-- [0-9]+ / { v = $2 + 0; print v, "", 0, $0; skip = 1; next }
  /^conn [0-9]+ (stream|request) [0-9]+ [A-Z]+ / {
    p = $6; sub(/,$/, "", p); n = 0; skip = p ~ /favicon/
    if (!skip) print v, p, n++, $5 " " p
    next }
  /^conn / { skip = 1; next }
  /^  (accept-encoding|available-dictionary|dictionary-id): / { if (!skip) print v, p, n++, $0 }
' | sort -t "$tab" -k1,1n -k2,2 -k3,3n | cut -f4-
