#!/bin/sh
# tests/integ/script-cssom.sh -- the CSS Object Model (src/script/cssom.c; its model, parser and
# serializers src/css/cssom.c and cssom_decl.c; PLAN.md 12al). The pages' consoles must be
# Chromium's line for line (tests/pages/NAME.chromium, from tools/bench/console.js):
#   cssom-sheets.html: document.styleSheets and a shadow root's, each sheet's ownerNode,
#     href, title, media, disabled, type, parentStyleSheet and ownerRule; element.sheet; a
#     MediaList's changes; a linked or imported sheet of a file: page refused (SecurityError);
#     a style element's text, type or place changed (a new sheet), the same text set again
#     (the same one); a disabled sheet and a sheet's media as the cascade sees them;
#   cssom-rules.html: every rule class Chromium has, each one's cssText, type, parentRule,
#     parentStyleSheet and own attributes; CSSKeyframesRule's appendRule, deleteRule and
#     findRule; the setters (selectorText, keyText, name, a counter style's descriptors);
#     the prototypes and constants; a rule deleted;
#   cssom-text.html: selectors, preludes and shorthands written back as Chromium writes them;
#   cssom-edit.html: insertRule, deleteRule, addRule and removeRule on a sheet, a grouping rule
#     and a style rule, each refusal's exception and message, and each change as the next
#     rendering's styles show it;
#   cssom-adopt.html: new CSSStyleSheet() and its options, replace() and replaceSync() (@import
#     refused, with Chromium's warning), adoptedStyleSheets of a document and of shadow trees
#     (declarative ones among them) assigned, pushed, spliced and set by index, each refusal,
#     the order sheets apply in, a sheet adopted by two trees, a frame's sheet refused;
#   cssom-decl.html: CSSStyleDeclaration, a rule's and an element's: getPropertyValue,
#     getPropertyPriority, setProperty (important), removeProperty, cssText, length, item(),
#     custom properties, values and shorthands as Chromium writes them, an element's style
#     attribute after each change.
# Then the boxes of a page whose sheets its script changes (cssom-apply.html) are held to
# Chromium's by tests/integ/boxes.sh (tests/pages/cssom-apply.chromium).
AMB_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/amb-scssom.XXXXXX")
export AMB_WORK=$work
amb() { sh "$AMB_ROOT/bin/ambitious" "$@" < /dev/null 2>&1 | sed "s|$AMB_ROOT|ROOT|g"; }
pages="$AMB_ROOT/tests/pages"
# a page's console as Chromium's is written: its messages, then its uncaught errors
console() { amb script "$1" > "$work/s.txt"; sed -n -e 's/^console [a-z]* *\[[0-9]*\] //p' "$work/s.txt"; sed -n -e 's/^error *[a-z]* *\[[0-9]*\] /error: /p' "$work/s.txt"; }
for p in cssom-sheets cssom-rules cssom-text cssom-edit cssom-adopt cssom-decl; do
  echo "-- the $p page's console, against Chromium's:"
  console "$pages/$p.html" > "$work/c.txt"
  if cmp -s "$pages/$p.chromium" "$work/c.txt"; then echo "the same $(wc -l < "$work/c.txt" | tr -d ' ') lines"
  else diff "$pages/$p.chromium" "$work/c.txt"; fi
done
rm -rf "$work"
