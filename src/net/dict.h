/* src/net/dict.h -- Compression Dictionary Transport (RFC 9842) as Chromium has it: what the
   loader (src/net/net.c) asks of a dictionary, src/net/dict.c's. SHA-256, a dictionary's name;
   Use-As-Dictionary read; its match, a URL pattern, held to the dictionary's URL and matched
   against a request's. The decoders with a dictionary are the painter's Brotli
   (src/paint/brotli.h) and src/net/zstd.h's. */
#ifndef NET_DICT_H
#define NET_DICT_H
#include <stdint.h>
#include <stddef.h>

/* SHA-256 (FIPS 180-4) of the n bytes at p, into out */
void net_sha256(const uint8_t *p, size_t n, uint8_t *out);

/* a Use-As-Dictionary field read (RFC 9842, 2.1, a structured field dictionary of RFC 9651):
   its match (a string), its match-dest (an inner list of strings: each followed by a
   newline in dest, has_dest when the list was given), its id (a string, empty for none) */
typedef struct net_dict_field {
    char match[2048];
    char dest[256];
    int has_dest;
    char id[1040];
} net_dict_field;

/* the field's value (n bytes) read into f: 1 when it is a dictionary's, as Chromium takes
   one: a structured field dictionary whose match is a string, whose match-dest, when
   given, is an inner list of strings, whose id, when given, a string of at most 1024
   characters, and whose type, when given, the token raw; 0 when it is none */
int net_dict_field_read(const char *v, size_t n, net_dict_field *f);

/* a match pattern (a URL pattern's constructor string, of the URL Pattern standard) against
   the URL of the dictionary that names it: 1 when it is one Chromium keeps -- of the
   dictionary's own origin (a pattern of a path alone is; one written with a scheme and host
   must name the same), with no regular expression in it (a group in parentheses); 0 when
   not. And whether the URL url matches it: its path the pattern's pathname, its query the
   pattern's search (any when the pattern names none), of its origin. Of the pattern syntax,
   a literal character, one escaped by a backslash, `*' (anything, `/' too, or nothing) and a
   named group `:name' (one character or more, no `/', in a path) are read; `{', a modifier
   after a named group and a group in parentheses make a pattern that is not kept */
int net_dict_pattern_ok(const char *pattern, const char *dict_url);
int net_dict_pattern_matches(const char *pattern, const char *dict_url, const char *url);

#endif
