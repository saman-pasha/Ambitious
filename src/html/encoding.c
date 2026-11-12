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
   for file: URLs alone), as for one whose bytes are mostly UTF-8 (ten bytes of its
   sequences to each in error, as Chromium's detector takes them); else the guess
   below; else windows-1252, the default of most locales.

   A meta past the first 1024 bytes. The tree builder meets one while the encoding is
   tentative and starts the parse again in its encoding (13.2.3.4), so the encoding is
   the document's from the first byte: Blink's own look (HTMLMetaCharsetParser) reads
   on past the prescan while the document is in its head section, and a page's meta
   counts as far as Blink's does.

   An encoding nothing declares, guessed from the bytes as Chromium's detector guesses
   one, from the words that hold a byte from 0x80 up (8 KiB of them). Japanese, Korean
   and Chinese by how much of a multi-byte encoding's reading is their commonest kana,
   hangul and hanzi; else each single-byte encoding Chromium guesses read, and the
   evidence that it is a language written in it weighed in bits: a word that mixes
   scripts, has a capital after a small letter, a symbol between letters, a final form
   inside it or (Cyrillic, Greek) no vowel is no word of a language, each letter of one
   is as likely as the likeliest of the encoding's languages makes it, and an encoding's
   prior (how common it is) is added. windows-1252 unless the likeliest other passes it
   by a margin a byte and its reading is a text's (no reading of mojibake is); neither
   UTF-8 (but for a file) nor a 7-bit encoding such as ISO-2022-JP is guessed, as
   Chromium guesses neither. Where Chromium's detector, which weighs pairs of bytes,
   names an encoding that misreads the text (Czech in ISO-8859-2 it names windows-1250;
   a word or two bytes alone it can name anything), this follows the letters.

   Cost. Bytes in UTF-8, or all ASCII in an encoding that reads ASCII as itself, go
   to the tokenizer as they are: the work is the scan for the first byte from 0x80
   up, eight bytes at a time, and past it the check that the bytes are UTF-8. A guess
   reads its 8 KiB sample once a candidate (some 30 million instructions for a
   megabyte of undeclared Russian, the late meta's look and the sample's gathering
   among them). */
#include <stdio.h>
#include <string.h>
#include <math.h>
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

/* the k bytes at a are the name at b's, ASCII case-insensitively */
static int ps_name_is(const char *a, const char *b, size_t k) {
    for (size_t j = 0; j < k; j++) {
        int x = (unsigned char)a[j], y = (unsigned char)b[j];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return 1;
}

/* a tag Blink's late look for a meta stays in the head section through: script,
   noscript, style, link, meta, object, title, base, and the start tags html and head */
static int ps_head_tag(const char *t, size_t n, int end) {
    return enc_is_ci(t, n, "script") || enc_is_ci(t, n, "noscript") || enc_is_ci(t, n, "style") || enc_is_ci(t, n, "link")
           || enc_is_ci(t, n, "meta") || enc_is_ci(t, n, "object") || enc_is_ci(t, n, "title") || enc_is_ci(t, n, "base")
           || (!end && (enc_is_ci(t, n, "html") || enc_is_ci(t, n, "head")));
}

/* an element whose text the tokenizer reads as text up to its end tag */
static int ps_raw_tag(const char *t, size_t n) {
    return enc_is_ci(t, n, "script") || enc_is_ci(t, n, "style") || enc_is_ci(t, n, "title") || enc_is_ci(t, n, "textarea")
           || enc_is_ci(t, n, "xmp") || enc_is_ci(t, n, "iframe") || enc_is_ci(t, n, "noembed") || enc_is_ci(t, n, "noframes");
}

/* the prescan of s[0..n) (head 0); or (head 1) Blink's look for a meta past it
   (HTMLMetaCharsetParser): the same steps, a raw text element's text skipped, up to the
   first byte past the first 1024 once a tag other than a head's has been met */
static int ps_scan(const char *s, size_t n, int *xml, int head);

/* prescan a byte stream to determine its encoding, the end condition the end of s */
int html_prescan(const char *s, size_t n, int *xml) { return ps_scan(s, n, xml, 0); }

/* a meta anywhere in the head section, as Blink honours one */
static int ps_late_meta(const char *s, size_t n) {
    int xml = 0;
    return ps_scan(s, n, &xml, 1);
}

static int ps_scan(const char *s, size_t n, int *xml, int head) {
    *xml = 0;
    /* UTF-16 XML declarations */
    if (n >= 6 && s[0] == 0x3C && s[1] == 0 && s[2] == 0x3F && s[3] == 0 && s[4] == 0x78 && s[5] == 0) { *xml = 1; return HTML_ENC_UTF_16LE; }
    if (n >= 6 && s[0] == 0 && s[1] == 0x3C && s[2] == 0 && s[3] == 0x3F && s[4] == 0 && s[5] == 0x78) { *xml = 1; return HTML_ENC_UTF_16BE; }
    size_t i = 0;
    int in_head = 1;
    while (i < n) {
        if (head && !in_head && i >= 1024) return -1;
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
            int end = s[i + 1] == '/';
            size_t ts = i + 1 + end;
            while (i < n && !enc_ascii_ws((unsigned char)s[i]) && s[i] != '>') i++;
            if (i >= n) break;
            size_t te = i;
            while (te > ts && s[te - 1] == '/') te--;
            if (head && !ps_head_tag(s + ts, te - ts, end)) in_head = 0;
            size_t ns = 0, nl = 0, vs = 0, vl = 0;
            int r;
            while ((r = ps_attribute(s, n, &i, &ns, &nl, &vs, &vl)) == 1) {}
            if (r == PS_OUT) break;
            i++;
            if (head && !end && enc_is_ci(s + ts, te - ts, "plaintext")) return -1;
            if (head && !end && ps_raw_tag(s + ts, te - ts)) {
                /* to its end tag */
                size_t k = te - ts;
                while (i < n && !(s[i] == '<' && i + 1 + k < n && s[i + 1] == '/' && ps_name_is(s + i + 2, s + ts, k))) i++;
            }
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
    if (head) return -1;
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

/* of s[0..n) from from, the bytes of the UTF-8 sequences of two bytes or more the
   decoder reads, and the bytes it finds in error */
static void enc_utf8_count(const char *s, size_t n, size_t from, size_t *good, size_t *bad) {
    size_t i = from;
    *good = 0;
    *bad = 0;
    while (i < n) {
        uint32_t b = (unsigned char)s[i];
        if (b < 0x80) { i++; continue; }
        size_t need = 0;
        uint32_t lo = 0x80, hi = 0xBF;
        if (b >= 0xC2 && b <= 0xDF) need = 1;
        else if (b >= 0xE0 && b <= 0xEF) { need = 2; if (b == 0xE0) lo = 0xA0; if (b == 0xED) hi = 0x9F; }
        else if (b >= 0xF0 && b <= 0xF4) { need = 3; if (b == 0xF0) lo = 0x90; if (b == 0xF4) hi = 0x8F; }
        int ok = need > 0 && n - i > need;
        if (ok) {
            uint32_t c = (unsigned char)s[i + 1];
            if (c < lo || c > hi) ok = 0;
            for (size_t k = 2; ok && k <= need; k++) if (((unsigned char)s[i + k] & 0xC0) != 0x80) ok = 0;
        }
        if (ok) { *good += need + 1; i += need + 1; }
        else { (*bad)++; i++; }
    }
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
/* ---- an encoding nothing declares, guessed from the bytes ---- */

/* the commonest hanzi of simplified and of traditional Chinese, as UTF-8: text whose
   decoding holds many of them is in that encoding */
static const char enc_hanzi_simplified[] =
    "的一是不了在人有我他这个们中来上大为和国地到以说时要就出会可也你对生能而子那得于着下自之年过发后作里用道行所然家种事成方多经么去法学如都同现当没动面起看定天分还进好小部其些主样理心她本前开但因只从想实日"
    "文字网页中华人民共和国北京市区东西南北新闻经济社会教育科技体育健康旅游汽车房产游戏视频图片论坛首页关于联系我们版权所有";
static const char enc_hanzi_traditional[] =
    "的一是不了在人有我他這個們中來上大為和國地到以說時要就出會可也你對生能而子那得於著下自之年過發後作裡用道行所然家種事成方多經麼去法學如都同現當沒動面起看定天分還進好小部其些主樣理心她本前開但因只從想實日"
    "文字網頁中華臺灣香港新聞經濟社會教育科技體育健康旅遊汽車房產遊戲視頻圖片論壇首頁關於聯繫我們版權所有";

/* the commonest hangul syllables of Korean, as UTF-8: Chinese read as EUC-KR decodes to
   hangul too (GB 2312's hanzi rows are KS X 1001's hangul rows), but to rare ones */
static const char enc_hangul_common[] =
    "이다는의에하고을를가한지서로기사정리대자인수도시전있해아니으나그어것들상일부보주제경여게과적만와성국중위관생동문무연소신내원비우장실공화구방회계안오조개마저발선당세통교요물학결업법단"
    "말할년월때거된되면며습입합니까요어서울특별사람우리또한더많은없는같은다른모든그리고하지만";

/* the code point at s[*i] of UTF-8 text, the index moved past it */
static uint32_t enc_next_cp(const char *s, size_t n, size_t *i) {
    unsigned char c = (unsigned char)s[*i];
    uint32_t cp = c;
    int k = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
    if (k) cp = c & (0x3F >> k);
    (*i)++;
    for (int q = 0; q < k && *i < n; q++, (*i)++) cp = cp << 6 | ((unsigned char)s[*i] & 0x3F);
    return cp;
}

/* code point cp is one of set's (UTF-8 text) */
static int enc_in_set(const char *set, size_t sn, uint32_t cp) {
    size_t i = 0;
    while (i < sn) if (enc_next_cp(set, sn, &i) == cp) return 1;
    return 0;
}

/* the commonest kanji of Japanese, as UTF-8: text of kanji alone is Japanese when they are
   these (Chinese and Korean read as EUC-JP decode to kanji too, but to others) */
static const char enc_kanji_common[] =
    "日一国会人年大十二本中長出三同時政事自行社見月分議後前民生連五発間対上部東者党地合市業内相方四定今回新場金員九入選立開手米力学問高代明実円関決子動京全目表戦経通外最言氏現理調体化田当八六約主題下首意法不来作性的要用制治度務強気小七成期公持野協取都和統以機平総加山思家話世受区領多県続進正安設保改数記院女初北午指権心界支第産結百派点教報済書府活原先共得解名交資予川向際査勝面委告軍文反元重近千考判認画海参売利組知案道信策集在件団別物側任引使求所次水半品昨論計死官増係感特情投示変打男基私各始島直両朝革価式確村提運終挙果西勢減台広容必応演電歳住争談能無再位置企真流格有疑口過局少放税検藤町常校料沢裁状工建語球営空職証土与急止送援供可役構木割聞身費付施切由説転食比難防補車優夫研収断井何南石足違消境神番規術護展態導鮮備宅害配副算視条幹独警宮究育席輸訪楽起万着乗店述残想線率病農州武声質念待試族象銀域助労例衛然早張映限親額監環験追審商葉義伝働形景落欧担好退準賞訴辺造英被株頭技低毎医復仕去姿味負閣韓渡失移差衆個門写評課末守若脳極種美岡影命含福蔵量望松非撃佐核観察整段横融型白深字答夜製票況音申様財港識注呼渉達良響阪帰針専推谷古候史天階程満敗管値歌買突兵接請器士光討路悪科攻崎督授催細効図週積丸他及湾録処省旧室憲太橋歩離岸客風紙激否周師摘材登系批郎母易健黒火戸速存花春飛殺央券赤号単盟座青破編捜竹除完降超責並療従右修捕隊危採織森競拡故館振給屋介読弁根色友苦就迎走販園具左異歴辞将秋因献厳馬愛幅休維富浜父遺彼般未塁貿講邦舞林装諸夏素亡劇河遣航抗冷模雄適婦鉄寄益込顔緊類児余禁印逆王返標換久短油妻暴輪占宣背昭廃植熱宿薬伊江清習険頼僚覚吉盛船倍均億途圧芸許皇臨踏駅署抜壊債便伸留罪停興爆陸玉源儀波創障継筋狙帯延羽努固闘精則葬乱避普散司康測豊洋静善逮婚厚喜齢囲卒迫略承浮惑崩順紀聴脱旅絶級幸岩練押軽倒了庁博城患締等救執層版老令角絡損房募曲撤裏払削密庭徒措仏績築貨志混載昇池陣我勤為血遅抑幕居染温雑招奈季困星傷永択秀著徴誌庫弾償刊像功拠香欠更秘拒刑坂刻底賛塚致抱繰服犯尾描布恐寺鈴盤息宇項喪伴遠養懸戻街巨震願絵希越契掲躍棄欲痛触邸依籍汚縮還枚属笑互複慮郵束仲栄札枠似夕恵板列露沖探逃借緩節需骨射傾届曜遊迷夢巻購揮君燃充雨閉緒跡包駐貢鹿弱却端賃折紹獲郡併草徹飲貴埼衝焦奪雇災浦暮替析預焼簡譲称肉納樹挑章臓律誘紛貸至宗促慎控贈智握照宙酒俊銭薄堂渋群銃悲秒操携奥診詰託晴撮誕侵括掛謝双孝刺到駆寝透津壁稲仮暗裂敏鳥純是飯排裕堅訳盗芝綱吸典賀扱顧弘看訟戒祖誉雲尊逸紅幼語";

/* how much of a document the guess reads: the runs of bytes from 0x40 up (no multi-byte
   encoding's character holds a byte below, so a run holds whole characters, and whole
   words) that hold a byte from 0x80 up, each with a space after it, to 8 KiB; and of a
   multi-byte reading, its first 2000 characters from 0x80 up */
#define ENC_SCAN 8192
#define ENC_CJK_CHARS 2000

static int enc_ascii_letter(uint32_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

/* how like its language a multi-byte encoding's decoding of src is: Japanese by its kana
   (or, with few, by its commonest kanji), Korean by its hangul syllables, Chinese by its
   commonest hanzi (kana and hangul then counting against it), a character between two
   ASCII letters as no character of theirs; negative when it decodes with errors */
static double enc_cjk_score(int enc, const char *src, size_t n) {
    vector<char> text;
    size_t errors = html_decode(enc, src, n, &text);
    if (errors) return -1;
    double good = 0, total = 0;
    size_t i = 0, tn = len(text);
    int ja = enc == HTML_ENC_SHIFT_JIS || enc == HTML_ENC_EUC_JP, ko = enc == HTML_ENC_EUC_KR;
    const char *set = enc_hanzi_simplified;
    size_t sn = sizeof enc_hanzi_simplified - 1;
    if (enc == HTML_ENC_BIG5) { set = enc_hanzi_traditional; sn = sizeof enc_hanzi_traditional - 1; }
    vector<uint32_t> cps;
    while (i < tn) push_back(cps, enc_next_cp(data(text), tn, &i));
    size_t cn = len(cps);
    /* Japanese with few kana: its kanji held to the commonest */
    int kana = 0, han = 0;
    for (size_t j = 0; j < cn; j++) {
        uint32_t cp = at(cps, j);
        if (cp >= 0x3041 && cp <= 0x30FF) kana++;
        if (cp >= 0x4E00 && cp <= 0x9FFF) han++;
    }
    if (ja && kana * 10 < kana + han) { set = enc_kanji_common; sn = sizeof enc_kanji_common - 1; ja = 0; }
    for (size_t j = 0; j < cn && total < ENC_CJK_CHARS; j++) {
        uint32_t cp = at(cps, j);
        if (cp < 0x80) continue;
        total++;
        int is_kana = cp >= 0x3041 && cp <= 0x30FF, is_hangul = cp >= 0xAC00 && cp <= 0xD7A3;
        int is_han = cp >= 0x4E00 && cp <= 0x9FFF, is_punct = (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFF01 && cp <= 0xFF5E);
        double g;
        if (ja) g = is_kana ? 1 : is_han ? 0.5 : is_punct ? 0.5 : (cp >= 0xFF61 && cp <= 0xFF9F) ? -0.5 : -1;
        else if (ko) g = is_hangul ? (enc_in_set(enc_hangul_common, sizeof enc_hangul_common - 1, cp) ? 1 : 0.2) : is_punct || is_han ? 0.2 : -1;
        else g = is_han ? (enc_in_set(set, sn, cp) ? 1 : 0.15) : is_punct ? 0.5 : is_kana ? 0.2 : -1;
        /* one between two ASCII letters is a Latin word's letter misread */
        uint32_t before = j > 0 ? at(cps, j - 1) : 0x20, after = j + 1 < cn ? at(cps, j + 1) : 0x20;
        if (enc_ascii_letter(before) && enc_ascii_letter(after)) g = -1;
        good += g;
    }
    if (total == 0) return -1;
    return good / total;
}

/* a letter's script, of those the single-byte encodings guessed write: 1 Latin, 2
   Cyrillic, 3 Greek, 4 Hebrew, 5 Arabic, 6 Thai, 0 none; *mark set for a combining mark
   (a vowel point, a tone mark), which a word goes on through but which is no letter */
static int enc_script_of(uint32_t cp, int *mark) {
    *mark = 0;
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) return 1;
    if (cp < 0xC0) return 0;
    if (cp <= 0x24F) return cp == 0xD7 || cp == 0xF7 ? 0 : 1;
    if (cp >= 0x386 && cp <= 0x3CE) return cp == 0x387 || cp == 0x3A2 ? 0 : 3;
    if (cp >= 0x400 && cp <= 0x4FF) return cp >= 0x482 && cp <= 0x489 ? 0 : 2;
    if (cp >= 0x591 && cp <= 0x5C7) { *mark = 1; return 4; }
    if (cp >= 0x5D0 && cp <= 0x5F2) return 4;
    if ((cp >= 0x621 && cp <= 0x63A) || (cp >= 0x641 && cp <= 0x64A) || (cp >= 0x671 && cp <= 0x6D3)) return 5;
    if (cp == 0x640 || (cp >= 0x64B && cp <= 0x65F) || cp == 0x670) { *mark = 1; return 5; }
    if (cp == 0xE31 || (cp >= 0xE34 && cp <= 0xE3A) || (cp >= 0xE47 && cp <= 0xE4E)) { *mark = 1; return 6; }
    if (cp >= 0xE01 && cp <= 0xE46) return 6;
    return 0;
}

/* a letter's small form (Latin, Cyrillic, Greek), or the letter */
static uint32_t enc_small(uint32_t cp) {
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) return cp + 32;
    if ((cp >= 0x100 && cp <= 0x12F) || (cp >= 0x132 && cp <= 0x137) || (cp >= 0x14A && cp <= 0x177) || (cp >= 0x218 && cp <= 0x21B)) return cp | 1;
    if ((cp >= 0x139 && cp <= 0x148) || (cp >= 0x179 && cp <= 0x17E)) return cp & 1 ? cp + 1 : cp;
    if (cp == 0x178) return 0xFF;
    if (cp == 0x386) return 0x3AC;
    if (cp >= 0x388 && cp <= 0x38A) return cp + 37;
    if (cp == 0x38C) return 0x3CC;
    if (cp == 0x38E || cp == 0x38F) return cp + 63;
    if (cp >= 0x391 && cp <= 0x3AB) return cp + 32;
    if (cp >= 0x400 && cp <= 0x40F) return cp + 0x50;
    if (cp >= 0x410 && cp <= 0x42F) return cp + 0x20;
    if (cp >= 0x460 && cp <= 0x4FF) return cp | 1;
    return cp;
}

/* a letter as the alphabets below list it: small, a Greek one without its accent or its
   final form, a Hebrew one without its final form */
static uint32_t enc_base(uint32_t cp) {
    cp = enc_small(cp);
    switch (cp) {
    case 0x3AC: return 0x3B1;
    case 0x3AD: return 0x3B5;
    case 0x3AE: return 0x3B7;
    case 0x3AF: case 0x3CA: case 0x390: return 0x3B9;
    case 0x3CC: return 0x3BF;
    case 0x3CD: case 0x3CB: case 0x3B0: return 0x3C5;
    case 0x3CE: return 0x3C9;
    case 0x3C2: return 0x3C3;
    case 0x5DA: return 0x5DB;
    case 0x5DD: return 0x5DE;
    case 0x5DF: return 0x5E0;
    case 0x5E3: return 0x5E4;
    case 0x5E5: return 0x5E6;
    default: return cp;
    }
}

/* a character from 0x80 up that is no misread letter where it stands after one: a
   no-break space or a soft hyphen, which ends a word; an apostrophe, a middle dot, an
   acute accent or a hyphen, which joins two */
static int enc_benign(uint32_t cp) {
    return cp == 0xA0 || cp == 0xAD || cp == 0xB4 || cp == 0xB7 || cp == 0x387 || cp == 0x2BC || cp == 0x2010 || cp == 0x2011 || cp == 0x2019;
}

/* a vowel of Cyrillic or Greek, as enc_base gives it (а е ё и о у ы э ю я і ї є, α ε η ι
   ο υ ω): a word of theirs of three letters or more has one */
static int enc_vowel(uint32_t c) {
    return c == 0x430 || c == 0x435 || c == 0x438 || c == 0x43E || c == 0x443 || c == 0x44B || c == 0x44D || c == 0x44E || c == 0x44F
           || c == 0x451 || c == 0x454 || c == 0x456 || c == 0x457 || c == 0x3B1 || c == 0x3B5 || c == 0x3B7 || c == 0x3B9 || c == 0x3BF
           || c == 0x3C5 || c == 0x3C9;
}

/* what a symbol from 0x80 up costs, in bits, outside a word: a space, a soft hyphen or a
   mark that joins letters nothing; punctuation, quotes, dashes and the commonest signs
   half a bit; the others (box drawing, fractions, superscripts, other signs) 3 */
static double enc_symbol_cost(uint32_t cp) {
    if (enc_benign(cp)) return 0;
    if ((cp >= 0x2010 && cp <= 0x205E) || cp == 0xA1 || cp == 0xA3 || cp == 0xA7 || cp == 0xA9 || cp == 0xAB || cp == 0xAE || cp == 0xB0
        || cp == 0xB6 || cp == 0xBB || cp == 0xBF || cp == 0x20AC || cp == 0x2116 || cp == 0x2122 || cp == 0x60C || cp == 0x61B || cp == 0x61F
        || cp == 0x5BE || cp == 0x5F3 || cp == 0x5F4 || cp == 0xE3F)
        return 0.5;
    return 3;
}

/* the languages of each group of candidates below, as their letters from 0x80 up (small,
   the commonest first) and a '|' after each, in UTF-8. Western: French, German, Spanish,
   Portuguese, Italian, Catalan, Dutch, Swedish, Danish and Norwegian, Finnish, Icelandic,
   Estonian, Irish, Albanian, Afrikaans, Faroese, Welsh */
static const char enc_alpha_western[] =
    "éèàêçâôîûùëïœüÿæ|üäßöé|óíáéñúü|ãçéáíóêõúâàôü|àèéìòùóíúî|èàéóòíçúïü|éëïöüèáó|äåöé|åøæéó|äöåšž|áðíóéúýþæö|äõüöšž|áíéóú|ëç|"
    "êëéôûîïèáóú|áðíóúýæø|âêîôûäëïöüéèáà|";
/* Russian, Ukrainian, Bulgarian, Serbian, Macedonian, Belarusian */
static const char enc_alpha_cyrillic[] =
    "оеаинтсрвлкмдпуяыьгзбчйхжшюцщэфъё|оанивітерсклудмпязбгчьхйцжшюєщїфґ|аоеитнрсвлкдпмзбъугячжйхшщцюфь|аиоенрстјувдклпмзгбчшцхжћњљђфџ|"
    "аоеинтрсвдклпмзјубгчшцжхњќљѓѕџф|аоныіерсквлдтмпуязбьгчшйхцэжюёфў|";
/* Polish, Czech, Slovak, Hungarian, Slovenian, Croatian, Romanian, Albanian */
static const char enc_alpha_central[] = "ęąółżśćńź|íáéěřýčšůžúňťďó|áíéýčžšúôľňťäďóŕĺ|áéőöüóíúű|čšž|čšžćđ|ăîşţâ|ëç|";
static const char enc_alpha_turkish[] = "ışğüçöİâîû|";
/* Lithuanian, Latvian, Estonian */
static const char enc_alpha_baltic[] = "ąčęėįšųūž|āčēģīķļņšūž|äõüöšž|";
static const char enc_alpha_greek[] = "αοιετσνηυρπκμλωδγχθφβξζψ|";
static const char enc_alpha_hebrew[] = "יוהלאמרתבשנדעכחקפסגזטצ|";
/* Arabic, and the letters Persian and Urdu add */
static const char enc_alpha_arabic[] = "اليمونرتبعهةسدفقكحجأإىشطصخذزثضغظئءؤآکگپچژٹڈڑںھہے|";
static const char enc_alpha_thai[] = "านรอกเงมยลวดทสตขบปใหคจแพชไะถศญษฟผโซณภธฝฉฐฮฒฑฎฏฌฬฦฤำๆฯ|";

/* the single-byte encodings Chromium's detector guesses, the script each writes, and the
   group of its languages (0 western, 1 Cyrillic, 2 central European, 3 Turkish, 4 Baltic,
   5 Greek, 6 Hebrew, 7 Arabic, 8 Thai). Where two read a text alike, the earlier is the
   one Chromium names */
#define ENC_CANDS 16
static const int enc_cand_enc[ENC_CANDS] = {
    HTML_ENC_WINDOWS_1252, HTML_ENC_WINDOWS_1251, HTML_ENC_KOI8_R, HTML_ENC_KOI8_U, HTML_ENC_IBM866, HTML_ENC_ISO_8859_5,
    HTML_ENC_WINDOWS_1250, HTML_ENC_ISO_8859_2, HTML_ENC_WINDOWS_1254, HTML_ENC_WINDOWS_1257, HTML_ENC_ISO_8859_7,
    HTML_ENC_WINDOWS_1253, HTML_ENC_WINDOWS_1255, HTML_ENC_WINDOWS_1256, HTML_ENC_ISO_8859_6, HTML_ENC_WINDOWS_874 };
static const int enc_cand_script[ENC_CANDS] = { 1, 2, 2, 2, 2, 2, 1, 1, 1, 1, 3, 3, 4, 5, 5, 6 };
/* how much likelier each is than not before its text is read, in bits: windows-1252 the
   default, the rarer encodings of a language below its commoner */
static const double enc_cand_prior[ENC_CANDS] = { 0, -1, -3, -5, -6, -6, -2, -3, -3, -4, -3, -3.5, -3, -3, -5, -3 };
static const int enc_cand_group[ENC_CANDS] = { 0, 1, 1, 1, 1, 1, 2, 2, 3, 4, 5, 5, 6, 7, 7, 8 };

/* the evidence, in bits, that the bytes s[0..n) are a text written in candidate k (its
   bytes from 0x80 up counted in *highs). A word (a run of letters, a symbol from 0x80 up between two taken in) with
   a letter from 0x80 up is a word of the language when it keeps to one script, has its
   capitals first or is all capitals, has no such symbol, (in Latin) has a letter below
   0x80 or is one letter alone, and (in Cyrillic and Greek) has a vowel or fewer than
   three letters; else each of its bytes from 0x80 up counts -6, as a
   C1 control or a byte with no character does. Each letter from 0x80 up of the words
   that are counts what the likeliest of the candidate's languages makes of it: log2 of
   its share there, an alphabet's letters each q times as common as the one before it (a
   letter it lacks 2^-10), above the alphabet's entropy; a capital 0.5 less. A symbol
   elsewhere counts what enc_symbol_cost says; a final form inside a word, as a symbol
   inside one, makes it no word */
/* what a byte reads as in a candidate, enc_cand_score's flags: its script (the low three
   bits), and whether it is a mark, a capital, a final form, a vowel, a symbol that is no
   misread letter, or no character */
#define ENC_F_MARK 8
#define ENC_F_UP 16
#define ENC_F_FIN 32
#define ENC_F_VOWEL 64
#define ENC_F_BENIGN 128
#define ENC_F_MISS 256

static double enc_cand_score(int k, const char *s, size_t n, double *highs_out) {
    int row = enc_cand_enc[k] - HTML_ENC_SINGLE_FIRST, g = enc_cand_group[k];
    /* each byte from 0x80 up read once: fl[b] its flags, those below 0x80 too */
    int fl[256];
    double cost[128];
    size_t cnt[128];
    for (int b = 0; b < 128; b++) fl[b] = (b >= 'a' && b <= 'z') ? 1 : (b >= 'A' && b <= 'Z') ? 1 | ENC_F_UP : 0;
    for (int b = 0; b < 128; b++) {
        uint32_t cp = html_enc_single[row][b];
        int mark = 0, sc = enc_script_of(cp, &mark), f = sc;
        if (cp < 0xA0) f = ENC_F_MISS;
        else if (sc) {
            if (mark) f |= ENC_F_MARK;
            if (enc_small(cp) != cp || cp == 0x130) f |= ENC_F_UP;
            if (cp == 0x3C2 || cp == 0x5DA || cp == 0x5DD || cp == 0x5DF || cp == 0x5E3 || cp == 0x5E5) f |= ENC_F_FIN;
            if ((sc == 2 || sc == 3) && enc_vowel(enc_base(cp))) f |= ENC_F_VOWEL;
        } else if (enc_benign(cp)) f |= ENC_F_BENIGN;
        fl[b + 128] = f;
        cost[b] = sc ? 0 : enc_symbol_cost(cp);
        cnt[b] = 0;
    }
    double lost = 0, caps = 0, highs = 0, symbols = 0, pend = 0;
    int script = 0, mixed = 0, high = 0, plain = 0, wlen = 0, bad = 0, lower = 0, pending = 0, fin = 0, vowel = 0;
    size_t ws = 0, we = 0;
    for (size_t i = 0; i <= n; i++) {
        int b = i < n ? (unsigned char)s[i] : 0x20, f = fl[b];
        if (b >= 0x80) highs++;
        int sc = f & 7;
        if (sc) {
            if (pending) { bad = 1; high = 1; }
            pending = 0;
            if (!wlen) ws = i;
            we = i + 1;
            if (script && sc != script) mixed = 1;
            script = sc;
            wlen++;
            if (b >= 0x80) high = 1;
            if (f & ENC_F_MARK) continue;
            /* a final form (Greek's sigma, Hebrew's five) ends its word */
            if (fin) bad = 1;
            fin = f & ENC_F_FIN;
            if (f & ENC_F_VOWEL) vowel = 1;
            int up = f & ENC_F_UP;
            if (up && lower) bad = 1;
            lower = !up;
            if (b < 0x80) plain = 1;
        } else if (b >= 0x80 && !(f & (ENC_F_MISS | ENC_F_BENIGN)) && !pending && wlen > 0) {
            pending = 1;
            pend = cost[b - 0x80];
        } else {
            if (pending) symbols += pend;
            if (high) {
                int good = !mixed && !bad && (script != 1 || plain || wlen < 2) && (vowel || wlen < 3 || (script != 2 && script != 3));
                for (size_t j = ws; j < we; j++) {
                    int c = (unsigned char)s[j];
                    if (c < 0x80) continue;
                    if (!good) { lost++; continue; }
                    int fc = fl[c];
                    if (!(fc & 7) || (fc & ENC_F_MARK)) continue;
                    cnt[c - 0x80]++;
                    if (fc & ENC_F_UP) caps++;
                }
            }
            script = 0;
            mixed = 0;
            high = 0;
            plain = 0;
            wlen = 0;
            bad = 0;
            lower = 0;
            pending = 0;
            fin = 0;
            vowel = 0;
            if (f & ENC_F_MISS) lost++;
            else if (b >= 0x80) symbols += cost[b - 0x80];
        }
    }
    *highs_out = highs;
    if (highs == 0) return 0;
    const char *al = enc_alpha_western;
    size_t an = sizeof enc_alpha_western - 1;
    if (g == 1) { al = enc_alpha_cyrillic; an = sizeof enc_alpha_cyrillic - 1; }
    else if (g == 2) { al = enc_alpha_central; an = sizeof enc_alpha_central - 1; }
    else if (g == 3) { al = enc_alpha_turkish; an = sizeof enc_alpha_turkish - 1; }
    else if (g == 4) { al = enc_alpha_baltic; an = sizeof enc_alpha_baltic - 1; }
    else if (g == 5) { al = enc_alpha_greek; an = sizeof enc_alpha_greek - 1; }
    else if (g == 6) { al = enc_alpha_hebrew; an = sizeof enc_alpha_hebrew - 1; }
    else if (g == 7) { al = enc_alpha_arabic; an = sizeof enc_alpha_arabic - 1; }
    else if (g == 8) { al = enc_alpha_thai; an = sizeof enc_alpha_thai - 1; }
    /* western Latin's letters from 0x80 up fall off fastest (French's é is half of them),
       central Europe's, Turkey's and the Baltic's slower, a whole alphabet's slowest */
    double q = g == 0 ? 0.7 : g == 2 || g == 3 || g == 4 ? 0.82 : 0.88;
    double best = 0;
    int first = 1;
    size_t i = 0;
    while (i < an) {
        /* a language: its letters from st, nl of them, up to its '|' */
        size_t st = i, nl = 0;
        while (i < an && enc_next_cp(al, an, &i) != '|') nl++;
        double qn = 1;
        for (size_t r = 0; r < nl; r++) qn *= q;
        double lz = log2((1 - q) / (1 - qn)), lq = log2(q), h = 0, pr = (1 - q) / (1 - qn);
        for (size_t r = 0; r < nl; r++) { h -= pr * (lz + r * lq); pr *= q; }
        double sum = 0;
        for (int b = 0; b < 128; b++) {
            if (!cnt[b]) continue;
            uint32_t base = enc_base(html_enc_single[row][b]);
            double e = -10;
            size_t j = st;
            for (size_t r = 0; r < nl; r++)
                if (enc_next_cp(al, an, &j) == base) { e = lz + r * lq; break; }
            sum += (double)cnt[b] * (e + h);
        }
        if (first || sum > best) best = sum;
        first = 0;
    }
    return best - 6 * lost - 0.5 * caps - symbols;
}

/* what another single-byte encoding's evidence must pass windows-1252's by, and the least
   it may be, in bits a byte (a language's own text is about 0) */
#define ENC_MARGIN 0.25
#define ENC_FLOOR -3

/* the encoding the bytes src of a document that declares none are most likely in (as
   Chromium's detector guesses one; -1 for windows-1252, its default): one of the
   multi-byte encodings of Japanese, Korean and Chinese whose decoding has no error and
   is most like its language; else the single-byte encoding whose reading is most like a
   language written in it, by a margin over windows-1252's. Neither valid UTF-8 (but for
   a file) nor a 7-bit encoding such as ISO-2022-JP is guessed, as Chromium guesses
   neither. Of the two Greek encodings, Chromium names ISO-8859-7 unless the text has a
   byte from 0x80 to 0x9F (a character only windows-1253 has) or many capitals with tonos */
static int enc_detect(const char *src, size_t n, size_t high) {
    if (high >= n || enc_utf8_valid(src, n, high)) return -1;
    /* the sample: the runs that hold a byte from 0x80 up */
    vector<char> sample;
    size_t run = high;
    while (run > 0 && (unsigned char)src[run - 1] >= 0x40) run--;
    int has = 0;
    for (size_t i = run; i <= n && len(sample) < ENC_SCAN; i++) {
        unsigned char b = i < n ? (unsigned char)src[i] : 0x20;
        if (b >= 0x80) has = 1;
        if (b >= 0x40) continue;
        if (has) {
            append_range(&sample, src + run, i - run);
            push_back(sample, ' ');
        }
        has = 0;
        run = i + 1;
        /* to the next byte from 0x80 up, 8 at a time */
        while (run + 8 <= n) {
            uint64_t w;
            memcpy(&w, src + run, 8);
            if (w & 0x8080808080808080ull) break;
            run += 8;
        }
        while (run < n && (unsigned char)src[run] < 0x80) run++;
        while (run > i + 1 && (unsigned char)src[run - 1] >= 0x40) run--;
        i = run - 1;
    }
    const char *t = data(sample);
    size_t m = len(sample);
    static const int cjk[5] = { HTML_ENC_SHIFT_JIS, HTML_ENC_EUC_JP, HTML_ENC_EUC_KR, HTML_ENC_GBK, HTML_ENC_BIG5 };
    int best = -1;
    double bs = 0.35;
    for (int k = 0; k < 5; k++) {
        double sc = enc_cjk_score(cjk[k], t, m);
        if (sc > bs) { bs = sc; best = cjk[k]; }
    }
    if (best >= 0) return best;
    /* the likeliest of the others, its prior and evidence summed; it wins if it passes
       windows-1252 by the margin a byte */
    int bk = -1;
    double highs = 0, s0 = enc_cand_score(0, t, m, &highs);
    double be = 0;
    for (int k = 1; k < ENC_CANDS; k++) {
        double hk = 0, ev = enc_cand_score(k, t, m, &hk), sc = enc_cand_prior[k] + ev;
        if (bk < 0 || sc > bs) { bs = sc; bk = k; be = ev; }
    }
    if (bs <= s0 + ENC_MARGIN * highs) return -1;
    /* and its evidence is a text's: every reading of mojibake is garbage, the least of it
       no language */
    if (be < ENC_FLOOR * highs) return -1;
    best = enc_cand_enc[bk];
    if (best == HTML_ENC_ISO_8859_7 || best == HTML_ENC_WINDOWS_1253) {
        size_t c1 = 0, tonos = 0, highs = 0;
        for (size_t i = 0; i < m; i++) {
            unsigned char b = (unsigned char)t[i];
            if (b < 0x80) continue;
            highs++;
            if (b < 0xA0) c1++;
            if (b == 0xA2 || b == 0xB6 || b == 0xB8 || b == 0xB9 || b == 0xBA || b == 0xBC || b == 0xBE || b == 0xBF) tonos++;
        }
        best = c1 || tonos * 20 > highs ? HTML_ENC_WINDOWS_1253 : HTML_ENC_ISO_8859_7;
    }
    return best;
}

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
        /* a meta past the first 1024 bytes: the tree builder meets it while the encoding is
           tentative, and the parse starts again in its encoding (13.2.3.4); so its encoding
           is the document's from the start. Blink looks for one up to the end of the head
           section (a tag no head holds) or the first 1024 bytes, whichever is later */
        if (enc < 0 && n > 1024) enc = ps_late_meta(src, n);
        if (enc >= 0) source = xml ? HTML_ENCODING_SOURCE_XML : HTML_ENCODING_SOURCE_META;
    }
    if (enc < 0 && file && out->high < n) {
        out->utf8 = enc_utf8_valid(src, n, out->high);
        if (out->utf8) { enc = HTML_ENC_UTF_8; source = HTML_ENCODING_SOURCE_DETECTED; }
        else {
            /* mostly UTF-8, as Chromium's detector takes a file whose bytes in UTF-8's
               sequences are ten to each byte in error or more */
            size_t good = 0, bad = 0;
            enc_utf8_count(src, n, out->high, &good, &bad);
            if (good >= 10 * bad) { enc = HTML_ENC_UTF_8; source = HTML_ENCODING_SOURCE_DETECTED; }
        }
    }
    if (enc < 0 && out->high < n) {
        /* nothing declares it: the bytes' likeliest legacy encoding */
        enc = enc_detect(src + out->bom, n - out->bom, out->high);
        if (enc >= 0) source = HTML_ENCODING_SOURCE_DETECTED;
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
