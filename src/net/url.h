/* src/net/url.h -- URLs as the network module reads them (src/net/net.c): a
   WHATWG subset. A unit's own use besides the module's operations: resolving a
   reference against a base, and an origin. Each writes a NUL-terminated string
   into out and answers 1, or 0 when the URL is not one it reads. */
#ifndef AMB_URL_H
#define AMB_URL_H
#include <stddef.h>

/* ref resolved against base (an absolute URL), serialized without its fragment */
int net_url_resolve(const char *base, const char *ref, char *out, size_t cap);

/* the origin of an absolute URL, serialized: scheme://host[:port], or null for an opaque one */
int net_url_origin(const char *url, char *out, size_t cap);

#endif
