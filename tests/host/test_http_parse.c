/* tests/host/test_http_parse.c -- HTTP/1.x response helpers for the browser. */
#include "pure.h"
#include "http_parse.h"
#include <assert.h>

static const char R[] =
  "HTTP/1.1 302 Found\r\nlocation: /next\r\nTransfer-Encoding: Chunked\r\n\r\n"
  "4\r\nWiki\r\n5;ext=1\r\npedia\r\n0\r\n\r\n";

int main(void){
    u32 n = (u32)strlen(R);
    assert(http_status_code(R, n) == 302);
    assert(http_status_code("garbage", 7) == 0);
    assert(http_status_code("HTTP/1.1 2", 10) == 0);             /* truncated */

    char v[64];
    assert(http_header(R, n, "Location", v, sizeof v) && !strcmp(v, "/next"));
    assert(http_header(R, n, "transfer-encoding", v, sizeof v) && !strcmp(v, "Chunked"));
    assert(!http_header(R, n, "Wiki", v, sizeof v));             /* body is not headers */
    assert(!http_header(R, n, "Locatio", v, sizeof v));          /* no prefix matches */
    char tiny[4];
    assert(http_header(R, n, "Location", tiny, sizeof tiny) && !strcmp(tiny, "/ne"));  /* bounded */
    assert(http_is_chunked(R, n));

    u32 off = http_body_offset(R, n); assert(off > 0);
    char body[128]; u32 bl = n - off; memcpy(body, R + off, bl); body[bl] = 0;
    u32 dl = http_dechunk(body, bl);
    assert(dl == 9 && !strncmp(body, "Wikipedia", 9));

    char t[] = "5\r\nhel";                                         /* truncated chunk */
    assert(http_dechunk(t, 6) == 3 && !strncmp(t, "hel", 3));
    char big[] = "FFFFFFFFFF\r\nab";                               /* absurd size: no overflow */
    assert(http_dechunk(big, (u32)strlen(big)) == 2 && !strncmp(big, "ab", 2));
    char none[] = "not chunked";
    assert(http_dechunk(none, (u32)strlen(none)) == 0);

    assert(http_body_offset("HTTP/1.0 200 OK\n\nhi", 19) == 17);  /* bare-LF servers */
    assert(http_body_offset("no terminator", 13) == 0);

    char out[256];
    http_url_resolve("https://a.com/x/y.html", "/p", out, sizeof out);      assert(!strcmp(out, "https://a.com/p"));
    http_url_resolve("https://a.com/x/y.html", "z.html", out, sizeof out);  assert(!strcmp(out, "https://a.com/x/z.html"));
    http_url_resolve("http://a.com/x", "//b.com/q", out, sizeof out);      assert(!strcmp(out, "http://b.com/q"));
    http_url_resolve("http://a.com/x?q=1", "?q=2", out, sizeof out);       assert(!strcmp(out, "http://a.com/x?q=2"));
    http_url_resolve("http://a.com/x", "https://c.org/", out, sizeof out); assert(!strcmp(out, "https://c.org/"));
    http_url_resolve("http://a.com", "", out, sizeof out);                 assert(!strcmp(out, "http://a.com"));
    char small[16];
    http_url_resolve("http://averylonghostname.example/", "/some/long/path", small, sizeof small);
    assert(strlen(small) == 15);                                          /* truncated, terminated */
    puts("http_parse OK"); return 0;
}
