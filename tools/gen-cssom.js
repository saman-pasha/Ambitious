// tools/gen-cssom.js > src/css/cssom_props.h -- Chromium's CSS properties as its CSSOM has them
// (Playwright's headless shell): each name a declaration may set, and what it is there.
// The names are the longhands getComputedStyle lists, each with -webkit- before it, and the
// shorthands and aliases Chromium's css_properties.json5 names (the list below); a name is
// one when CSS.supports(name: initial) holds. What it is comes from setting it to initial
// on an element's style and reading the longhands it then lists (CSSOM's item()): itself, a
// longhand; one other, an alias of that property; several, a shorthand of them, in that
// order, or an alias of a shorthand when cssText names another. The cssText's own name,
// for an alias, is the one it is serialized by.
const { chromium } = require('playwright');
const SHORTS = ('all animation animation-range background background-position border border-block border-block-color ' +
  'border-block-end border-block-start border-block-style border-block-width border-bottom border-color border-image ' +
  'border-inline border-inline-color border-inline-end border-inline-start border-inline-style border-inline-width ' +
  'border-left border-radius border-right border-spacing border-style border-top border-width column-rule columns ' +
  'contain-intrinsic-size container flex flex-flow font font-synthesis font-variant gap grid grid-area grid-column ' +
  'grid-gap grid-column-gap grid-row-gap grid-row grid-template inset inset-block inset-inline list-style margin ' +
  'margin-block margin-inline marker mask mask-border offset outline overflow overscroll-behavior padding ' +
  'padding-block padding-inline page-break-after page-break-before page-break-inside place-content place-items ' +
  'place-self position-try scroll-margin scroll-margin-block scroll-margin-inline scroll-padding scroll-padding-block ' +
  'scroll-padding-inline scroll-start scroll-timeline text-decoration text-emphasis text-spacing text-wrap transition ' +
  'view-timeline white-space -webkit-border-after -webkit-border-before -webkit-border-end -webkit-border-start ' +
  '-webkit-column-break-after -webkit-column-break-before -webkit-column-break-inside -webkit-mask-box-image ' +
  '-webkit-text-stroke -webkit-text-emphasis -webkit-mask -webkit-mask-position -webkit-margin-collapse word-wrap ' +
  '-webkit-flex -webkit-flex-flow -webkit-animation -webkit-transition -webkit-border-radius -webkit-columns ' +
  '-webkit-column-rule corner-shape text-box position-area interest-delay rule row-rule column-rule-break ' +
  '-epub-caption-side -epub-text-combine -epub-text-emphasis -epub-text-emphasis-color -epub-text-emphasis-style ' +
  '-epub-text-orientation -epub-text-transform -epub-word-break -epub-writing-mode -webkit-align-content ' +
  '-webkit-font-feature-settings -webkit-hyphenate-character -webkit-shape-image-threshold -webkit-shape-margin ' +
  '-webkit-shape-outside').split(' ');
(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.setContent('<!DOCTYPE html><div></div>');
  const rows = await page.evaluate((shorts) => {
    const names = new Set(shorts);
    const cs = getComputedStyle(document.body);
    for (let i = 0; i < cs.length; i++) { names.add(cs[i]); if (cs[i][0] !== '-') names.add('-webkit-' + cs[i]); }
    // each name's longhands; a longhand named only by a shorthand (column-height ...) a name too
    const classify = (n) => {
      if (!CSS.supports(n, 'initial')) return null;
      const d = document.createElement('div').style;
      d.setProperty(n, 'initial');
      const items = [];
      for (let i = 0; i < d.length; i++) items.push(d[i]);
      if (!items.length) return null;
      return [n, items, d.cssText.slice(0, d.cssText.indexOf(':'))];
    };
    for (let round = 0; round < 3; round++) {
      for (const n of [...names]) {
        const r = classify(n);
        if (r && n !== 'all') for (const it of r[1]) names.add(it);
      }
    }
    const out = [];
    for (const n of [...names].sort()) {
      const r = classify(n);
      if (r) out.push(r);
    }
    return out;
  }, SHORTS);
  await browser.close();
  // ids: the names in order; a shorthand's longhands, a span of LONGS
  const id = new Map(rows.map((r, i) => [r[0], i]));
  const longs = [];
  const lines = [];
  for (const [n, items, shown] of rows) {
    let kind, first = 0, cnt = 0, target = 0;
    if (n === 'all') { kind = 1; first = longs.length; cnt = 0; }
    else if (items.length === 1 && items[0] === n) kind = 0;
    else if (items.length === 1) { kind = 2; target = id.get(items[0]); }
    else if (shown !== n && id.has(shown)) { kind = 2; target = id.get(shown); }
    else {
      kind = 1;
      first = longs.length;
      cnt = items.length;
      for (const it of items) longs.push(id.get(it));
    }
    if (target === undefined) throw new Error('no id for the target of ' + n);
    lines.push(`    { "${n}", ${kind}, ${first}, ${cnt}, ${target} },`);
  }
  const head = `/* src/css/cssom_props.h -- Chromium's CSS properties as its CSSOM has them, written by
   tools/gen-cssom.js from Chromium (Playwright's headless shell); do not edit. Each name a
   declaration may set, in byte order (a property's number is its place): its kind (0 a
   longhand, 1 a shorthand, 2 an alias), a shorthand's longhands (their first place in
   css_om_longs and their count, in the order Chromium lists them), an alias's property.
   all is the shorthand of every longhand but direction and unicode-bidi (none listed). */
typedef struct css_om_pdef { const char *name; uint16_t kind, first, n, target; } css_om_pdef;
enum { CSS_OM_NPROPS = ${rows.length}, CSS_OM_NLONGS = ${longs.length} };
static const css_om_pdef css_om_pdefs[CSS_OM_NPROPS] = {
`;
  let longText = '';
  for (let i = 0; i < longs.length; i += 16) longText += '    ' + longs.slice(i, i + 16).join(', ') + ',\n';
  process.stdout.write(head + lines.join('\n') + '\n};\nstatic const uint16_t css_om_longs[CSS_OM_NLONGS] = {\n' + longText + '};\n');
})();
