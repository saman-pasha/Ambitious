/* tests/exec/net_json.h -- what the URL tests read of JSON (included by
   tests/exec/net_*.c): web-platform-tests' data files, each an array of comments
   (strings) and cases (objects whose members are strings, null, true or false).
   Strings are decoded into a pool, \u escapes and their surrogate pairs as UTF-8;
   a case's members are kept by name; a printed string's bytes that are no printable
   ASCII are written \xHH or \u{HEX}.

   The file and the pool are fixed arrays, not vectors: a type macro's function
   called in an included header is not made (toolchain/REPORT.md, finding 22). Their
   sizes are enum constants: a macro as an array's size in an included header is not
   expanded (finding 23). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

enum { JSON_FILE_MAX = 1 << 20, JSON_POOL_MAX = 1 << 16, JSON_MEMBERS = 24 };
static char json_file[JSON_FILE_MAX];
static size_t json_file_len;
static char json_pool[JSON_POOL_MAX];
static size_t json_pool_len;

/* a case: each member's name and value as spans of the pool, and the value's kind:
   's' a string, 'n' null, 't' true, 'f' false, '?' another */
typedef struct json_obj {
    int n;
    size_t key[JSON_MEMBERS], key_len[JSON_MEMBERS], val[JSON_MEMBERS], val_len[JSON_MEMBERS];
    char kind[JSON_MEMBERS];
} json_obj;

/* the file read whole into json_file: 0 when it cannot be, or is past 1 MB */
static int json_read_file(const char *path) {
    own FILE *f = fopen(path, "rb");
    if (!f) return 0;
    defer(f) { fclose(f); }
    json_file_len = fread(json_file, 1, JSON_FILE_MAX, f);
    return json_file_len < JSON_FILE_MAX;
}

static int json_put(char c) {
    if (json_pool_len >= JSON_POOL_MAX) return 0;
    json_pool[json_pool_len++] = c;
    return 1;
}

static void json_ws(size_t *p) {
    while (*p < json_file_len && (json_file[*p] == ' ' || json_file[*p] == '\n' || json_file[*p] == '\r' || json_file[*p] == '\t')) (*p)++;
}

static int json_put_utf8(uint32_t c) {
    if (c < 0x80) return json_put((char)c);
    if (c < 0x800) return json_put((char)(0xC0 | c >> 6)) && json_put((char)(0x80 | (c & 0x3F)));
    if (c < 0x10000) return json_put((char)(0xE0 | c >> 12)) && json_put((char)(0x80 | (c >> 6 & 0x3F))) && json_put((char)(0x80 | (c & 0x3F)));
    return json_put((char)(0xF0 | c >> 18)) && json_put((char)(0x80 | (c >> 12 & 0x3F))) && json_put((char)(0x80 | (c >> 6 & 0x3F))) &&
           json_put((char)(0x80 | (c & 0x3F)));
}

static int json_hexdigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int json_hex4(size_t p, uint32_t *v) {
    uint32_t x = 0;
    if (p + 4 > json_file_len) return 0;
    for (size_t i = p; i < p + 4; i++) {
        int d = json_hexdigit(json_file[i]);
        if (d < 0) return 0;
        x = x * 16 + (uint32_t)d;
    }
    *v = x;
    return 1;
}

/* the string whose opening quote is at *p, decoded onto the pool */
static int json_string(size_t *p) {
    if (*p >= json_file_len || json_file[*p] != '"') return 0;
    (*p)++;
    while (*p < json_file_len && json_file[*p] != '"') {
        char c = json_file[*p];
        (*p)++;
        if (c != '\\') {
            if (!json_put(c)) return 0;
            continue;
        }
        if (*p >= json_file_len) return 0;
        char e = json_file[*p];
        (*p)++;
        int ok = 1;
        if (e == 'n') ok = json_put('\n');
        else if (e == 't') ok = json_put('\t');
        else if (e == 'r') ok = json_put('\r');
        else if (e == 'b') ok = json_put('\b');
        else if (e == 'f') ok = json_put('\f');
        else if (e == 'u') {
            uint32_t u = 0, lo = 0;
            if (!json_hex4(*p, &u)) return 0;
            *p += 4;
            if (u >= 0xD800 && u <= 0xDBFF && *p + 6 <= json_file_len && json_file[*p] == '\\' && json_file[*p + 1] == 'u' &&
                json_hex4(*p + 2, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                *p += 6;
            }
            ok = json_put_utf8(u);
        } else ok = json_put(e);
        if (!ok) return 0;
    }
    if (*p >= json_file_len) return 0;
    (*p)++;
    return 1;
}

/* the top-level array's next element: 1 a case (its members in *o, their strings in
   the pool, emptied first), 2 a comment, 0 the end or what is not read here */
static int json_next(size_t *p, json_obj *o) {
    o->n = 0;
    json_pool_len = 0;
    json_ws(p);
    if (*p < json_file_len && (json_file[*p] == '[' || json_file[*p] == ',')) (*p)++;
    json_ws(p);
    if (*p >= json_file_len || json_file[*p] == ']') return 0;
    if (json_file[*p] == '"') return json_string(p) ? 2 : 0;
    if (json_file[*p] != '{') return 0;
    (*p)++;
    for (;;) {
        json_ws(p);
        if (*p < json_file_len && json_file[*p] == ',') {
            (*p)++;
            json_ws(p);
        }
        if (*p >= json_file_len) return 0;
        if (json_file[*p] == '}') {
            (*p)++;
            return 1;
        }
        if (o->n == JSON_MEMBERS) return 0;
        int k = o->n;
        o->key[k] = json_pool_len;
        if (!json_string(p)) return 0;
        o->key_len[k] = json_pool_len - o->key[k];
        json_ws(p);
        if (*p >= json_file_len || json_file[*p] != ':') return 0;
        (*p)++;
        json_ws(p);
        o->val[k] = json_pool_len;
        o->val_len[k] = 0;
        if (*p < json_file_len && json_file[*p] == '"') {
            if (!json_string(p)) return 0;
            o->val_len[k] = json_pool_len - o->val[k];
            o->kind[k] = 's';
        } else if (*p + 4 <= json_file_len && memcmp(json_file + *p, "null", 4) == 0) {
            o->kind[k] = 'n';
            *p += 4;
        } else if (*p + 4 <= json_file_len && memcmp(json_file + *p, "true", 4) == 0) {
            o->kind[k] = 't';
            *p += 4;
        } else if (*p + 5 <= json_file_len && memcmp(json_file + *p, "false", 5) == 0) {
            o->kind[k] = 'f';
            *p += 5;
        } else {
            o->kind[k] = '?';
            while (*p < json_file_len && json_file[*p] != ',' && json_file[*p] != '}') (*p)++;
        }
        o->n++;
    }
}

/* the member of that name: its kind (0 when there is none), its value's span of the pool */
static char json_get(const json_obj *o, const char *name, size_t *at, size_t *n) {
    size_t nl = strlen(name);
    for (int k = 0; k < o->n; k++)
        if (o->key_len[k] == nl && memcmp(json_pool + o->key[k], name, nl) == 0) {
            *at = o->val[k];
            *n = o->val_len[k];
            return o->kind[k];
        }
    *at = 0;
    *n = 0;
    return 0;
}

/* s[0, n) printed with what is no printable ASCII escaped */
static void json_print(const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        uint8_t b = (uint8_t)s[i];
        if (b >= 0x20 && b < 0x7F) {
            putchar(b);
            i++;
            continue;
        }
        int more = 0;
        uint32_t c = b;
        if (b >= 0xF0) { more = 3; c = b & 7u; }
        else if (b >= 0xE0) { more = 2; c = b & 15u; }
        else if (b >= 0xC0) { more = 1; c = b & 31u; }
        int ok = more > 0;
        for (int j = 1; ok && j <= more; j++) {
            if (i + (size_t)j >= n || ((uint8_t)s[i + (size_t)j] & 0xC0) != 0x80) ok = 0;
            else c = c << 6 | ((uint8_t)s[i + (size_t)j] & 0x3Fu);
        }
        if (!ok) {
            printf("\\x%02X", b);
            i++;
            continue;
        }
        printf("\\u{%X}", c);
        i += 1 + (size_t)more;
    }
}
