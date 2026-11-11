/* src/html/encoding.h -- the WHATWG Encoding standard's decoders and HTML's encoding
   sniffing (HTML 13.2.3.2), src/html/encoding.c's. A unit includes it after
   <vector.pl>, amb_interface(dom) and amb_interface(html), whose types it names.

   An encoding is a number: its place in the standard's table (enum html_enc of
   src/html/encoding_tables.h, which encoding.c alone includes), or -1 for none. A
   caller names one by a label, as a page does. */
#ifndef AMBITIOUS_HTML_ENCODING_H
#define AMBITIOUS_HTML_ENCODING_H

/* a document's encoding, as the encoding sniffing algorithm finds it from its bytes,
   the media type it was served as, and whether it is a file of the local file system */
typedef struct html_sniffed {
    int enc;
    int source;          /* where it came from: an html_encoding_source */
    size_t bom;          /* the byte order mark's length, which decoding skips */
    size_t high;         /* where the first byte from 0x80 up is, past the byte order mark */
    int utf8;            /* the bytes past the mark checked: 1 UTF-8, 0 not; -1 not checked */
} html_sniffed;
void html_sniff(const char *src, size_t n, const char *type, size_t type_len, int file, html_sniffed *out);

/* the HTML document of the bytes src, served as type (a file of the local file system
   when file is 1): their encoding sniffed and recorded in p, the bytes decoded to UTF-8
   and parsed into doc as html_parse parses them */
html_status html_parse_bytes(html_parser *p, dom_document *doc, const char *src, size_t src_len,
                             const char *type, size_t type_len, int file);

/* the encoding a label names (ASCII white space trimmed, ASCII case ignored), or -1 */
int html_enc_of_label(const char *label, size_t n);

/* an encoding's name, as the standard writes it: "UTF-8", "Shift_JIS", "windows-1252" */
const char *html_enc_name(int enc);

/* the encoding a media type's charset parameter names (MIME Sniffing's parsing of a
   MIME type), or -1: a Content-Type, or a data: URL's type */
int html_enc_of_type(const char *type, size_t n);

/* the prescan of src[0..n) (n at most 1024) for the encoding a meta element declares,
   else an XML declaration's, else -1; *xml is 1 when it is the XML declaration's */
int html_prescan(const char *src, size_t n, int *xml);

/* src decoded by enc's decoder, each error a U+FFFD, appended to out as UTF-8; the
   number of errors. No byte order mark is looked for: html_sniff finds it. A unit
   that calls it says HTML_DECODE_DECL; at file scope: cicilang 0.284 drops a header's
   declaration that names a vector<T> (toolchain finding), and the macro puts the
   declaration in the unit's own text. */
#define HTML_DECODE_DECL size_t html_decode(int enc, const char *src, size_t n, vector<char> *out)

#endif
