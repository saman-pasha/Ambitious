// tools/bench/js/suite.js -- the JavaScript workloads every engine is timed on:
// node (V8), node --jitless (V8's interpreter alone), QuickJS, and Ambitious.
// Each line printed is `NAME MS CHECKSUM`; the checksums must agree across engines.
var clock = (typeof performance !== 'undefined' && performance.now) ? function () { return performance.now(); }
          : (typeof now === 'function' ? now : function () { return Date.now(); });
var out = (typeof console !== 'undefined' && console.log) ? function (s) { console.log(s); } : print;

function bench(name, f) {
  var t0 = clock();
  var r = f();
  var t = clock() - t0;
  out(name + ' ' + t.toFixed(1) + ' ' + r);
}

bench('loop', function () {
  var s = 0;
  for (var i = 0; i < 3000000; i++) s = (s + i * i) % 1000003;
  return s;
});

bench('fib', function () {
  function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
  return fib(25);
});

bench('objects', function () {
  var a = [];
  for (var i = 0; i < 200000; i++) a.push({ x: i, y: i * 2, tag: 'p' + (i % 7) });
  var s = 0;
  for (var j = 0; j < a.length; j++) s = (s + a[j].x + a[j].y + a[j].tag.length) % 1000003;
  return s;
});

bench('classes', function () {
  class Vec {
    constructor(x, y) { this.x = x; this.y = y; }
    add(o) { return new Vec(this.x + o.x, this.y + o.y); }
    len2() { return this.x * this.x + this.y * this.y; }
  }
  var v = new Vec(0, 0), d = new Vec(1, 2), s = 0;
  for (var i = 0; i < 300000; i++) { v = v.add(d); s = (s + v.len2()) % 1000003; }
  return s;
});

bench('closures', function () {
  function counter() { var n = 0; return function (k) { n += k; return n; }; }
  var c = counter(), s = 0;
  var twice = function (f) { return function (x) { return f(f(x)); }; };
  var inc = twice(function (x) { return x + 1; });
  for (var i = 0; i < 500000; i++) s = (c(i % 3) + inc(i)) % 1000003;
  return s;
});

bench('strings', function () {
  var parts = [];
  for (var i = 0; i < 50000; i++) parts.push('item' + i);
  var s = parts.join(',');
  var back = s.split(',');
  var n = 0;
  for (var j = 0; j < back.length; j += 97) n += s.indexOf(back[j]) % 1000;
  var t = '';
  for (var k = 0; k < 20000; k++) t += String.fromCharCode(97 + (k % 26));
  return (n + back.length + t.toUpperCase().charCodeAt(1234) + `${t.length}:${s.length}`.length) % 1000003;
});

bench('arrays', function () {
  var a = [];
  for (var i = 0; i < 100000; i++) a.push((i * 7919) % 100003);
  var b = a.map(function (x) { return x * 2; }).filter(function (x) { return x % 3 === 0; });
  var r = b.reduce(function (s, x) { return (s + x) % 1000003; }, 0);
  var c = a.slice(0, 20000).sort(function (x, y) { return x - y; });
  return (r + c[0] + c[19999] + a.indexOf(4242)) % 1000003;
});

bench('json', function () {
  var data = [];
  for (var i = 0; i < 5000; i++) data.push({ id: i, name: 'n' + i, tags: ['a', 'b', i % 5], ok: i % 2 === 0 });
  var s = JSON.stringify(data);
  var back = JSON.parse(s);
  var n = 0;
  for (var j = 0; j < back.length; j++) n += back[j].tags[2] + (back[j].ok ? 1 : 0);
  return (n + s.length) % 1000003;
});
