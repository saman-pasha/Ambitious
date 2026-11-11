#!/bin/sh
# tests/integ/loop.sh -- the DOM vertical slice end to end (the architecture's
# §6.3, criteria 7, 8 and 9), through the built binary: a document, its snapshot
# reasoned over in another process, the reasoner's requests validated and
# applied, the change in a second snapshot, the old batch refused, and the
# reasoner's answers held to the C side's own traversal.
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-loop.XXXXXX")
AMB_WORK=$work sh "$AMB_ROOT/bin/ambitious" loop
st=$?
rm -rf "$work"
echo "exit $st"
