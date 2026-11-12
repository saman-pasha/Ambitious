#!/usr/bin/env python3
"""tools/gen-garbage-page.py [TICKS] [ITEMS] -- a page that makes garbage (docs/performance.md,
the collector's row): a timer that runs TICKS times (100 by default), 10 ms apart, each
time building ITEMS records (10,000 by default: an object, an array and a string each)
and keeping one string of them; at the end the page writes what it kept, and logs it.
Without a collector every tick's records stay until the page is closed; with one, a
tick's are garbage once the next begins."""
import sys

ticks = int(sys.argv[1]) if len(sys.argv) > 1 else 100
items = int(sys.argv[2]) if len(sys.argv) > 2 else 10000
print("""<!DOCTYPE html>
<html lang="en">
<head>
<title>A page that makes garbage</title>
</head>
<body>
<p id="out">working</p>
<script>
(function () {
  var ticks = 0, kept = [];
  function work() {
    var tmp = [];
    for (var i = 0; i < %d; i++) tmp.push({ i: i, s: 'item ' + i + ' of tick ' + ticks, pair: [i, i * 2] });
    kept.push(tmp[(ticks * 7919) %% tmp.length].s);
    ticks++;
    if (ticks < %d) { setTimeout(work, 10); return; }
    var said = ticks + ' ticks, ' + kept.length + ' kept, the last: ' + kept[kept.length - 1];
    document.getElementById('out').textContent = said;
    console.log(said);
  }
  setTimeout(work, 10);
})();
</script>
</body>
</html>""" % (items, ticks))
