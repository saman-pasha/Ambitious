#!/usr/bin/env python3
"""tools/cssref.py AMB REF -- Ambitious's computed values (AMB: `ambitious query`'s
answers to dom_attr(N, id, I), css_computed(N, P, V, _), and of custom properties
css_custom(N, P, V)) held to Chromium's (REF:
tools/bench/cssref.js's lines `id property value`). A value agrees when the two are the
same text, spaces aside, or both lengths in px within 0.02 px of each other (Chromium
gives a box's width as laid out, in 64ths of a px). Prints each that differs, then how
many agree."""
import re
import sys


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: tools/cssref.py AMB REF")
    ours = {}
    for line in open(sys.argv[1]):
        m = re.match(r".*\bI = '?([^',]*)'?, P = '?(--[A-Za-z0-9_-]+|[a-z_]+)'?, V = (.*), [A-Z][a-z]* = .*$", line.strip())
        if not m:
            m = re.match(r".*\bI = '?([^',]*)'?, P = '?(--[A-Za-z0-9_-]+|[a-z_]+)'?, V = (.*)$", line.strip())
        if not m:
            continue
        ours.setdefault((m.group(1), m.group(2)), m.group(3).strip("'").replace(" ", ""))
    total = agree = 0
    name = None
    for line in open(sys.argv[2]):
        if line.startswith("#"):
            m = re.search(r"tests/pages/(\S+)\.html", line)
            name = m.group(1) if m else "page"
            continue
        parts = line.split(" ", 2)
        if len(parts) != 3:
            continue
        ident, prop, want = parts[0], parts[1], parts[2].strip()
        total += 1
        have = ours.get((ident, prop if prop.startswith("--") else prop.replace("-", "_")))
        if have is None:
            print("%s: #%s %s: Ambitious has no value; Chromium %s" % (name, ident, prop, want))
            continue
        ok = have == want
        if not ok:
            a, b = re.fullmatch(r"(-?[0-9.e+-]+)px", have), re.fullmatch(r"(-?[0-9.e+-]+)px", want)
            ok = bool(a and b and abs(float(a.group(1)) - float(b.group(1))) <= 0.02)
        if ok:
            agree += 1
        else:
            print("%s: #%s %s: Ambitious %s; Chromium %s" % (name, ident, prop, have, want))
    print("%s: %d of %d values agree with Chromium's" % (name, agree, total))


if __name__ == "__main__":
    main()
