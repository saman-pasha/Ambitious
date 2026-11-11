# Limitations, as of Phase 8

What each module does not do, so that a reader knows what an answer can rest
on. Each module's file says the same at its top.

| module | not done |
|---|---|
| HTML (`src/html/`) | foreign content (SVG, MathML) and template contents, which are 327 of html5lib's 347 failures; the 2025 select rules; encodings other than UTF-8; `document.write` |
| CSS (`src/css/`) | 29 properties, no more (no float, position, flex or grid properties, vertical-align, overflow, z-index, transforms, animations); border-style folded into the border shorthands; one border colour for four sides; `@supports`, `@layer`, `@import`, `@font-face`; pseudo-classes of state and pseudo-elements; `:has()`; custom properties and `calc()`; rem against 16px, not the root's size |
| Layout (`src/layout/`) | floats, positioning, flex and grid as such (their containers are blocks), vertical-align, min/max-height, a percentage height, rowspan, writing modes and bidirectional text, hyphenation, kerning |
| Paint (`src/paint/`) | images other than PNG drawn (GIF and JPEG are sized but not decoded); border styles (every border solid); background images; shadows; text set from six DejaVu faces only, slanted for italic |
| Network (`src/net/`) | https (no TLS), cookies, caching, compression, `@import`, `srcset`, iframes, forms, IDNA and IPv6 literals |
| Accessibility (`src/a11y/`) | descriptions, states and properties (checked, expanded, ...), live regions, accname's recursion through embedded controls |
| Profile (`prolog/amb/profile.pl`) | sessions restored into tabs (there are no tabs), and preferences that change rendering |
| JavaScript (`src/js/`) | async generators and `for await`; modules, `with`, BigInt (and so BigInt64Array, BigUint64Array and DataView's BigInt methods), `Intl`, direct `eval`; ArrayBuffers are never detached or resized, and TextDecoder reads UTF-8 alone; Proxy's invariants unchecked; weak collections hold their objects strongly (no collector); in regular expressions, the v flag's set operations and nested classes, and the modifiers `(?i:...)`; `String.prototype.normalize`; no collector: a page's objects live as long as the page, under a cap |
| Scripts (`src/script/`) | node lists are arrays, read once, not live; `getComputedStyle` reads the inline style and `getBoundingClientRect` is empty (the scripts run before the cascade and layout); `fetch`, XHR, inserted scripts and `document.write`; keyboard, focus and pointer events beyond a click; forms' submission; `MutationObserver`, shadow DOM, custom elements; the window's properties beyond its timers, `location` and `navigator` |
| Reasoning | snapshots are whole, not deltas, so a page of 16,000 nodes takes about a second to advise on (`docs/performance.md`); the reasoner runs unsandboxed |
| Toolchain | the findings in `toolchain/REPORT.md`, among them no deep indexing in cocolog (worked around), a 16 MiB file read (worked around), and `struct addrinfo` not lowered from `<netdb.h>` (declared locally) |
