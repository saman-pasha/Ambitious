#!/usr/bin/env python3
"""tools/gen-cssom-page.py [N] [K] [--control] -- the CSSOM's cost page (PLAN.md 12al): a list of N
items (2,000 by default) under a sheet of N/10 rules, and a script that K times (100 by default)
inserts a rule at the end of the page's sheet with insertRule and reads an item's computed
colour (a restyle each time, as the cascade sees the sheet then). With --control the script
toggles a class on the item it reads instead, the same reads after it: a page that touches no CSSOM, its
restyles as many. Each prints its loop's milliseconds (performance.now(), Chromium's own clock;
the engine's script time is `ambitious bench`'s) and the colour it read last."""
import sys

args = [a for a in sys.argv[1:] if not a.startswith('--')]
control = '--control' in sys.argv
n = int(args[0]) if len(args) > 0 else 2000
k = int(args[1]) if len(args) > 1 else 100
rules = '\n'.join('  .r%d { margin-left: %dpx; }' % (i, i % 7) for i in range(max(1, n // 10)))
items = '\n'.join('<li class="item r%d" id="i%d">item %d <span class="price">%d.00</span></li>' % (i % max(1, n // 10), i, i, i % 97)
                  for i in range(n))
if control:
    change = "probe.className = (i % 2) ? 'item on' : 'item off';"
    label = 'control'
else:
    change = "sheet.insertRule('#i0 { color: rgb(' + (i % 256) + ', 0, 0) }', sheet.cssRules.length);"
    label = 'insertRule'
page = '''<!DOCTYPE html>
<html lang="en">
<head>
<title>CSSOM cost: %(n)d items, %(k)d restyles (%(label)s)</title>
<style id="s">
  body { font-family: sans-serif; margin: 16px; }
  li { padding: 2px 4px; }
  li.on { color: rgb(0, 0, 255); }
  .price { color: #b00; }
%(rules)s
</style>
</head>
<body>
<ul id="list">
%(items)s
</ul>
<script>
(function () {
  var sheet = document.getElementById('s').sheet, list = document.getElementById('list');
  var probe = document.getElementById('i0'), c = '';
  var t0 = performance.now();
  for (var i = 0; i < %(k)d; i++) {
    %(change)s
    c = getComputedStyle(probe).color;
  }
  console.log('%(label)s: %(k)d restyles, ' + (performance.now() - t0).toFixed(1) + ' ms, ' + c);
})();
</script>
</body>
</html>''' % {'n': n, 'k': k, 'label': label, 'rules': rules, 'items': items, 'change': change}
print(page)
