/* tests/exec/net_h2.c -- HTTP/2's frames (src/net/h2.c), the client's side, against a
   server played here frame by frame.

   The client's first bytes: the preface, its SETTINGS and the connection's WINDOW_UPDATE,
   Chromium's values. Requests of five priorities, their HEADERS' dependencies and weights
   as Chromium's tree gives them, a block decoded as the server would. The server's
   SETTINGS acknowledged; a response's head in HEADERS and two CONTINUATIONs, padded and
   with a priority; padded DATA; an interim head, then the final one, DATA in pieces and
   trailers; a stream reset by the server; a malformed head reset by the client; a PING
   answered; a stream's window and the connection's given back past half; GOAWAY, after
   which the streams it did not process are forgotten. A body sent within a window of 100
   bytes, as WINDOW_UPDATEs open it. Then each kind of connection error and stream error,
   each with its words and what the client sent for it; and conversations broken at
   random, each frame read or refused, nothing read outside them (valgrind). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "../../src/net/h2.h"

static const char *const types[10] = { "DATA", "HEADERS", "PRIORITY", "RST_STREAM", "SETTINGS", "PUSH_PROMISE", "PING", "GOAWAY",
                                       "WINDOW_UPDATE", "CONTINUATION" };
static const char *const kinds[8] = { "none", "headers", "data", "reset", "goaway", "window", "settings", "error" };

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put32(vector<uint8_t> *o, uint32_t v) {
    push_back(o, (uint8_t)(v >> 24));
    push_back(o, (uint8_t)(v >> 16));
    push_back(o, (uint8_t)(v >> 8));
    push_back(o, (uint8_t)v);
}
static void frame(vector<uint8_t> *o, uint32_t len, uint32_t type, uint32_t flags, uint32_t stream) {
    push_back(o, (uint8_t)(len >> 16));
    push_back(o, (uint8_t)(len >> 8));
    push_back(o, (uint8_t)len);
    push_back(o, (uint8_t)type);
    push_back(o, (uint8_t)flags);
    put32(o, stream);
}
static void add(vector<hpack_field> *fs, vector<uint8_t> *b, const char *name, const char *value) {
    hpack_field f = { (uint32_t)len(*b), (uint32_t)strlen(name), 0, (uint32_t)strlen(value), 0 };
    append_range(b, name, strlen(name));
    f.value = (uint32_t)len(*b);
    append_range(b, value, strlen(value));
    push_back(fs, f);
}

/* the server's side: its HPACK tables (one to encode its heads, one to decode the
   client's) */
typedef struct server { hpack_table enc, dec; } server;
#define SERVER_DROP(s) HPACK_TABLE_DROP((s).enc); HPACK_TABLE_DROP((s).dec)

/* a head the server sends: a header block of "name", "value" pairs, ending at a null
   name, in HEADERS (split over CONTINUATIONs of pieces bytes when pieces is not 0) */
static void server_head(server *sv, vector<uint8_t> *o, uint32_t stream, uint32_t flags, const char *const *nv, size_t pieces) {
    vector<hpack_field> fs;
    vector<uint8_t> b, block;
    for (size_t i = 0; nv[i]; i += 2) add(&fs, &b, nv[i], nv[i + 1]);
    hpack_encode(&sv->enc, data(fs), len(fs), data(b), HPACK_INDEX_ALL, 1, &block);
    size_t n = len(block), first = pieces && pieces < n ? pieces : n;
    frame(o, (uint32_t)first, H2_HEADERS, flags | (first == n ? H2_END_HEADERS : 0), stream);
    append_range(o, data(block), first);
    for (size_t at = first; at < n;) {
        size_t k = n - at < pieces ? n - at : pieces;
        frame(o, (uint32_t)k, H2_CONTINUATION, at + k == n ? H2_END_HEADERS : 0, stream);
        append_range(o, data(block) + at, k);
        at += k;
    }
}

/* what the client sent, frame by frame, its header blocks decoded */
static void show_sent(server *sv, const vector<uint8_t> *out, size_t from) {
    size_t at = from, n = len(*out);
    const uint8_t *p = data(*out);
    if (n - at >= 24 && memcmp(p + at, "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n", 24) == 0) {
        printf("  > the preface\n");
        at += 24;
    }
    while (at + 9 <= n) {
        uint32_t len = (uint32_t)p[at] << 16 | (uint32_t)p[at + 1] << 8 | p[at + 2], type = p[at + 3], flags = p[at + 4],
                 id = get32(p + at + 5) & 0x7fffffffu;
        const uint8_t *q = p + at + 9;
        printf("  > %s flags 0x%x stream %u, %u bytes", type < 10 ? types[type] : "?", flags, id, len);
        if (type == H2_SETTINGS)
            for (uint32_t k = 0; k + 6 <= len; k += 6) printf(" [%u = %u]", (uint32_t)q[k] << 8 | q[k + 1], get32(q + k + 2));
        if (type == H2_WINDOW_UPDATE || type == H2_RST_STREAM) printf(": %u", get32(q));
        if (type == H2_GOAWAY) printf(": last %u, code %u", get32(q) & 0x7fffffffu, get32(q + 4));
        if (type == H2_PING) printf(": %02x%02x%02x%02x%02x%02x%02x%02x", q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]);
        if (type == H2_HEADERS) {
            size_t o = 0;
            if (flags & H2_PRIORITY_FLAG) {
                printf(": depends on %u%s, weight %u", get32(q) & 0x7fffffffu, q[0] & 0x80 ? " exclusively" : "", q[4] + 1u);
                o = 5;
            }
            vector<hpack_field> fs;
            vector<uint8_t> fb;
            char why[200];
            if (hpack_decode(&sv->dec, q + o, len - o, &fs, &fb, 1 << 20, why, sizeof why) == HPACK_OK) {
                printf("\n");
                for (size_t k = 0; k < len(fs); k++)
                    printf("      %.*s: %.*s\n", (int)fs[k].nlen, (const char *)data(fb) + fs[k].name, (int)fs[k].vlen, (const char *)data(fb) + fs[k].value);
                at += 9 + len;
                continue;
            }
            printf(" (its block no HPACK: %s)", why);
        }
        printf("\n");
        at += 9 + len;
    }
}

/* the server's bytes read by the client, frame by frame: each event said, a head's fields
   too when heads is 1; what the client sent for them shown */
static void feed(h2_conn *c, server *sv, const vector<uint8_t> *in, int heads) {
    vector<uint8_t> out;
    size_t at = 0;
    for (;;) {
        h2_event ev;
        vector<hpack_field> fs;
        vector<uint8_t> fb;
        size_t used = 0;
        if (!h2_read(c, data(*in) + at, len(*in) - at, &used, &ev, &fs, &fb, &out)) break;
        if (ev.kind != H2_EV_NONE) {
            printf("  < %s, stream %u", kinds[ev.kind], ev.stream);
            if (ev.kind == H2_EV_HEADERS) printf(", status %u", ev.status);
            if (ev.kind == H2_EV_DATA) printf(", %zu bytes \"%.*s\"", ev.len, ev.len < 24 ? (int)ev.len : 24, (const char *)data(*in) + at + ev.off);
            if (ev.end_stream) printf(", the stream's end");
            if (ev.kind == H2_EV_GOAWAY) printf(", last %u", ev.last);
            if (ev.why[0]) printf(": %s", ev.why);
            printf("\n");
            if (heads)
                for (size_t k = 0; k < len(fs); k++)
                    printf("      %.*s: %.*s\n", (int)fs[k].nlen, (const char *)data(fb) + fs[k].name, (int)fs[k].vlen, (const char *)data(fb) + fs[k].value);
        }
        at += used;
        if (ev.kind == H2_EV_ERROR) break;
    }
    if (at < len(*in)) printf("  (%zu bytes not read)\n", len(*in) - at);
    show_sent(sv, &out, 0);
}

static void settings(vector<uint8_t> *o, const uint32_t *kv, int n) {
    frame(o, (uint32_t)(6 * n), H2_SETTINGS, 0, 0);
    for (int i = 0; i < 2 * n; i += 2) {
        push_back(o, (uint8_t)(kv[i] >> 8));
        push_back(o, (uint8_t)kv[i]);
        put32(o, kv[i + 1]);
    }
}

/* a request of the client's: GET of path, at priority p */
static uint32_t get(h2_conn *c, const char *path, uint32_t p, vector<uint8_t> *out) {
    vector<hpack_field> fs;
    vector<uint8_t> b;
    add(&fs, &b, ":method", "GET");
    add(&fs, &b, ":authority", "localhost:8443");
    add(&fs, &b, ":scheme", "https");
    add(&fs, &b, ":path", path);
    add(&fs, &b, "user-agent", "Ambitious/0.1");
    add(&fs, &b, "accept-encoding", "gzip, deflate, br, zstd");
    return h2_request(c, data(fs), len(fs), data(b), 1, p, out);
}

/* a fresh connection with its server's SETTINGS read and one request open: the bytes in
   then read, and what came of them said. squeeze 1 makes the stream's window 10 bytes, 2
   the connection's */
static void error_case(const char *title, int open, void (*make)(server *, vector<uint8_t> *), int squeeze) {
    h2_conn c;
    server sv;
    memset(&c, 0, sizeof c);
    memset(&sv, 0, sizeof sv);
    defer(c) { H2_CONN_DROP(c); }
    defer(sv) { SERVER_DROP(sv); }
    hpack_init(&sv.enc, 4096);
    hpack_init(&sv.dec, 4096);
    vector<uint8_t> out, in;
    h2_start(&c, &out);
    if (open) {
        static const uint32_t none[2] = { 3, 100 };
        settings(&in, none, 1);
        for (int i = 0; i < open; i++) get(&c, "/x", 1, &out);
    }
    if (squeeze == 1 && len(c.streams)) c.streams[0].recv_window = 10;
    if (squeeze == 2) c.recv_window = 10;
    make(&sv, &in);
    printf("-- %s\n", title);
    feed(&c, &sv, &in, 0);
}

static const char *const ok_head[] = { ":status", "200", "content-type", "text/css", 0 };
static void e_first(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 8, H2_PING, 0, 0); for (int i = 0; i < 8; i++) push_back(in, 0); }
static void e_big(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 16385, H2_DATA, 0, 1); }
static void e_data0(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 1, H2_DATA, 0, 0); push_back(in, 'x'); }
static void e_even(server *sv, vector<uint8_t> *in) { server_head(sv, in, 2, 0, ok_head, 0); }
static void e_idle(server *sv, vector<uint8_t> *in) { server_head(sv, in, 9, 0, ok_head, 0); }
static void e_cont(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 1, H2_CONTINUATION, H2_END_HEADERS, 1); push_back(in, 0x88); }
static void e_interleave(server *sv, vector<uint8_t> *in) {
    (void)sv;
    frame(in, 1, H2_HEADERS, 0, 1);
    push_back(in, 0x88);
    frame(in, 8, H2_PING, 0, 0);
    for (int i = 0; i < 8; i++) push_back(in, 0);
}
static void e_push(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 5, H2_PUSH_PROMISE, H2_END_HEADERS, 1); put32(in, 2); push_back(in, 0x88); }
static void e_set1(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 0, H2_SETTINGS, 0, 1); }
static void e_set7(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 7, H2_SETTINGS, 0, 0); for (int i = 0; i < 7; i++) push_back(in, 0); }
static void e_push2(server *sv, vector<uint8_t> *in) { (void)sv; static const uint32_t kv[2] = { 2, 2 }; settings(in, kv, 1); }
static void e_window(server *sv, vector<uint8_t> *in) { (void)sv; static const uint32_t kv[2] = { 4, 0x80000000u }; settings(in, kv, 1); }
static void e_frame(server *sv, vector<uint8_t> *in) { (void)sv; static const uint32_t kv[2] = { 5, 100 }; settings(in, kv, 1); }
static void e_wu0(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 4, H2_WINDOW_UPDATE, 0, 0); put32(in, 0); }
static void e_wu_over(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 4, H2_WINDOW_UPDATE, 0, 0); put32(in, 0x7fffffffu); }
static void e_pad(server *sv, vector<uint8_t> *in) {
    server_head(sv, in, 1, 0, ok_head, 0);
    frame(in, 3, H2_DATA, H2_PADDED, 1);
    push_back(in, 3);
    push_back(in, 'a');
    push_back(in, 'b');
}
static void e_hpack(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 2, H2_HEADERS, H2_END_HEADERS, 1); push_back(in, 0x80); push_back(in, 0x80); }
static void e_rst3(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 3, H2_RST_STREAM, 0, 1); for (int i = 0; i < 3; i++) push_back(in, 0); }
static void e_goaway1(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 8, H2_GOAWAY, 0, 1); put32(in, 0); put32(in, 0); }
static void e_ping7(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 7, H2_PING, 0, 0); for (int i = 0; i < 7; i++) push_back(in, 0); }
/* stream errors */
static void e_upper(server *sv, vector<uint8_t> *in) {
    static const char *const h[] = { ":status", "200", "Content-Type", "text/css", 0 };
    server_head(sv, in, 1, 0, h, 0);
}
static void e_nostatus(server *sv, vector<uint8_t> *in) {
    static const char *const h[] = { "content-type", "text/css", 0 };
    server_head(sv, in, 1, 0, h, 0);
}
static void e_twostatus(server *sv, vector<uint8_t> *in) {
    static const char *const h[] = { ":status", "200", ":status", "204", 0 };
    server_head(sv, in, 1, 0, h, 0);
}
static void e_pseudo_late(server *sv, vector<uint8_t> *in) {
    static const char *const h[] = { "content-type", "text/css", ":status", "200", 0 };
    server_head(sv, in, 1, 0, h, 0);
}
static void e_path(server *sv, vector<uint8_t> *in) {
    static const char *const h[] = { ":status", "200", ":path", "/", 0 };
    server_head(sv, in, 1, 0, h, 0);
}
static void e_conn(server *sv, vector<uint8_t> *in) {
    static const char *const h[] = { ":status", "200", "connection", "keep-alive", 0 };
    server_head(sv, in, 1, 0, h, 0);
}
static void e_te(server *sv, vector<uint8_t> *in) {
    static const char *const h[] = { ":status", "200", "te", "gzip", 0 };
    server_head(sv, in, 1, 0, h, 0);
}
static void e_101(server *sv, vector<uint8_t> *in) {
    static const char *const h[] = { ":status", "101", 0 };
    server_head(sv, in, 1, 0, h, 0);
}
static void e_nohead(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 2, H2_DATA, 0, 1); push_back(in, 'h'); push_back(in, 'i'); }
static void e_after(server *sv, vector<uint8_t> *in) {
    static const char *const h[] = { ":status", "200", 0 };
    server_head(sv, in, 1, 0, h, 0);
    frame(in, 2, H2_DATA, H2_END_STREAM, 1);
    push_back(in, 'h');
    push_back(in, 'i');
    frame(in, 2, H2_DATA, 0, 1);
    push_back(in, 'h');
    push_back(in, 'i');
}
static void e_trailers(server *sv, vector<uint8_t> *in) {
    static const char *const h[] = { ":status", "200", 0 };
    static const char *const t[] = { ":status", "200", 0 };
    server_head(sv, in, 1, 0, h, 0);
    server_head(sv, in, 1, H2_END_STREAM, t, 0);
}
static void e_window11(server *sv, vector<uint8_t> *in) {
    static const char *const h[] = { ":status", "200", 0 };
    server_head(sv, in, 1, 0, h, 0);
    frame(in, 11, H2_DATA, 0, 1);
    append_range(in, "eleven byte", 11);
}
static void e_wu_stream0(server *sv, vector<uint8_t> *in) { (void)sv; frame(in, 4, H2_WINDOW_UPDATE, 0, 1); put32(in, 0); }

static uint32_t rnd(uint32_t *x) {
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}

int main(void) {
    h2_conn c;
    server sv;
    memset(&c, 0, sizeof c);
    memset(&sv, 0, sizeof sv);
    defer(c) { H2_CONN_DROP(c); }
    defer(sv) { SERVER_DROP(sv); }
    hpack_init(&sv.enc, 4096);
    hpack_init(&sv.dec, 8192);   /* the table the server's SETTINGS will offer */
    vector<uint8_t> out;
    printf("== the client's first bytes, as Chromium's\n");
    h2_start(&c, &out);
    show_sent(&sv, &out, 0);
    printf("== six requests: a page (0), two sheets (0), a script (1), two images (2)\n");
    clear(out);
    get(&c, "/page.html", 0, &out);
    show_sent(&sv, &out, 0);
    clear(out);
    get(&c, "/a.css", 0, &out);
    get(&c, "/b.css", 0, &out);
    get(&c, "/app.js", 1, &out);
    get(&c, "/a.png", 2, &out);
    get(&c, "/b.png", 2, &out);
    show_sent(&sv, &out, 0);
    printf("%u streams open\n", h2_open_streams(&c));

    vector<uint8_t> in;
    printf("== the server's SETTINGS: a table of 8192, at most 4 streams, frames to 32768\n");
    {
        static const uint32_t kv[6] = { 1, 8192, 3, 4, 5, 32768 };
        settings(&in, kv, 3);
        feed(&c, &sv, &in, 0);
        printf("a stream may open: %s\n", h2_can_open(&c) ? "yes" : "no (6 open, 4 allowed)");
    }
    printf("== stream 1's head in HEADERS (padded, a priority) and two CONTINUATIONs, then DATA padded, its end\n");
    {
        clear(in);
        static const char *const h[] = { ":status", "200", "content-type", "text/html; charset=utf-8", "cache-control", "max-age=60",
                                         "set-cookie", "a=1; Path=/", "set-cookie", "b=2", 0 };
        vector<hpack_field> fs;
        vector<uint8_t> b, block;
        for (size_t i = 0; h[i]; i += 2) add(&fs, &b, h[i], h[i + 1]);
        hpack_encode(&sv.enc, data(fs), len(fs), data(b), HPACK_INDEX_ALL, 1, &block);
        size_t n = len(block), k1 = n / 3, k2 = n / 3;
        frame(&in, (uint32_t)(1 + 5 + k1 + 4), H2_HEADERS, H2_PADDED | H2_PRIORITY_FLAG, 1);
        push_back(in, 4);
        put32(&in, 0);
        push_back(in, 15);
        append_range(in, data(block), k1);
        for (int i = 0; i < 4; i++) push_back(in, 0);
        frame(&in, (uint32_t)k2, H2_CONTINUATION, 0, 1);
        append_range(in, data(block) + k1, k2);
        frame(&in, (uint32_t)(n - k1 - k2), H2_CONTINUATION, H2_END_HEADERS, 1);
        append_range(in, data(block) + k1 + k2, n - k1 - k2);
        frame(&in, 1 + 11 + 5, H2_DATA, H2_PADDED | H2_END_STREAM, 1);
        push_back(in, 5);
        append_range(in, "<p>hello</p", 11);
        for (int i = 0; i < 5; i++) push_back(in, 0);
        feed(&c, &sv, &in, 1);
    }
    printf("== stream 3: a 103, then its head, DATA in three pieces, trailers\n");
    {
        clear(in);
        static const char *const e[] = { ":status", "103", "link", "</a.css>; rel=preload", 0 };
        static const char *const h[] = { ":status", "200", "content-type", "text/css", 0 };
        static const char *const t[] = { "x-checksum", "abc", 0 };
        server_head(&sv, &in, 3, 0, e, 0);
        server_head(&sv, &in, 3, 0, h, 0);
        for (int i = 0; i < 3; i++) {
            frame(&in, 6, H2_DATA, 0, 3);
            append_range(in, ".a { }", 6);
        }
        server_head(&sv, &in, 3, H2_END_STREAM, t, 0);
        feed(&c, &sv, &in, 1);
    }
    printf("== stream 5 reset by the server (REFUSED_STREAM); stream 7's head malformed, reset by the client; a PING\n");
    {
        clear(in);
        frame(&in, 4, H2_RST_STREAM, 0, 5);
        put32(&in, H2_REFUSED_STREAM);
        static const char *const h[] = { ":status", "200", "X-Upper", "1", 0 };
        server_head(&sv, &in, 7, 0, h, 0);
        frame(&in, 8, H2_PING, 0, 0);
        append_range(in, "ping1234", 8);
        feed(&c, &sv, &in, 0);
        printf("%u streams open; a stream may open: %s\n", h2_open_streams(&c), h2_can_open(&c) ? "yes" : "no");
    }
    printf("== stream 9: 8 MB of DATA, its window and the connection's given back past half\n");
    {
        clear(in);
        static const char *const h[] = { ":status", "200", "content-type", "image/png", 0 };
        server_head(&sv, &in, 9, 0, h, 0);
        for (int i = 0; i < 500; i++) {
            frame(&in, 16384, H2_DATA, i == 499 ? H2_END_STREAM : 0, 9);
            size_t o = len(in);
            resize(in, o + 16384);
            memset(data(in) + o, 'p', 16384);
        }
        vector<uint8_t> o2;
        size_t at = 0, got = 0;
        unsigned events = 0;
        for (;;) {
            h2_event ev;
            vector<hpack_field> fs;
            vector<uint8_t> fb;
            size_t used = 0;
            if (!h2_read(&c, data(in) + at, len(in) - at, &used, &ev, &fs, &fb, &o2)) break;
            if (ev.kind == H2_EV_DATA) got += ev.len;
            if (ev.kind != H2_EV_NONE) events++;
            at += used;
        }
        printf("  < %u events, %zu bytes of DATA\n", events, got);
        show_sent(&sv, &o2, 0);
    }
    printf("== a new request (stream 13), then GOAWAY after stream 9: 11 and 13 were not processed\n");
    {
        clear(in);
        clear(out);
        get(&c, "/late.png", 2, &out);
        show_sent(&sv, &out, 0);
        frame(&in, 8 + 5, H2_GOAWAY, 0, 0);
        put32(&in, 9);
        put32(&in, H2_NO_ERROR);
        append_range(in, "bye!!", 5);
        feed(&c, &sv, &in, 0);
        printf("%u streams open; a stream may open: %s\n", h2_open_streams(&c), h2_can_open(&c) ? "yes" : "no");
    }

    printf("== a body of 250 bytes within a stream window of 100\n");
    {
        h2_conn d;
        server s2;
        memset(&d, 0, sizeof d);
        memset(&s2, 0, sizeof s2);
        defer(d) { H2_CONN_DROP(d); }
        defer(s2) { SERVER_DROP(s2); }
        hpack_init(&s2.enc, 4096);
        hpack_init(&s2.dec, 4096);
        vector<uint8_t> o, i2;
        h2_start(&d, &o);
        static const uint32_t kv[2] = { 4, 100 };
        settings(&i2, kv, 1);
        feed(&d, &s2, &i2, 0);
        clear(o);
        vector<hpack_field> fs;
        vector<uint8_t> b;
        add(&fs, &b, ":method", "POST");
        add(&fs, &b, ":authority", "localhost");
        add(&fs, &b, ":scheme", "https");
        add(&fs, &b, ":path", "/form");
        add(&fs, &b, "content-length", "250");
        uint32_t id = h2_request(&d, data(fs), len(fs), data(b), 0, 0, &o);
        char body[250];
        memset(body, 'q', sizeof body);
        size_t sent = h2_data(&d, id, (const uint8_t *)body, 250, 1, &o);
        printf("sent %zu of 250, the window %lld\n", sent, (long long)h2_send_window(&d, id));
        for (int round = 0; round < 3 && sent < 250; round++) {
            clear(i2);
            frame(&i2, 4, H2_WINDOW_UPDATE, 0, id);
            put32(&i2, 100);
            feed(&d, &s2, &i2, 0);
            sent += h2_data(&d, id, (const uint8_t *)body + sent, 250 - sent, 1, &o);
            printf("after a WINDOW_UPDATE of 100: sent %zu of 250\n", sent);
        }
        show_sent(&s2, &o, 0);
    }

    printf("== connection errors, each on a fresh connection\n");
    error_case("the server's first frame a PING", 0, e_first, 0);
    error_case("a frame of 16385 bytes", 1, e_big, 0);
    error_case("DATA on stream 0", 1, e_data0, 0);
    error_case("HEADERS on stream 2, which a server may not open", 1, e_even, 0);
    error_case("HEADERS on stream 9, never opened", 1, e_idle, 0);
    error_case("a CONTINUATION with no HEADERS before it", 1, e_cont, 0);
    error_case("a header block broken off by a PING", 1, e_interleave, 0);
    error_case("a PUSH_PROMISE", 1, e_push, 0);
    error_case("SETTINGS on stream 1", 1, e_set1, 0);
    error_case("SETTINGS of 7 bytes", 1, e_set7, 0);
    error_case("SETTINGS_ENABLE_PUSH of 2", 1, e_push2, 0);
    error_case("SETTINGS_INITIAL_WINDOW_SIZE of 2^31", 1, e_window, 0);
    error_case("SETTINGS_MAX_FRAME_SIZE of 100", 1, e_frame, 0);
    error_case("a WINDOW_UPDATE of the connection by 0", 1, e_wu0, 0);
    error_case("a WINDOW_UPDATE that takes the connection's window past 2^31-1", 1, e_wu_over, 0);
    error_case("DATA whose padding is its whole payload", 1, e_pad, 0);
    error_case("a header block that is no HPACK", 1, e_hpack, 0);
    error_case("RST_STREAM of 3 bytes", 1, e_rst3, 0);
    error_case("GOAWAY on stream 1", 1, e_goaway1, 0);
    error_case("PING of 7 bytes", 1, e_ping7, 0);
    error_case("11 bytes of DATA past a connection window made 10 bytes", 1, e_window11, 2);
    printf("== stream errors: the stream reset, the connection kept\n");
    error_case("a field named in uppercase", 1, e_upper, 0);
    error_case("a head without :status", 1, e_nostatus, 0);
    error_case("a head with two :status", 1, e_twostatus, 0);
    error_case(":status after a field", 1, e_pseudo_late, 0);
    error_case(":path in a response", 1, e_path, 0);
    error_case("a connection field", 1, e_conn, 0);
    error_case("te: gzip", 1, e_te, 0);
    error_case("101", 1, e_101, 0);
    error_case("DATA before the head", 1, e_nohead, 0);
    error_case("DATA after the stream's end: passed over, as Chromium passes over frames of streams it no longer holds", 1, e_after, 0);
    error_case("trailers with :status", 1, e_trailers, 0);
    error_case("11 bytes of DATA past a stream window made 10 bytes", 1, e_window11, 1);
    error_case("a WINDOW_UPDATE of a stream by 0", 1, e_wu_stream0, 0);

    printf("== conversations broken at random\n");
    {
        uint32_t x = 1234567;
        unsigned counts[8];
        memset(counts, 0, sizeof counts);
        unsigned frames = 0;
        for (int round = 0; round < 3000; round++) {
            h2_conn d;
            server s2;
            memset(&d, 0, sizeof d);
            memset(&s2, 0, sizeof s2);
            defer(d) { H2_CONN_DROP(d); }
            defer(s2) { SERVER_DROP(s2); }
            hpack_init(&s2.enc, 4096);
            hpack_init(&s2.dec, 4096);
            vector<uint8_t> o, i2;
            h2_start(&d, &o);
            for (int k = 0; k < 4; k++) get(&d, "/r", (uint32_t)(rnd(&x) % 5), &o);
            static const uint32_t kv[4] = { 3, 100, 4, 65535 };
            settings(&i2, kv, 2);
            static const char *const h[] = { ":status", "200", "content-type", "text/plain", 0 };
            for (uint32_t s = 1; s <= 7; s += 2) {
                server_head(&s2, &i2, s, 0, h, rnd(&x) % 3 ? 0 : 3);
                frame(&i2, 5, H2_DATA, H2_END_STREAM, s);
                append_range(i2, "hello", 5);
            }
            frame(&i2, 8, H2_PING, 0, 0);
            append_range(i2, "12345678", 8);
            frame(&i2, 8, H2_GOAWAY, 0, 0);
            put32(&i2, 7);
            put32(&i2, 0);
            int m = 1 + (int)(rnd(&x) % 6);
            for (int k = 0; k < m; k++) {
                size_t p = rnd(&x) % len(i2);
                if (rnd(&x) % 3) i2[p] ^= (uint8_t)(1u << (rnd(&x) % 8));
                else i2[p] = (uint8_t)rnd(&x);
            }
            if (rnd(&x) % 4 == 0) resize(i2, rnd(&x) % len(i2));
            size_t at = 0;
            for (;;) {
                h2_event ev;
                vector<hpack_field> fs;
                vector<uint8_t> fb;
                size_t used = 0;
                if (!h2_read(&d, data(i2) + at, len(i2) - at, &used, &ev, &fs, &fb, &o)) break;
                counts[ev.kind]++;
                frames++;
                at += used;
                if (ev.kind == H2_EV_ERROR) break;
            }
        }
        printf("3000 conversations, %u frames read:", frames);
        for (int k = 0; k < 8; k++) printf(" %s %u", kinds[k], counts[k]);
        printf("\n");
    }
    return 0;
}
