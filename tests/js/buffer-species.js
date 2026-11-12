const t = (s, f) => { try { const r = f(); console.log(s, String(r)); } catch (e) { console.log(s, e.constructor.name + ': ' + e.message); } };
class B extends ArrayBuffer {}
t('ab slice', () => new B(4).slice(1).constructor.name);
t('ab species', () => { let n = 0; class C extends ArrayBuffer { static get [Symbol.species]() { n++; return ArrayBuffer; } } new C(4).slice(1); return n; });
t('ab species bad', () => { class C extends ArrayBuffer { static get [Symbol.species]() { return Object; } } return new C(4).slice(1); });
t('ab species small', () => { class C extends ArrayBuffer { static get [Symbol.species]() { return function () { return new ArrayBuffer(1); }; } } return new C(4).slice(1); });
t('ab species same', () => { let b; class C extends ArrayBuffer { static get [Symbol.species]() { return function () { return b; }; } } b = new C(4); return b.slice(1); });
