#!/usr/bin/env python3
"""tools/gen-scripted-page.py [N] -- the benchmark's scripted page (PLAN.md section 11):
its script builds a list of N items (2,000 by default) with createElement,
textContent, className, setAttribute and appendChild; its button's click puts every
item on sale, toggling a class and rewriting a price in each, then a count. The
page is the same for every engine, so its numbers compare."""
import sys

n = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
print("""<!DOCTYPE html>
<html lang="en">
<head>
<title>A list of %d built by script</title>
<style>
  body { font-family: sans-serif; margin: 16px; }
  li { padding: 2px 4px; }
  li.odd { background: #f4f4ff; }
  li.sale .price { color: #b00; font-weight: bold; }
  .name { display: inline-block; width: 200px; }
</style>
</head>
<body>
<h1>Products</h1>
<p><button id="sale" type="button">Put every item on sale</button> <span id="count"></span></p>
<ul id="list"></ul>
<script>
(function () {
  /* inside a function: a global var name would be window.name, a string */
  var n = %d;
  var list = document.getElementById('list');
  for (var i = 0; i < n; i++) {
    var li = document.createElement('li');
    li.className = i %% 2 ? 'item odd' : 'item';
    var name = document.createElement('span');
    name.className = 'name';
    name.textContent = 'Item ' + (i + 1);
    var price = document.createElement('span');
    price.className = 'price';
    price.textContent = '$' + (10 + (i * 37) %% 90) + '.00';
    li.appendChild(name);
    li.appendChild(price);
    li.setAttribute('data-id', String(i));
    list.appendChild(li);
  }
  document.getElementById('count').textContent = list.children.length + ' items';
  document.getElementById('sale').addEventListener('click', function () {
    var items = list.querySelectorAll('li');
    for (var k = 0; k < items.length; k++) {
      var it = items[k];
      it.classList.toggle('sale');
      var p = it.querySelector('.price');
      var cents = Math.round(parseFloat(p.textContent.slice(1)) * 80);
      p.textContent = '$' + (cents / 100).toFixed(2);
    }
    document.getElementById('count').textContent = items.length + ' items on sale';
  });
})();
</script>
</body>
</html>""" % (n, n))
