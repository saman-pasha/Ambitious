// tools/bench/axtree.js PAGE -- the page loaded in Chromium (Playwright's headless shell) and its
// accessibility tree (the DevTools protocol's Accessibility.getFullAXTree) printed a line a node,
// for each node whose element has an id (a shadow tree's too, open or closed), in the tree's order: the reference that the expected
// trees of the accessibility fixtures (tests/pages/a11y-*.chromium) come from. A line is
//
//   ROLE#ID "NAME" desc "DESCRIPTION" level=N checked=V selected=true expanded=V pressed=V
//   disabled=true required=true readonly=true invalid=true busy=true haspopup=V orientation=V
//   multiselectable=true modal=true autocomplete=V multiline=true value="V" placeholder="P"
//   valuenow=N valuemin=N valuemax=N live=P atomic=B relevant="R"
//
// each part only when the node has it, in that order: the form tests/integ/a11y-chromium.sh
// writes Ambitious's tree in. What is read, and how:
//   - names and descriptions trimmed, white space collapsed;
//   - Chromium's own roles mapped to WAI-ARIA's: image is img, a details element's
//     DisclosureTriangle is the button it is in ARIA, and a label's LabelText and a legend's
//     Legend are generic, as HTML-AAM gives those elements no role;
//   - a true-or-false state only when true, but checked, pressed and expanded, whose false says
//     something; invalid's grammar and spelling are true, as Chromium has them; haspopup false
//     and autocomplete none are no state;
//   - a range's value, least and greatest values; a spinbutton's least and greatest values of
//     0 and 0 are Chromium's way of saying it has none, and so is a number input's bound that
//     its min or max attribute does not give (Chromium's float of an infinite bound, written 0);
//   - the placeholder from the name's sources, where Chromium gives it;
//   - live, atomic and relevant of a live region that is not off;
//   - a generic node with nothing more to say is no line, as Ambitious has no node for it.
// Numbers are written with six significant digits (Chromium keeps a range's values as floats).
'use strict';
const { chromium } = require('playwright');
const path = require('path');

const RANGE = new Set(['slider', 'spinbutton', 'scrollbar', 'progressbar', 'meter', 'separator']);
const ROLES = { image: 'img', DisclosureTriangle: 'button', RootWebArea: 'document', LabelText: 'generic', Legend: 'generic' };
const text = (s) => String(s).replace(/\s+/g, ' ').trim();
const num = (v) => String(Number(Number(v).toPrecision(6)));

// a valid floating-point number (HTML 2.3.4.3), as a number input's min and max are read
const VALID = /^-?(\d+(\.\d+)?|\.\d+)([eE][-+]?\d+)?$/;

function line(n, id, el) {
  const role = ROLES[n.role.value] || n.role.value;
  const p = {};
  for (const q of n.properties || []) p[q.name] = q.value.value;
  const parts = [];
  const name = n.name ? text(n.name.value) : '';
  if (name) parts.push(` "${name}"`);
  const desc = n.description ? text(n.description.value) : '';
  if (desc) parts.push(` desc "${desc}"`);
  if (p.level !== undefined) parts.push(` level=${p.level}`);
  if (p.checked !== undefined) parts.push(` checked=${p.checked}`);
  if (p.selected === true) parts.push(' selected=true');
  if (p.expanded !== undefined) parts.push(` expanded=${p.expanded}`);
  if (p.pressed !== undefined) parts.push(` pressed=${p.pressed}`);
  for (const b of ['disabled', 'required', 'readonly']) if (p[b] === true) parts.push(` ${b}=true`);
  if (p.invalid !== undefined && p.invalid !== 'false') parts.push(' invalid=true');
  if (p.busy) parts.push(' busy=true');
  if (p.hasPopup !== undefined && p.hasPopup !== 'false') parts.push(` haspopup=${p.hasPopup}`);
  if (p.orientation !== undefined) parts.push(` orientation=${p.orientation}`);
  for (const b of ['multiselectable', 'modal']) if (p[b] === true) parts.push(` ${b}=true`);
  if (p.autocomplete !== undefined && p.autocomplete !== 'none') parts.push(` autocomplete=${p.autocomplete}`);
  if (p.multiline === true) parts.push(' multiline=true');
  const value = n.value ? n.value.value : undefined;
  if (!RANGE.has(role) && value !== undefined && text(value) !== '') parts.push(` value="${text(value)}"`);
  const ph = ((n.name && n.name.sources) || []).find((s) => s.type === 'placeholder' && s.value && text(s.value.value) !== '');
  if (ph) parts.push(` placeholder="${text(ph.value.value)}"`);
  if (RANGE.has(role)) {
    if (value !== undefined) parts.push(` valuenow=${num(value)}`);
    const native = el && el.tag === 'input' && (el.at.type || '').trim().toLowerCase() === 'number';
    const none = !native && role === 'spinbutton' && Number(p.valuemin) === 0 && Number(p.valuemax) === 0;
    const bound = (a) => !native || (el.at[a] !== undefined && VALID.test(el.at[a]));
    if (p.valuemin !== undefined && !none && bound('min')) parts.push(` valuemin=${num(p.valuemin)}`);
    if (p.valuemax !== undefined && !none && bound('max')) parts.push(` valuemax=${num(p.valuemax)}`);
  }
  if (p.live !== undefined && p.live !== 'off')
    parts.push(` live=${p.live} atomic=${p.atomic === true} relevant="${p.relevant || 'additions text'}"`);
  if (role === 'generic' && parts.length === 0) return null;
  return `${role}#${id}${parts.join('')}`;
}

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 800, height: 600 } });
  await page.goto('file://' + path.resolve(process.argv[2]), { waitUntil: 'load' });
  const cdp = await page.context().newCDPSession(page);
  // each element's id, and its name and attributes, by its backend node id
  const ids = new Map();
  const els = new Map();
  const walk = (d) => {
    const a = d.attributes || [];
    const at = {};
    for (let i = 0; i < a.length; i += 2) at[a[i]] = a[i + 1];
    if (at.id !== undefined) ids.set(d.backendNodeId, at.id);
    els.set(d.backendNodeId, { tag: String(d.nodeName || '').toLowerCase(), at });
    for (const c of d.shadowRoots || []) walk(c);
    for (const c of d.children || []) walk(c);
  };
  walk((await cdp.send('DOM.getDocument', { depth: -1, pierce: true })).root);
  const { nodes } = await cdp.send('Accessibility.getFullAXTree');
  const byId = new Map(nodes.map((n) => [n.nodeId, n]));
  const out = [];
  const visit = (n) => {
    if (!n.ignored && n.backendDOMNodeId && ids.has(n.backendDOMNodeId)) {
      const l = line(n, ids.get(n.backendDOMNodeId), els.get(n.backendDOMNodeId));
      if (l) out.push(l);
    }
    for (const c of n.childIds || []) if (byId.has(c)) visit(byId.get(c));
  };
  visit(nodes[0]);
  await browser.close();
  process.stdout.write(out.map((l) => l + '\n').join(''));
})();
