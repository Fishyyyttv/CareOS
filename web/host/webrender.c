/* web/host/webrender.c -- host harness: render a URL or HTML file to PNG.
 *
 *   webrender <url|file.html> <out.png> [width=1024] [viewport_h=768] [max_h=4000]
 *
 * Fetches with curl(1); fonts are read from ../../fonts. Build: web/host/build.sh */
#define _POSIX_C_SOURCE 200809L
#include "../engine/web.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

static const char *g_font_dir = "fonts";
static unsigned char *g_fonts[WEB_FONT_COUNT];
static size_t g_font_len[WEB_FONT_COUNT];
static int g_fetches;

static unsigned char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    b[n] = 0; fclose(f);
    *len = (size_t)n;
    return b;
}

static unsigned char *h_fetch(void *ctx, const char *url, size_t *len, char *final_url, size_t cap) {
    (void)ctx;
    if (strncmp(url, "file://", 7) == 0) return read_file(url + 7, len);
    if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) return NULL;
    char cmd[4600], esc[4096]; size_t o = 0;
    for (const char *p = url; *p && o + 4 < sizeof esc; p++) { if (*p == '\'') { memcpy(esc + o, "%27", 3); o += 3; } else esc[o++] = *p; }
    esc[o] = 0;
    char tmp[64]; snprintf(tmp, sizeof tmp, "/tmp/webrender_%d.bin", g_fetches++);
    snprintf(cmd, sizeof cmd, "curl -sL --compressed --max-time 20 -A 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36' -o %s -w '%%{url_effective}' '%s'", tmp, esc);
    FILE *p = popen(cmd, "r");
    if (!p) return NULL;
    char eff[4096] = {0};
    size_t n = fread(eff, 1, sizeof eff - 1, p);
    eff[n] = 0;
    pclose(p);
    if (final_url && cap) snprintf(final_url, cap, "%s", eff);
    unsigned char *b = read_file(tmp, len);
    remove(tmp);
    fprintf(stderr, "  fetch %s -> %zu bytes\n", url, b ? *len : 0);
    return b;
}

static const unsigned char *h_font(void *ctx, int face, size_t *len) {
    (void)ctx;
    static const char *names[WEB_FONT_COUNT] = {
        "IBMPlexSans-Regular.ttf", "IBMPlexSans-Bold.ttf", "IBMPlexMono-Regular.ttf", "IBMPlexMono-Bold.ttf" };
    if (face < 0 || face >= WEB_FONT_COUNT) return NULL;
    if (!g_fonts[face]) {
        char path[512]; snprintf(path, sizeof path, "%s/%s", g_font_dir, names[face]);
        g_fonts[face] = read_file(path, &g_font_len[face]);
    }
    *len = g_font_len[face];
    return g_fonts[face];
}

static void h_log(void *ctx, const char *m) { (void)ctx; fprintf(stderr, "[web] %s\n", m); }

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: webrender <url|file> <out.png> [w] [vh] [maxh]\n"); return 1; }
    const char *src = argv[1], *out = argv[2];
    int w = argc > 3 ? atoi(argv[3]) : 1024, vh = argc > 4 ? atoi(argv[4]) : 768;
    int maxh = argc > 5 ? atoi(argv[5]) : 4000;
    const char *fd = getenv("WEB_FONT_DIR"); if (fd) g_font_dir = fd;
    web_platform_t p = { NULL, h_fetch, h_font, NULL, h_log };
    web_init(&p);
    size_t len = 0;
    char final_url[4096];
    unsigned char *html;
    double t0 = now();
    if (strncmp(src, "http", 4) == 0) { html = h_fetch(NULL, src, &len, final_url, sizeof final_url); }
    else { html = read_file(src, &len); snprintf(final_url, sizeof final_url, "file://%s", src); }
    if (!html) { fprintf(stderr, "cannot load %s\n", src); return 1; }
    double t1 = now();
    web_page_t *pg = web_page_load(final_url, (const char *)html, len, w, vh);
    double t2 = now();
    int pending = web_page_load_images(pg, 0);
    double t3 = now();
    web_page_relayout_if_needed(pg, w, vh);
    double t4 = now();
    int H = web_page_height(pg);
    int h = H < maxh ? H : maxh;
    if (h < 1) h = 1;
    uint32_t *px = malloc((size_t)w * h * 4);
    web_page_paint(pg, px, w, h, w, 0, 0);
    double t5 = now();
    /* ARGB -> RGBA bytes */
    unsigned char *rgb = malloc((size_t)w * h * 3);
    for (size_t i = 0; i < (size_t)w * h; i++) { uint32_t c = px[i]; rgb[i*3] = c >> 16; rgb[i*3+1] = c >> 8; rgb[i*3+2] = c; }
    stbi_write_png(out, w, h, 3, rgb, w * 3);
    fprintf(stderr, "title: %s\ndoc %dx%d  fetch %.0fms  load+layout %.0fms  images %.0fms (%d pending)  relayout %.0fms  paint %.0fms\n",
            web_page_title(pg), web_page_width(pg), H, (t1-t0)*1e3, (t2-t1)*1e3, (t3-t2)*1e3, pending, (t4-t3)*1e3, (t5-t4)*1e3);
    web_page_free(pg);
    free(px); free(rgb); free(html);
    return 0;
}
