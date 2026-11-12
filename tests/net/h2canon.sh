#!/bin/sh
# tests/net/h2canon.sh -- a log of tests/net/h2serve.js (--log-headers, and --log-frames) on
# stdin, as the HTTP/2 gate holds a visit's requests to Chromium's: under each `-- N URL'
# line, each request's method and path; its HEADERS frame's priority, its weight and whether
# its dependency is exclusive (which stream it depends on follows which streams are open, so
# the order in which a browser asks); then its header block, a field a line, as the server
# decoded it. The requests are sorted by path (a browser asks for a page's parts in an order of
# its own). Written alike on both sides: the browser's own name and version (user-agent,
# sec-ch-ua), and in accept the formats Ambitious does not offer, the painter drawing none of
# them (image/avif, image/svg+xml, application/signed-exchange: src/net/net.c's
# net_accepts). A favicon's request is dropped, and the log's other lines (connections, the
# other frames, HPACK's table) left out.
tab=$(printf '\t')
awk -v OFS="$tab" '
  /^-- [0-9]+ / { v = $2 + 0; print v, "", 0, $0; skip = 1; next }
  /^conn [0-9]+ > HEADERS stream / {
    s = $6; sub(/,$/, "", s); w = $0
    if (w ~ /, weight /) { sub(/^.*, on [0-9]+ ?/, "", w); sub(/, its block.*$/, "", w); if (w !~ /^exclusively/) w = "not exclusively" w }
    else w = "none"
    pri[$2 " " s] = w
    next }
  /^conn [0-9]+ stream [0-9]+ [A-Z]+ [^ ]+, [0-9]+ open$/ {
    p = $6; sub(/,$/, "", p); n = 0; skip = p ~ /favicon/
    if (!skip) {
      print v, p, n++, $5 " " p
      if (($2 " " $4) in pri) print v, p, n++, "  (its HEADERS frame'"'"'s priority: " pri[$2 " " $4] ")"
    }
    next }
  /^conn / { skip = 1; next }
  /^  / {
    if (skip) next
    line = $0
    if (line ~ /^  user-agent: /) line = "  user-agent: (the browser'"'"'s)"
    if (line ~ /^  sec-ch-ua: /) line = "  sec-ch-ua: (the browser'"'"'s)"
    if (line ~ /^  accept: /) { gsub(/image\/avif,|image\/svg\+xml,|,application\/signed-exchange;v=b3;q=0\.7/, "", line) }
    print v, p, n++, line }
' | sort -t "$tab" -k1,1n -k2,2 -k3,3n | cut -f4-
