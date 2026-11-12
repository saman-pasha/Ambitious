#!/bin/sh
# tests/integ/quirks.sh -- the document's mode as the reasoner has it: the HTML parser's mode kept
# on the document, a fact (dom_mode/2) for a quirks-mode page and for a limited-quirks one, none for
# a standards-mode one, and dom_document_mode/2 derived for each. Which mode a DOCTYPE gives is
# tests/exec/quirks_mode's (Chromium's, for 107 document starts); what each mode does, the
# quirks-*.html pages' gates (boxes.sh, paintref.sh, cssref.sh: document.compatMode among them).
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-quirks.XXXXXX")
export AMB_WORK=$work
pages=$AMB_ROOT/tests/pages
for p in quirks-css quirks-limited layout; do
  echo "-- $p:"
  sh "$AMB_ROOT/bin/ambitious" query "$pages/$p.html" "dom_mode(D, M)" 2>&1
  sh "$AMB_ROOT/bin/ambitious" query "$pages/$p.html" "dom_document_mode(D, M)" 2>&1
done
rm -rf "$work"
