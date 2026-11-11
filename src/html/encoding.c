/* src/html/encoding.c -- the WHATWG Encoding standard's decoders
   (https://encoding.spec.whatwg.org/) and HTML's encoding sniffing (13.2.3.2): a
   document's bytes decoded to the UTF-8 the tokenizer reads, and the encoding they
   were read in kept as a record of the HTML module's store (contracts/html.pl),
   which the reasoner sees as a fact.

   Decoders. Every encoding of the standard's table: UTF-8; the 28 single-byte
   encodings; gb18030 and GBK, one decoder; Big5; EUC-JP, ISO-2022-JP and Shift_JIS;
   EUC-KR; replacement; UTF-16BE and UTF-16LE; x-user-defined. Each is the
   standard's handler, its steps in its order, run over the whole input with the
   end of the queue last. Restoring bytes to the queue steps back over the bytes the
   handler has just read, which are always the last ones it read; an error is a
   U+FFFD, the replacement error mode. The indexes are src/html/encoding_tables.h,
   which tools/gen-encodings.py writes from the standard's own files.

   Sniffing (13.2.3.2). A byte order mark; else the transport layer's charset, the
   charset parameter of the media type the loader recorded; else the prescan of the
   first 1024 bytes for a meta element's charset or http-equiv content-type, by the
   standard's get-an-attribute algorithm, or for an XML declaration's encoding; else,
   for a file of the local file system whose bytes are UTF-8 and not all ASCII,
   UTF-8 (step 8's autodetection, the one the standard suggests and Chromium makes,
   for file: URLs alone); else windows-1252, the default of most locales. Not done:
   changing the encoding when the tree builder meets a later meta (13.2.3.4), and the
   frequency analysis that guesses other undeclared encodings.

   Cost. Bytes in UTF-8, or all ASCII in an encoding that reads ASCII as itself, go
   to the tokenizer as they are: the work is the scan for the first byte from 0x80
   up, eight bytes at a time, and past it the check that the bytes are UTF-8. */
#include <stdio.h>
#include <string.h>
#include <vector.pl>
#include <amb.pl>

amb_interface(dom);
amb_interface(html);

#include "encoding.h"
#include "encoding_tables.h"

/* the end of the queue, as a handler reads it */
#define ENC_EOQ (-1)
/* the next item of the queue: a byte, or its end */
#define ENC_NEXT(b) do { (b) = ENC_EOQ; if (i < n) (b) = (unsigned char)s[i++]; } while (0)
/* an error, in the replacement error mode */
#define ENC_ERR() do { enc_put(out, 0xFFFD); errors++; } while (0)

static void enc_put(vector<char> *out, uint32_t cp) {
    if (cp < 0x80) push_back(out, (char)cp);
    else if (cp < 0x800) { push_back(out, (char)(0xC0 | (cp >> 6))); push_back(out, (char)(0x80 | (cp & 0x3F))); }
    else if (cp < 0x10000) {
        push_back(out, (char)(0xE0 | (cp >> 12))); push_back(out, (char)(0x80 | ((cp >> 6) & 0x3F)));
        push_back(out, (char)(0x80 | (cp & 0x3F)));
    } else {
        push_back(out, (char)(0xF0 | (cp >> 18))); push_back(out, (char)(0x80 | ((cp >> 12) & 0x3F)));
        push_back(out, (char)(0x80 | ((cp >> 6) & 0x3F))); push_back(out, (char)(0x80 | (cp & 0x3F)));
    }
}

/* ---- the decoders ------------------------------------------------------------- */

/* 8.1.1 UTF-8 */
static size_t enc_utf8(const char *s, size_t n, vector<char> *out) {
    size_t i = 0, errors = 0;
    uint32_t cp = 0, seen = 0, need = 0, lo = 0x80, hi = 0xBF;
    for (;;) {
        int b;
        ENC_NEXT(b);
        if (b == ENC_EOQ) {
            if (need) ENC_ERR();
            return errors;
        }
        if (need == 0) {
            if (b < 0x80) push_back(out, (char)b);
            else if (b >= 0xC2 && b <= 0xDF) { need = 1; cp = (uint32_t)b & 0x1F; }
            else if (b >= 0xE0 && b <= 0xEF) {
                if (b == 0xE0) lo = 0xA0;
                if (b == 0xED) hi = 0x9F;
                need = 2;
                cp = (uint32_t)b & 0xF;
            } else if (b >= 0xF0 && b <= 0xF4) {
                if (b == 0xF0) lo = 0x90;
                if (b == 0xF4) hi = 0x8F;
                need = 3;
                cp = (uint32_t)b & 0x7;
            } else ENC_ERR();
            continue;
        }
        if ((uint32_t)b < lo || (uint32_t)b > hi) {
            cp = need = seen = 0;
            lo = 0x80;
            hi = 0xBF;
            i--;                                   /* the byte restored */
            ENC_ERR();
            continue;
        }
        lo = 0x80;
        hi = 0xBF;
        cp = (cp << 6) | ((uint32_t)b & 0x3F);
        if (++seen != need) continue;
        enc_put(out, cp);
        cp = need = seen = 0;
    }
}

/* 9.1 the single-byte encodings: the row of html_enc_single */
static size_t enc_single(int row, const char *s, size_t n, vector<char> *out) {
    size_t errors = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned b = (unsigned char)s[i];
        if (b < 0x80) { push_back(out, (char)b); continue; }
        uint32_t cp = html_enc_single[row][b - 0x80];
        if (cp) enc_put(out, cp);
        else ENC_ERR();
    }
    return errors;
}

/* index gb18030 ranges code point: 0 for null */
static uint32_t enc_gb18030_range(uint32_t p) {
    if ((p > 39419 && p < 189000) || p > 1237575) return 0;
    if (p == 7457) return 0xE7C7;
    /* the last range that starts at or before p (the first starts at 0) */
    size_t lo = 0, hi = HTML_ENC_GB18030_RANGES - 1;
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        if (html_enc_gb18030_ranges[mid][0] <= p) lo = mid;
        else hi = mid - 1;
    }
    return html_enc_gb18030_ranges[lo][1] + p - html_enc_gb18030_ranges[lo][0];
}

/* 10.2.1 gb18030, which is GBK's decoder too */
static size_t enc_gb18030(const char *s, size_t n, vector<char> *out) {
    size_t i = 0, errors = 0;
    uint32_t first = 0, second = 0, third = 0;
    for (;;) {
        int b;
        ENC_NEXT(b);
        if (b == ENC_EOQ) {
            if (first || second || third) ENC_ERR();
            return errors;
        }
        if (third) {
            if (b < 0x30 || b > 0x39) {
                i -= 3;                            /* second, third and the byte restored */
                first = second = third = 0;
                ENC_ERR();
                continue;
            }
            uint32_t p = (first - 0x81) * (10 * 126 * 10) + (second - 0x30) * (10 * 126) + (third - 0x81) * 10 + (uint32_t)b - 0x30;
            first = second = third = 0;
            uint32_t cp = enc_gb18030_range(p);
            if (cp) enc_put(out, cp);
            else ENC_ERR();
            continue;
        }
        if (second) {
            if (b >= 0x81 && b <= 0xFE) { third = (uint32_t)b; continue; }
            i -= 2;                                /* second and the byte restored */
            first = second = 0;
            ENC_ERR();
            continue;
        }
        if (first) {
            if (b >= 0x30 && b <= 0x39) { second = (uint32_t)b; continue; }
            uint32_t lead = first, cp = 0;
            first = 0;
            uint32_t off = b < 0x7F ? 0x40 : 0x41;
            if ((b >= 0x40 && b <= 0x7E) || (b >= 0x80 && b <= 0xFE)) cp = html_enc_gb18030[(lead - 0x81) * 190 + ((uint32_t)b - off)];
            if (cp) { enc_put(out, cp); continue; }
            if (b < 0x80) i--;                     /* an ASCII byte restored */
            ENC_ERR();
            continue;
        }
        if (b < 0x80) push_back(out, (char)b);
        else if (b == 0x80) enc_put(out, 0x20AC);
        else if (b <= 0xFE) first = (uint32_t)b;
        else ENC_ERR();
    }
}

/* index Big5 code point: 0 for null */
static uint32_t enc_big5_cp(uint32_t p) {
    uint32_t cp = html_enc_big5[p];
    if (cp && ((html_enc_big5_plane2[p >> 3] >> (p & 7)) & 1)) cp += 0x20000;
    return cp;
}

/* 11.1.1 Big5 */
static size_t enc_big5(const char *s, size_t n, vector<char> *out) {
    size_t i = 0, errors = 0;
    uint32_t leading = 0;
    for (;;) {
        int b;
        ENC_NEXT(b);
        if (b == ENC_EOQ) {
            if (leading) ENC_ERR();
            return errors;
        }
        if (leading) {
            uint32_t lead = leading, cp = 0;
            leading = 0;
            int64_t p = -1;
            uint32_t off = b < 0x7F ? 0x40 : 0x62;
            if ((b >= 0x40 && b <= 0x7E) || (b >= 0xA1 && b <= 0xFE)) p = (int64_t)((lead - 0x81) * 157 + ((uint32_t)b - off));
            /* the four pointers of two code points each */
            if (p == 1133 || p == 1135 || p == 1164 || p == 1166) {
                enc_put(out, p < 1164 ? 0xCA : 0xEA);
                enc_put(out, p == 1133 || p == 1164 ? 0x304 : 0x30C);
                continue;
            }
            if (p >= 0) cp = enc_big5_cp((uint32_t)p);
            if (cp) { enc_put(out, cp); continue; }
            if (b < 0x80) i--;
            ENC_ERR();
            continue;
        }
        if (b < 0x80) push_back(out, (char)b);
        else if (b >= 0x81 && b <= 0xFE) leading = (uint32_t)b;
        else ENC_ERR();
    }
}

/* 12.1.1 EUC-JP */
static size_t enc_euc_jp(const char *s, size_t n, vector<char> *out) {
    size_t i = 0, errors = 0;
    uint32_t leading = 0;
    int jis0212 = 0;
    for (;;) {
        int b;
        ENC_NEXT(b);
        if (b == ENC_EOQ) {
            if (leading) ENC_ERR();
            return errors;
        }
        if (leading == 0x8E && b >= 0xA1 && b <= 0xDF) { leading = 0; enc_put(out, 0xFF61 - 0xA1 + (uint32_t)b); continue; }
        if (leading == 0x8F && b >= 0xA1 && b <= 0xFE) { jis0212 = 1; leading = (uint32_t)b; continue; }
        if (leading) {
            uint32_t lead = leading, cp = 0;
            leading = 0;
            if (lead >= 0xA1 && lead <= 0xFE && b >= 0xA1 && b <= 0xFE) {
                uint32_t p = (lead - 0xA1) * 94 + (uint32_t)b - 0xA1;
                cp = jis0212 ? html_enc_jis0212[p] : html_enc_jis0208[p];
            }
            jis0212 = 0;
            if (cp) { enc_put(out, cp); continue; }
            if (b < 0x80) i--;
            ENC_ERR();
            continue;
        }
        if (b < 0x80) push_back(out, (char)b);
        else if (b == 0x8E || b == 0x8F || (b >= 0xA1 && b <= 0xFE)) leading = (uint32_t)b;
        else ENC_ERR();
    }
}

/* 12.2.1 ISO-2022-JP: its states */
enum { J_ASCII, J_ROMAN, J_KATAKANA, J_LEAD, J_TRAIL, J_ESC_START, J_ESC };

static size_t enc_iso_2022_jp(const char *s, size_t n, vector<char> *out) {
    size_t i = 0, errors = 0;
    int state = J_ASCII, out_state = J_ASCII, output = 0;
    uint32_t leading = 0;
    for (;;) {
        int b;
        ENC_NEXT(b);
        switch (state) {
        case J_ASCII:
        case J_ROMAN:
            if (b == 0x1B) { state = J_ESC_START; break; }
            if (b == ENC_EOQ) return errors;
            output = 0;
            if (state == J_ROMAN && b == 0x5C) enc_put(out, 0xA5);
            else if (state == J_ROMAN && b == 0x7E) enc_put(out, 0x203E);
            else if (b <= 0x7F && b != 0x0E && b != 0x0F) push_back(out, (char)b);
            else ENC_ERR();
            break;
        case J_KATAKANA:
            if (b == 0x1B) { state = J_ESC_START; break; }
            if (b == ENC_EOQ) return errors;
            output = 0;
            if (b >= 0x21 && b <= 0x5F) enc_put(out, 0xFF61 - 0x21 + (uint32_t)b);
            else ENC_ERR();
            break;
        case J_LEAD:
            if (b == 0x1B) { state = J_ESC_START; break; }
            if (b == ENC_EOQ) return errors;
            output = 0;
            if (b >= 0x21 && b <= 0x7E) { leading = (uint32_t)b; state = J_TRAIL; }
            else ENC_ERR();
            break;
        case J_TRAIL:
            if (b == 0x1B) { state = J_ESC_START; ENC_ERR(); break; }
            state = J_LEAD;
            if (b >= 0x21 && b <= 0x7E) {
                uint32_t cp = html_enc_jis0208[(leading - 0x21) * 94 + (uint32_t)b - 0x21];
                if (cp) enc_put(out, cp);
                else ENC_ERR();
            } else ENC_ERR();                      /* end of queue too: the lead state then finishes */
            break;
        case J_ESC_START:
            if (b == 0x24 || b == 0x28) { leading = (uint32_t)b; state = J_ESC; break; }
            if (b != ENC_EOQ) i--;
            output = 0;
            state = out_state;
            ENC_ERR();
            break;
        case J_ESC: {
            uint32_t lead = leading;
            int st = -1;
            leading = 0;
            if (lead == 0x28 && b == 0x42) st = J_ASCII;
            else if (lead == 0x28 && b == 0x4A) st = J_ROMAN;
            else if (lead == 0x28 && b == 0x49) st = J_KATAKANA;
            else if (lead == 0x24 && (b == 0x40 || b == 0x42)) st = J_LEAD;
            if (st >= 0) {
                state = out_state = st;
                int was = output;
                output = 1;
                if (was) ENC_ERR();                /* two escapes with nothing between */
                break;
            }
            i -= b == ENC_EOQ ? 1 : 2;             /* the lead (and the byte) restored */
            output = 0;
            state = out_state;
            ENC_ERR();
            break;
        }
        }
    }
}

/* 12.3.1 Shift_JIS */
static size_t enc_shift_jis(const char *s, size_t n, vector<char> *out) {
    size_t i = 0, errors = 0;
    uint32_t leading = 0;
    for (;;) {
        int b;
        ENC_NEXT(b);
        if (b == ENC_EOQ) {
            if (leading) ENC_ERR();
            return errors;
        }
        if (leading) {
            uint32_t lead = leading, cp = 0;
            leading = 0;
            int64_t p = -1;
            uint32_t off = b < 0x7F ? 0x40 : 0x41, lead_off = lead < 0xA0 ? 0x81 : 0xC1;
            if ((b >= 0x40 && b <= 0x7E) || (b >= 0x80 && b <= 0xFC)) p = (int64_t)((lead - lead_off) * 188 + (uint32_t)b - off);
            /* EUDC, interoperable legacy from Windows: the Private Use Area */
            if (p >= 8836 && p <= 10715) { enc_put(out, 0xE000 - 8836 + (uint32_t)p); continue; }
            if (p >= 0) cp = html_enc_jis0208[p];
            if (cp) { enc_put(out, cp); continue; }
            if (b < 0x80) i--;
            ENC_ERR();
            continue;
        }
        if (b <= 0x80) enc_put(out, (uint32_t)b);
        else if (b >= 0xA1 && b <= 0xDF) enc_put(out, 0xFF61 - 0xA1 + (uint32_t)b);
        else if ((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)) leading = (uint32_t)b;
        else ENC_ERR();
    }
}

/* 13.1.1 EUC-KR */
static size_t enc_euc_kr(const char *s, size_t n, vector<char> *out) {
    size_t i = 0, errors = 0;
    uint32_t leading = 0;
    for (;;) {
        int b;
        ENC_NEXT(b);
        if (b == ENC_EOQ) {
            if (leading) ENC_ERR();
            return errors;
        }
        if (leading) {
            uint32_t lead = leading, cp = 0;
            leading = 0;
            if (b >= 0x41 && b <= 0xFE) cp = html_enc_euc_kr[(lead - 0x81) * 190 + ((uint32_t)b - 0x41)];
            if (cp) { enc_put(out, cp); continue; }
            if (b < 0x80) i--;
            ENC_ERR();
            continue;
        }
        if (b < 0x80) push_back(out, (char)b);
        else if (b >= 0x81 && b <= 0xFE) leading = (uint32_t)b;
        else ENC_ERR();
    }
}

/* 14.1.1 replacement: one error for input of any length, none for none */
static size_t enc_replacement(size_t n, vector<char> *out) {
    size_t errors = 0;
    if (n) ENC_ERR();
    return errors;
}

/* 14.2.1 the shared UTF-16 decoder */
static size_t enc_utf16(int be, const char *s, size_t n, vector<char> *out) {
    size_t i = 0, errors = 0;
    int lead_byte = -1;
    uint32_t lead_surrogate = 0;
    for (;;) {
        int b;
        ENC_NEXT(b);
        if (b == ENC_EOQ) {
            if (lead_byte >= 0 || lead_surrogate) ENC_ERR();
            return errors;
        }
        if (lead_byte < 0) { lead_byte = b; continue; }
        uint32_t unit = be ? ((uint32_t)lead_byte << 8) + (uint32_t)b : ((uint32_t)b << 8) + (uint32_t)lead_byte;
        lead_byte = -1;
        if (lead_surrogate) {
            uint32_t lead = lead_surrogate;
            lead_surrogate = 0;
            if (unit >= 0xDC00 && unit <= 0xDFFF) { enc_put(out, 0x10000 + ((lead - 0xD800) << 10) + (unit - 0xDC00)); continue; }
            i -= 2;                                /* the unit's two bytes restored */
            ENC_ERR();
            continue;
        }
        if (unit >= 0xD800 && unit <= 0xDBFF) { lead_surrogate = unit; continue; }
        if (unit >= 0xDC00 && unit <= 0xDFFF) { ENC_ERR(); continue; }
        enc_put(out, unit);
    }
}

/* 14.5.1 x-user-defined */
static size_t enc_x_user_defined(const char *s, size_t n, vector<char> *out) {
    for (size_t i = 0; i < n; i++) {
        unsigned b = (unsigned char)s[i];
        if (b < 0x80) push_back(out, (char)b);
        else enc_put(out, 0xF780 + b - 0x80);
    }
    return 0;
}

size_t html_decode(int enc, const char *src, size_t n, vector<char> *out) {
    if (enc >= HTML_ENC_SINGLE_FIRST && enc <= HTML_ENC_SINGLE_LAST) return enc_single(enc - HTML_ENC_SINGLE_FIRST, src, n, out);
    switch (enc) {
    case HTML_ENC_GBK:
    case HTML_ENC_GB18030: return enc_gb18030(src, n, out);
    case HTML_ENC_BIG5: return enc_big5(src, n, out);
    case HTML_ENC_EUC_JP: return enc_euc_jp(src, n, out);
    case HTML_ENC_ISO_2022_JP: return enc_iso_2022_jp(src, n, out);
    case HTML_ENC_SHIFT_JIS: return enc_shift_jis(src, n, out);
    case HTML_ENC_EUC_KR: return enc_euc_kr(src, n, out);
    case HTML_ENC_REPLACEMENT: return enc_replacement(n, out);
    case HTML_ENC_UTF_16BE: return enc_utf16(1, src, n, out);
    case HTML_ENC_UTF_16LE: return enc_utf16(0, src, n, out);
    case HTML_ENC_X_USER_DEFINED: return enc_x_user_defined(src, n, out);
    }
    return enc_utf8(src, n, out);
}

/* ---- names and labels ---------------------------------------------------------- */

const char *html_enc_name(int enc) {
    if (enc < 0 || enc >= HTML_ENC_COUNT) return "?";
    return html_enc_names[enc];
}

/* ASCII white space (Infra), which a label is trimmed of; HTTP's, which a media type is */
static int enc_ascii_ws(int c) { return c == 0x09 || c == 0x0A || c == 0x0C || c == 0x0D || c == 0x20; }
static int enc_http_ws(int c) { return c == 0x09 || c == 0x0A || c == 0x0D || c == 0x20; }
static int enc_lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* 4.2 get an encoding */
int html_enc_of_label(const char *label, size_t n) {
    size_t a = 0, b = n;
    while (a < b && enc_ascii_ws((unsigned char)label[a])) a++;
    while (b > a && enc_ascii_ws((unsigned char)label[b - 1])) b--;
    size_t m = b - a;
    if (m == 0 || m > HTML_ENC_LABEL_LONGEST) return -1;
    char low[HTML_ENC_LABEL_LONGEST];
    for (size_t k = 0; k < m; k++) low[k] = (char)enc_lower((unsigned char)label[a + k]);
    int lo = 0, hi = HTML_ENC_LABEL_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        size_t l = html_enc_labels[mid].len;
        size_t k = l < m ? l : m;
        int c = memcmp(html_enc_label_text + html_enc_labels[mid].off, low, k);
        if (c == 0) c = l < m ? -1 : (l > m ? 1 : 0);
        if (c == 0) return html_enc_labels[mid].enc;
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

/* p[0..n) is the word w, ASCII case ignored */
static int enc_is_ci(const char *p, size_t n, const char *w) {
    size_t k = 0;
    for (; k < n && w[k]; k++) if (enc_lower((unsigned char)p[k]) != w[k]) return 0;
    return k == n && w[k] == 0;
}

/* an HTTP token code point (Fetch), of a type, a subtype or a parameter's name */
static int enc_token(int c) {
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return 1;
    return c != 0 && strchr("!#$%&'*+-.^_`|~", c) != 0;
}

/* MIME Sniffing 4.4, parse a MIME type: its charset parameter's value, got an encoding */
int html_enc_of_type(const char *t, size_t n) {
    size_t a = 0, b = n;
    while (a < b && enc_http_ws((unsigned char)t[a])) a++;
    while (b > a && enc_http_ws((unsigned char)t[b - 1])) b--;
    /* type / subtype, each a token */
    size_t i = a;
    while (i < b && t[i] != '/') { if (!enc_token((unsigned char)t[i])) return -1; i++; }
    if (i == a || i >= b) return -1;
    size_t sub = ++i;
    while (i < b && t[i] != ';') i++;
    size_t sub_end = i;
    while (sub_end > sub && enc_http_ws((unsigned char)t[sub_end - 1])) sub_end--;
    if (sub_end == sub) return -1;
    for (size_t k = sub; k < sub_end; k++) if (!enc_token((unsigned char)t[k])) return -1;
    /* the parameters: the first valid charset's is the one */
    while (i < b) {
        i++;                                       /* past the ; */
        while (i < b && enc_http_ws((unsigned char)t[i])) i++;
        size_t ns = i;
        while (i < b && t[i] != ';' && t[i] != '=') i++;
        size_t nl = i - ns;
        if (i >= b) break;
        if (t[i] == ';') continue;
        i++;                                       /* past the = */
        if (i >= b) break;
        char v[256];
        size_t vl = 0;
        int ok = 1;
        if (t[i] == '"') {
            /* Fetch, collect an HTTP quoted string, its value extracted */
            i++;
            for (;;) {
                while (i < b && t[i] != '"' && t[i] != '\\') { if (vl < sizeof v) v[vl++] = t[i]; else ok = 0; i++; }
                if (i >= b) break;
                char q = t[i++];
                if (q != '\\') break;
                if (i >= b) { if (vl < sizeof v) v[vl++] = '\\'; else ok = 0; break; }
                if (vl < sizeof v) v[vl++] = t[i]; else ok = 0;
                i++;
            }
            while (i < b && t[i] != ';') i++;
        } else {
            size_t vs = i;
            while (i < b && t[i] != ';') i++;
            size_t ve = i;
            while (ve > vs && enc_http_ws((unsigned char)t[ve - 1])) ve--;
            if (ve == vs) continue;
            for (size_t k = vs; k < ve; k++) { if (vl < sizeof v) v[vl++] = t[k]; else ok = 0; }
        }
        /* a name of token code points, a value of quoted-string token code points */
        for (size_t k = ns; k < ns + nl; k++) if (!enc_token((unsigned char)t[k])) ok = 0;
        for (size_t k = 0; k < vl; k++) {
            int c = (unsigned char)v[k];
            if (!(c == 0x09 || (c >= 0x20 && c <= 0x7E) || c >= 0x80)) ok = 0;
        }
        if (ok && nl > 0 && enc_is_ci(t + ns, nl, "charset")) return html_enc_of_label(v, vl);
    }
    return -1;
}

/* ---- the prescan (HTML 13.2.3.2) ------------------------------------------------ */

/* the prescan's bytes ran out: it ends, and the XML declaration is looked for */
#define PS_OUT (-2)

/* get an attribute, at *pos: 1 with its name and value as runs of s (the standard
   lowercases them; every comparison here ignores ASCII case instead), 0 when there is
   none (*pos at a >), PS_OUT when the bytes run out */
static int ps_attribute(const char *s, size_t n, size_t *pos, size_t *ns, size_t *nl, size_t *vs, size_t *vl) {
    size_t i = *pos;
    while (i < n && (enc_ascii_ws((unsigned char)s[i]) || s[i] == '/')) i++;
    if (i >= n) return PS_OUT;
    if (s[i] == '>') { *pos = i; return 0; }
    *ns = i;
    *vs = i;
    *vl = 0;
    size_t len = 0;
    /* the name */
    for (;;) {
        if (i >= n) return PS_OUT;
        int c = (unsigned char)s[i];
        if (c == '=' && len > 0) { i++; break; }
        if (enc_ascii_ws(c)) {
            /* spaces: an = may follow them, else the attribute has no value */
            while (i < n && enc_ascii_ws((unsigned char)s[i])) i++;
            if (i >= n) return PS_OUT;
            if (s[i] != '=') { *nl = len; *pos = i; return 1; }
            i++;
            break;
        }
        if (c == '/' || c == '>') { *nl = len; *pos = i; return 1; }
        len++;
        i++;
    }
    *nl = len;
    /* the value */
    while (i < n && enc_ascii_ws((unsigned char)s[i])) i++;
    if (i >= n) return PS_OUT;
    if (s[i] == '"' || s[i] == '\'') {
        char q = s[i++];
        *vs = i;
        while (i < n && s[i] != q) i++;
        if (i >= n) return PS_OUT;
        *vl = i - *vs;
        *pos = i + 1;
        return 1;
    }
    if (s[i] == '>') { *pos = i; return 1; }
    *vs = i;
    i++;
    for (;;) {
        if (i >= n) return PS_OUT;
        if (enc_ascii_ws((unsigned char)s[i]) || s[i] == '>') { *vl = i - *vs; *pos = i; return 1; }
        i++;
    }
}

/* 2.5.3 extracting a character encoding from a meta element: from a content value */
static int ps_meta_content(const char *s, size_t n) {
    size_t i = 0;
    for (;;) {
        size_t k = i;
        while (k + 7 <= n && !enc_is_ci(s + k, 7, "charset")) k++;
        if (k + 7 > n) return -1;
        i = k + 7;
        while (i < n && enc_ascii_ws((unsigned char)s[i])) i++;
        if (i >= n || s[i] != '=') continue;      /* the loop again, just before that character */
        i++;
        while (i < n && enc_ascii_ws((unsigned char)s[i])) i++;
        if (i >= n) return -1;
        char q = s[i];
        if (q == '"' || q == '\'') {
            size_t j = i + 1;
            while (j < n && s[j] != q) j++;
            if (j >= n) return -1;                 /* an unmatched quote */
            return html_enc_of_label(s + i + 1, j - i - 1);
        }
        size_t j = i;
        while (j < n && !enc_ascii_ws((unsigned char)s[j]) && s[j] != ';') j++;
        return html_enc_of_label(s + i, j - i);
    }
}

/* get an XML encoding: the encoding a declaration at the stream's start names, or -1 */
static int ps_xml(const char *s, size_t n) {
    if (n < 5 || memcmp(s, "<?xml", 5) != 0) return -1;
    size_t end = 5;
    while (end < n && s[end] != '>') end++;
    if (end >= n) return -1;
    size_t k = 0;
    while (k + 8 <= end && memcmp(s + k, "encoding", 8) != 0) k++;
    if (k + 8 > end) return -1;
    size_t i = k + 8;
    while (i < n && (unsigned char)s[i] <= 0x20) i++;
    if (i >= n || s[i] != '=') return -1;
    i++;
    while (i < n && (unsigned char)s[i] <= 0x20) i++;
    if (i >= n || (s[i] != '"' && s[i] != '\'')) return -1;
    char q = s[i++];
    size_t vs = i;
    while (i < n && s[i] != q) i++;
    if (i >= n) return -1;
    for (size_t j = vs; j < i; j++) if ((unsigned char)s[j] <= 0x20) return -1;
    int e = html_enc_of_label(s + vs, i - vs);
    if (e == HTML_ENC_UTF_16BE || e == HTML_ENC_UTF_16LE) e = HTML_ENC_UTF_8;
    return e;
}

static int ps_starts_ci(const char *s, size_t n, size_t i, const char *w) {
    size_t k = 0;
    for (; w[k]; k++) if (i + k >= n || enc_lower((unsigned char)s[i + k]) != w[k]) return 0;
    return 1;
}
static int ps_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* prescan a byte stream to determine its encoding, the end condition the end of s */
int html_prescan(const char *s, size_t n, int *xml) {
    *xml = 0;
    /* UTF-16 XML declarations */
    if (n >= 6 && s[0] == 0x3C && s[1] == 0 && s[2] == 0x3F && s[3] == 0 && s[4] == 0x78 && s[5] == 0) { *xml = 1; return HTML_ENC_UTF_16LE; }
    if (n >= 6 && s[0] == 0 && s[1] == 0x3C && s[2] == 0 && s[3] == 0x3F && s[4] == 0 && s[5] == 0x78) { *xml = 1; return HTML_ENC_UTF_16BE; }
    size_t i = 0;
    while (i < n) {
        /* every step but the last (any other byte: nothing) begins at a < */
        if (s[i] != '<') { i++; continue; }
        if (ps_starts_ci(s, n, i, "<!--")) {
            /* to the first > after two -, which may be the comment opener's */
            size_t j = i + 4;
            while (j < n && !(s[j] == '>' && s[j - 1] == '-' && s[j - 2] == '-')) j++;
            if (j >= n) break;
            i = j + 1;
            continue;
        }
        if (ps_starts_ci(s, n, i, "<meta") && i + 5 < n && (enc_ascii_ws((unsigned char)s[i + 5]) || s[i + 5] == '/')) {
            i += 5;
            int got_pragma = 0, need_pragma = -1;  /* need pragma: -1 null, 0 false, 1 true */
            int charset = -3;                      /* -3 null, -1 failure, else the encoding */
            int seen_equiv = 0, seen_content = 0, seen_charset = 0;
            int r;
            size_t ns = 0, nl = 0, vs = 0, vl = 0;
            while ((r = ps_attribute(s, n, &i, &ns, &nl, &vs, &vl)) == 1) {
                if (enc_is_ci(s + ns, nl, "http-equiv")) {
                    if (seen_equiv) continue;
                    seen_equiv = 1;
                    if (enc_is_ci(s + vs, vl, "content-type")) got_pragma = 1;
                } else if (enc_is_ci(s + ns, nl, "content")) {
                    if (seen_content) continue;
                    seen_content = 1;
                    int e = ps_meta_content(s + vs, vl);
                    if (e >= 0 && charset == -3) { charset = e; need_pragma = 1; }
                } else if (enc_is_ci(s + ns, nl, "charset")) {
                    if (seen_charset) continue;
                    seen_charset = 1;
                    charset = html_enc_of_label(s + vs, vl);
                    need_pragma = 0;
                }
            }
            if (r == PS_OUT) break;
            /* processing */
            if (need_pragma >= 0 && !(need_pragma == 1 && !got_pragma) && charset >= 0) {
                if (charset == HTML_ENC_UTF_16BE || charset == HTML_ENC_UTF_16LE) charset = HTML_ENC_UTF_8;
                if (charset == HTML_ENC_X_USER_DEFINED) charset = HTML_ENC_WINDOWS_1252;
                return charset;
            }
            i++;
            continue;
        }
        if (s[i] == '<' && ((i + 1 < n && ps_alpha((unsigned char)s[i + 1])) || (i + 2 < n && s[i + 1] == '/' && ps_alpha((unsigned char)s[i + 2])))) {
            /* a tag: to the white space or > after its name, then its attributes */
            while (i < n && !enc_ascii_ws((unsigned char)s[i]) && s[i] != '>') i++;
            if (i >= n) break;
            size_t ns = 0, nl = 0, vs = 0, vl = 0;
            int r;
            while ((r = ps_attribute(s, n, &i, &ns, &nl, &vs, &vl)) == 1) {}
            if (r == PS_OUT) break;
            i++;
            continue;
        }
        if (s[i] == '<' && i + 1 < n && (s[i + 1] == '!' || s[i + 1] == '/' || s[i + 1] == '?')) {
            size_t j = i + 1;
            while (j < n && s[j] != '>') j++;
            if (j >= n) break;
            i = j + 1;
            continue;
        }
        i++;
    }
    int e = ps_xml(s, n);
    if (e >= 0) *xml = 1;
    return e;
}

/* ---- sniffing, and the document parsed ------------------------------------------ */

/* where the first byte from 0x80 up is in s[0..n), or n: 32 bytes at a time, then 8 */
static size_t enc_first_high(const char *s, size_t n) {
    size_t i = 0;
    while (i + 32 <= n) {
        uint64_t w0, w1, w2, w3;
        memcpy(&w0, s + i, 8);
        memcpy(&w1, s + i + 8, 8);
        memcpy(&w2, s + i + 16, 8);
        memcpy(&w3, s + i + 24, 8);
        if ((w0 | w1 | w2 | w3) & 0x8080808080808080ull) break;
        i += 32;
    }
    while (i + 8 <= n) {
        uint64_t w;
        memcpy(&w, s + i, 8);
        if (w & 0x8080808080808080ull) break;
        i += 8;
    }
    while (i < n && (unsigned char)s[i] < 0x80) i++;
    return i;
}

/* s[0..n) is UTF-8 that the decoder reads without an error; from is where its first
   byte from 0x80 up is */
static int enc_utf8_valid(const char *s, size_t n, size_t from) {
    size_t i = from;
    while (i < n) {
        uint32_t b = (unsigned char)s[i];
        if (b < 0x80) {
            i++;
            while (i + 8 <= n) {
                uint64_t w;
                memcpy(&w, s + i, 8);
                if (w & 0x8080808080808080ull) break;
                i += 8;
            }
            continue;
        }
        size_t need;
        uint32_t lo = 0x80, hi = 0xBF;
        if (b >= 0xC2 && b <= 0xDF) need = 1;
        else if (b >= 0xE0 && b <= 0xEF) { need = 2; if (b == 0xE0) lo = 0xA0; if (b == 0xED) hi = 0x9F; }
        else if (b >= 0xF0 && b <= 0xF4) { need = 3; if (b == 0xF0) lo = 0x90; if (b == 0xF4) hi = 0x8F; }
        else return 0;
        if (n - i <= need) return 0;
        uint32_t c = (unsigned char)s[i + 1];
        if (c < lo || c > hi) return 0;
        for (size_t k = 2; k <= need; k++) if (((unsigned char)s[i + k] & 0xC0) != 0x80) return 0;
        i += need + 1;
    }
    return 1;
}

/* the encoding sniffing algorithm, its steps that need no user and no history: the
   byte order mark, the transport layer, the prescan, UTF-8's autodetection, the default */
void html_sniff(const char *src, size_t n, const char *type, size_t type_len, int file, html_sniffed *out) {
    int enc = -1;
    int source = HTML_ENCODING_SOURCE_DEFAULT;
    out->bom = 0;
    out->utf8 = -1;
    /* BOM sniffing */
    if (n >= 3 && (unsigned char)src[0] == 0xEF && (unsigned char)src[1] == 0xBB && (unsigned char)src[2] == 0xBF) { enc = HTML_ENC_UTF_8; out->bom = 3; }
    else if (n >= 2 && (unsigned char)src[0] == 0xFE && (unsigned char)src[1] == 0xFF) { enc = HTML_ENC_UTF_16BE; out->bom = 2; }
    else if (n >= 2 && (unsigned char)src[0] == 0xFF && (unsigned char)src[1] == 0xFE) { enc = HTML_ENC_UTF_16LE; out->bom = 2; }
    if (enc >= 0) source = HTML_ENCODING_SOURCE_BOM;
    out->high = enc_first_high(src + out->bom, n - out->bom);
    if (enc < 0) {
        enc = html_enc_of_type(type, type_len);
        if (enc >= 0) source = HTML_ENCODING_SOURCE_HTTP;
    }
    if (enc < 0) {
        int xml = 0;
        enc = html_prescan(src, n < 1024 ? n : 1024, &xml);
        if (enc >= 0) source = xml ? HTML_ENCODING_SOURCE_XML : HTML_ENCODING_SOURCE_META;
    }
    if (enc < 0 && file && out->high < n) {
        out->utf8 = enc_utf8_valid(src, n, out->high);
        if (out->utf8) { enc = HTML_ENC_UTF_8; source = HTML_ENCODING_SOURCE_DETECTED; }
    }
    if (enc < 0) enc = HTML_ENC_WINDOWS_1252;
    out->enc = enc;
    out->source = source;
}

html_status html_parse_bytes(html_parser *p, dom_document *doc, const char *src, size_t src_len,
                             const char *type, size_t type_len, int file) {
    html_sniffed sn;
    html_sniff(src, src_len, type, type_len, file, &sn);
    size_t n = src_len - sn.bom;
    int ascii = sn.high == n;
    /* the bytes as they are, when the encoding reads them as themselves */
    int same = 0;
    if (sn.enc == HTML_ENC_UTF_8) {
        if (sn.utf8 < 0) sn.utf8 = ascii || enc_utf8_valid(src + sn.bom, n, sn.high);
        same = sn.utf8;
    } else if (ascii) same = sn.enc != HTML_ENC_UTF_16BE && sn.enc != HTML_ENC_UTF_16LE && sn.enc != HTML_ENC_ISO_2022_JP &&
                             sn.enc != HTML_ENC_REPLACEMENT;
    size_t name_len = strlen(html_enc_name(sn.enc));
    amb_span nm = bytes_add(p, html_enc_name(sn.enc), name_len);
    html_encoding_h e = encoding_new(p);
    p->encoding[e.index].name = nm;
    p->encoding[e.index].source = (html_encoding_source)sn.source;
    p->encoding[e.index].ascii = (uint8_t)ascii;
    if (same) return html_parse(p, doc, src + sn.bom, n);
    vector<char> text;
    reserve(text, n + n / 2 + 4);
    size_t errors = html_decode(sn.enc, src + sn.bom, n, &text);
    p->encoding[e.index].errors = (uint32_t)errors;
    return html_parse(p, doc, data(text), len(text));
}
