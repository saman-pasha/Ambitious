#!/bin/sh
# tests/integ/sandbox.sh -- the reasoner's sandbox (Phase 12i, 2): limits, no network,
# a syscall filter, one directory to write in. First what a child given the sandbox
# finds it refused (the lines that hold on every kernel: the network and Landlock lines
# say which layer does it here). Then queries that reach past it, each refused (an
# error the reasoner reports) or ending the reasoner with the reason the shell gives:
# a socket, a file written or removed outside its directory, a new process, memory
# past its limit, CPU time past its limit. In a session, a reasoner that ended is
# started again with the whole snapshot, and one that does not answer in time is
# ended. Last, advice with the sandbox and without it is the same on four pages.
# Paths are written ROOT, the file outside ESCAPE.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-sandbox.XXXXXX")
export AMB_WORK=$work/session
escape=$work/escape.txt
pages=$AMB_ROOT/tests/pages
# an unbound variable's name in an answer is the reasoner's own, written _
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" 2>&1 | sed -e "s|$escape|ESCAPE|g" -e "s|$AMB_ROOT|ROOT|g" -e "s|$work|WORK|g" -e "s/_G[0-9]*/_/g"; }
echo "-- what the sandbox refuses a child:"
amb sandbox | grep -v '^network:\|^file writes:\|working directory'
echo "-- a socket:"
amb query "$pages/news.html" "use_module(library(tcp)), tcp_listen(0, L)"
echo "-- a file written outside its directory:"
amb query "$pages/news.html" "use_module(library(stream)), open('$escape', write, S), write(S, x), close(S)"
[ -e "$escape" ] && echo "ESCAPE WAS WRITTEN" || echo "ESCAPE is not there"
echo "-- a file removed outside its directory:"
cp "$pages/news.html" "$work/victim.html"
amb query "$pages/news.html" "delete_file('$work/victim.html')"
[ -e "$work/victim.html" ] && echo "the file is still there" || echo "THE FILE WAS REMOVED"
echo "-- a new process:"
amb query "$pages/news.html" "use_module(library(process)), proc_run('true', 1000, Out, Exit)"
echo "-- memory past its limit (300 MiB):"
AMB_REASON_MEM=300M amb query "$pages/news.html" "forall(between(1, 120, I), ( numlist(1, 200000, L), assertz(amb_big(I, L)) ))"
echo "-- and the same within the default limit:"
amb query "$pages/news.html" "forall(between(1, 120, I), ( numlist(1, 200000, L), assertz(amb_big(I, L)) )), aggregate_all(count, amb_big(_, _), N)"
echo "-- CPU time past its limit (1 s):"
AMB_REASON_CPU=1 amb query "$pages/news.html" "repeat, fail"
echo "-- in a session: the reasoner ended, then started again; one that does not answer in time:"
AMB_REASON_CPU=2 AMB_REASON_TIMEOUT=3 amb loop - <<EOF
open $pages/counter.html
query dom_attr(N, id, count), dom_text_content(N, T)
query repeat, fail
query dom_attr(N, id, count), dom_text_content(N, T)
query sleep(10)
reasoner
advise
reasoner
quit
EOF
echo "-- advice with the sandbox and without it:"
for p in news a11y style layout; do
  sh "$AMB_ROOT/bin/ambitious" advise "$pages/$p.html" > "$work/on.txt" 2>&1
  AMB_SANDBOX=0 sh "$AMB_ROOT/bin/ambitious" advise "$pages/$p.html" > "$work/off.txt" 2>&1
  if cmp -s "$work/on.txt" "$work/off.txt"; then echo "$p.html: the same $(wc -l < "$work/on.txt" | tr -d ' ') lines"
  else echo "$p.html: DIFFERENT"; diff "$work/off.txt" "$work/on.txt" | head -5; fi
done
rm -rf "$work"
