/* src/net/h2.c -- HTTP/2 (RFC 9113) for a client (h2.h): frames, streams, flow control.

   A connection begins as Chromium begins one: the preface, then SETTINGS of a header
   table of 65536 bytes, push off, a stream window of 6291456 and header lists to 262144,
   then a WINDOW_UPDATE that opens the connection's window to 15728640 (measured). A
   request is a stream of the next odd id: its header block HPACK coded as Chromium codes
   it, in HEADERS and the CONTINUATIONs the peer's frame size asks, with END_STREAM when
   it has no body; and with Chromium's priorities, a list of the open streams by SPDY
   priority, each new one an exclusive dependency on the newest open stream of its own
   priority, else of the next higher, its weight that priority's (256, 220, 183, 147,
   110). A body goes in DATA within both send windows and the peer's frame size.

   What the server sends is checked as RFC 9113 says, frame by frame:
   - the connection: its first frame its SETTINGS; a frame past 16384 bytes (none larger
     is asked for); a header block broken off by any frame but its own CONTINUATION; DATA,
     HEADERS, RST_STREAM or WINDOW_UPDATE on stream 0, or on a stream never opened; a
     PUSH_PROMISE (push is off); SETTINGS, PING or GOAWAY on a stream; a padding past its
     frame; a frame of the wrong length; a window past 2^31-1 or a WINDOW_UPDATE of 0;
     DATA past a window; a header block that is no HPACK. Each a connection error: a
     GOAWAY, and the connection dead.
   - a stream: its response's head malformed (an uppercase name, a pseudo-header after a
     field or other than :status, no :status or two, 101, a connection-specific field,
     te but trailers), DATA before its head or after its end, its header list past 256 KB.
     Each a stream error: a RST_STREAM, the stream's request failed.
   SETTINGS are acknowledged and applied: the encoder's table size, the most concurrent
   streams, the initial window (every open stream's moved by the change), the frame
   size. A PING is answered. A 1xx head is passed over for the final one; trailers are
   read and dropped. GOAWAY's last stream is kept: the streams after it were not
   processed, and their requests may go again on another connection. As Chromium does, a
   window is given back with a WINDOW_UPDATE once more than half of it has been used:
   each stream's by itself, the connection's by its own. A frame of a stream this side
   reset still counts for the connection's window, and its header block is decoded, for
   the table's sake, and dropped. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vector.pl>
#include "h2.h"

static const char *const h2_codes[14] = { "NO_ERROR", "PROTOCOL_ERROR", "INTERNAL_ERROR", "FLOW_CONTROL_ERROR", "SETTINGS_TIMEOUT",
                                          "STREAM_CLOSED", "FRAME_SIZE_ERROR", "REFUSED_STREAM", "CANCEL", "COMPRESSION_ERROR",
                                          "CONNECT_ERROR", "ENHANCE_YOUR_CALM", "INADEQUATE_SECURITY", "HTTP_1_1_REQUIRED" };
/* Chromium's weights by SPDY priority (Spdy3PriorityToHttp2Weight) */
static const uint32_t h2_weights[5] = { 256, 220, 183, 147, 110 };

static uint32_t h2_get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void h2_put32(vector<uint8_t> *o, uint32_t v) {
    push_back(o, (uint8_t)(v >> 24));
    push_back(o, (uint8_t)(v >> 16));
    push_back(o, (uint8_t)(v >> 8));
    push_back(o, (uint8_t)v);
}
/* a frame's header (4.1) */
static void h2_head(vector<uint8_t> *o, uint32_t len, uint32_t type, uint32_t flags, uint32_t stream) {
    push_back(o, (uint8_t)(len >> 16));
    push_back(o, (uint8_t)(len >> 8));
    push_back(o, (uint8_t)len);
    push_back(o, (uint8_t)type);
    push_back(o, (uint8_t)flags);
    h2_put32(o, stream & 0x7fffffffu);
}
/* an error code's name */
static void h2_code_name(uint32_t code, char *out, size_t cap) {
    if (code < 14) snprintf(out, cap, "%s", h2_codes[code]);
    else snprintf(out, cap, "error 0x%x", code);
}

static int h2_find(const h2_conn *c, uint32_t id) {
    for (size_t i = 0; i < len(c->streams); i++)
        if (c->streams[i].id == id) return (int)i;
    return -1;
}
/* the stream at i forgotten, the others kept in their order */
static void h2_forget(h2_conn *c, int i) {
    size_t n = len(c->streams);
    for (size_t k = (size_t)i; k + 1 < n; k++) c->streams[k] = c->streams[k + 1];
    resize(c->streams, n - 1);
}

void h2_start(h2_conn *c, vector<uint8_t> *out) {
    hpack_init(&c->enc, 4096);
    hpack_init(&c->dec, H2_OUR_TABLE);
    clear(c->streams);
    clear(c->block);
    c->block_stream = 0;
    c->block_end = 0;
    c->block_discard = 0;
    c->next_id = 1;
    c->peer_table = 4096;
    c->peer_max_streams = 0xffffffffu;
    c->peer_window = 65535;
    c->peer_frame = H2_FRAME_MAX;
    c->peer_header_list = 0xffffffffu;
    c->settings_seen = 0;
    c->send_window = 65535;
    c->recv_window = H2_OUR_CONN_WINDOW;
    c->unacked = 0;
    c->goaway = 0;
    c->goaway_last = 0x7fffffffu;
    c->dead = 0;
    static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    append_range(out, preface, 24);
    static const uint32_t settings[8] = { 1, H2_OUR_TABLE, 2, 0, 4, H2_OUR_WINDOW, 6, H2_OUR_HEADER_LIST };
    h2_head(out, 24, H2_SETTINGS, 0, 0);
    for (int k = 0; k < 8; k += 2) {
        push_back(out, (uint8_t)(settings[k] >> 8));
        push_back(out, (uint8_t)settings[k]);
        h2_put32(out, settings[k + 1]);
    }
    h2_head(out, 4, H2_WINDOW_UPDATE, 0, 0);
    h2_put32(out, H2_OUR_CONN_WINDOW - 65535);
}

uint32_t h2_request(h2_conn *c, const hpack_field *f, size_t nf, const uint8_t *bytes, int end_stream, uint32_t priority,
                    vector<uint8_t> *out) {
    uint32_t id = c->next_id;
    c->next_id += 2;
    if (priority > 4) priority = 4;
    /* its parent: the newest open stream of its priority, else of the next higher one */
    uint32_t parent = 0;
    for (int p = (int)priority; p >= 0 && !parent; p--)
        for (size_t i = 0; i < len(c->streams); i++)
            if (c->streams[i].priority == (uint32_t)p && c->streams[i].id > parent) parent = c->streams[i].id;
    vector<uint8_t> block;
    hpack_encode(&c->enc, f, nf, bytes, HPACK_CHROMIUM, 1, &block);
    size_t n = len(block), room = c->peer_frame - 5, first = n < room ? n : room;
    uint32_t flags = H2_PRIORITY_FLAG | (end_stream ? H2_END_STREAM : 0) | (first == n ? H2_END_HEADERS : 0);
    h2_head(out, (uint32_t)(5 + first), H2_HEADERS, flags, id);
    h2_put32(out, parent | 0x80000000u);
    push_back(out, (uint8_t)(h2_weights[priority] - 1));
    append_range(out, data(block), first);
    for (size_t at = first; at < n;) {
        size_t k = n - at < c->peer_frame ? n - at : c->peer_frame;
        h2_head(out, (uint32_t)k, H2_CONTINUATION, at + k == n ? H2_END_HEADERS : 0, id);
        append_range(out, data(block) + at, k);
        at += k;
    }
    h2_stream s;
    memset(&s, 0, sizeof s);
    s.id = id;
    s.priority = priority;
    s.sent_end = (uint32_t)(end_stream != 0);
    s.send_window = c->peer_window;
    s.recv_window = H2_OUR_WINDOW;
    push_back(c->streams, s);
    return id;
}

size_t h2_data(h2_conn *c, uint32_t stream, const uint8_t *p, size_t n, int end, vector<uint8_t> *out) {
    int i = h2_find(c, stream);
    if (i < 0 || c->dead || c->streams[i].sent_end) return 0;
    size_t sent = 0;
    for (;;) {
        int64_t room = c->send_window < c->streams[i].send_window ? c->send_window : c->streams[i].send_window;
        if (room > (int64_t)c->peer_frame) room = c->peer_frame;
        if (room < 0) room = 0;
        size_t k = n - sent < (size_t)room ? n - sent : (size_t)room;
        int last = end && sent + k == n;
        if (k == 0 && !last) break;
        h2_head(out, (uint32_t)k, H2_DATA, last ? H2_END_STREAM : 0, stream);
        append_range(out, p + sent, k);
        c->send_window -= (int64_t)k;
        c->streams[i].send_window -= (int64_t)k;
        sent += k;
        if (last) {
            c->streams[i].sent_end = 1;
            break;
        }
        if (sent == n) break;
    }
    return sent;
}

void h2_reset(h2_conn *c, uint32_t stream, uint32_t code, vector<uint8_t> *out) {
    h2_head(out, 4, H2_RST_STREAM, 0, stream);
    h2_put32(out, code);
    int i = h2_find(c, stream);
    if (i >= 0) h2_forget(c, i);
}

void h2_goaway(h2_conn *c, uint32_t code, vector<uint8_t> *out) {
    h2_head(out, 8, H2_GOAWAY, 0, 0);
    h2_put32(out, 0);
    h2_put32(out, code);
    c->goaway = 1;
    if (code != H2_NO_ERROR) c->dead = 1;
}

int h2_can_open(const h2_conn *c) {
    return !c->dead && !c->goaway && len(c->streams) < c->peer_max_streams && c->next_id < 0x7fffffffu;
}
uint32_t h2_open_streams(const h2_conn *c) { return (uint32_t)len(c->streams); }
int64_t h2_send_window(const h2_conn *c, uint32_t stream) {
    int i = h2_find(c, stream);
    if (i < 0) return 0;
    return c->streams[i].send_window < c->send_window ? c->streams[i].send_window : c->send_window;
}

/* ---- reading ---- */

/* a connection error, its words in ev->why: GOAWAY sent, the connection dead */
static int h2_error(h2_conn *c, h2_event *ev, uint32_t code, vector<uint8_t> *out) {
    ev->kind = H2_EV_ERROR;
    ev->code = code;
    char name[40];
    h2_code_name(code, name, sizeof name);
    size_t k = strlen(ev->why);
    snprintf(ev->why + k, sizeof ev->why - k, " (%s)", name);
    if (!c->dead) h2_goaway(c, code, out);
    c->dead = 1;
    return 1;
}
/* a stream error, its words in ev->why: RST_STREAM sent, the stream's request failed */
static int h2_stream_error(h2_conn *c, h2_event *ev, uint32_t stream, uint32_t code, vector<uint8_t> *out) {
    ev->kind = H2_EV_RESET;
    ev->stream = stream;
    ev->code = code;
    char name[40];
    h2_code_name(code, name, sizeof name);
    size_t k = strlen(ev->why);
    snprintf(ev->why + k, sizeof ev->why - k, " (%s)", name);
    h2_reset(c, stream, code, out);
    return 1;
}

/* the connection's window given back once more than half of it is used */
static void h2_ack_connection(h2_conn *c, vector<uint8_t> *out) {
    if (c->unacked <= H2_OUR_CONN_WINDOW / 2) return;
    h2_head(out, 4, H2_WINDOW_UPDATE, 0, 0);
    h2_put32(out, c->unacked);
    c->recv_window += c->unacked;
    c->unacked = 0;
}

/* a field name is a connection's own (8.2.2), which HTTP/2 forbids */
static int h2_connection_field(const uint8_t *n, size_t k) {
    static const char *const names[5] = { "connection", "keep-alive", "proxy-connection", "transfer-encoding", "upgrade" };
    for (int i = 0; i < 5; i++)
        if (strlen(names[i]) == k && memcmp(names[i], n, k) == 0) return 1;
    return 0;
}

/* a header block whole: decoded, for the table, then checked and given to the stream */
static int h2_headers_done(h2_conn *c, uint32_t id, h2_event *ev, vector<hpack_field> *fields, vector<uint8_t> *fbytes, vector<uint8_t> *out) {
    c->block_stream = 0;
    vector<hpack_field> fs;
    vector<uint8_t> fb;
    char why[200];
    why[0] = 0;
    int r = hpack_decode(&c->dec, data(c->block), len(c->block), &fs, &fb, H2_OUR_HEADER_LIST, why, sizeof why);
    clear(c->block);
    if (r == HPACK_BAD) {
        snprintf(ev->why, sizeof ev->why, "the server's header block is no HPACK: %s", why);
        return h2_error(c, ev, H2_COMPRESSION_ERROR, out);
    }
    int i = h2_find(c, id);
    if (c->block_discard || i < 0) return 1;
    if (r == HPACK_TOO_LARGE) {
        snprintf(ev->why, sizeof ev->why, "the response's header list passes %u bytes", H2_OUR_HEADER_LIST);
        return h2_stream_error(c, ev, id, H2_PROTOCOL_ERROR, out);
    }
    /* what HTTP/2 asks of a response's fields (8.2, 8.3.2) */
    uint32_t status = 0, nstatus = 0, regular = 0;
    const char *bad = 0;
    for (size_t k = 0; k < len(fs) && !bad; k++) {
        const uint8_t *nm = data(fb) + fs[k].name, *v = data(fb) + fs[k].value;
        size_t nl = fs[k].nlen, vl = fs[k].vlen;
        for (size_t j = 0; j < nl; j++)
            if (nm[j] >= 'A' && nm[j] <= 'Z') bad = "a field's name has an uppercase letter";
        if (bad) break;
        if (nl && nm[0] == ':') {
            if (regular) bad = "a pseudo-header field comes after a field";
            else if (nl != 7 || memcmp(nm, ":status", 7) != 0) bad = "a pseudo-header field other than :status";
            else {
                nstatus++;
                if (vl != 3 || v[0] < '1' || v[0] > '9' || v[1] < '0' || v[1] > '9' || v[2] < '0' || v[2] > '9') bad = ":status is not three digits";
                else status = (uint32_t)((v[0] - '0') * 100 + (v[1] - '0') * 10 + (v[2] - '0'));
            }
        } else {
            regular = 1;
            if (h2_connection_field(nm, nl)) bad = "a connection-specific field";
            else if (nl == 2 && memcmp(nm, "te", 2) == 0 && !(vl == 8 && memcmp(v, "trailers", 8) == 0)) bad = "te other than trailers";
        }
    }
    if (!c->streams[i].got_head) {
        if (!bad && nstatus != 1) bad = "the response has no single :status";
        if (!bad && status == 101) bad = "101 is not a status HTTP/2 has";
        if (!bad && status < 200 && c->block_end) bad = "an interim head ends the stream";
        if (bad) {
            snprintf(ev->why, sizeof ev->why, "the response's head is malformed: %s", bad);
            return h2_stream_error(c, ev, id, H2_PROTOCOL_ERROR, out);
        }
        if (status < 200) return 1;   /* an interim response: the final one is still to come */
        c->streams[i].got_head = 1;
        ev->kind = H2_EV_HEADERS;
        ev->status = status;
        append_range(fields, data(fs), len(fs));
        /* the fields' places are the head's bytes', past those already given */
        size_t base = len(*fbytes);
        for (size_t k = len(*fields) - len(fs); k < len(*fields); k++) {
            hpack_field f = (*fields)[k];
            f.name += (uint32_t)base;
            f.value += (uint32_t)base;
            (*fields)[k] = f;
        }
        append_range(fbytes, data(fb), len(fb));
    } else {
        if (!bad && (nstatus || !c->block_end)) bad = "trailers with a pseudo-header field, or that do not end the stream";
        if (bad) {
            snprintf(ev->why, sizeof ev->why, "the response's trailers are malformed: %s", bad);
            return h2_stream_error(c, ev, id, H2_PROTOCOL_ERROR, out);
        }
        ev->kind = H2_EV_HEADERS;
    }
    ev->end_stream = c->block_end;
    if (c->block_end) {
        c->streams[i].got_end = 1;
        if (c->streams[i].sent_end) h2_forget(c, i);
    }
    return 1;
}

int h2_read(h2_conn *c, const uint8_t *in, size_t n, size_t *used, h2_event *ev, vector<hpack_field> *fields, vector<uint8_t> *fbytes,
            vector<uint8_t> *out) {
    memset(ev, 0, sizeof *ev);
    if (c->dead || n < 9) return 0;
    uint32_t len = (uint32_t)in[0] << 16 | (uint32_t)in[1] << 8 | in[2], type = in[3], flags = in[4], id = h2_get32(in + 5) & 0x7fffffffu;
    ev->stream = id;
    if (len > H2_FRAME_MAX) {
        *used = n;
        snprintf(ev->why, sizeof ev->why, "the server sent a frame of %u bytes, past the %u allowed", len, (uint32_t)H2_FRAME_MAX);
        return h2_error(c, ev, H2_FRAME_SIZE_ERROR, out);
    }
    if (n < 9 + (size_t)len) return 0;
    *used = 9 + (size_t)len;
    const uint8_t *p = in + 9;
    if (!c->settings_seen && (type != H2_SETTINGS || (flags & H2_ACK))) {
        snprintf(ev->why, sizeof ev->why, "the server's first frame is not its SETTINGS");
        return h2_error(c, ev, H2_PROTOCOL_ERROR, out);
    }
    if (c->block_stream && (type != H2_CONTINUATION || id != c->block_stream)) {
        snprintf(ev->why, sizeof ev->why, "a header block was broken off by another frame");
        return h2_error(c, ev, H2_PROTOCOL_ERROR, out);
    }
    if (type == H2_DATA || type == H2_HEADERS) {
        if (id == 0) {
            snprintf(ev->why, sizeof ev->why, "%s on stream 0", type == H2_DATA ? "DATA" : "HEADERS");
            return h2_error(c, ev, H2_PROTOCOL_ERROR, out);
        }
        size_t at = 0, pad = 0;
        if (flags & H2_PADDED) {
            if (len < 1) {
                snprintf(ev->why, sizeof ev->why, "a padded frame without its pad length");
                return h2_error(c, ev, H2_FRAME_SIZE_ERROR, out);
            }
            pad = p[0];
            at = 1;
        }
        if (type == H2_HEADERS && (flags & H2_PRIORITY_FLAG)) at += 5;
        if (at + pad > len || (type == H2_DATA && pad && pad >= len)) {
            snprintf(ev->why, sizeof ev->why, "a frame's padding passes its payload");
            return h2_error(c, ev, H2_PROTOCOL_ERROR, out);
        }
        int i = h2_find(c, id);
        if (i < 0 && (!(id & 1) || id >= c->next_id)) {
            snprintf(ev->why, sizeof ev->why, "%s on stream %u, which this side never opened", type == H2_DATA ? "DATA" : "HEADERS", id);
            return h2_error(c, ev, H2_PROTOCOL_ERROR, out);
        }
        if (type == H2_HEADERS) {
            clear(c->block);
            append_range(c->block, p + at, len - at - pad);
            c->block_end = flags & H2_END_STREAM;
            c->block_discard = (uint32_t)(i < 0);
            if (!(flags & H2_END_HEADERS)) {
                c->block_stream = id;
                return 1;
            }
            return h2_headers_done(c, id, ev, fields, fbytes, out);
        }
        /* DATA: the whole payload, padding too, counts against the windows */
        if ((int64_t)len > c->recv_window) {
            snprintf(ev->why, sizeof ev->why, "the server's DATA passes the connection's window");
            return h2_error(c, ev, H2_FLOW_CONTROL_ERROR, out);
        }
        c->recv_window -= len;
        c->unacked += len;
        if (i < 0) {
            h2_ack_connection(c, out);
            return 1;
        }
        if (!c->streams[i].got_head || c->streams[i].got_end) {
            h2_ack_connection(c, out);
            snprintf(ev->why, sizeof ev->why, "DATA %s", c->streams[i].got_end ? "after the stream's end" : "before the response's head");
            return h2_stream_error(c, ev, id, c->streams[i].got_end ? H2_STREAM_CLOSED : H2_PROTOCOL_ERROR, out);
        }
        if ((int64_t)len > c->streams[i].recv_window) {
            h2_ack_connection(c, out);
            snprintf(ev->why, sizeof ev->why, "the server's DATA passes the stream's window");
            return h2_stream_error(c, ev, id, H2_FLOW_CONTROL_ERROR, out);
        }
        c->streams[i].recv_window -= len;
        c->streams[i].unacked += len;
        ev->kind = H2_EV_DATA;
        ev->off = 9 + at;
        ev->len = len - at - pad;
        ev->end_stream = flags & H2_END_STREAM;
        if (ev->end_stream) {
            c->streams[i].got_end = 1;
            if (c->streams[i].sent_end) h2_forget(c, i);
        } else if (c->streams[i].unacked > H2_OUR_WINDOW / 2) {
            h2_head(out, 4, H2_WINDOW_UPDATE, 0, id);
            h2_put32(out, c->streams[i].unacked);
            c->streams[i].recv_window += c->streams[i].unacked;
            c->streams[i].unacked = 0;
        }
        h2_ack_connection(c, out);
        return 1;
    }
    if (type == H2_CONTINUATION) {
        if (!c->block_stream) {
            snprintf(ev->why, sizeof ev->why, "a CONTINUATION with no header block before it");
            return h2_error(c, ev, H2_PROTOCOL_ERROR, out);
        }
        append_range(c->block, p, len);
        if (len(c->block) > 4 * H2_OUR_HEADER_LIST) {
            snprintf(ev->why, sizeof ev->why, "a header block passes %u bytes", 4 * H2_OUR_HEADER_LIST);
            return h2_error(c, ev, H2_ENHANCE_YOUR_CALM, out);
        }
        if (!(flags & H2_END_HEADERS)) return 1;
        return h2_headers_done(c, id, ev, fields, fbytes, out);
    }
    if (type == H2_RST_STREAM || type == H2_WINDOW_UPDATE) {
        if (len != 4) {
            snprintf(ev->why, sizeof ev->why, "%s of %u bytes", type == H2_RST_STREAM ? "RST_STREAM" : "WINDOW_UPDATE", len);
            return h2_error(c, ev, H2_FRAME_SIZE_ERROR, out);
        }
        uint32_t v = h2_get32(p);
        if (type == H2_WINDOW_UPDATE && id == 0) {
            v &= 0x7fffffffu;
            if (v == 0 || c->send_window + v > 0x7fffffff) {
                snprintf(ev->why, sizeof ev->why, "a WINDOW_UPDATE of the connection by %u", v);
                return h2_error(c, ev, v ? H2_FLOW_CONTROL_ERROR : H2_PROTOCOL_ERROR, out);
            }
            c->send_window += v;
            ev->kind = H2_EV_WINDOW;
            return 1;
        }
        if (id == 0) {
            snprintf(ev->why, sizeof ev->why, "RST_STREAM on stream 0");
            return h2_error(c, ev, H2_PROTOCOL_ERROR, out);
        }
        int i = h2_find(c, id);
        if (i < 0) {
            if (!(id & 1) || id >= c->next_id) {
                snprintf(ev->why, sizeof ev->why, "%s on stream %u, which this side never opened", type == H2_RST_STREAM ? "RST_STREAM" : "WINDOW_UPDATE", id);
                return h2_error(c, ev, H2_PROTOCOL_ERROR, out);
            }
            return 1;   /* a stream closed: what it was sent no longer matters */
        }
        if (type == H2_RST_STREAM) {
            char name[40];
            h2_code_name(v, name, sizeof name);
            h2_forget(c, i);
            ev->kind = H2_EV_RESET;
            ev->code = v;
            snprintf(ev->why, sizeof ev->why, "the server reset the stream (%s)", name);
            return 1;
        }
        v &= 0x7fffffffu;
        if (v == 0 || c->streams[i].send_window + v > 0x7fffffff) {
            snprintf(ev->why, sizeof ev->why, "a WINDOW_UPDATE of the stream by %u", v);
            return h2_stream_error(c, ev, id, v ? H2_FLOW_CONTROL_ERROR : H2_PROTOCOL_ERROR, out);
        }
        c->streams[i].send_window += v;
        ev->kind = H2_EV_WINDOW;
        return 1;
    }
    if (type == H2_SETTINGS) {
        if (id != 0 || (flags & H2_ACK ? len != 0 : len % 6 != 0)) {
            snprintf(ev->why, sizeof ev->why, "a SETTINGS frame on stream %u of %u bytes", id, len);
            return h2_error(c, ev, id ? H2_PROTOCOL_ERROR : H2_FRAME_SIZE_ERROR, out);
        }
        if (flags & H2_ACK) return 1;
        for (uint32_t k = 0; k < len; k += 6) {
            uint32_t key = (uint32_t)p[k] << 8 | p[k + 1], v = h2_get32(p + k + 2);
            if (key == 1) {
                hpack_set_limit(&c->enc, v);
                c->peer_table = v;
            } else if (key == 2 && v > 1) {
                snprintf(ev->why, sizeof ev->why, "SETTINGS_ENABLE_PUSH of %u", v);
                return h2_error(c, ev, H2_PROTOCOL_ERROR, out);
            } else if (key == 3) c->peer_max_streams = v;
            else if (key == 4) {
                if (v > 0x7fffffffu) {
                    snprintf(ev->why, sizeof ev->why, "SETTINGS_INITIAL_WINDOW_SIZE of %u", v);
                    return h2_error(c, ev, H2_FLOW_CONTROL_ERROR, out);
                }
                int64_t delta = (int64_t)v - (int64_t)c->peer_window;
                for (size_t s = 0; s < len(c->streams); s++) {
                    if (c->streams[s].send_window + delta > 0x7fffffff) {
                        snprintf(ev->why, sizeof ev->why, "SETTINGS_INITIAL_WINDOW_SIZE takes a stream's window past 2^31-1");
                        return h2_error(c, ev, H2_FLOW_CONTROL_ERROR, out);
                    }
                    c->streams[s].send_window += delta;
                }
                c->peer_window = v;
            } else if (key == 5) {
                if (v < 16384 || v > 16777215) {
                    snprintf(ev->why, sizeof ev->why, "SETTINGS_MAX_FRAME_SIZE of %u", v);
                    return h2_error(c, ev, H2_PROTOCOL_ERROR, out);
                }
                c->peer_frame = v;
            } else if (key == 6) c->peer_header_list = v;
        }
        h2_head(out, 0, H2_SETTINGS, H2_ACK, 0);
        c->settings_seen = 1;
        ev->kind = H2_EV_SETTINGS;
        return 1;
    }
    if (type == H2_PUSH_PROMISE) {
        snprintf(ev->why, sizeof ev->why, "the server pushed a stream, though push is off");
        return h2_error(c, ev, H2_PROTOCOL_ERROR, out);
    }
    if (type == H2_PING) {
        if (id != 0 || len != 8) {
            snprintf(ev->why, sizeof ev->why, "a PING on stream %u of %u bytes", id, len);
            return h2_error(c, ev, id ? H2_PROTOCOL_ERROR : H2_FRAME_SIZE_ERROR, out);
        }
        if (!(flags & H2_ACK)) {
            h2_head(out, 8, H2_PING, H2_ACK, 0);
            append_range(out, p, 8);
        }
        return 1;
    }
    if (type == H2_GOAWAY) {
        if (id != 0 || len < 8) {
            snprintf(ev->why, sizeof ev->why, "a GOAWAY on stream %u of %u bytes", id, len);
            return h2_error(c, ev, id ? H2_PROTOCOL_ERROR : H2_FRAME_SIZE_ERROR, out);
        }
        uint32_t last = h2_get32(p) & 0x7fffffffu, code = h2_get32(p + 4);
        if (last < c->goaway_last) c->goaway_last = last;
        c->goaway = 1;
        /* the streams it did not process are forgotten: their requests may go again */
        for (size_t k = len(c->streams); k > 0; k--)
            if (c->streams[k - 1].id > c->goaway_last) h2_forget(c, (int)(k - 1));
        char name[40];
        h2_code_name(code, name, sizeof name);
        ev->kind = H2_EV_GOAWAY;
        ev->last = c->goaway_last;
        ev->code = code;
        snprintf(ev->why, sizeof ev->why, "the server went away (%s) after stream %u", name, c->goaway_last);
        return 1;
    }
    if (type == H2_PRIORITY) {
        if (id == 0) {
            snprintf(ev->why, sizeof ev->why, "PRIORITY on stream 0");
            return h2_error(c, ev, H2_PROTOCOL_ERROR, out);
        }
        if (len != 5 && h2_find(c, id) >= 0) {
            snprintf(ev->why, sizeof ev->why, "a PRIORITY frame of %u bytes", len);
            return h2_stream_error(c, ev, id, H2_FRAME_SIZE_ERROR, out);
        }
        return 1;
    }
    return 1;   /* a frame of a type not known here: passed over (5.5) */
}
