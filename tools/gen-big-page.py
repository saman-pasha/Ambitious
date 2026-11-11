#!/usr/bin/env python3
"""tools/gen-big-page.py SECTIONS > page.html -- a large page to measure the engine with:
SECTIONS sections, each a heading, paragraphs with links and emphasis, a list and a
table, under a stylesheet of some sixty rules. Deterministic."""
import sys

n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
out = ["<!DOCTYPE html><html lang=en><head><title>A large page</title><style>"]
out.append("body { font: 15px/1.5 Georgia, serif; margin: 0 auto; max-width: 760px; padding: 0 12px }")
for i in range(20):
    out.append(".s%d h2 { color: #%02x3355; border-bottom: 1px solid #ccc } .s%d p:nth-child(odd) { background: #f8f8f8 }" % (i, (i * 37) % 256, i))
    out.append("section.s%d li:first-child { font-weight: bold } .s%d td { padding: 2px 6px; border: 1px solid #ddd }" % (i, i))
out.append("</style></head><body><h1>A large page</h1>")
words = "the quick brown fox jumps over a lazy dog while reasoning about every node of the tree".split()
for s in range(n):
    out.append('<section class="s%d" id="sec%d"><h2>Section %d</h2>' % (s % 20, s, s))
    for p in range(3):
        text = " ".join(words[(s + p + k) % len(words)] for k in range(40))
        out.append('<p>%s <a href="#sec%d">see %d</a>, <em>emphasis</em> and <strong>strength</strong>.</p>' % (text, (s + 1) % n, s + 1))
    out.append("<ul>" + "".join("<li>item %d.%d</li>" % (s, k) for k in range(4)) + "</ul>")
    out.append("<table>" + "".join("<tr><td>row %d</td><td>%d</td><td>%s</td></tr>" % (r, r * s, words[r]) for r in range(4)) + "</table></section>")
out.append("</body></html>")
sys.stdout.write("\n".join(out) + "\n")
