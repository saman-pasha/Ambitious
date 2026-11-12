# Security: the boundaries, audited

*Phase 11.* Ambitious puts a process boundary between the engine and anything
that reasons, and a contract on each side of it. This audit goes through each
boundary: what crosses it, what checks it, and the test that shows the check
holds. The gaps it found are fixed, and listed at the end; the gaps that remain
are listed after them.

## 1. The engine to the reasoner: snapshots

**What crosses.** One fact a line, written by code generated from the module's
contract (`include/amb.pl`, `prolog/amb/gen_c.pl`). Everything that came from
a page (text, attribute values, URLs, selectors, declarations) crosses only
inside a quoted atom. `amb_put_qatom` escapes `'` and `\`, and writes every
other control character as `\xN\`, so page content cannot end its atom, its
fact or its line.

**What checks it.** The reasoner, `prolog/amb/reasoner.pl`, reads a line at a
time with `read_term_from_atom`. Each line must be ground, and must be either
a section header or a fact the section's module declares. Every argument must
match the shape in the schema: a handle of the section's own document, a value
of the declared enum, an atom, an integer or a number. A snapshot with
anything else in it is refused whole. A section's fingerprint must equal the
schema's, so an engine built from another text of the contract cannot be
reasoned over by this one. There is no `consult`, so no directive can run.

**Tested.** `tests/reason/snapshots.pl` covers nine kinds of bad snapshot and
an injection attempt that stays data. `tests/exec/fuzz_pipeline` runs every
module's exporter over 60 documents of tag soup and random bytes.

## 2. The reasoner to the engine: requests

**What crosses.** `amb_request(Doc, Rev, Op)` lines, which the reasoner only
proposes.

**What checks it.** The engine reads requests with its own lexer
(`amb_lex_*`), never a Prolog reader, and applies one only if all of these
hold:
- its operation is requestable in the contract;
- its document is this one;
- its revision is the batch's, so an old batch is refused whole;
- every handle in it is live, with the right generation;
- the operation's generated preconditions hold.

It then runs through the same C function every caller uses. A name argument
must be a valid name (`amb_valid_name`). Anything else is refused, with the
reason.

**Tested.** `tests/integ/loop` (the batch applied, then refused as stale) and
`tests/exec/dom_*` (the dispatcher's refusals).

## 3. The network to the engine

- **Bounds:** 32 MB a body, five redirects, ten seconds a read; a PNG at most
  2^24 pixels, and inflate bounded by the size its image needs. Over HTTP/2
  (12q), the bounds Chromium announces: a frame of 16,384 bytes, a header list
  of 256 KB, HPACK's table of 64 KB, a stream's window of 6 MB; 256 streams
  at once at most. A decoded body, gzip, deflate, br or zstd, stops at 32 MB,
  and a zstd frame's window at 8 MB (Chromium's limit, RFC 9659's).
- **Parsing:** URLs are parsed into fixed buffers that truncate, never
  overflow. An image's size is read only from header bytes that exist. The
  PNG decoder checks every chunk length, colour type, bit depth and filter.
- **Same-origin, in C:** a page served over `http:` may not read a `file:`
  URL. A site's denied kinds (the profile's permissions) are blocked by the
  loader before any fetch.
- **Tested:** `tests/integ/net` (blocked, too_many_redirects, http_error),
  `tests/integ/net-h2` (a body past 32 MB over HTTP/2, a reset stream, a
  GOAWAY, a stalled stream), `tests/exec/png_decode`, `tests/exec/net_url`,
  and `tests/exec/net_hpack`, `net_h2` and `net_zstd`, each with blocks,
  conversations or frames broken at random, under valgrind.

## 4. The profile

The keeper writes six declared kinds of fact, and only from named commands. A
profile query is checked to be read-only before it runs: only the profile's
own predicates and pure tests. A private session uses a memory store, and
never opens the profile's directory. The profile lives in its own process and
store, so no page snapshot holds it and the reasoner cannot read it.
**Tested:** `tests/integ/profile`, where the profile's files are byte-identical
after a private session and a writing query is refused.

## 5. Memory and processes

- **Memory safety.** All engine code is cicilang's Safe C, ownership- and
  borrow-checked at compile time (`tests/safe`: six refusals). ccllib vectors
  stop the program at an index past their end rather than read past it. Store
  handles are checked by generation. The exec gate runs every exec test,
  fuzzer included, under valgrind.
- **Processes.** The reasoner and the keeper are started with `fork`/`execv`
  and an argument vector, never through a shell. A goal or a URL is one
  argument, whatever it holds. The session's work directory is made `0700`.

## Fixed by this audit

| what | before | now |
|---|---|---|
| markup nested 100,000 deep | the engine crashed (`SIGSEGV`: layout, paint and the accessibility tree recurse) | the parser keeps the DOM within 512 open elements, as Blink's does (`tests/exec/hostile`) |
| a selector nested 20,000 deep (`:is(:is(...))`) | crashed | nesting is bounded at 16; past it nothing matches |
| descendant combinators made to backtrack (`div div ... p` over 400 divs) | exponential time | each match tests at most 4,096 compounds |
| a snapshot past 16 MiB | the reasoner exited 1 and said nothing | read a line at a time; a failed command says so |

## What is not done

- **No TLS.** `https:` is `unsupported_scheme`, and nothing is fetched over a
  channel an attacker cannot read.
- **The reasoner is not sandboxed.** It runs cocolog as the user. The goals
  the user types to `query` and `why` are trusted, as a shell's are. The
  engine's own `advise` runs only rules from `prolog/amb/rules/`.
- **No private-network rule.** A page may make the engine connect to any host
  and port, `localhost` included, as a browser without such rules would.
- **No limits on work for script**, because there is no script (Phase 8).
