# Limitations, as of Phase 12 (in progress)

What each module does not do, so that a reader knows what an answer can rest
on. Each module's file says the same at its top.

| module | not done |
|---|---|
| HTML (`src/html/`) | the scripting flag: the parser runs with scripting off, so the 8 of html5lib's 1,792 tree-construction cases that need it are skipped (the other 1,784 pass); a `<meta charset>` the tree builder meets late does not restart the parse in its encoding (13.2.3.4), and no frequency analysis guesses an encoding nothing declares |
| CSS (`src/css/`) | 29 properties, no more (no float, position, flex or grid properties, vertical-align, overflow, z-index, transforms, animations); border-style folded into the border shorthands; one border colour for four sides; `@supports`, `@layer`, `@import`, `@font-face`; pseudo-classes of state and pseudo-elements; `:has()`; custom properties and `calc()`; rem against 16px, not the root's size |
| Layout (`src/layout/`) | floats, positioning, flex and grid as such (their containers are blocks), vertical-align, min/max-height, a percentage height, rowspan, writing modes and bidirectional text, hyphenation, kerning |
| Paint (`src/paint/`) | images in WebP, AVIF or SVG (PNG, GIF and JPEG are decoded; of a GIF its first frame alone, of JPEG not its arithmetic coding or 12-bit samples); border styles (every border solid); background images; shadows; text set from six DejaVu faces only, slanted for italic |
| Network (`src/net/`) | HTTP/2 and HTTP/3 (HTTP/1.1 alone, over TLS 1.2 or 1.3 for https); the rest of IDNA's mapping table, NFC and its bidi and joiner rules (a host is lowercased and Punycoded); percent-encoding sets beyond a file: path's; `<picture>`'s `<source>`s; iframes |
| Accessibility (`src/a11y/`) | visibility (the cascade does not compute it); a closed `details`' content and a `dialog` without open; CSS generated content; constraint validation (invalid comes from aria-invalid alone); presentational children and inherited presentation; aria-owns reordering the tree |
| Profile (`prolog/amb/profile.pl`) | sessions restored into tabs (there are no tabs), and preferences that change rendering |
| JavaScript (`src/js/`) | `Intl`; `<script type=module>` in pages (the engine has modules: `ambitious js` runs a `.mjs` file, `import()` loads files); ArrayBuffers are never detached or resized, and TextDecoder reads UTF-8 alone; Proxy's invariants unchecked; weak collections hold their objects strongly (no collector); in regular expressions, the v flag's set operations and nested classes, and the modifiers `(?i:...)`; `String.prototype.normalize`; no collector: a page's objects live as long as the page, under a cap |
| Scripts (`src/script/`) | node lists are arrays, read once, not live; `getComputedStyle` reads the inline style and `getBoundingClientRect` is empty (the scripts run before the cascade and layout); `fetch`, XHR, inserted scripts and `document.write`; keyboard, focus and pointer events beyond a click; forms' submission; `MutationObserver`, shadow DOM, custom elements; the window's properties beyond its timers, `location` and `navigator` |
| Reasoning | snapshots are whole, not deltas, so a page of 16,000 nodes takes about a second to advise on (`docs/performance.md`); the reasoner runs unsandboxed |
| Toolchain | the findings in `toolchain/REPORT.md`, among them no deep indexing in cocolog (worked around), a 16 MiB file read (worked around), and `struct addrinfo` not lowered from `<netdb.h>` (declared locally) |
