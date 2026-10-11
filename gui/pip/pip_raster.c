/* gui/pip/pip_raster.c -- Pip's shape rasterizer (pure). See pip_raster.h. */
#include "pip_raster.h"

static const i32 SIN_Q16[91] = {
    0,1144,2287,3430,4572,5712,6850,7987,9121,10252,11380,12505,13626,14742,15855,16962,18064,
    19161,20252,21336,22415,23486,24550,25607,26656,27697,28729,29753,30767,31772,32768,33754,
    34729,35693,36647,37590,38521,39441,40348,41243,42126,42995,43852,44695,45525,46341,47143,
    47930,48703,49461,50203,50931,51643,52339,53020,53684,54332,54963,55578,56175,56756,57319,
    57865,58393,58903,59396,59870,60326,60764,61183,61584,61966,62328,62672,62997,63303,63589,
    63856,64104,64332,64540,64729,64898,65048,65177,65287,65376,65446,65496,65526,65536
};

i32 pr_sin(i32 deg) {
    deg %= 360;
    if (deg < 0) deg += 360;
    if (deg <= 90)  return SIN_Q16[deg];
    if (deg <= 180) return SIN_Q16[180 - deg];
    if (deg <= 270) return -SIN_Q16[deg - 180];
    return -SIN_Q16[360 - deg];
}
i32 pr_cos(i32 deg) { return pr_sin(deg + 90); }

static i32 qmul(i32 a, i32 b) { return (i32)(((i64)a * b) >> 16); }

pr_mat_t pr_identity(void) { pr_mat_t m = { 65536, 0, 0, 65536, 0, 0 }; return m; }
pr_mat_t pr_translate(i32 tx, i32 ty) { pr_mat_t m = { 65536, 0, 0, 65536, tx, ty }; return m; }
pr_mat_t pr_scale(i32 sx, i32 sy) { pr_mat_t m = { sx, 0, 0, sy, 0, 0 }; return m; }
pr_mat_t pr_rotate(i32 deg) {
    i32 s = pr_sin(deg), c = pr_cos(deg);
    pr_mat_t m = { c, s, -s, c, 0, 0 };
    return m;
}
pr_mat_t pr_mul(pr_mat_t o, pr_mat_t i) {
    pr_mat_t r;
    r.a = qmul(o.a, i.a) + qmul(o.c, i.b);
    r.b = qmul(o.b, i.a) + qmul(o.d, i.b);
    r.c = qmul(o.a, i.c) + qmul(o.c, i.d);
    r.d = qmul(o.b, i.c) + qmul(o.d, i.d);
    r.e = qmul(o.a, i.e) + qmul(o.c, i.f) + o.e;
    r.f = qmul(o.b, i.e) + qmul(o.d, i.f) + o.f;
    return r;
}
pr_mat_t pr_about(pr_mat_t m, i32 cx, i32 cy) {
    return pr_mul(pr_translate(cx, cy), pr_mul(m, pr_translate(-cx, -cy)));
}
void pr_apply(pr_mat_t m, i32 x, i32 y, i32 *ox, i32 *oy) {
    *ox = qmul(m.a, x) + qmul(m.c, y) + m.e;
    *oy = qmul(m.b, x) + qmul(m.d, y) + m.f;
}

pr_paint_t pr_solid(u32 argb) {
    pr_paint_t p;
    for (u32 i = 0; i < sizeof p; i++) ((u8 *)&p)[i] = 0;
    p.kind = PR_SOLID; p.color = argb;
    return p;
}

static u64 isqrt64(u64 v) {
    u64 r = 0, bit = (u64)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* ---- Geometry scratch: points of the current shape in design units ---- */
#define PR_MAXP 1600
#define PR_MAXS 48
static i32  g_px[PR_MAXP], g_py[PR_MAXP];
static u32  g_np;
static u32  g_sub[PR_MAXS + 1];
static bool g_closed[PR_MAXS];
static u32  g_nsub;

static bool add_pt(i32 x, i32 y) {
    if (g_np >= PR_MAXP) return false;
    g_px[g_np] = x; g_py[g_np] = y; g_np++;
    return true;
}
static bool new_sub(void) {
    if (g_nsub >= PR_MAXS) return false;
    g_sub[g_nsub] = g_np; g_closed[g_nsub] = false; g_nsub++;
    return true;
}
static void reset_geom(void) { g_np = 0; g_nsub = 0; }
static u32 sub_end(u32 s) { return s + 1 < g_nsub ? g_sub[s + 1] : g_np; }

static const char *skip(const char *s) {
    while (*s == ' ' || *s == ',' || *s == '\n' || *s == '\t' || *s == '\r') s++;
    return s;
}
static const char *num(const char *s, i32 *out) {
    s = skip(s);
    bool neg = false;
    if (*s == '-') { neg = true; s++; } else if (*s == '+') s++;
    if (!((*s >= '0' && *s <= '9') || *s == '.')) return NULL;
    i64 v = 0, frac = 0, div = 1;
    while (*s >= '0' && *s <= '9') { if (v < 100000) v = v * 10 + (*s - '0'); s++; }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { if (div < 100000) { frac = frac * 10 + (*s - '0'); div *= 10; } s++; }
    }
    i64 q = (v << 16) + (frac << 16) / div;
    *out = (i32)(neg ? -q : q);
    return s;
}

static bool parse_path(const char *d) {
    reset_geom();
    if (!d) return false;
    i32 cx = 0, cy = 0, sx = 0, sy = 0;
    char cmd = 0;
    const char *s = d;
    for (;;) {
        s = skip(s);
        if (!*s) break;
        if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) {
            cmd = *s++;
            if (cmd == 'Z' || cmd == 'z') {
                if (g_nsub) g_closed[g_nsub - 1] = true;
                cx = sx; cy = sy;
                continue;
            }
        } else if (!cmd) return false;
        u32 need = (cmd == 'M' || cmd == 'L') ? 2 : cmd == 'Q' ? 4 : cmd == 'C' ? 6 : 0;
        if (!need) return false;
        i32 v[6];
        for (u32 k = 0; k < need; k++) { s = num(s, &v[k]); if (!s) return false; }
        if (cmd == 'M') {
            if (!new_sub() || !add_pt(v[0], v[1])) return false;
            cx = sx = v[0]; cy = sy = v[1];
            cmd = 'L';                                      /* implicit lineto after M */
            continue;
        }
        if (!g_nsub) { if (!new_sub() || !add_pt(cx, cy)) return false; }
        if (cmd == 'L') {
            if (!add_pt(v[0], v[1])) return false;
            cx = v[0]; cy = v[1];
        } else if (cmd == 'Q') {
            for (i64 i = 1; i <= 12; i++) {
                i64 t = i * 65536 / 12, u = 65536 - t;
                i64 b0 = (u * u) >> 16, b1 = (2 * u * t) >> 16, b2 = (t * t) >> 16;
                if (!add_pt((i32)((b0 * cx + b1 * v[0] + b2 * v[2]) >> 16),
                            (i32)((b0 * cy + b1 * v[1] + b2 * v[3]) >> 16))) return false;
            }
            cx = v[2]; cy = v[3];
        } else {
            for (i64 i = 1; i <= 16; i++) {
                i64 t = i * 65536 / 16, u = 65536 - t;
                i64 u2 = (u * u) >> 16, t2 = (t * t) >> 16;
                i64 b0 = (u2 * u) >> 16, b1 = (3 * u2 * t) >> 16, b2 = (3 * u * t2) >> 16, b3 = (t2 * t) >> 16;
                if (!add_pt((i32)((b0 * cx + b1 * v[0] + b2 * v[2] + b3 * v[4]) >> 16),
                            (i32)((b0 * cy + b1 * v[1] + b2 * v[3] + b3 * v[5]) >> 16))) return false;
            }
            cx = v[4]; cy = v[5];
        }
    }
    return g_np > 0;
}

static void ellipse_pts(i32 cx, i32 cy, i32 rx, i32 ry, i32 rot) {
    reset_geom();
    new_sub();
    g_closed[0] = true;
    i32 cr = pr_cos(rot), sr = pr_sin(rot);
    for (i32 a = 0; a < 360; a += 5) {
        i32 ex = qmul(rx, pr_cos(a)), ey = qmul(ry, pr_sin(a));
        add_pt(cx + qmul(ex, cr) - qmul(ey, sr), cy + qmul(ex, sr) + qmul(ey, cr));
    }
}

static void rrect_pts(i32 x, i32 y, i32 w, i32 h, i32 r) {
    reset_geom();
    new_sub();
    g_closed[0] = true;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    i32 ccx[4] = { x + w - r, x + w - r, x + r, x + r };
    i32 ccy[4] = { y + r, y + h - r, y + h - r, y + r };
    i32 a0[4] = { 270, 0, 90, 180 };
    for (u32 k = 0; k < 4; k++)
        for (i32 a = 0; a <= 90; a += 15)
            add_pt(ccx[k] + qmul(r, pr_cos(a0[k] + a)), ccy[k] + qmul(r, pr_sin(a0[k] + a)));
}

/* Design-space bounding box of the current points (for gradients). */
static void geom_bbox(i32 *x0, i32 *y0, i32 *x1, i32 *y1) {
    *x0 = *y0 = 0x7fffffff; *x1 = *y1 = -0x7fffffff;
    for (u32 i = 0; i < g_np; i++) {
        if (g_px[i] < *x0) *x0 = g_px[i];
        if (g_px[i] > *x1) *x1 = g_px[i];
        if (g_py[i] < *y0) *y0 = g_py[i];
        if (g_py[i] > *y1) *y1 = g_py[i];
    }
}

/* ---- Edges in canvas space, Q8 (1/256 px) ---- */
#define PR_MAXE 6000
static i32 e_x0[PR_MAXE], e_y0[PR_MAXE], e_x1[PR_MAXE], e_y1[PR_MAXE];
static i8  e_dir[PR_MAXE];
static u32 g_ne;
static i32 g_ymin, g_ymax;

static void edge(i32 x0, i32 y0, i32 x1, i32 y1) {
    if (y0 == y1 || g_ne >= PR_MAXE) return;
    i8 dir = 1;
    if (y0 > y1) { i32 t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1; }
    e_x0[g_ne] = x0; e_y0[g_ne] = y0; e_x1[g_ne] = x1; e_y1[g_ne] = y1; e_dir[g_ne] = dir; g_ne++;
    if (y0 < g_ymin) g_ymin = y0;
    if (y1 > g_ymax) g_ymax = y1;
}
static void edges_reset(void) { g_ne = 0; g_ymin = 0x7fffffff; g_ymax = -0x7fffffff; }

static i32 to_q8(i32 v16) { return v16 >> 8; }

/* A polygon (canvas Q8 points), oriented so that every polygon winds the
 * same way: overlapping pieces then union under the non-zero rule. */
static void poly(const i32 *xs, const i32 *ys, u32 n) {
    if (n < 3) return;
    i64 area = 0;
    for (u32 i = 0; i < n; i++) {
        u32 j = (i + 1) % n;
        area += (i64)xs[i] * ys[j] - (i64)xs[j] * ys[i];
    }
    for (u32 i = 0; i < n; i++) {
        u32 j = (i + 1) % n;
        if (area >= 0) edge(xs[i], ys[i], xs[j], ys[j]);
        else edge(xs[j], ys[j], xs[i], ys[i]);
    }
}

/* Fill edges from the current geometry (each subpath closed). */
static void edges_from_fill(pr_mat_t m) {
    edges_reset();
    for (u32 s = 0; s < g_nsub; s++) {
        u32 a = g_sub[s], b = sub_end(s);
        if (b - a < 2) continue;
        i32 px0, py0, pxp, pyp;
        pr_apply(m, g_px[a], g_py[a], &px0, &py0);
        pxp = px0; pyp = py0;
        for (u32 i = a + 1; i < b; i++) {
            i32 x, y; pr_apply(m, g_px[i], g_py[i], &x, &y);
            edge(to_q8(pxp), to_q8(pyp), to_q8(x), to_q8(y));
            pxp = x; pyp = y;
        }
        edge(to_q8(pxp), to_q8(pyp), to_q8(px0), to_q8(py0));
    }
}

/* Device stroke half-width in Q8 from a design width (Q16). */
static i32 half_width_q8(pr_mat_t m, i32 width) {
    i64 det = (i64)m.a * m.d - (i64)m.b * m.c;            /* Q32 */
    if (det < 0) det = -det;
    i32 scale = (i32)isqrt64((u64)det);                   /* Q16 */
    return qmul(width, scale) >> 9;                       /* /2 and Q16 -> Q8 */
}

static void circle_poly(i32 cx, i32 cy, i32 r) {
    i32 xs[16], ys[16];
    for (u32 k = 0; k < 16; k++) {
        xs[k] = cx + qmul(r, pr_cos((i32)k * 360 / 16));
        ys[k] = cy + qmul(r, pr_sin((i32)k * 360 / 16));
    }
    poly(xs, ys, 16);
}

static void seg_quad(i32 x0, i32 y0, i32 x1, i32 y1, i32 hw) {
    i64 dx = x1 - x0, dy = y1 - y0;
    i64 len = (i64)isqrt64((u64)(dx * dx + dy * dy));
    if (len == 0) return;
    i32 nx = (i32)(-dy * hw / len), ny = (i32)(dx * hw / len);
    i32 xs[4] = { x0 + nx, x1 + nx, x1 - nx, x0 - nx };
    i32 ys[4] = { y0 + ny, y1 + ny, y1 - ny, y0 - ny };
    poly(xs, ys, 4);
}

static void edges_from_stroke(pr_mat_t m, i32 width) {
    edges_reset();
    i32 hw = half_width_q8(m, width);
    if (hw < 32) hw = 32;                                 /* never thinner than 1/4 px */
    for (u32 s = 0; s < g_nsub; s++) {
        u32 a = g_sub[s], b = sub_end(s);
        if (b <= a) continue;
        i32 fx, fy, px_, py_;
        pr_apply(m, g_px[a], g_py[a], &fx, &fy);
        fx = to_q8(fx); fy = to_q8(fy);
        px_ = fx; py_ = fy;
        circle_poly(fx, fy, hw);
        for (u32 i = a + 1; i < b; i++) {
            i32 x, y; pr_apply(m, g_px[i], g_py[i], &x, &y);
            x = to_q8(x); y = to_q8(y);
            seg_quad(px_, py_, x, y, hw);
            circle_poly(x, y, hw);
            px_ = x; py_ = y;
        }
        if (g_closed[s]) seg_quad(px_, py_, fx, fy, hw);
    }
}

/* ---- Scanline fill and compositing ---- */
#define PR_SS     5
#define PR_MAXW   1024
#define PR_MAXX   512
static i32 g_acc[PR_MAXW];
static i32 g_xs[PR_MAXX];
static i8  g_xd[PR_MAXX];

static u32 lerp_argb(u32 a, u32 b, u32 t /*0..65536*/) {
    u32 r = 0;
    for (u32 sh = 0; sh < 32; sh += 8) {
        i32 ca = (i32)((a >> sh) & 255), cb = (i32)((b >> sh) & 255);
        r |= (u32)(ca + (((cb - ca) * (i32)t) >> 16)) << sh;
    }
    return r;
}

typedef struct { pr_mat_t inv; bool ok; i32 gx, gy, rx, ry; } pr_grad_t;

static bool invert(pr_mat_t m, pr_mat_t *out) {
    i64 det = ((i64)m.a * m.d - (i64)m.b * m.c) >> 16;    /* Q16 */
    if (det == 0) return false;
    pr_mat_t r;
    r.a = (i32)(((i64)m.d * 65536) / det);
    r.b = (i32)(((i64)-m.b * 65536) / det);
    r.c = (i32)(((i64)-m.c * 65536) / det);
    r.d = (i32)(((i64)m.a * 65536) / det);
    r.e = -(qmul(r.a, m.e) + qmul(r.c, m.f));
    r.f = -(qmul(r.b, m.e) + qmul(r.d, m.f));
    *out = r;
    return true;
}

static u32 paint_at(const pr_paint_t *p, const pr_grad_t *g, i32 px, i32 py) {
    if (p->kind != PR_RADIAL || !g->ok || p->nstops == 0) return p->kind == PR_RADIAL ? (p->nstops ? p->stops[0].argb : 0) : p->color;
    i32 dx, dy;
    pr_apply(g->inv, (px << 16) + 32768, (py << 16) + 32768, &dx, &dy);
    i64 nx = g->rx ? ((i64)(dx - g->gx) * 65536) / g->rx : 0;
    i64 ny = g->ry ? ((i64)(dy - g->gy) * 65536) / g->ry : 0;
    u64 t = isqrt64((u64)(nx * nx + ny * ny));            /* Q16 */
    if (t >= 65535) return p->stops[p->nstops - 1].argb;
    if (t <= p->stops[0].pos) return p->stops[0].argb;
    for (u32 i = 1; i < p->nstops; i++) {
        u32 a = p->stops[i - 1].pos, b = p->stops[i].pos;
        if (t <= b) {
            u32 f = b > a ? (u32)(((t - a) << 16) / (b - a)) : 65536;
            return lerp_argb(p->stops[i - 1].argb, p->stops[i].argb, f);
        }
    }
    return p->stops[p->nstops - 1].argb;
}

static void blend(u32 *d, u32 col, u32 a) {
    if (!a) return;
    if (a > 255) a = 255;
    u32 inv = 255 - a, dv = *d;
    u32 sr = ((col >> 16) & 255) * a / 255, sg = ((col >> 8) & 255) * a / 255, sb = (col & 255) * a / 255;
    u32 da = dv >> 24, dr = (dv >> 16) & 255, dg = (dv >> 8) & 255, db = dv & 255;
    *d = ((a + da * inv / 255) << 24) | ((sr + dr * inv / 255) << 16) | ((sg + dg * inv / 255) << 8) | (sb + db * inv / 255);
}

static void add_span(i32 xa, i32 xb, i32 w) {
    if (xa < 0) xa = 0;
    if (xb > w * 256) xb = w * 256;
    if (xb <= xa) return;
    i32 ia = xa >> 8, ib = xb >> 8;
    if (ia == ib) { g_acc[ia] += xb - xa; return; }
    g_acc[ia] += 256 - (xa & 255);
    for (i32 i = ia + 1; i < ib && i < w; i++) g_acc[i] += 256;
    if (ib < w) g_acc[ib] += xb & 255;
}

static void raster(pr_canvas_t *c, const pr_paint_t *p, u8 opacity, const pr_grad_t *g) {
    if (!g_ne || !c || !c->px || c->w <= 0 || c->h <= 0 || c->w > PR_MAXW) return;
    i32 y0 = g_ymin >> 8, y1 = (g_ymax - 1) >> 8;
    if (y0 < 0) y0 = 0;
    if (y1 >= c->h) y1 = c->h - 1;
    for (i32 py = y0; py <= y1; py++) {
        for (i32 i = 0; i < c->w; i++) g_acc[i] = 0;
        bool any = false;
        for (i32 s = 0; s < PR_SS; s++) {
            i32 sy = py * 256 + (2 * s + 1) * 128 / PR_SS;
            u32 n = 0;
            for (u32 e = 0; e < g_ne && n < PR_MAXX; e++) {
                if (sy < e_y0[e] || sy >= e_y1[e]) continue;
                i32 x = e_x0[e] + (i32)(((i64)(sy - e_y0[e]) * (e_x1[e] - e_x0[e])) / (e_y1[e] - e_y0[e]));
                u32 k = n++;
                while (k > 0 && g_xs[k - 1] > x) { g_xs[k] = g_xs[k - 1]; g_xd[k] = g_xd[k - 1]; k--; }
                g_xs[k] = x; g_xd[k] = e_dir[e];
            }
            i32 wind = 0, start = 0;
            for (u32 k = 0; k < n; k++) {
                i32 before = wind;
                wind += g_xd[k];
                if (before == 0 && wind != 0) start = g_xs[k];
                else if (before != 0 && wind == 0) { add_span(start, g_xs[k], c->w); any = true; }
            }
        }
        if (!any) continue;
        u32 *row = c->px + py * c->w;
        for (i32 px = 0; px < c->w; px++) {
            if (!g_acc[px]) continue;
            u32 cov = (u32)g_acc[px] * 255 / (256 * PR_SS);
            if (cov > 255) cov = 255;
            u32 col = paint_at(p, g, px, py);
            u32 a = ((col >> 24) * cov / 255) * opacity / 255;
            blend(&row[px], col, a);
        }
    }
}

static void grad_setup(const pr_paint_t *p, pr_mat_t m, pr_grad_t *g) {
    g->ok = false;
    if (p->kind != PR_RADIAL) return;
    i32 x0, y0, x1, y1;
    geom_bbox(&x0, &y0, &x1, &y1);
    i32 bw = x1 - x0, bh = y1 - y0;
    g->gx = x0 + qmul(bw, p->cx);
    g->gy = y0 + qmul(bh, p->cy);
    g->rx = qmul(bw, p->r);
    g->ry = qmul(bh, p->r);
    g->ok = invert(m, &g->inv) && g->rx > 0 && g->ry > 0;
}

static void fill_current(pr_canvas_t *c, pr_mat_t m, const pr_paint_t *p, u8 opacity) {
    pr_grad_t g;
    grad_setup(p, m, &g);
    edges_from_fill(m);
    raster(c, p, opacity, &g);
}

void pr_fill_ellipse(pr_canvas_t *c, pr_mat_t m, i32 cx, i32 cy, i32 rx, i32 ry, i32 rot,
                     const pr_paint_t *p, u8 opacity) {
    ellipse_pts(cx, cy, rx, ry, rot);
    fill_current(c, m, p, opacity);
}

void pr_stroke_ellipse(pr_canvas_t *c, pr_mat_t m, i32 cx, i32 cy, i32 rx, i32 ry, i32 rot,
                       i32 width, const pr_paint_t *p, u8 opacity) {
    ellipse_pts(cx, cy, rx, ry, rot);
    edges_from_stroke(m, width);
    pr_grad_t g; g.ok = false;
    raster(c, p, opacity, &g);
}

void pr_fill_rrect(pr_canvas_t *c, pr_mat_t m, i32 x, i32 y, i32 w, i32 h, i32 r,
                   const pr_paint_t *p, u8 opacity) {
    rrect_pts(x, y, w, h, r);
    fill_current(c, m, p, opacity);
}

bool pr_fill_path(pr_canvas_t *c, pr_mat_t m, const char *d, const pr_paint_t *p, u8 opacity) {
    if (!parse_path(d)) return false;
    fill_current(c, m, p, opacity);
    return true;
}

bool pr_stroke_path(pr_canvas_t *c, pr_mat_t m, const char *d, i32 width, const pr_paint_t *p, u8 opacity) {
    if (!parse_path(d)) return false;
    edges_from_stroke(m, width);
    pr_grad_t g; g.ok = false;
    raster(c, p, opacity, &g);
    return true;
}

void pr_unpremultiply(pr_canvas_t *c) {
    for (i32 i = 0; i < c->w * c->h; i++) {
        u32 v = c->px[i], a = v >> 24;
        if (a == 0 || a == 255) continue;
        u32 r = ((v >> 16) & 255) * 255 / a, g = ((v >> 8) & 255) * 255 / a, b = (v & 255) * 255 / a;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        c->px[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}
