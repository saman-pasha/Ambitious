#!/bin/sh
# tests/net/tlscanon.sh pool|resume NAME=LOG... -- the logs of tests/net/h2serve.js --log-tls
# servers (their ports already written by name), each named NAME, as the gate holds a visit's
# connections to Chromium's: under each `-- N URL' line, the lines of each server in turn, each
# headed by its NAME:
#
#   pool    each connection, its SNI (none for an address) and the requests it carried, each
#           its authority and path, sorted; the connections sorted (which opens first is a
#           browser's race)
#   resume  for each host (its first request's authority: a resumed TLS 1.2 session shows no
#           SNI), how many connections there were, how many resumed a TLS session and how
#           many made a full handshake (which of a page's parts made at once gets a session is
#           a race too)
#
# A favicon's request is dropped, and a connection that carried none (one Chromium made in a race
# of its connect jobs, a second for a host whose first connection it did not yet know spoke h2,
# and left unused).
mode=$1
shift
tab=$(printf '\t')
for arg in "$@"; do
  name=${arg%%=*}
  awk -v mode="$mode" -v name="$name" -v OFS="$tab" '
    function flush(   c, s, line, k, n, i, j, t, out, r) {
      if (v == "") return
      n = 0
      if (mode == "pool") {
        for (c in sni) {
          if (!nreq[c]) continue
          k = 0
          for (i = 1; i <= nreq[c]; i++) r[++k] = req[c, i]
          for (i = 1; i <= k; i++) for (j = i + 1; j <= k; j++) if (r[j] < r[i]) { t = r[i]; r[i] = r[j]; r[j] = t }
          line = name " sni=" sni[c] ":"
          for (i = 1; i <= k; i++) line = line (i > 1 ? "," : "") " " r[i]
          out[++n] = line
        }
      } else {
        for (c in state) { if (host[c] == "") continue; s = host[c]; total[s]++; if (state[c] == "resumed") resumed[s]++; else full[s]++ }
        for (s in total) out[++n] = sprintf("%s %s: %d connection%s, %d resumed, %d full", name, s, total[s], total[s] == 1 ? "" : "s", resumed[s] + 0, full[s] + 0)
      }
      for (i = 1; i <= n; i++) for (j = i + 1; j <= n; j++) if (out[j] < out[i]) { t = out[i]; out[i] = out[j]; out[j] = t }
      for (i = 1; i <= n; i++) print v, 1, out[i]
      split("", sni); split("", nreq); split("", req); split("", total); split("", resumed); split("", full); split("", state); split("", host)
    }
    /^-- [0-9]+ / { flush(); v = $2; print v, 0, $0; next }
    /^conn [0-9]+ (h2|http\/1\.1) TLSv[0-9.]+ sni=[^ ]+ (full|resumed)$/ {
      c = $2; sni[c] = substr($5, 5); nreq[c] = 0; state[c] = $6; host[c] = ""
      next }
    /^conn [0-9]+ (stream|request) [0-9]+ [A-Z]+ / {
      if ($0 ~ /favicon/) next
      c = $2; p = $6; sub(/,$/, "", p); a = $0; sub(/^.*, from /, "", a)
      if (!(c in sni)) { sni[c] = "?"; nreq[c] = 0 }
      if ((c in host) && host[c] == "") host[c] = a
      req[c, ++nreq[c]] = a " " p
      next }
    END { flush() }
  ' "${arg#*=}"
done | sort -s -t "$tab" -k1,1n -k2,2n | awk -F "$tab" '$2 == 0 { if ($3 == last) next; last = $3 } { print $3 }'
