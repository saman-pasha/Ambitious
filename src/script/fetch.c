/* src/script/fetch.c -- the network a page's scripts reach: fetch() with Request,
   Response and Headers (Fetch), and XMLHttpRequest (XHR), through the loader
   (src/net/), in virtual time.

   A REQUEST is made when a script asks: its URL resolved against the page's, its method,
   header lines and body as the script gave them, fetched by the loader's
   net_fetch_request (a GET of a data: or http: URL, or any method over http:). Its
   answer is a host task on the timers' queue, 1 ms of virtual time later, as a response
   comes from the network: a timeout of 0 the script set before runs first. fetch's
   promise is settled then, an asynchronous XMLHttpRequest's events fired then; a
   synchronous one's before send() returns.

   THE ORIGIN'S RULES are the loader's origins: a URL of the page's origin is fetched; a
   data: URL is; a request of another origin is blocked, as a server that sends no CORS
   header has it (the loader keeps no response's headers but its type, so no
   Access-Control-Allow-Origin is read) -- unless fetch's mode is no-cors, which fetches
   it and answers an opaque response, its status 0 and no body to read. A file: URL is
   not fetched for a script. Each refusal, and each HTTP error, writes the message
   Chromium's console does, at the moment the answer comes.

   What is simplified: a response's headers are its content type and length, the only
   ones the loader keeps; its status text is HTTP's for its status; no request can be
   aborted once it is answered; no CORS preflight, credentials mode or cache mode. Each
   request a script made is a record of the runtime (script_request/5 ...): what asked,
   the method, the URL, how it ended, the loader's record of it. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <vector.pl>
#include <amb.pl>
#include "../net/url.h"
amb_interface(dom);
amb_interface(net);
amb_interface(script);
#include "../js/js.h"
#include "host.h"
#include "script.h"

/* the loader's fetch by method (src/net/net.c) */
net_resource_h net_fetch_request(net_loader *l, const char *url, size_t url_len, net_kind kind, const char *method, const char *sent,
                                 size_t sent_n, const char *hdrs, size_t hdrs_n, uint32_t from);
/* script.c's, which takes a vector */
int sh_utf8(js_vm *vm, jv v, vector<char> *out);

/* the natives of this unit */
enum {
    SF_FETCH = SH_FETCH_FIRST, SF_RESPONSE, SF_RESPONSE_ERROR, SF_RS_TEXT, SF_RS_JSON, SF_RS_BUFFER, SF_RS_CLONE,
    SF_HEADERS, SF_H_GET, SF_H_SET, SF_H_APPEND, SF_H_HAS, SF_H_DELETE, SF_H_FOREACH, SF_H_ENTRIES, SF_H_KEYS, SF_H_VALUES,
    SF_REQUEST, SF_RQ_CLONE, SF_RQ_TEXT, SF_XHR, SF_X_OPEN, SF_X_SEND, SF_X_ABORT, SF_X_SETHDR, SF_X_GETHDR, SF_X_ALLHDRS,
    SF_X_MIME, SF_X_ADD, SF_X_REMOVE, SF_X_DISPATCH, SF_PROGRESS, SF_N
};
/* a request's kind, and fetch's modes */
enum { SR_FETCH = 1, SR_XHR, SR_OBJECT, SR_BODY };
enum { SM_CORS, SM_NO_CORS, SM_SAME_ORIGIN };
/* a response's type */
enum { ST_BASIC, ST_CORS, ST_OPAQUE, ST_DEFAULT, ST_ERROR };
/* XMLHttpRequest's responseType */
enum { RT_TEXT, RT_JSON, RT_ARRAYBUFFER, RT_BLOB, RT_DOCUMENT };
/* a request's flags */
#define SRF_SENT 1u
#define SRF_ABORTED 2u
#define SRF_DONE 4u
#define SRF_READ 8u            /* a Request's body read */
#define SRF_REDIRECTED 16u     /* blocked at a redirect to another origin */
#define SRF_SAME_MODE 32u      /* blocked by the same-origin mode */

/* ---- the network text ---- */

static uint32_t sf_put(struct js_host *h, const char *p, size_t n) {
    uint32_t off = (uint32_t)len(h->ntext);
    append_range(h->ntext, p, n);
    return off;
}
static jv sf_text(js_vm *vm, uint32_t off, uint32_t n) { return sh_str(vm, data(vm->host->ntext) + off, n); }
static int sf_ci_eq(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char x = a[i] >= 'A' && a[i] <= 'Z' ? (char)(a[i] + 32) : a[i], y = b[i] >= 'A' && b[i] <= 'Z' ? (char)(b[i] + 32) : b[i];
        if (x != y) return 0;
    }
    return 1;
}
static int sf_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

/* HTTP's reason phrase for a status (RFC 9110 15), into out */
void script_reason(uint32_t s, char *out, size_t cap) {
    static const uint32_t codes[] = { 200, 201, 202, 203, 204, 205, 206, 300, 301, 302, 303, 304, 307, 308, 400, 401, 402, 403, 404,
                                      405, 406, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417, 418, 421, 422, 425, 426, 428,
                                      429, 431, 451, 500, 501, 502, 503, 504, 505 };
    static const char *const words[] = { "OK", "Created", "Accepted", "Non-Authoritative Information", "No Content", "Reset Content",
        "Partial Content", "Multiple Choices", "Moved Permanently", "Found", "See Other", "Not Modified", "Temporary Redirect",
        "Permanent Redirect", "Bad Request", "Unauthorized", "Payment Required", "Forbidden", "Not Found", "Method Not Allowed",
        "Not Acceptable", "Proxy Authentication Required", "Request Timeout", "Conflict", "Gone", "Length Required",
        "Precondition Failed", "Content Too Large", "URI Too Long", "Unsupported Media Type", "Range Not Satisfiable",
        "Expectation Failed", "I'm a teapot", "Misdirected Request", "Unprocessable Content", "Too Early", "Upgrade Required",
        "Precondition Required", "Too Many Requests", "Request Header Fields Too Large", "Unavailable For Legal Reasons",
        "Internal Server Error", "Not Implemented", "Bad Gateway", "Service Unavailable", "Gateway Timeout",
        "HTTP Version Not Supported" };
    out[0] = 0;
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) if (codes[i] == s) snprintf(out, cap, "%s", words[i]);
}

/* a message of the browser's own on the console, as Chromium writes it: an error */
static void sf_console(js_vm *vm, const char *msg) { js_console(vm, 3, js_str_utf8(vm, msg, strlen(msg))); }

/* ---- the origin's rules ---- */

/* the scheme of a URL: 1 http(s), 2 data, 3 file, 0 another */
static int sf_scheme(const char *u) {
    if (strncmp(u, "http:", 5) == 0 || strncmp(u, "https:", 6) == 0) return 1;
    if (strncmp(u, "data:", 5) == 0) return 2;
    if (strncmp(u, "file:", 5) == 0) return 3;
    return 0;
}
/* the page's origin, serialized (null for a file: page's) */
static void sf_page_origin(js_vm *vm, char *out, size_t cap) {
    char page[4200];
    sh_page_url(vm, page, sizeof page);
    if (!net_url_origin(page, out, cap)) snprintf(out, cap, "null");
}
static int sf_same_origin(js_vm *vm, const char *url) {
    char a[600], b[600];
    sf_page_origin(vm, a, sizeof a);
    if (!net_url_origin(url, b, sizeof b)) return 0;
    return strcmp(a, "null") != 0 && strcmp(a, b) == 0;
}

/* ---- the records ---- */

/* the runtime's record of a request: who asked, what, how it ended */
static uint32_t sf_rec(js_vm *vm, uint32_t api, const char *method, const char *url, uint32_t resource, uint32_t status,
                       script_req_outcome oc, int cross) {
    struct js_host *h = vm->host;
    amb_span m = text_add(h->rt, method, strlen(method));
    amb_span u = text_add(h->rt, url, strlen(url));
    script_request_h r = request_new(h->rt);
    h->rt->request[r.index].run = vm->cur_run;
    h->rt->request[r.index].api = (script_api)api;
    h->rt->request[r.index].method = m;
    h->rt->request[r.index].url = u;
    net_resource_h none = { 0, 0, 0 };
    h->rt->request[r.index].resource = resource ? resource_handle(h->nt, resource) : none;
    h->rt->request[r.index].status = status;
    h->rt->request[r.index].outcome = oc;
    h->rt->request[r.index].cross = (uint8_t)(cross ? 1 : 0);
    return r.index;
}

/* a host object of this unit: its kind, its prototype, its record */
static jv sf_obj(js_vm *vm, uint32_t kind, uint32_t proto, uint32_t rec) {
    uint32_t o = js_obj_new(vm, JK_HOST, vm->host->protos[proto]);
    vm->objs[o].a = rec;
    vm->objs[o].c = kind;
    return jv_obj(o);
}
static int sf_is(js_vm *vm, jv v, uint32_t kind) { return jv_is_obj(v) && vm->objs[jv_idx(v)].kind == JK_HOST && vm->objs[jv_idx(v)].c == kind; }

/* ---- Headers (Fetch 5.2): a list of names lower-cased and values, an object's ---- */

static void sf_hdr_add(struct js_host *h, uint32_t owner, const char *n, size_t nn, const char *v, size_t vn) {
    /* the value's leading and trailing white space is not its */
    while (vn && sf_ws(v[vn - 1])) vn--;
    while (vn && sf_ws(v[0])) { v++; vn--; }
    sh_hdr e;
    e.owner = owner;
    e.noff = (uint32_t)len(h->ntext);
    e.nlen = (uint32_t)nn;
    for (size_t i = 0; i < nn; i++) push_back(h->ntext, n[i] >= 'A' && n[i] <= 'Z' ? (char)(n[i] + 32) : n[i]);
    e.voff = sf_put(h, v, vn);
    e.vlen = (uint32_t)vn;
    e.live = 1;
    push_back(h->hdrs, e);
}
static void sf_hdr_del(struct js_host *h, uint32_t owner, const char *n, size_t nn) {
    for (size_t i = 0; i < len(h->hdrs); i++) {
        sh_hdr e = h->hdrs[i];
        if (e.live && e.owner == owner && e.nlen == nn && sf_ci_eq(data(h->ntext) + e.noff, n, nn)) h->hdrs[i].live = 0;
    }
}
/* the values of a name, joined by ", " (Fetch's get), into out: 0 when it has none */
static int sf_hdr_get(struct js_host *h, uint32_t owner, const char *n, size_t nn, vector<char> *out) {
    clear(*out);
    int any = 0;
    for (size_t i = 0; i < len(h->hdrs); i++) {
        sh_hdr e = h->hdrs[i];
        if (!e.live || e.owner != owner || e.nlen != nn || !sf_ci_eq(data(h->ntext) + e.noff, n, nn)) continue;
        if (any) append_range(*out, ", ", 2);
        append_range(*out, data(h->ntext) + e.voff, e.vlen);
        any = 1;
    }
    return any;
}
/* a header's name is a token (RFC 9110 5.6.2) */
static int sf_token(const char *n, size_t nn) {
    if (!nn) return 0;
    for (size_t i = 0; i < nn; i++) {
        unsigned char c = (unsigned char)n[i];
        if (c <= 32 || c >= 127 || strchr("()<>@,;:\\\"/[]?={}", c)) return 0;
    }
    return 1;
}
/* the header lines of a Headers object, to send: Name: value, each ending in CRLF */
static void sf_hdr_lines(struct js_host *h, uint32_t owner, vector<char> *out) {
    for (size_t i = 0; i < len(h->hdrs); i++) {
        sh_hdr e = h->hdrs[i];
        if (!e.live || e.owner != owner) continue;
        append_range(*out, data(h->ntext) + e.noff, e.nlen);
        append_range(*out, ": ", 2);
        append_range(*out, data(h->ntext) + e.voff, e.vlen);
        append_range(*out, "\r\n", 2);
    }
}
/* a Headers object: new, empty, of a guard (1: immutable, a response's from the network) */
static jv sf_headers_new(js_vm *vm, uint32_t immutable) {
    jv o = sf_obj(vm, HK_HEADERS, HP_HEADERS, 0);
    vm->objs[jv_idx(o)].a = jv_idx(o);
    vm->objs[jv_idx(o)].b = immutable;
    return o;
}
/* a Headers init (a Headers, a sequence of pairs, a record) into the list of owner */
static int sf_hdr_fill(js_vm *vm, uint32_t owner, jv init) {
    struct js_host *h = vm->host;
    if (jv_is_nullish(init)) return 1;
    if (!jv_is_obj(init)) { js_throw_msg(vm, vm->type_error_proto, "Failed to construct 'Headers': The provided value is not of type '(record<ByteString, ByteString> or sequence<sequence<ByteString>>)'."); return 0; }
    vector<char> n, v;
    if (sf_is(vm, init, HK_HEADERS)) {
        uint32_t from = vm->objs[jv_idx(init)].a;
        size_t k = len(h->hdrs);
        for (size_t i = 0; i < k; i++) {
            sh_hdr e = h->hdrs[i];
            if (!e.live || e.owner != from) continue;
            clear(n); clear(v);
            append_range(n, data(h->ntext) + e.noff, e.nlen);
            append_range(v, data(h->ntext) + e.voff, e.vlen);
            sf_hdr_add(h, owner, data(n), len(n), data(v), len(v));
        }
        return 1;
    }
    if (vm->objs[jv_idx(init)].kind == JK_ARRAY) {
        uint32_t a = jv_idx(init);
        for (uint32_t i = 0; i < vm->objs[a].b; i++) {
            jv pair = js_array_get(vm, a, i);
            if (!jv_is_obj(pair) || vm->objs[jv_idx(pair)].kind != JK_ARRAY || vm->objs[jv_idx(pair)].b != 2) {
                js_throw_msg(vm, vm->type_error_proto, "Failed to construct 'Headers': Invalid value");
                return 0;
            }
            if (!sh_utf8(vm, js_array_get(vm, jv_idx(pair), 0), &n) || !sh_utf8(vm, js_array_get(vm, jv_idx(pair), 1), &v)) return 0;
            if (!sf_token(data(n), len(n))) { js_throw_msg(vm, vm->type_error_proto, "Failed to construct 'Headers': Invalid name"); return 0; }
            sf_hdr_add(h, owner, data(n), len(n), data(v), len(v));
        }
        return 1;
    }
    uint32_t keys = js_array_new(vm, 0);
    js_own_keys(vm, jv_idx(init), 1, keys);
    for (uint32_t i = 0; i < vm->objs[keys].b; i++) {
        jv k = js_array_get(vm, keys, i);
        jv val = js_get(vm, init, js_atom(vm, jv_idx(k)));
        if (vm->throwing) return 0;
        if (!sh_utf8(vm, k, &n) || !sh_utf8(vm, val, &v)) return 0;
        if (!sf_token(data(n), len(n))) { js_throw_msg(vm, vm->type_error_proto, "Failed to construct 'Headers': Invalid name"); return 0; }
        sf_hdr_add(h, owner, data(n), len(n), data(v), len(v));
    }
    return 1;
}
/* the header list sorted by name and combined (Fetch's sort and combine), as pairs of
   strings in an array */
static jv sf_hdr_sorted(js_vm *vm, uint32_t owner) {
    struct js_host *h = vm->host;
    vector<uint32_t> names;
    for (size_t i = 0; i < len(h->hdrs); i++) {
        sh_hdr e = h->hdrs[i];
        if (!e.live || e.owner != owner) continue;
        int dup = 0;
        for (size_t k = 0; k < len(names) && !dup; k++) {
            sh_hdr f = h->hdrs[names[k]];
            if (f.nlen == e.nlen && memcmp(data(h->ntext) + f.noff, data(h->ntext) + e.noff, e.nlen) == 0) dup = 1;
        }
        if (!dup) push_back(names, (uint32_t)i);
    }
    /* by name, a small insertion sort */
    for (size_t i = 1; i < len(names); i++) {
        uint32_t x = names[i];
        size_t j = i;
        while (j > 0) {
            sh_hdr a = h->hdrs[names[j - 1]], b = h->hdrs[x];
            size_t m = a.nlen < b.nlen ? a.nlen : b.nlen;
            int c = memcmp(data(h->ntext) + a.noff, data(h->ntext) + b.noff, m);
            if (c < 0 || (c == 0 && a.nlen <= b.nlen)) break;
            names[j] = names[j - 1];
            j--;
        }
        names[j] = x;
    }
    uint32_t out = js_array_new(vm, 0);
    vector<char> v, n;
    for (size_t i = 0; i < len(names); i++) {
        sh_hdr e = h->hdrs[names[i]];
        clear(n);
        append_range(n, data(h->ntext) + e.noff, e.nlen);
        sf_hdr_get(h, owner, data(n), len(n), &v);
        uint32_t pair = js_array_new(vm, 0);
        jv ns = sh_str(vm, data(n), len(n));
        js_array_push(vm, pair, ns);
        jv vs = sh_str(vm, data(v), len(v));
        js_array_push(vm, pair, vs);
        js_array_push(vm, out, jv_obj(pair));
    }
    return jv_obj(out);
}

/* ---- Response (Fetch 5.5) ---- */

static uint32_t sf_resp_push(struct js_host *h, const sh_resp *r) {
    push_back(h->resps, *r);
    return (uint32_t)len(h->resps) - 1;
}
/* a body's text: its bytes as UTF-8, a byte order mark not its */
static jv sf_body_text(js_vm *vm, uint32_t off, uint32_t n) {
    const char *p = data(vm->host->ntext) + off;
    if (n >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) { off += 3; n -= 3; }
    return sf_text(vm, off, n);
}
/* a body as an ArrayBuffer of its bytes */
static jv sf_body_buffer(js_vm *vm, uint32_t off, uint32_t n) {
    jv ctor = js_get(vm, jv_obj(vm->global), js_atom_ascii(vm, "ArrayBuffer"));
    jv len0 = jv_num(n);
    jv ab = js_construct(vm, ctor, &len0, 1, ctor);
    if (vm->throwing || !jv_is_obj(ab) || vm->objs[jv_idx(ab)].kind != JK_ARRAYBUFFER) return JV_HOLE;
    uint32_t first = vm->objs[jv_idx(ab)].a;
    for (uint32_t i = 0; i < n; i++) vm->bytes[first + i] = (uint8_t)vm->host->ntext[off + i];
    return ab;
}
/* a body parsed as JSON, by the page's own JSON.parse */
static jv sf_body_json(js_vm *vm, uint32_t off, uint32_t n) {
    jv text = sf_body_text(vm, off, n);
    jv json = js_get(vm, jv_obj(vm->global), js_atom_ascii(vm, "JSON"));
    jv parse = js_get(vm, json, js_atom_ascii(vm, "parse"));
    if (vm->throwing) return JV_HOLE;
    return js_call(vm, parse, json, &text, 1);
}
static jv sf_rejected(js_vm *vm, const char *msg) {
    uint32_t e = js_error_new(vm, vm->type_error_proto, js_str_ascii(vm, msg));
    uint32_t p = js_promise_new(vm);
    js_promise_settle(vm, p, 1, jv_obj(e));
    return jv_obj(p);
}
static uint32_t sf_req_push(struct js_host *h, const sh_req *q);
/* a body read by text(), json() or arrayBuffer(): its promise, settled by a host task, as
   a body's stream is read after the task that asked; a body already read rejects */
static jv sf_read_body(js_vm *vm, uint32_t id, const char *what, uint32_t boff, uint32_t blen, uint32_t *used, const char *iface) {
    if (*used) {
        char msg[200];
        snprintf(msg, sizeof msg, "Failed to execute '%s' on '%s': body stream already read", what, iface);
        return sf_rejected(vm, msg);
    }
    *used = 1;
    sh_req q;
    memset(&q, 0, sizeof q);
    q.kind = SR_BODY;
    q.obj = js_promise_new(vm);
    q.flags = id;
    q.boff = boff;
    q.blen = blen;
    uint32_t r = sf_req_push(vm->host, &q);
    sh_task(vm, 0, SH_TASK_BODY, r);
    return jv_obj(q.obj);
}
/* the body read: its promise settled with the text, the JSON's value or the ArrayBuffer */
static void sf_body_task(js_vm *vm, uint32_t r) {
    sh_req q = vm->host->reqs[r];
    jv v = q.flags == SF_RS_JSON ? sf_body_json(vm, q.boff, q.blen) : q.flags == SF_RS_BUFFER ? sf_body_buffer(vm, q.boff, q.blen)
                                                                                                : sf_body_text(vm, q.boff, q.blen);
    if (vm->throwing == 1) {
        jv e = vm->exception;
        vm->throwing = 0;
        vm->exception = JV_UNDEF;
        js_promise_settle(vm, q.obj, 1, e);
    } else if (!vm->throwing) js_promise_resolve_with(vm, q.obj, v);
}

/* the Response of a request the loader answered: its status, type, URL, body */
static jv sf_response_of(js_vm *vm, uint32_t id) {
    struct js_host *h = vm->host;
    sh_req q = h->reqs[id];
    sh_resp r;
    memset(&r, 0, sizeof r);
    r.type = ST_BASIC;
    if (q.outcome == SCRIPT_REQ_OUTCOME_OPAQUE) {
        r.type = ST_OPAQUE;
        r.uoff = r.boff = r.toff = r.soff = (uint32_t)len(h->ntext);
    } else {
        net_resource res = h->nt->resource[q.resource];
        r.status = res.status;
        amb_span fu = res.final_url, ty = res.type, cs = res.charset;
        r.uoff = sf_put(h, text_ptr(h->nt, fu), fu.len);
        r.ulen = fu.len;
        r.boff = sf_put(h, bytes_ptr(h->nt, res.body), res.body.len);
        r.blen = res.body.len;
        vector<char> t;
        append_range(t, text_ptr(h->nt, ty), ty.len);
        if (cs.len) { append_range(t, ";charset=", 9); append_range(t, text_ptr(h->nt, cs), cs.len); }
        r.toff = sf_put(h, data(t), len(t));
        r.tlen = (uint32_t)len(t);
        char reason[64];
        script_reason(r.status, reason, sizeof reason);
        r.soff = sf_put(h, reason, strlen(reason));
        r.slen = (uint32_t)strlen(reason);
        for (uint32_t k = 1; k < h->nt->hop_len; k++) if (h->nt->hop[k].live && h->nt->hop[k].resource == q.resource) r.redirected = 1;
    }
    uint32_t ri = sf_resp_push(h, &r);
    return sf_obj(vm, HK_RESPONSE, HP_RESPONSE, ri);
}
/* a Response's Headers object: its content type and length, made at the first ask */
static jv sf_resp_headers(js_vm *vm, uint32_t ri) {
    struct js_host *h = vm->host;
    if (h->resps[ri].headers) return jv_obj(h->resps[ri].headers);
    sh_resp r = h->resps[ri];
    jv hv = sf_headers_new(vm, r.type == ST_DEFAULT ? 0 : 1);
    uint32_t owner = jv_idx(hv);
    if (r.type != ST_OPAQUE && r.type != ST_DEFAULT) {
        char n[32];
        snprintf(n, sizeof n, "%u", r.blen);
        sf_hdr_add(h, owner, "content-length", 14, n, strlen(n));
    }
    if (r.tlen) {
        vector<char> t;
        append_range(t, data(h->ntext) + r.toff, r.tlen);
        sf_hdr_add(h, owner, "content-type", 12, data(t), len(t));
    }
    h->resps[ri].headers = owner;
    return hv;
}

/* ---- a request's parts from a script's arguments ---- */

/* a method, as Fetch normalizes one (the standard ones upper-cased): 0 when it is no
   token or is forbidden (CONNECT, TRACE, TRACK) */
static int sf_method(js_vm *vm, jv v, vector<char> *out, const char *iface) {
    if (!sh_utf8(vm, v, out)) return 0;
    static const char *const std[] = { "DELETE", "GET", "HEAD", "OPTIONS", "POST", "PUT", "PATCH" };
    for (size_t i = 0; i < sizeof std / sizeof std[0]; i++)
        if (len(*out) == strlen(std[i]) && sf_ci_eq(data(*out), std[i], len(*out))) { clear(*out); append_range(*out, std[i], strlen(std[i])); }
    char msg[200];
    if (!sf_token(data(*out), len(*out))) {
        snprintf(msg, sizeof msg, "Failed to execute '%s': '%.*s' is not a valid HTTP method.", iface, (int)len(*out), data(*out));
        js_throw_msg(vm, vm->type_error_proto, msg);
        return 0;
    }
    static const char *const bad[] = { "CONNECT", "TRACE", "TRACK" };
    for (size_t i = 0; i < 3; i++)
        if (len(*out) == strlen(bad[i]) && sf_ci_eq(data(*out), bad[i], len(*out))) {
            snprintf(msg, sizeof msg, "Failed to execute '%s': '%.*s' HTTP method is unsupported.", iface, (int)len(*out), data(*out));
            js_throw_msg(vm, vm->type_error_proto, msg);
            return 0;
        }
    return 1;
}
/* a URL resolved against the page's into out: 0 when it is none */
static int sf_resolve(js_vm *vm, const vector<char> *ref, char *out, size_t cap) {
    char base[4200], r[8192];
    sh_page_url(vm, base, sizeof base);
    snprintf(r, sizeof r, "%.*s", (int)len(*ref), data(*ref));
    if (base[0] && net_url_resolve(base, r, out, cap)) return 1;
    return sf_scheme(r) && net_url_resolve(r, r, out, cap);
}
/* a body as bytes: a string's UTF-8, an ArrayBuffer's or a view's bytes; its default
   content type into ct ("" for none) */
static int sf_body(js_vm *vm, jv v, vector<char> *out, char *ct, size_t cap) {
    clear(*out);
    ct[0] = 0;
    if (jv_is_nullish(v)) return 1;
    if (jv_is_obj(v)) {
        uint32_t o = jv_idx(v), k = vm->objs[o].kind;
        if (k == JK_ARRAYBUFFER) {
            for (uint32_t i = 0; i < vm->objs[o].b; i++) push_back(*out, (char)vm->bytes[vm->objs[o].a + i]);
            return 1;
        }
        if (k == JK_TYPEDARRAY || k == JK_DATAVIEW) {
            uint32_t buf = vm->objs[o].a, at = vm->objs[o].c;
            uint32_t n = k == JK_DATAVIEW ? vm->objs[o].b : (uint32_t)jv_dbl(js_get(vm, v, js_atom_ascii(vm, "byteLength")));
            for (uint32_t i = 0; i < n; i++) push_back(*out, (char)vm->bytes[vm->objs[buf].a + at + i]);
            return 1;
        }
    }
    snprintf(ct, cap, "text/plain;charset=UTF-8");
    return sh_utf8(vm, v, out);
}
static int sf_has_header(const vector<char> *lines, const char *name) {
    size_t n = strlen(name), i = 0;
    while (i < len(*lines)) {
        if (len(*lines) - i > n && sf_ci_eq(data(*lines) + i, name, n) && (*lines)[i + n] == ':') return 1;
        while (i < len(*lines) && (*lines)[i] != '\n') i++;
        i++;
    }
    return 0;
}

/* fetch's and Request's input and init into the request q: its URL, method, header lines,
   body and mode. 0 with a TypeError thrown */
static int sf_init(js_vm *vm, sh_req *q, jv input, jv init, const char *iface) {
    struct js_host *h = vm->host;
    vector<char> method, url, lines, body;
    append_range(method, "GET", 3);
    char abs[8192], ct[64], msg[300];
    ct[0] = 0;
    abs[0] = 0;
    uint32_t mode = SM_CORS;
    if (sf_is(vm, input, HK_REQUEST)) {
        sh_req from = h->reqs[vm->objs[jv_idx(input)].a];
        clear(method);
        append_range(method, data(h->ntext) + from.moff, from.mlen);
        snprintf(abs, sizeof abs, "%.*s", (int)from.ulen, data(h->ntext) + from.uoff);
        append_range(lines, data(h->ntext) + from.hoff, from.hlen);
        append_range(body, data(h->ntext) + from.boff, from.blen);
        mode = from.mode;
    } else {
        if (!sh_utf8(vm, input, &url)) return 0;
        if (!sf_resolve(vm, &url, abs, sizeof abs)) {
            snprintf(msg, sizeof msg, "Failed to execute '%s': Failed to parse URL from %.*s", iface, (int)len(url), data(url));
            js_throw_msg(vm, vm->type_error_proto, msg);
            return 0;
        }
    }
    if (jv_is_obj(init)) {
        jv m = js_get(vm, init, js_atom_ascii(vm, "method"));
        if (vm->throwing) return 0;
        if (m != JV_UNDEF && !sf_method(vm, m, &method, iface)) return 0;
        jv hv = js_get(vm, init, js_atom_ascii(vm, "headers"));
        if (vm->throwing) return 0;
        if (hv != JV_UNDEF) {
            jv tmp = sf_headers_new(vm, 0);
            if (!sf_hdr_fill(vm, jv_idx(tmp), hv)) return 0;
            clear(lines);
            sf_hdr_lines(h, jv_idx(tmp), &lines);
        }
        jv bv = js_get(vm, init, js_atom_ascii(vm, "body"));
        if (vm->throwing) return 0;
        if (!jv_is_nullish(bv)) {
            if (!sf_body(vm, bv, &body, ct, sizeof ct)) return 0;
            if ((len(method) == 3 && memcmp(data(method), "GET", 3) == 0) || (len(method) == 4 && memcmp(data(method), "HEAD", 4) == 0)) {
                snprintf(msg, sizeof msg, "Failed to execute '%s': Request with GET/HEAD method cannot have body.", iface);
                js_throw_msg(vm, vm->type_error_proto, msg);
                return 0;
            }
            if (ct[0] && !sf_has_header(&lines, "content-type")) {
                append_range(lines, "Content-Type: ", 14);
                append_range(lines, ct, strlen(ct));
                append_range(lines, "\r\n", 2);
            }
        }
        jv mv = js_get(vm, init, js_atom_ascii(vm, "mode"));
        if (vm->throwing) return 0;
        if (mv != JV_UNDEF) {
            vector<char> ms;
            if (!sh_utf8(vm, mv, &ms)) return 0;
            push_back(ms, 0);
            if (strcmp(data(ms), "no-cors") == 0) mode = SM_NO_CORS;
            else if (strcmp(data(ms), "same-origin") == 0) mode = SM_SAME_ORIGIN;
            else if (strcmp(data(ms), "cors") == 0 || strcmp(data(ms), "navigate") == 0) mode = SM_CORS;
            else {
                snprintf(msg, sizeof msg, "Failed to execute '%s': The provided value '%s' is not a valid enum value of type RequestMode.", iface, data(ms));
                js_throw_msg(vm, vm->type_error_proto, msg);
                return 0;
            }
        }
    }
    q->moff = sf_put(h, data(method), len(method));
    q->mlen = (uint32_t)len(method);
    q->uoff = sf_put(h, abs, strlen(abs));
    q->ulen = (uint32_t)strlen(abs);
    q->hoff = sf_put(h, data(lines), len(lines));
    q->hlen = (uint32_t)len(lines);
    q->boff = sf_put(h, data(body), len(body));
    q->blen = (uint32_t)len(body);
    q->mode = mode;
    return 1;
}
static uint32_t sf_req_push(struct js_host *h, const sh_req *q) {
    if (!len(h->reqs)) { sh_req none; memset(&none, 0, sizeof none); push_back(h->reqs, none); }
    push_back(h->reqs, *q);
    return (uint32_t)len(h->reqs) - 1;
}

/* ---- the request made ---- */

/* the request id asked for now, by the loader, as the origin's rules let it: its outcome
   and the loader's record kept with it, and the runtime's record made */
static void sf_send(js_vm *vm, uint32_t id) {
    struct js_host *h = vm->host;
    sh_req q = h->reqs[id];
    char url[8192], method[32];
    snprintf(url, sizeof url, "%.*s", (int)q.ulen, data(h->ntext) + q.uoff);
    snprintf(method, sizeof method, "%.*s", (int)q.mlen, data(h->ntext) + q.moff);
    int scheme = sf_scheme(url), same = scheme == 2 || sf_same_origin(vm, url);
    script_req_outcome oc = SCRIPT_REQ_OUTCOME_OK;
    uint32_t res = 0, status = 0;
    if (scheme == 0 || scheme == 3 || (!same && q.mode != SM_NO_CORS)) {
        oc = scheme == 0 ? SCRIPT_REQ_OUTCOME_FAILED : SCRIPT_REQ_OUTCOME_BLOCKED;
        if (!same && q.mode == SM_SAME_ORIGIN) h->reqs[id].flags |= SRF_SAME_MODE;
    }
    else {
        vector<char> hdrs, body;
        append_range(hdrs, data(h->ntext) + q.hoff, q.hlen);
        append_range(body, data(h->ntext) + q.boff, q.blen);
        net_resource_h r = net_fetch_request(h->nt, url, strlen(url), NET_KIND_OTHER, method, data(body), len(body), data(hdrs), len(hdrs), h->page);
        res = r.index;
        net_outcome no = h->nt->resource[res].outcome;
        status = h->nt->resource[res].status;
        if (no == NET_OUTCOME_OK) oc = SCRIPT_REQ_OUTCOME_OK;
        else if (no == NET_OUTCOME_HTTP_ERROR) oc = SCRIPT_REQ_OUTCOME_HTTP_ERROR;
        else if (no == NET_OUTCOME_BLOCKED) oc = SCRIPT_REQ_OUTCOME_BLOCKED;
        else oc = SCRIPT_REQ_OUTCOME_FAILED;
        if (oc == SCRIPT_REQ_OUTCOME_FAILED || oc == SCRIPT_REQ_OUTCOME_BLOCKED) status = 0;
        /* a redirect to another origin is another origin's answer */
        if (same && scheme == 1 && (oc == SCRIPT_REQ_OUTCOME_OK || oc == SCRIPT_REQ_OUTCOME_HTTP_ERROR)) {
            char fu[8192];
            amb_span f = h->nt->resource[res].final_url;
            snprintf(fu, sizeof fu, "%.*s", (int)f.len, text_ptr(h->nt, f));
            if (!sf_same_origin(vm, fu)) {
                same = 0;
                if (q.mode != SM_NO_CORS) { oc = SCRIPT_REQ_OUTCOME_BLOCKED; status = 0; h->reqs[id].flags |= SRF_REDIRECTED; }
            }
        }
        if (!same && q.mode == SM_NO_CORS && oc != SCRIPT_REQ_OUTCOME_FAILED) { oc = SCRIPT_REQ_OUTCOME_OPAQUE; status = 0; }
    }
    h->reqs[id].resource = res;
    h->reqs[id].status = status;
    h->reqs[id].outcome = oc;
    h->reqs[id].flags |= SRF_SENT;
    h->reqs[id].rec = sf_rec(vm, q.kind == SR_XHR ? SCRIPT_API_XHR : SCRIPT_API_FETCH, method, url, res, status, oc, scheme == 1 && !same);
}

/* the console's messages for a request answered as it was: Chromium's words for a
   refusal by CORS, an unsupported scheme, a failure, an HTTP error. A request the CORS
   refused writes its second line after an XMLHttpRequest's events (late) */
static void sf_messages(js_vm *vm, uint32_t id, int late) {
    struct js_host *h = vm->host;
    sh_req q = h->reqs[id];
    char url[8192], origin[600], msg[9000], reason[64];
    snprintf(url, sizeof url, "%.*s", (int)q.ulen, data(h->ntext) + q.uoff);
    sf_page_origin(vm, origin, sizeof origin);
    int scheme = sf_scheme(url);
    if (q.outcome == SCRIPT_REQ_OUTCOME_BLOCKED && scheme == 1 && (q.flags & SRF_SAME_MODE)) {
        if (!late) {
            snprintf(msg, sizeof msg, "Fetch API cannot load %s. Request mode is \"same-origin\" but the URL's origin is not same as the request origin %s.", url, origin);
            sf_console(vm, msg);
        }
        return;
    }
    if (q.outcome == SCRIPT_REQ_OUTCOME_BLOCKED && scheme == 1) {
        if (!late) {
            char at[8400];
            snprintf(at, sizeof at, "'%s'", url);
            if ((q.flags & SRF_REDIRECTED) && q.resource) {
                amb_span f = h->nt->resource[q.resource].final_url;
                snprintf(at, sizeof at, "'%.*s' (redirected from '%s')", (int)f.len, text_ptr(h->nt, f), url);
            }
            snprintf(msg, sizeof msg, "Access to %s at %s from origin '%s' has been blocked by CORS policy: No 'Access-Control-Allow-Origin' header is present on the requested resource.",
                     q.kind == SR_XHR ? "XMLHttpRequest" : "fetch", at, origin);
            sf_console(vm, msg);
        }
        /* fetch's network line before its promise rejects; an XHR's comes later, as Chromium's
           inspector reports it after the request's events */
        if (late == (q.kind == SR_XHR)) sf_console(vm, "Failed to load resource: net::ERR_FAILED");
        return;
    }
    if (late) return;
    if (q.outcome == SCRIPT_REQ_OUTCOME_BLOCKED || q.outcome == SCRIPT_REQ_OUTCOME_FAILED) {
        if (scheme == 3 || scheme == 0) {
            int sn = 0;
            while (url[sn] && url[sn] != ':') sn++;
            snprintf(msg, sizeof msg, "Fetch API cannot load %s. URL scheme \"%.*s\" is not supported.", url, sn, url);
            sf_console(vm, msg);
        } else sf_console(vm, "Failed to load resource: net::ERR_FAILED");
        return;
    }
    if (q.outcome == SCRIPT_REQ_OUTCOME_HTTP_ERROR) {
        script_reason(q.status, reason, sizeof reason);
        snprintf(msg, sizeof msg, "Failed to load resource: the server responded with a status of %u (%s)", q.status, reason);
        sf_console(vm, msg);
    }
}

/* ---- fetch() ---- */

static jv sf_fetch(js_vm *vm, uint32_t args, uint32_t argc) {
    sh_req q;
    memset(&q, 0, sizeof q);
    q.kind = SR_FETCH;
    if (!sf_init(vm, &q, js_arg(vm, args, argc, 0), js_arg(vm, args, argc, 1), "fetch' on 'Window")) {
        /* fetch rejects where Request's constructor throws */
        jv e = vm->exception;
        vm->throwing = 0;
        vm->exception = JV_UNDEF;
        uint32_t p = js_promise_new(vm);
        js_promise_settle(vm, p, 1, e);
        return jv_obj(p);
    }
    uint32_t p = js_promise_new(vm);
    q.obj = p;
    uint32_t id = sf_req_push(vm->host, &q);
    sf_send(vm, id);
    sh_task(vm, 1, SH_TASK_FETCH, id);
    return jv_obj(p);
}

/* ---- XMLHttpRequest (XHR 3) ---- */

static jv sf_progress(js_vm *vm, const char *type, double loaded, double total) {
    jv ev = sh_event_new(vm, js_atom_ascii(vm, type), EV_TRUSTED);
    uint32_t e = jv_idx(ev);
    js_set_proto(vm, e, vm->host->protos[HP_PROGRESS]);
    js_define(vm, e, js_atom_ascii(vm, "loaded"), jv_num(loaded), 0);
    js_define(vm, e, js_atom_ascii(vm, "total"), jv_num(total), 0);
    js_define(vm, e, js_atom_ascii(vm, "lengthComputable"), jv_bool(total > 0), 0);
    return ev;
}
/* an XHR's event: a plain one, or a progress event of its bytes so far (loadstart's none) */
static void sf_xhr_fire(js_vm *vm, uint32_t id, const char *type, int progress) {
    struct js_host *h = vm->host;
    sh_req q = h->reqs[id];
    double n = q.resource && q.outcome != SCRIPT_REQ_OUTCOME_FAILED && q.outcome != SCRIPT_REQ_OUTCOME_BLOCKED ? h->nt->resource[q.resource].body.len : 0;
    if (strcmp(type, "loadstart") == 0) n = 0;
    jv ev = progress ? sf_progress(vm, type, n, n) : sh_event_new(vm, js_atom_ascii(vm, type), EV_TRUSTED);
    sh_dispatch(vm, SH_OBJECT_TARGET | q.obj, ev);
}
static void sf_xhr_state(js_vm *vm, uint32_t id, uint32_t state) {
    vm->host->reqs[id].state = state;
    sf_xhr_fire(vm, id, "readystatechange", 0);
}
/* the XHR's answer: its response's states and events in order, or an error's */
static void sf_xhr_done(js_vm *vm, uint32_t id) {
    struct js_host *h = vm->host;
    sh_req q = h->reqs[id];
    if (q.flags & (SRF_ABORTED | SRF_DONE)) return;
    h->reqs[id].flags |= SRF_DONE;
    sf_messages(vm, id, 0);
    if (q.outcome == SCRIPT_REQ_OUTCOME_FAILED || q.outcome == SCRIPT_REQ_OUTCOME_BLOCKED) {
        h->reqs[id].status = 0;
        sf_xhr_state(vm, id, 4);
        sf_xhr_fire(vm, id, "error", 1);
        sf_xhr_fire(vm, id, "loadend", 1);
        sh_task(vm, 1, SH_TASK_NOTE, id);
        return;
    }
    jv resp = sf_response_of(vm, id);
    h->reqs[id].resp = vm->objs[jv_idx(resp)].a;
    if (!q.sync) {
        sf_xhr_state(vm, id, 2);
        sf_xhr_state(vm, id, 3);
        sf_xhr_fire(vm, id, "progress", 1);
    }
    sf_xhr_state(vm, id, 4);
    sf_xhr_fire(vm, id, "load", 1);
    sf_xhr_fire(vm, id, "loadend", 1);
}
/* this XHR's record: its id, or 0 with a TypeError thrown */
static uint32_t sf_this_xhr(js_vm *vm, jv thisv) {
    if (!sf_is(vm, thisv, HK_XHR)) { js_throw_msg(vm, vm->type_error_proto, "Illegal invocation"); return 0; }
    return vm->objs[jv_idx(thisv)].a;
}
/* an XHR's response as its responseType makes it */
static jv sf_xhr_response(js_vm *vm, uint32_t id, int text_only) {
    struct js_host *h = vm->host;
    sh_req q = h->reqs[id];
    if (text_only && q.rtype != RT_TEXT) return sh_dom_error(vm, "InvalidStateError", "Failed to read the 'responseText' property from 'XMLHttpRequest': The value is only accessible if the object's 'responseType' is '' or 'text' (was 'json').");
    int have = q.resp && q.state >= 3 && q.outcome != SCRIPT_REQ_OUTCOME_FAILED && q.outcome != SCRIPT_REQ_OUTCOME_BLOCKED;
    if (q.rtype == RT_TEXT) {
        if (!have) return jv_str(A_EMPTY);
        sh_resp r = h->resps[q.resp];
        return sf_body_text(vm, r.boff, r.blen);
    }
    if (!have || q.state != 4) return JV_NULL;
    sh_resp r = h->resps[q.resp];
    if (q.rtype == RT_JSON) {
        jv v = sf_body_json(vm, r.boff, r.blen);
        if (vm->throwing == 1) { vm->throwing = 0; vm->exception = JV_UNDEF; return JV_NULL; }
        return v;
    }
    if (q.rtype == RT_ARRAYBUFFER) return sf_body_buffer(vm, r.boff, r.blen);
    return JV_NULL;
}

/* ---- the unit's natives ---- */

jv script_fetch_call(js_vm *vm, uint32_t id, jv callee, jv thisv, uint32_t args, uint32_t argc, jv newt, int *handled) {
    (void)callee;
    (void)newt;
    struct js_host *h = vm->host;
    *handled = 1;
    jv a0 = js_arg(vm, args, argc, 0), a1 = js_arg(vm, args, argc, 1);
    vector<char> n, v;
    switch (id) {
    case SF_FETCH: return sf_fetch(vm, args, argc);
    case SF_RESPONSE: {
        /* new Response(body, init): a response of the script's own, its type default */
        sh_resp r;
        memset(&r, 0, sizeof r);
        r.type = ST_DEFAULT;
        r.status = 200;
        char ct[64];
        if (!sf_body(vm, a0, &v, ct, sizeof ct)) return JV_HOLE;
        r.boff = sf_put(h, data(v), len(v));
        r.blen = (uint32_t)len(v);
        r.uoff = r.soff = r.toff = (uint32_t)len(h->ntext);
        jv hinit = JV_UNDEF;
        if (jv_is_obj(a1)) {
            jv st = js_get(vm, a1, js_atom_ascii(vm, "status"));
            if (vm->throwing) return JV_HOLE;
            if (st != JV_UNDEF) {
                double s = js_to_number(vm, st);
                if (!(s >= 200 && s <= 599)) return js_throw_msg(vm, vm->range_error_proto, "Failed to construct 'Response': The status provided is outside the range [200, 599].");
                r.status = (uint32_t)s;
            }
            jv tx = js_get(vm, a1, js_atom_ascii(vm, "statusText"));
            if (tx != JV_UNDEF) {
                if (!sh_utf8(vm, tx, &n)) return JV_HOLE;
                r.soff = sf_put(h, data(n), len(n));
                r.slen = (uint32_t)len(n);
            }
            hinit = js_get(vm, a1, js_atom_ascii(vm, "headers"));
            if (vm->throwing) return JV_HOLE;
        }
        jv hv = sf_headers_new(vm, 0);
        if (!sf_hdr_fill(vm, jv_idx(hv), hinit)) return JV_HOLE;
        if (ct[0] && !sf_hdr_get(h, jv_idx(hv), "content-type", 12, &n)) sf_hdr_add(h, jv_idx(hv), "content-type", 12, ct, strlen(ct));
        r.headers = jv_idx(hv);
        uint32_t ri = sf_resp_push(h, &r);
        return sf_obj(vm, HK_RESPONSE, HP_RESPONSE, ri);
    }
    case SF_RESPONSE_ERROR: {
        sh_resp r;
        memset(&r, 0, sizeof r);
        r.type = ST_ERROR;
        r.uoff = r.soff = r.toff = r.boff = (uint32_t)len(h->ntext);
        uint32_t ri = sf_resp_push(h, &r);
        return sf_obj(vm, HK_RESPONSE, HP_RESPONSE, ri);
    }
    case SF_RS_TEXT: case SF_RS_JSON: case SF_RS_BUFFER: case SF_RS_CLONE: {
        static const char *const what[] = { "text", "json", "arrayBuffer", "clone" };
        int is_req = sf_is(vm, thisv, HK_REQUEST);
        if (!sf_is(vm, thisv, HK_RESPONSE) && !is_req) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        uint32_t ri = vm->objs[jv_idx(thisv)].a;
        if (is_req) {
            if (id == SF_RS_CLONE) return JV_UNDEF;
            sh_req q = h->reqs[ri];
            uint32_t used = q.flags & SRF_READ ? 1 : 0;
            jv p = sf_read_body(vm, id, what[id - SF_RS_TEXT], q.boff, q.blen, &used, "Request");
            if (used) h->reqs[ri].flags |= SRF_READ;
            return p;
        }
        sh_resp r = h->resps[ri];
        if (id == SF_RS_CLONE) {
            if (r.used) return js_throw_msg(vm, vm->type_error_proto, "Failed to execute 'clone' on 'Response': Response body is already used");
            r.headers = 0;
            uint32_t c = sf_resp_push(h, &r);
            return sf_obj(vm, HK_RESPONSE, HP_RESPONSE, c);
        }
        uint32_t used = r.used;
        jv p = sf_read_body(vm, id, what[id - SF_RS_TEXT], r.boff, r.blen, &used, "Response");
        h->resps[ri].used = used;
        return p;
    }
    case SF_HEADERS: {
        jv hv = sf_headers_new(vm, 0);
        if (!sf_hdr_fill(vm, jv_idx(hv), a0)) return JV_HOLE;
        return hv;
    }
    case SF_H_GET: case SF_H_SET: case SF_H_APPEND: case SF_H_HAS: case SF_H_DELETE: {
        if (!sf_is(vm, thisv, HK_HEADERS)) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        uint32_t owner = vm->objs[jv_idx(thisv)].a, immutable = vm->objs[jv_idx(thisv)].b;
        if (!sh_utf8(vm, a0, &n)) return JV_HOLE;
        static const char *const what[] = { "get", "set", "append", "has", "delete" };
        char msg[200];
        if (!sf_token(data(n), len(n))) {
            snprintf(msg, sizeof msg, "Failed to execute '%s' on 'Headers': Invalid name", what[id - SF_H_GET]);
            return js_throw_msg(vm, vm->type_error_proto, msg);
        }
        if (id == SF_H_GET) return sf_hdr_get(h, owner, data(n), len(n), &v) ? sh_str(vm, data(v), len(v)) : JV_NULL;
        if (id == SF_H_HAS) return jv_bool(sf_hdr_get(h, owner, data(n), len(n), &v));
        if (immutable) {
            snprintf(msg, sizeof msg, "Failed to execute '%s' on 'Headers': Headers are immutable", what[id - SF_H_GET]);
            return js_throw_msg(vm, vm->type_error_proto, msg);
        }
        if (id == SF_H_DELETE) { sf_hdr_del(h, owner, data(n), len(n)); return JV_UNDEF; }
        if (!sh_utf8(vm, a1, &v)) return JV_HOLE;
        if (id == SF_H_SET) sf_hdr_del(h, owner, data(n), len(n));
        sf_hdr_add(h, owner, data(n), len(n), data(v), len(v));
        return JV_UNDEF;
    }
    case SF_H_FOREACH: case SF_H_ENTRIES: case SF_H_KEYS: case SF_H_VALUES: {
        if (!sf_is(vm, thisv, HK_HEADERS)) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        jv pairs = sf_hdr_sorted(vm, vm->objs[jv_idx(thisv)].a);
        uint32_t pa = jv_idx(pairs);
        if (id == SF_H_FOREACH) {
            if (!js_is_callable(vm, a0)) return js_throw_msg(vm, vm->type_error_proto, "Failed to execute 'forEach' on 'Headers': The callback provided as parameter 1 is not a function.");
            for (uint32_t i = 0; i < vm->objs[pa].b; i++) {
                jv pair = js_array_get(vm, pa, i);
                jv cargs[3] = { js_array_get(vm, jv_idx(pair), 1), js_array_get(vm, jv_idx(pair), 0), thisv };
                js_call(vm, a0, a1, cargs, 3);
                if (vm->throwing) return JV_HOLE;
            }
            return JV_UNDEF;
        }
        uint32_t out = js_array_new(vm, 0);
        for (uint32_t i = 0; i < vm->objs[pa].b; i++) {
            jv pair = js_array_get(vm, pa, i);
            if (id == SF_H_ENTRIES) js_array_push(vm, out, pair);
            else js_array_push(vm, out, js_array_get(vm, jv_idx(pair), id == SF_H_KEYS ? 0 : 1));
        }
        return js_iter_of(vm, jv_obj(out));
    }
    case SF_REQUEST: {
        sh_req q;
        memset(&q, 0, sizeof q);
        q.kind = SR_OBJECT;
        if (!sf_init(vm, &q, a0, a1, "Request': Failed to construct 'Request")) return JV_HOLE;
        uint32_t ri = sf_req_push(h, &q);
        return sf_obj(vm, HK_REQUEST, HP_REQUEST, ri);
    }
    case SF_RQ_CLONE: {
        if (!sf_is(vm, thisv, HK_REQUEST)) return js_throw_msg(vm, vm->type_error_proto, "Illegal invocation");
        sh_req q = h->reqs[vm->objs[jv_idx(thisv)].a];
        uint32_t ri = sf_req_push(h, &q);
        return sf_obj(vm, HK_REQUEST, HP_REQUEST, ri);
    }
    case SF_XHR: {
        sh_req q;
        memset(&q, 0, sizeof q);
        q.kind = SR_XHR;
        uint32_t ri = sf_req_push(h, &q);
        jv x = sf_obj(vm, HK_XHR, HP_XHR, ri);
        h->reqs[ri].obj = jv_idx(x);
        return x;
    }
    case SF_X_OPEN: {
        uint32_t xi = sf_this_xhr(vm, thisv);
        if (!xi) return JV_HOLE;
        if (argc < 2) return js_throw_msg(vm, vm->type_error_proto, "Failed to execute 'open' on 'XMLHttpRequest': 2 arguments required, but only 1 present.");
        vector<char> method, url;
        if (!sf_method(vm, a0, &method, "open' on 'XMLHttpRequest")) return JV_HOLE;
        if (!sh_utf8(vm, a1, &url)) return JV_HOLE;
        char abs[8192];
        if (!sf_resolve(vm, &url, abs, sizeof abs)) return sh_dom_error(vm, "SyntaxError", "Failed to execute 'open' on 'XMLHttpRequest': Invalid URL");
        jv av = js_arg(vm, args, argc, 2);
        sh_req q = h->reqs[xi];
        q.sync = argc >= 3 && !js_truthy(vm, av);
        q.moff = sf_put(h, data(method), len(method));
        q.mlen = (uint32_t)len(method);
        q.uoff = sf_put(h, abs, strlen(abs));
        q.ulen = (uint32_t)strlen(abs);
        q.hoff = (uint32_t)len(h->ntext);
        q.hlen = 0;
        q.flags = 0;
        q.status = 0;
        q.resp = 0;
        q.resource = 0;
        q.outcome = SCRIPT_REQ_OUTCOME_PENDING;
        q.mode = SM_CORS;
        h->reqs[xi] = q;
        sf_xhr_state(vm, xi, 1);
        return JV_UNDEF;
    }
    case SF_X_SETHDR: {
        uint32_t xi = sf_this_xhr(vm, thisv);
        if (!xi) return JV_HOLE;
        sh_req q = h->reqs[xi];
        if (q.state != 1 || (q.flags & SRF_SENT))
            return sh_dom_error(vm, "InvalidStateError", "Failed to execute 'setRequestHeader' on 'XMLHttpRequest': The object's state must be OPENED.");
        if (!sh_utf8(vm, a0, &n) || !sh_utf8(vm, a1, &v)) return JV_HOLE;
        if (!sf_token(data(n), len(n))) return sh_dom_error(vm, "SyntaxError", "Failed to execute 'setRequestHeader' on 'XMLHttpRequest': Invalid header name");
        /* the request's lines so far, and this one after them */
        vector<char> lines;
        append_range(lines, data(h->ntext) + q.hoff, q.hlen);
        append_range(lines, data(n), len(n));
        append_range(lines, ": ", 2);
        append_range(lines, data(v), len(v));
        append_range(lines, "\r\n", 2);
        h->reqs[xi].hoff = sf_put(h, data(lines), len(lines));
        h->reqs[xi].hlen = (uint32_t)len(lines);
        return JV_UNDEF;
    }
    case SF_X_SEND: {
        uint32_t xi = sf_this_xhr(vm, thisv);
        if (!xi) return JV_HOLE;
        sh_req q = h->reqs[xi];
        if (q.state != 1 || (q.flags & SRF_SENT))
            return sh_dom_error(vm, "InvalidStateError", "Failed to execute 'send' on 'XMLHttpRequest': The object's state must be OPENED.");
        char ct[64];
        int body_ok = !(q.mlen == 3 && memcmp(data(h->ntext) + q.moff, "GET", 3) == 0) && !(q.mlen == 4 && memcmp(data(h->ntext) + q.moff, "HEAD", 4) == 0);
        if (!sf_body(vm, body_ok ? a0 : JV_NULL, &v, ct, sizeof ct)) return JV_HOLE;
        vector<char> lines;
        append_range(lines, data(h->ntext) + q.hoff, q.hlen);
        if (ct[0] && !sf_has_header(&lines, "content-type")) { append_range(lines, "Content-Type: ", 14); append_range(lines, ct, strlen(ct)); append_range(lines, "\r\n", 2); }
        h->reqs[xi].hoff = sf_put(h, data(lines), len(lines));
        h->reqs[xi].hlen = (uint32_t)len(lines);
        h->reqs[xi].boff = sf_put(h, data(v), len(v));
        h->reqs[xi].blen = (uint32_t)len(v);
        sf_send(vm, xi);
        if (q.sync) {
            sf_xhr_done(vm, xi);
            return JV_UNDEF;
        }
        sf_xhr_fire(vm, xi, "loadstart", 1);
        sh_task(vm, 1, SH_TASK_XHR, xi);
        return JV_UNDEF;
    }
    case SF_X_ABORT: {
        uint32_t xi = sf_this_xhr(vm, thisv);
        if (!xi) return JV_HOLE;
        sh_req q = h->reqs[xi];
        if ((q.flags & SRF_SENT) && !(q.flags & (SRF_DONE | SRF_ABORTED))) {
            h->reqs[xi].flags |= SRF_ABORTED;
            if (q.rec && q.rec < h->rt->request_len) h->rt->request[q.rec].outcome = SCRIPT_REQ_OUTCOME_ABORTED;
            h->reqs[xi].status = 0;
            sf_xhr_state(vm, xi, 4);
            sf_xhr_fire(vm, xi, "abort", 1);
            sf_xhr_fire(vm, xi, "loadend", 1);
        }
        h->reqs[xi].state = 0;
        return JV_UNDEF;
    }
    case SF_X_GETHDR: case SF_X_ALLHDRS: {
        uint32_t xi = sf_this_xhr(vm, thisv);
        if (!xi) return JV_HOLE;
        sh_req q = h->reqs[xi];
        if (!q.resp || q.state < 2) return id == SF_X_GETHDR ? JV_NULL : jv_str(A_EMPTY);
        jv hv = sf_resp_headers(vm, q.resp);
        uint32_t owner = jv_idx(hv);
        if (id == SF_X_GETHDR) {
            if (!sh_utf8(vm, a0, &n)) return JV_HOLE;
            return sf_hdr_get(h, owner, data(n), len(n), &v) ? sh_str(vm, data(v), len(v)) : JV_NULL;
        }
        jv pairs = sf_hdr_sorted(vm, owner);
        vector<char> all;
        for (uint32_t i = 0; i < vm->objs[jv_idx(pairs)].b; i++) {
            jv pair = js_array_get(vm, jv_idx(pairs), i);
            if (!sh_utf8(vm, js_array_get(vm, jv_idx(pair), 0), &n) || !sh_utf8(vm, js_array_get(vm, jv_idx(pair), 1), &v)) return JV_HOLE;
            append_range(all, data(n), len(n));
            append_range(all, ": ", 2);
            append_range(all, data(v), len(v));
            append_range(all, "\r\n", 2);
        }
        return sh_str(vm, data(all), len(all));
    }
    case SF_X_MIME: return JV_UNDEF;
    case SF_PROGRESS: {
        /* new ProgressEvent(type, { loaded, total, lengthComputable }) */
        if (!sh_utf8(vm, a0, &n)) return JV_HOLE;
        push_back(n, 0);
        double loaded = 0, total = 0;
        if (jv_is_obj(a1)) {
            loaded = js_to_number(vm, js_get(vm, a1, js_atom_ascii(vm, "loaded")));
            total = js_to_number(vm, js_get(vm, a1, js_atom_ascii(vm, "total")));
            if (loaded != loaded) loaded = 0;
            if (total != total) total = 0;
        }
        jv ev = sf_progress(vm, data(n), loaded, total);
        vm->objs[jv_idx(ev)].a &= ~EV_TRUSTED;
        return ev;
    }
    case SF_X_ADD: case SF_X_REMOVE: case SF_X_DISPATCH: {
        uint32_t xi = sf_this_xhr(vm, thisv);
        if (!xi) return JV_HOLE;
        uint32_t target = SH_OBJECT_TARGET | jv_idx(thisv);
        if (id == SF_X_ADD) return sh_add_listener(vm, target, args, argc);
        if (id == SF_X_REMOVE) return sh_remove_listener(vm, target, args, argc);
        if (!jv_is_obj(a0) || vm->objs[jv_idx(a0)].c != HK_EVENT) return js_throw_msg(vm, vm->type_error_proto, "dispatchEvent needs an Event");
        return jv_bool(sh_dispatch(vm, target, a0));
    }
    default: break;
    }
    *handled = 0;
    return JV_UNDEF;
}

/* ---- the unit's objects' properties ---- */

jv script_fetch_get(js_vm *vm, uint32_t o, uint32_t atom, int *handled) {
    struct js_host *h = vm->host;
    uint32_t kind = vm->objs[o].c, rec = vm->objs[o].a;
    *handled = 0;
    if (js_key_is_sym(atom)) return JV_UNDEF;
    vector<char> name;
    js_str_to_utf8(vm, atom, &name);
    push_back(name, 0);
    const char *nm = data(name);
    if (kind == HK_RESPONSE) {
        sh_resp r = h->resps[rec];
        *handled = 1;
        if (strcmp(nm, "status") == 0) return jv_num(r.status);
        if (strcmp(nm, "ok") == 0) return jv_bool(r.status >= 200 && r.status <= 299);
        if (strcmp(nm, "statusText") == 0) return sf_text(vm, r.soff, r.slen);
        if (strcmp(nm, "url") == 0) return sf_text(vm, r.uoff, r.ulen);
        if (strcmp(nm, "redirected") == 0) return jv_bool(r.redirected);
        if (strcmp(nm, "bodyUsed") == 0) return jv_bool(r.used);
        if (strcmp(nm, "headers") == 0) return sf_resp_headers(vm, rec);
        if (strcmp(nm, "type") == 0) {
            static const char *const types[] = { "basic", "cors", "opaque", "default", "error" };
            return sh_cstr(vm, types[r.type]);
        }
        *handled = 0;
        return JV_UNDEF;
    }
    if (kind == HK_REQUEST) {
        sh_req q = h->reqs[rec];
        *handled = 1;
        if (strcmp(nm, "method") == 0) return sf_text(vm, q.moff, q.mlen);
        if (strcmp(nm, "url") == 0) return sf_text(vm, q.uoff, q.ulen);
        if (strcmp(nm, "mode") == 0) return sh_cstr(vm, q.mode == SM_NO_CORS ? "no-cors" : q.mode == SM_SAME_ORIGIN ? "same-origin" : "cors");
        if (strcmp(nm, "credentials") == 0) return sh_cstr(vm, "same-origin");
        if (strcmp(nm, "cache") == 0) return sh_cstr(vm, "default");
        if (strcmp(nm, "redirect") == 0) return sh_cstr(vm, "follow");
        if (strcmp(nm, "bodyUsed") == 0) return jv_bool(q.flags & SRF_READ);
        if (strcmp(nm, "headers") == 0) {
            /* its header lines as a Headers object of their own */
            jv hv = sf_headers_new(vm, 0);
            vector<char> lines;
            append_range(lines, data(h->ntext) + q.hoff, q.hlen);
            size_t i = 0;
            while (i < len(lines)) {
                size_t b = i, c = i;
                while (i < len(lines) && lines[i] != '\r') i++;
                while (c < i && lines[c] != ':') c++;
                if (c < i) sf_hdr_add(h, jv_idx(hv), data(lines) + b, c - b, data(lines) + c + 1, i - c - 1);
                i += 2;
            }
            return hv;
        }
        *handled = 0;
        return JV_UNDEF;
    }
    if (kind == HK_XHR) {
        sh_req q = h->reqs[rec];
        *handled = 1;
        if (strcmp(nm, "readyState") == 0) return jv_num(q.state);
        if (strcmp(nm, "status") == 0) return jv_num(q.state >= 2 && q.resp ? q.status : 0);
        if (strcmp(nm, "statusText") == 0) {
            if (q.state < 2 || !q.resp) return jv_str(A_EMPTY);
            sh_resp r = h->resps[q.resp];
            return sf_text(vm, r.soff, r.slen);
        }
        if (strcmp(nm, "responseText") == 0) return sf_xhr_response(vm, rec, 1);
        if (strcmp(nm, "response") == 0) return sf_xhr_response(vm, rec, 0);
        if (strcmp(nm, "responseURL") == 0) {
            if (!q.resp) return jv_str(A_EMPTY);
            sh_resp r = h->resps[q.resp];
            return sf_text(vm, r.uoff, r.ulen);
        }
        if (strcmp(nm, "responseType") == 0) {
            static const char *const rts[] = { "", "json", "arraybuffer", "blob", "document" };
            return sh_cstr(vm, rts[q.rtype]);
        }
        if (strcmp(nm, "responseXML") == 0) return JV_NULL;
        if (strcmp(nm, "withCredentials") == 0) return JV_FALSE;
        *handled = 0;
        return JV_UNDEF;
    }
    return JV_UNDEF;
}

int script_fetch_set(js_vm *vm, uint32_t o, uint32_t atom, jv v, int *handled) {
    struct js_host *h = vm->host;
    *handled = 0;
    if (vm->objs[o].c != HK_XHR || js_key_is_sym(atom)) return 1;
    vector<char> name, val;
    js_str_to_utf8(vm, atom, &name);
    push_back(name, 0);
    if (strcmp(data(name), "responseType") == 0) {
        *handled = 1;
        if (!sh_utf8(vm, v, &val)) return 0;
        push_back(val, 0);
        static const char *const rts[] = { "text", "json", "arraybuffer", "blob", "document" };
        uint32_t rec = vm->objs[o].a;
        if (strcmp(data(val), "") == 0) { h->reqs[rec].rtype = RT_TEXT; return 1; }
        for (uint32_t i = 0; i < 5; i++) if (strcmp(data(val), rts[i]) == 0) h->reqs[rec].rtype = i;
        return 1;
    }
    return 1;
}

/* ---- the answers, as host tasks ---- */

void script_fetch_task(js_vm *vm, uint32_t kind, uint32_t id) {
    struct js_host *h = vm->host;
    if (!id || id >= len(h->reqs)) return;
    if (kind == SH_TASK_XHR) { sf_xhr_done(vm, id); return; }
    if (kind == SH_TASK_BODY) { sf_body_task(vm, id); return; }
    if (kind == SH_TASK_NOTE) { sf_messages(vm, id, 1); return; }
    sh_req q = h->reqs[id];
    sf_messages(vm, id, 0);
    if (q.outcome == SCRIPT_REQ_OUTCOME_FAILED || q.outcome == SCRIPT_REQ_OUTCOME_BLOCKED) {
        uint32_t e = js_error_new(vm, vm->type_error_proto, js_str_ascii(vm, "Failed to fetch"));
        js_promise_settle(vm, q.obj, 1, jv_obj(e));
        sf_messages(vm, id, 1);
        return;
    }
    jv resp = sf_response_of(vm, id);
    js_promise_resolve_with(vm, q.obj, resp);
}

/* ---- the globals ---- */

void script_fetch_setup(js_vm *vm) {
    struct js_host *h = vm->host;
    uint32_t g = vm->global;
    h->protos[HP_RESPONSE] = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    h->protos[HP_REQUEST] = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    h->protos[HP_HEADERS] = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    h->protos[HP_XHR] = js_obj_new(vm, JK_PLAIN, vm->object_proto);
    h->protos[HP_PROGRESS] = js_obj_new(vm, JK_PLAIN, h->protos[HP_EVENT]);
    sh_method(vm, g, "fetch", SF_FETCH, 1);
    uint32_t rp = h->protos[HP_RESPONSE], qp = h->protos[HP_REQUEST], hp = h->protos[HP_HEADERS], xp = h->protos[HP_XHR];
    sh_method(vm, rp, "text", SF_RS_TEXT, 0);
    sh_method(vm, rp, "json", SF_RS_JSON, 0);
    sh_method(vm, rp, "arrayBuffer", SF_RS_BUFFER, 0);
    sh_method(vm, rp, "clone", SF_RS_CLONE, 0);
    sh_method(vm, qp, "text", SF_RS_TEXT, 0);
    sh_method(vm, qp, "json", SF_RS_JSON, 0);
    sh_method(vm, qp, "arrayBuffer", SF_RS_BUFFER, 0);
    sh_method(vm, qp, "clone", SF_RQ_CLONE, 0);
    static const char *const hm[] = { "get", "set", "append", "has", "delete", "forEach", "entries", "keys", "values" };
    for (uint32_t i = 0; i < 9; i++) sh_method(vm, hp, hm[i], SF_H_GET + i, i == 1 || i == 2 ? 2 : i >= 6 ? 0 : 1);
    js_define(vm, hp, JS_KEY_ITERATOR, js_get(vm, jv_obj(hp), js_atom_ascii(vm, "entries")), JS_SHAPE_HIDDEN);
    static const char *const xm[] = { "open", "send", "abort", "setRequestHeader", "getResponseHeader", "getAllResponseHeaders",
                                      "overrideMimeType", "addEventListener", "removeEventListener", "dispatchEvent" };
    static const uint32_t xn[] = { 2, 0, 0, 2, 1, 0, 1, 2, 2, 1 };
    for (uint32_t i = 0; i < 10; i++) sh_method(vm, xp, xm[i], SF_X_OPEN + i, xn[i]);
    uint32_t rc = sh_interface(vm, "Response", SF_RESPONSE, rp);
    sh_method(vm, rc, "error", SF_RESPONSE_ERROR, 0);
    sh_interface(vm, "Request", SF_REQUEST, qp);
    sh_interface(vm, "Headers", SF_HEADERS, hp);
    uint32_t xc = sh_interface(vm, "XMLHttpRequest", SF_XHR, xp);
    sh_interface(vm, "ProgressEvent", SF_PROGRESS, h->protos[HP_PROGRESS]);
    static const char *const states[] = { "UNSENT", "OPENED", "HEADERS_RECEIVED", "LOADING", "DONE" };
    for (uint32_t i = 0; i < 5; i++) {
        uint32_t a = js_atom_ascii(vm, states[i]);
        js_define(vm, xc, a, jv_num(i), JS_SHAPE_HIDDEN);
        js_define(vm, xp, a, jv_num(i), JS_SHAPE_HIDDEN);
    }
}
