/* src/net/hpack.c -- HPACK (RFC 7541) for HTTP/2 (hpack.h).

   A header block is representations: an indexed field (an index into the static table,
   Appendix A, or the dynamic table after it), a literal with incremental indexing (added
   to the dynamic table), without indexing, or never indexed (its name by index or
   literal), and dynamic table size updates, which may come only at the block's start and
   no larger than the decoder's SETTINGS_HEADER_TABLE_SIZE. Integers have their N-bit
   prefixes (5.1); strings are raw or Huffman coded by the canonical code of Appendix B,
   whose padding must be fewer than 8 bits, all ones, and never an EOS. The dynamic table
   holds entries newest first, each counted as its name and value and 32 bytes; an entry
   that does not fit empties it. A new entry whose name is an entry that adding it evicts
   keeps that name (4.4).

   The encoder chooses as Chromium's does (quiche's HpackEncoder): a field a table holds
   whole is indexed, the static table's first; else a literal, its name an index when a
   table holds the name (the static table's first, else the newest), its strings Huffman
   coded when that is shorter; indexed incrementally, but a pseudo-header field other than
   :authority, sent without indexing (a field marked never, never indexed). A change of
   the peer's SETTINGS_HEADER_TABLE_SIZE is said at the next block's start, the least
   since the last block first when it was less. RFC 7541's own examples index every
   literal (HPACK_INDEX_ALL), and Huffman code a string as long so as raw. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "hpack.h"

/* the static table (Appendix A) */
static const char *const hp_static_name[61] = {
    ":authority", ":method", ":method", ":path", ":path", ":scheme", ":scheme", ":status", ":status", ":status", ":status",
    ":status", ":status", ":status", "accept-charset", "accept-encoding", "accept-language", "accept-ranges", "accept",
    "access-control-allow-origin", "age", "allow", "authorization", "cache-control", "content-disposition", "content-encoding",
    "content-language", "content-length", "content-location", "content-range", "content-type", "cookie", "date", "etag",
    "expect", "expires", "from", "host", "if-match", "if-modified-since", "if-none-match", "if-range", "if-unmodified-since",
    "last-modified", "link", "location", "max-forwards", "proxy-authenticate", "proxy-authorization", "range", "referer",
    "refresh", "retry-after", "server", "set-cookie", "strict-transport-security", "transfer-encoding", "user-agent", "vary",
    "via", "www-authenticate",
};
static const char *const hp_static_value[61] = {
    "", "GET", "POST", "/", "/index.html", "http", "https", "200", "204", "206", "304", "400", "404", "500", "", "gzip, deflate",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "", "", "", "", "", "", "",
};

/* the Huffman code's lengths, by symbol (Appendix B; 256 is EOS). The code is canonical:
   each length's codes are consecutive, in symbol order, after the shorter lengths' */
static const uint8_t hp_huff_len[257] = {
    13, 23, 28, 28, 28, 28, 28, 28, 28, 24, 30, 28, 28, 30, 28, 28, 28, 28, 28, 28, 28, 28, 30, 28, 28, 28, 28, 28, 28, 28, 28,
    28, 6, 10, 10, 12, 13, 6, 8, 11, 10, 10, 8, 11, 8, 6, 6, 6, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 7, 8, 15, 6, 12, 10, 13, 6, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 7, 8, 13, 19, 13, 14, 6, 15, 5, 6, 5, 6, 5, 6, 6, 6, 5, 7,
    7, 6, 6, 6, 5, 6, 7, 6, 5, 5, 6, 7, 7, 7, 7, 7, 15, 11, 14, 13, 28, 20, 22, 20, 20, 22, 22, 22, 23, 22, 23, 23, 23, 23,
    23, 24, 23, 24, 24, 22, 23, 24, 23, 23, 23, 23, 21, 22, 23, 22, 23, 23, 24, 22, 21, 20, 22, 22, 23, 23, 21, 23, 22, 22,
    24, 21, 22, 23, 23, 21, 21, 22, 21, 23, 22, 23, 23, 20, 22, 22, 22, 23, 22, 22, 23, 26, 26, 20, 19, 22, 23, 22, 25, 26,
    26, 26, 27, 27, 26, 24, 25, 19, 21, 26, 27, 27, 26, 27, 24, 21, 21, 26, 26, 28, 27, 27, 27, 20, 24, 20, 21, 22, 21, 21,
    23, 22, 22, 25, 25, 24, 24, 26, 23, 26, 27, 26, 26, 27, 27, 27, 27, 27, 28, 27, 27, 27, 27, 27, 26, 30,
};

/* the code made from its lengths at first use: each symbol's code; by length, the first
   code, how many there are, and where their symbols begin in hp_sym, which holds the
   symbols by length, then by value */
static uint32_t hp_code[257];
static uint32_t hp_first[32], hp_count[32], hp_offset[32];
static uint16_t hp_sym[257];
static int hp_ready;

static void hp_build(void) {
    if (hp_ready) return;
    uint32_t fill[32];
    memset(hp_count, 0, sizeof hp_count);
    for (uint32_t s = 0; s < 257; s++) hp_count[hp_huff_len[s]]++;
    uint32_t off = 0, code = 0;
    for (uint32_t k = 1; k < 32; k++) {
        hp_offset[k] = off;
        fill[k] = off;
        off += hp_count[k];
        hp_first[k] = code;
        code = (code + hp_count[k]) << 1;
    }
    for (uint32_t s = 0; s < 257; s++) {
        uint32_t k = hp_huff_len[s];
        hp_code[s] = hp_first[k] + (fill[k] - hp_offset[k]);
        hp_sym[fill[k]++] = (uint16_t)s;
    }
    hp_ready = 1;
}

size_t hpack_huffman_size(const uint8_t *s, size_t n) {
    uint64_t bits = 0;
    for (size_t i = 0; i < n; i++) bits += hp_huff_len[s[i]];
    return (size_t)((bits + 7) / 8);
}

void hpack_huffman_encode(const uint8_t *s, size_t n, vector<uint8_t> *out) {
    hp_build();
    uint64_t acc = 0;
    uint32_t bits = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t k = hp_huff_len[s[i]];
        acc = acc << k | hp_code[s[i]];
        bits += k;
        while (bits >= 8) {
            bits -= 8;
            push_back(out, (uint8_t)(acc >> bits));
        }
        acc &= ((uint64_t)1 << bits) - 1;
    }
    /* the last byte filled with ones, EOS's first bits */
    if (bits) push_back(out, (uint8_t)(acc << (8 - bits) | (0xffu >> bits)));
}

int hpack_huffman_decode(const uint8_t *s, size_t n, vector<uint8_t> *out) {
    hp_build();
    uint32_t code = 0, k = 0;
    for (size_t i = 0; i < n; i++)
        for (int b = 7; b >= 0; b--) {
            code = code << 1 | (uint32_t)(s[i] >> b & 1);
            k++;
            if (k >= 5 && code >= hp_first[k] && code - hp_first[k] < hp_count[k]) {
                uint32_t sym = hp_sym[hp_offset[k] + code - hp_first[k]];
                if (sym == 256) return 0;
                push_back(out, (uint8_t)sym);
                code = 0;
                k = 0;
            } else if (k >= 30) return 0;
        }
    /* padding: fewer than 8 bits, all ones */
    return k < 8 && code == (1u << k) - 1;
}

void hpack_int_encode(uint32_t v, uint32_t bits, uint8_t flags, vector<uint8_t> *out) {
    uint32_t max = (1u << bits) - 1;
    if (v < max) {
        push_back(out, (uint8_t)(flags | v));
        return;
    }
    push_back(out, (uint8_t)(flags | max));
    v -= max;
    while (v >= 128) {
        push_back(out, (uint8_t)(v % 128 + 128));
        v /= 128;
    }
    push_back(out, (uint8_t)v);
}

int hpack_int_decode(const uint8_t *in, size_t n, size_t *at, uint32_t bits, uint32_t *v) {
    if (*at >= n) return 0;
    uint32_t max = (1u << bits) - 1, x = in[(*at)++] & max;
    if (x < max) {
        *v = x;
        return 1;
    }
    for (uint32_t m = 0;; m += 7) {
        if (*at >= n || m > 21) return 0;
        uint8_t b = in[(*at)++];
        x += (uint32_t)(b & 127) << m;
        if (!(b & 128)) break;
    }
    *v = x;
    return 1;
}

/* ---- the dynamic table ---- */

void hpack_init(hpack_table *t, uint32_t limit) {
    clear(t->ents);
    clear(t->bytes);
    t->first = 0;
    t->size = 0;
    t->limit = limit;
    t->max = limit < 4096 ? limit : 4096;
    t->owed = 0;
    t->least = 0xffffffffu;
}

static uint32_t hp_count_live(const hpack_table *t) { return (uint32_t)len(t->ents) - t->first; }

/* the oldest entries evicted until the table's size and need fit its maximum; the dead
   entries' room given back once they are half the table */
static void hp_evict(hpack_table *t, uint32_t need) {
    while (t->first < len(t->ents) && t->size + need > t->max) {
        hpack_entry e = t->ents[t->first];
        t->size -= e.nlen + e.vlen + 32;
        t->first++;
    }
    if (t->first == len(t->ents)) {
        clear(t->ents);
        clear(t->bytes);
        t->first = 0;
    } else if (t->first >= 32 && t->first >= hp_count_live(t)) {
        uint32_t base = t->ents[t->first].off, live = hp_count_live(t);
        size_t nb = len(t->bytes) - base;
        memmove(data(t->bytes), data(t->bytes) + base, nb);
        resize(t->bytes, nb);
        for (uint32_t i = 0; i < live; i++) {
            hpack_entry e = t->ents[t->first + i];
            e.off -= base;
            t->ents[i] = e;
        }
        resize(t->ents, live);
        t->first = 0;
    }
}

/* name and value added as the newest entry, or the table emptied when they do not fit */
static void hp_insert(hpack_table *t, const uint8_t *name, size_t nlen, const uint8_t *value, size_t vlen) {
    uint32_t need = (uint32_t)(nlen + vlen + 32);
    hp_evict(t, need);
    if (need > t->max) return;
    hpack_entry e = { (uint32_t)len(t->bytes), (uint32_t)nlen, (uint32_t)vlen };
    append_range(t->bytes, name, nlen);
    append_range(t->bytes, value, vlen);
    push_back(t->ents, e);
    t->size += need;
}

/* the field at index idx (1 the static table's first; past 61 the dynamic table's, the
   newest first): its name, and its value when value is given, appended. 0 when there is
   none */
static int hp_field_at(const hpack_table *t, uint32_t idx, vector<uint8_t> *name, vector<uint8_t> *value) {
    if (idx == 0) return 0;
    if (idx <= 61) {
        append_range(name, hp_static_name[idx - 1], strlen(hp_static_name[idx - 1]));
        if (value) append_range(value, hp_static_value[idx - 1], strlen(hp_static_value[idx - 1]));
        return 1;
    }
    uint32_t d = idx - 62;
    if (d >= hp_count_live(t)) return 0;
    hpack_entry e = t->ents[len(t->ents) - 1 - d];
    append_range(name, data(t->bytes) + e.off, e.nlen);
    if (value) append_range(value, data(t->bytes) + e.off + e.nlen, e.vlen);
    return 1;
}

/* the index of a field the tables hold: its name and value both (whole), or its name
   alone; the static table's first, else the dynamic table's newest; 0 for none */
static uint32_t hp_find(const hpack_table *t, const uint8_t *name, size_t nlen, const uint8_t *value, size_t vlen, int whole) {
    for (uint32_t i = 0; i < 61; i++) {
        if (strlen(hp_static_name[i]) != nlen || memcmp(hp_static_name[i], name, nlen) != 0) continue;
        if (!whole || (strlen(hp_static_value[i]) == vlen && memcmp(hp_static_value[i], value, vlen) == 0)) return i + 1;
    }
    uint32_t live = hp_count_live(t);
    for (uint32_t d = 0; d < live; d++) {
        hpack_entry e = t->ents[len(t->ents) - 1 - d];
        if (e.nlen != nlen || memcmp(data(t->bytes) + e.off, name, nlen) != 0) continue;
        if (!whole || (e.vlen == vlen && memcmp(data(t->bytes) + e.off + e.nlen, value, vlen) == 0)) return 62 + d;
    }
    return 0;
}

/* ---- decoding ---- */

/* a string literal at in[*at] (5.2) appended to out: 0 when it runs past the block or
   its Huffman code is no code */
static int hp_string_decode(const uint8_t *in, size_t n, size_t *at, vector<uint8_t> *out) {
    if (*at >= n) return 0;
    int huff = in[*at] & 0x80;
    uint32_t sl = 0;
    if (!hpack_int_decode(in, n, at, 7, &sl) || sl > n - *at) return 0;
    if (huff) {
        if (!hpack_huffman_decode(in + *at, sl, out)) return 0;
    } else append_range(out, in + *at, sl);
    *at += sl;
    return 1;
}

int hpack_decode(hpack_table *t, const uint8_t *in, size_t n, vector<hpack_field> *fields, vector<uint8_t> *bytes, size_t max_list,
                 char *why, size_t wcap) {
    size_t at = 0, list = 0;
    int any = 0, too = 0;
    vector<uint8_t> name, value;
    while (at < n) {
        uint8_t b = in[at];
        uint32_t idx = 0, never = 0;
        clear(name);
        clear(value);
        if (b & 0x80) {
            if (!hpack_int_decode(in, n, &at, 7, &idx)) {
                snprintf(why, wcap, "an index is cut short, or past 2^28");
                return HPACK_BAD;
            }
            if (!hp_field_at(t, idx, &name, &value)) {
                snprintf(why, wcap, "index %u is in neither table", idx);
                return HPACK_BAD;
            }
        } else if ((b & 0xe0) == 0x20) {
            uint32_t size = 0;
            if (any) {
                snprintf(why, wcap, "a dynamic table size update after a field");
                return HPACK_BAD;
            }
            if (!hpack_int_decode(in, n, &at, 5, &size) || size > t->limit) {
                snprintf(why, wcap, "a dynamic table size update past the %u bytes allowed", t->limit);
                return HPACK_BAD;
            }
            t->max = size;
            hp_evict(t, 0);
            continue;
        } else {
            int incremental = (b & 0xc0) == 0x40;
            never = (uint32_t)(!incremental && (b & 0xf0) == 0x10);
            if (!hpack_int_decode(in, n, &at, incremental ? 6 : 4, &idx)) {
                snprintf(why, wcap, "a literal's name index runs past the header block");
                return HPACK_BAD;
            }
            if (idx == 0 ? !hp_string_decode(in, n, &at, &name) : !hp_field_at(t, idx, &name, 0)) {
                snprintf(why, wcap, "a literal's name is not one (index %u)", idx);
                return HPACK_BAD;
            }
            if (!hp_string_decode(in, n, &at, &value)) {
                snprintf(why, wcap, "a literal's value is not one");
                return HPACK_BAD;
            }
            if (incremental) hp_insert(t, data(name), len(name), data(value), len(value));
        }
        any = 1;
        list += len(name) + len(value) + 32;
        if (list > max_list) too = 1;
        if (too) continue;
        hpack_field f = { (uint32_t)len(*bytes), (uint32_t)len(name), 0, (uint32_t)len(value), never };
        append_range(bytes, data(name), len(name));
        f.value = (uint32_t)len(*bytes);
        append_range(bytes, data(value), len(value));
        push_back(fields, f);
    }
    if (too) {
        snprintf(why, wcap, "the header list passes %zu bytes", max_list);
        return HPACK_TOO_LARGE;
    }
    return HPACK_OK;
}

/* ---- encoding ---- */

/* a string literal, Huffman coded when that is shorter (huffman 1), or always (2) */
static void hp_string_encode(const uint8_t *s, size_t n, int huffman, vector<uint8_t> *out) {
    size_t hs = huffman ? hpack_huffman_size(s, n) : n;
    if (hs < n || (huffman == 2 && n)) {
        hpack_int_encode((uint32_t)hs, 7, 0x80, out);
        hpack_huffman_encode(s, n, out);
    } else {
        hpack_int_encode((uint32_t)n, 7, 0, out);
        append_range(out, s, n);
    }
}

void hpack_set_limit(hpack_table *t, uint32_t limit) {
    if (limit == t->limit) return;
    if (limit < t->limit && limit < t->least) t->least = limit;
    t->limit = limit;
    t->max = limit;
    hp_evict(t, 0);
    t->owed = 1;
}

void hpack_encode(hpack_table *t, const hpack_field *f, size_t nf, const uint8_t *bytes, int policy, int huffman, vector<uint8_t> *out) {
    if (t->owed) {
        if (t->least < t->max) hpack_int_encode(t->least, 5, 0x20, out);
        hpack_int_encode(t->max, 5, 0x20, out);
        t->owed = 0;
        t->least = 0xffffffffu;
    }
    for (size_t i = 0; i < nf; i++) {
        const uint8_t *name = bytes + f[i].name, *value = bytes + f[i].value;
        size_t nlen = f[i].nlen, vlen = f[i].vlen;
        uint32_t whole = f[i].never ? 0 : hp_find(t, name, nlen, value, vlen, 1);
        if (whole) {
            hpack_int_encode(whole, 7, 0x80, out);
            continue;
        }
        int index = !f[i].never &&
                    (policy == HPACK_INDEX_ALL || (nlen > 0 && (name[0] != ':' || (nlen == 10 && memcmp(name, ":authority", 10) == 0))));
        uint32_t ni = hp_find(t, name, nlen, 0, 0, 0);
        if (index) hpack_int_encode(ni, 6, 0x40, out);
        else hpack_int_encode(ni, 4, f[i].never ? 0x10 : 0, out);
        if (!ni) hp_string_encode(name, nlen, huffman, out);
        hp_string_encode(value, vlen, huffman, out);
        if (index) hp_insert(t, name, nlen, value, vlen);
    }
}
