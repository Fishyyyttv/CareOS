/* apps/http_parse.h -- HTTP/1.x response helpers for the browser (pure). */
#ifndef CAREOS_HTTP_PARSE_H
#define CAREOS_HTTP_PARSE_H
#include "pure.h"

/* "HTTP/1.1 302 Found..." -> 302; 0 if resp does not start with a status line. */
int  http_status_code(const char *resp, u32 len);
/* Index just past the header terminator (CRLFCRLF, or LFLF); 0 if none. */
u32  http_body_offset(const char *resp, u32 len);
/* Case-insensitive header lookup within the header block only. The value is
 * trimmed and copied into out (always NUL-terminated, truncated to fit). */
bool http_header(const char *resp, u32 len, const char *name, char *out, u32 out_max);
/* Transfer-Encoding includes "chunked" (any case). */
bool http_is_chunked(const char *resp, u32 len);
/* Decode a chunked body in place; tolerates truncation and absurd sizes.
 * body must have len+1 bytes (the result is NUL-terminated). */
u32  http_dechunk(char *body, u32 len);
/* Resolve href against base into an absolute http(s) URL: absolute,
 * protocol-relative, absolute-path, query-only and relative forms. The
 * result is always NUL-terminated and truncated to max. */
void http_url_resolve(const char *base, const char *href, char *out, u32 max);

#endif
