// tools/bench/chromium.js -- the reference engine's numbers for tools/bench/compare.py.
//
//   node tools/bench/chromium.js RUNS AXE_JS PAGE...
//
// Chromium (Playwright's, headless, one renderer, an 800x600 viewport at scale 1)
// loads each page RUNS times, each from about:blank, as `ambitious bench` loads it:
//   - warm: navigation to the load event, a forced layout, then a full-page PNG;
//   - stages: one traced load, the main thread's ParseHTML, style (UpdateLayoutTree,
//     ParseAuthorStyleSheet), Layout, paint (PrePaint, Paint, Layerize) and the
//     raster threads' RasterTask, summed by name;
//   - cold: a browser launched, the page loaded and drawn, the browser closed;
//   - audit: axe-core (AXE_JS) injected and run over the loaded page.
// It writes one JSON object to stdout: { page: { warm, stages, cold, audit } }.
'use strict';
const { chromium } = require('playwright');
const { pathToFileURL } = require('url');

const median = xs => { const s = [...xs].sort((a, b) => a - b); return s.length ? s[s.length >> 1] : NaN; };
const ms = t0 => Number(process.hrtime.bigint() - t0) / 1e6;

const STAGES = {
  parse: ['ParseHTML'],
  style: ['UpdateLayoutTree', 'ParseAuthorStyleSheet'],
  layout: ['Layout'],
  paint: ['PrePaint', 'Paint', 'Layerize', 'UpdateLayer'],
  raster: ['RasterTask', 'ImageDecodeTask'],
};

// the durations of the named events of a trace, in ms; nested events of one name
// (a Layout inside a Layout) counted once
function stageTimes(trace) {
  const events = JSON.parse(trace.toString()).traceEvents || [];
  const want = new Map();
  for (const [stage, names] of Object.entries(STAGES)) for (const n of names) want.set(n, stage);
  const out = Object.fromEntries(Object.keys(STAGES).map(k => [k, 0]));
  const open = new Map();   // tid|name -> [depth, start]
  for (const e of events) {
    const stage = want.get(e.name);
    if (!stage) continue;
    if (e.ph === 'X') {
      const key = `${e.pid}|${e.tid}|${e.name}`;
      // an X event inside another of the same name on the same thread starts before it ends
      const last = open.get(key);
      if (last && e.ts < last) continue;
      open.set(key, e.ts + (e.dur || 0));
      out[stage] += (e.dur || 0) / 1000;
    } else if (e.ph === 'B' || e.ph === 'E') {
      const key = `${e.pid}|${e.tid}|${e.name}|be`;
      const st = open.get(key) || [];
      if (e.ph === 'B') st.push(e.ts);
      else if (st.length) { const t = st.pop(); if (!st.length) out[stage] += (e.ts - t) / 1000; }
      open.set(key, st);
    }
  }
  return out;
}

async function loadAndDraw(page, url) {
  const t0 = process.hrtime.bigint();
  await page.goto(url, { waitUntil: 'load' });
  await page.evaluate(() => document.documentElement.getBoundingClientRect().height);
  const loaded = ms(t0);
  const png = await page.screenshot({ fullPage: true, type: 'png' });
  return { loaded, total: ms(t0), bytes: png.length };
}

async function main() {
  const [runsArg, axePath, ...pages] = process.argv.slice(2);
  const runs = Math.max(1, Number(runsArg) || 5);
  const result = {};
  const browser = await chromium.launch();
  const context = await browser.newContext({ viewport: { width: 800, height: 600 }, deviceScaleFactor: 1 });
  const page = await context.newPage();
  const tab = await context.newPage();          // the traced loads' own tab
  for (const p of pages) {
    const url = pathToFileURL(p).href;
    const warm = { loaded: [], total: [], png_bytes: 0, nav: null };
    await page.goto('about:blank');
    await loadAndDraw(page, url);              // a first load, not counted: the file cache warm
    for (let r = 0; r < runs; r++) {
      await page.goto('about:blank');
      const t = await loadAndDraw(page, url);
      warm.loaded.push(t.loaded); warm.total.push(t.total); warm.png_bytes = t.bytes;
    }
    warm.nav = await page.evaluate(() => {
      const n = performance.getEntriesByType('navigation')[0];
      return { response_to_dom_interactive: n.domInteractive - n.responseEnd, dom_content_loaded: n.domContentLoadedEventEnd,
               load_event_end: n.loadEventEnd, nodes: document.getElementsByTagName('*').length,
               height: document.documentElement.scrollHeight };
    });
    // the stages: traced loads, each from about:blank, summed then averaged
    const stages = Object.fromEntries(Object.keys(STAGES).map(k => [k, 0]));
    const traced = Math.min(runs, 3);
    for (let r = 0; r < traced; r++) {
      await tab.goto('about:blank');
      await browser.startTracing(tab, { categories: ['devtools.timeline', 'disabled-by-default-devtools.timeline', 'cc',
                                                      'disabled-by-default-cc.debug'] });
      await loadAndDraw(tab, url);
      const s = stageTimes(await browser.stopTracing());
      for (const k of Object.keys(stages)) stages[k] += s[k] / traced;
    }
    // the audit: axe-core, its script injected into the loaded page, then run
    let audit = null;
    if (axePath && axePath !== '-') {
      const inject = [], run = [];
      let violations = 0, nodes = 0;
      for (let r = 0; r < runs; r++) {
        await page.goto(url, { waitUntil: 'load' });
        const t0 = process.hrtime.bigint();
        await page.addScriptTag({ path: axePath });
        inject.push(ms(t0));
        const a = await page.evaluate(async () => {
          const t = performance.now();
          const res = await axe.run(document, { resultTypes: ['violations'] });
          return { ms: performance.now() - t, violations: res.violations.length,
                   nodes: res.violations.reduce((n, v) => n + v.nodes.length, 0) };
        });
        run.push(a.ms); violations = a.violations; nodes = a.nodes;
      }
      audit = { inject: median(inject), run: median(run), violations, nodes };
    }
    // cold: a browser of its own each time
    const cold = [];
    for (let r = 0; r < Math.min(runs, 5); r++) {
      const t0 = process.hrtime.bigint();
      const b = await chromium.launch();
      const c = await b.newContext({ viewport: { width: 800, height: 600 }, deviceScaleFactor: 1 });
      const pg = await c.newPage();
      await loadAndDraw(pg, url);
      await b.close();
      cold.push(ms(t0));
    }
    result[p] = { warm: { loaded: median(warm.loaded), total: median(warm.total), png_bytes: warm.png_bytes, nav: warm.nav },
                  stages, cold: median(cold), audit };
  }
  await browser.close();
  process.stdout.write(JSON.stringify(result, null, 1) + '\n');
}

main().catch(e => { console.error(e); process.exit(1); });
