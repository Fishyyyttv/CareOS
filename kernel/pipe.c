/* CareOS v9 -- kernel/pipe.c -- Buffered sequential pipe */
#include "kernel.h"

static pipe_t pipe_pool[8];
static bool   pipe_used[8];

pipe_t *pipe_create(void) {
    for (int i = 0; i < 8; i++) {
        if (!pipe_used[i]) {
            pipe_used[i] = true;
            kmemset(&pipe_pool[i], 0, sizeof(pipe_t));
            return &pipe_pool[i];
        }
    }
    return NULL;
}

int pipe_write(pipe_t *p, const char *data, u32 len) {
    if (!p || p->closed) return -1;
    u32 space = PIPE_BUF_SIZE - p->len - 1;
    if (len > space) len = space;
    kmemcpy(p->buf + p->len, data, len);
    p->len += len;
    p->buf[p->len] = '\0';
    return (int)len;
}

int pipe_read(pipe_t *p, char *out, u32 maxlen) {
    if (!p) return -1;
    u32 n = p->len < maxlen - 1 ? p->len : maxlen - 1;
    kmemcpy(out, p->buf, n);
    out[n] = '\0';
    return (int)n;
}

void pipe_close(pipe_t *p) {
    if (!p) return;
    for (int i = 0; i < 8; i++) {
        if (&pipe_pool[i] == p) { pipe_used[i] = false; break; }
    }
    p->closed = true;
}

void pipe_reset(pipe_t *p) {
    if (!p) return;
    p->len    = 0;
    p->closed = false;
    p->buf[0] = '\0';
}
