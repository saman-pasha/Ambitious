// tests/exec/cssom/chromium.js CASES -- tests/exec/cssom_text.c's lines run in Chromium, printed as it prints them: `node tests/exec/cssom/chromium.js tests/exec/cssom/cases.txt > tests/exec/cssom_text.expect`
const { chromium } = require('playwright');
const fs = require('fs');
(async () => {
  const lines = fs.readFileSync(process.argv[2], 'utf8').split('\n').filter((l) => l.length);
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.setContent('<!DOCTYPE html><html><head></head><body></body></html>');
  const out = await page.evaluate((lines) => {
    const res = [];
    for (const line of lines) {
      if (line.startsWith('sheet: ')) {
        const st = document.createElement('style');
        st.textContent = line.slice(7);
        document.head.appendChild(st);
        res.push(line);
        const r = st.sheet.cssRules;
        for (let i = 0; i < r.length; i++) res.push('  ' + i + ': ' + r[i].cssText);
        st.remove();
      } else if (line.startsWith('rule: ')) {
        const st = document.createElement('style');
        document.head.appendChild(st);
        let e = 0, t = '';
        try { st.sheet.insertRule(line.slice(6), 0); t = st.sheet.cssRules[0].cssText; } catch (x) { e = x.name; }
        res.push(line + ' -> ' + (e ? 'ERR' : '0') + ' ' + t);
        st.remove();
      } else if (line.startsWith('decl: ')) {
        const st = document.createElement('style');
        document.head.appendChild(st);
        st.sheet.insertRule('.x { ' + line.slice(6) + ' }', 0);
        const d = st.sheet.cssRules[0].style, o = [];
        for (let i = 0; i < d.length; i++) o.push(d[i] + '=' + d.getPropertyValue(d[i]) + (d.getPropertyPriority(d[i]) ? '!' : ''));
        res.push(line + ' -> "' + d.cssText + '" [' + o.join('; ') + ']');
        st.remove();
      } else if (line.startsWith('media: ')) {
        const st = document.createElement('style');
        st.media = line.slice(7);
        document.head.appendChild(st);
        res.push(line + ' -> ' + st.sheet.media.mediaText);
        st.remove();
      }
    }
    return res;
  }, lines);
  await browser.close();
  process.stdout.write(out.map((l) => l + '\n').join(''));
})();
