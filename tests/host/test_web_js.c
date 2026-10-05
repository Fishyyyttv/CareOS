/* End-to-end tests of scripts, DOM lifetime, forms, timers and JS budgets. */
#include "engine.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

#ifdef CAREOS_WEB_TEST
#undef assert
#define assert(expr) do { if (!(expr)) { serial_write("[web-test] FAIL " #expr "\n"); outl(0xf4, 1); for (;;) __asm__ volatile("cli; hlt"); } } while (0)
#define main web_test_main
#endif

static int errors;
static uint64_t now_ms(void *unused) {
#ifdef CAREOS_WEB_TEST
    return timer_ms();
#else
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
#endif
}
static void log_line(void *unused, const char *s) {
    if (strstr(s, "js error") || strstr(s, "js: error:")) errors++;
#ifdef CAREOS_WEB_TEST
    serial_write(s); serial_write("\n");
#else
    fprintf(stderr, "%s\n", s);
#endif
}
static unsigned char *fetch(void *unused, const char *url, size_t *len, char *final, size_t cap) {
    assert(!strcmp(url, "https://test.local/external.js"));
    const char *script = "document.title = 'external-ok';";
    *len = strlen(script);
    return (unsigned char *)strdup(script);
}
static page_t *load(const char *html) {
    page_t *pg = web_page_load("https://test.local/", html, strlen(html), 800, 600);
    assert(pg && pg->js && pg->root);
    return pg;
}
static box_t *find_box(box_t *b, const char *id, float *x, float *y) {
    if (!b) return NULL;
    if (b->el) {
        size_t n; const char *s = (const char *)lxb_dom_element_get_attribute(b->el, (const lxb_char_t *)"id", 2, &n);
        if (s && n == strlen(id) && !memcmp(s, id, n)) return b;
    }
    for (box_t *c = b->first; c; c = c->next) {
        float cx = *x + c->x + c->rel_x, cy = *y + c->y + c->rel_y;
        box_t *r = find_box(c, id, &cx, &cy);
        if (r) { *x = cx; *y = cy; return r; }
    }
    return NULL;
}
static int click(page_t *pg, const char *id, char *url) {
    float x = pg->root->x, y = pg->root->y;
    box_t *b = find_box(pg->root, id, &x, &y); assert(b);
    return web_page_click(pg, (int)(x + b->w / 2), (int)(y + b->h / 2), url, 2048);
}

int main(void) {
    web_platform_t platform = { .fetch = fetch, .now_ms = now_ms, .log = log_line };
    web_init(&platform);
    char url[2048];
    page_t *pg = load(
        "<!doctype html><style>.late {width:237px} button,input {display:block}</style>"
        "<div id=target><span>old</span></div><button id=btn>Go</button>"
        "<form id=form action=/search><input id=input name=q></form><p id=out></p>"
        "<script>"
        "function check(v){if(!v)throw Error('assertion failed')}"
        "const t=document.getElementById('target'), old=t.firstChild;"
        "t.innerHTML='<b>new</b>'; check(old.textContent==='old');"
        "old.textContent='retained'; t.appendChild(old); check(t.lastChild===old);"
        "const clone=t.cloneNode(true);check(clone!==t && clone.lastChild!==old);"
        "clone.lastChild.textContent='clone';check(old.textContent==='retained');"
        "t.replaceChild(old,old); check(t.lastChild===old);"
        "let cycle=false;try{old.appendChild(t)}catch(e){cycle=true}check(cycle);"
        "t.className='late';"
        "const b=document.getElementById('btn');"
        "b.onclick=()=>{const held=t.firstChild;t.textContent='clicked';check(held.textContent==='new');};"
        "const input=document.getElementById('input');"
        "input.oninput=()=>{document.getElementById('out').textContent=input.value};"
        "document.getElementById('form').onsubmit=e=>{e.preventDefault();document.title='submitted-'+input.value};"
        "check(JSON.parse('1.25e2')===125); check(Math.sqrt(4)===2);"
        "check(Object.is(Math.trunc(-0),-0));check(Number.isNaN(Math.sqrt(-1)));"
        "let oom=false;try{new ArrayBuffer(80*1024*1024)}catch(e){oom=true}check(oom);"
        "document.addEventListener('DOMContentLoaded',()=>{document.title='load-ok'});"
        "</script>");
    assert(!strcmp(web_page_title(pg), "load-ok") && errors == 0);
    float x=0,y=0; box_t *t = find_box(pg->root, "target", &x, &y);
    assert(t && t->w == 237);
    assert(click(pg, "btn", url) == WEB_ACT_REPAINT);
    assert(errors == 0);
    click(pg, "input", url); assert(web_page_focused(pg));
    assert(web_page_key(pg, 'a', url, sizeof url) == WEB_ACT_REPAINT);
    assert(web_page_focused(pg));
    assert(web_page_key(pg, 'b', url, sizeof url) == WEB_ACT_REPAINT);
    assert(web_page_focused(pg));
    assert(web_page_key(pg, WEB_KEY_ENTER, url, sizeof url) != WEB_ACT_NAVIGATE);
    assert(!strcmp(web_page_title(pg), "submitted-ab") && errors == 0);
    web_page_free(pg);

    pg = load("<script src=external.js></script>");
    assert(!strcmp(web_page_title(pg), "external-ok")); web_page_free(pg);
    pg = load("<p id=t>before</p><script>setTimeout(()=>{document.getElementById('t').textContent='after';"
              "document.title='timer-ok';scrollTo(0,80);location.href='/next'},0)</script>");
    assert(web_page_step(pg));
    assert(!strcmp(web_page_title(pg), "timer-ok"));
    assert(web_page_take_scroll(pg) == 80 && web_page_take_scroll(pg) == -1);
    assert(web_page_take_navigation(pg, url, sizeof url));
    assert(!strcmp(url, "https://test.local/next"));
    assert(!web_page_take_navigation(pg, url, sizeof url)); web_page_free(pg);

    /* repeated selector queries reuse one parser (regression: the second
     * parse used a memory pool freed by the first -- crashed bbc.com) */
    pg = load("<ul><li class=a>1</li><li class=b>2</li></ul><script>"
              "let n=0;for(let i=0;i<50;i++){n+=document.querySelectorAll('li').length;"
              "n+=document.querySelector('.b')?1:0;n+=document.body.matches('body')?1:0;"
              "document.querySelectorAll('[[bad');}"
              "document.title='q'+n</script>");
    assert(!strcmp(web_page_title(pg), "q200") && errors == 0); web_page_free(pg);

    pg = load("<script>setTimeout(()=>{Promise.resolve().then(()=>{while(true){}})},0)</script>");
    uint64_t start = now_ms(NULL);
    web_page_step(pg);
    assert(now_ms(NULL)-start < 2000 && errors == 1);
    web_page_free(pg);
    puts("PASS: JavaScript DOM lifetime, forms, lifecycle, external scripts, timers, navigation and timeouts");
    return 0;
}
