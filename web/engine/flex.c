/* web/engine/flex.c -- flexbox and (common-subset) grid layout. */
#include "engine.h"

static inline float hpb(const box_t *b) { return b->p[1] + b->p[3] + b->b[1] + b->b[3]; }
static inline float vpb(const box_t *b) { return b->p[0] + b->p[2] + b->b[0] + b->b[2]; }

typedef struct {
    box_t *b;
    float base, hyp, target;   /* main sizes (border box) */
    float minm, maxm;          /* main-axis clamps (border box) */
    float cross;               /* border-box cross size */
    float mm0, mm1, cm0, cm1;  /* margins: main start/end, cross start/end */
    u8 auto0, auto1, frozen;
} fitem_t;

static int cmp_order(const void *a, const void *b) {
    const fitem_t *x = (const fitem_t *)a, *y = (const fitem_t *)b;
    if (x->b->st->order != y->b->st->order) return x->b->st->order < y->b->st->order ? -1 : 1;
    return (x->b->list_index > y->b->list_index) - (x->b->list_index < y->b->list_index);
}

void flex_intrinsic(page_t *pg, box_t *b, float *minw, float *maxw) {
    bool row = b->st->flex_dir <= 1;
    float mn = 0, mx = 0; int n = 0;
    for (box_t *c = b->first; c; c = c->next) {
        if (c->is_abs) continue;
        float a, z; box_intrinsic(pg, c, &a, &z);
        float m = (c->st->margin[1].u == L_PX ? c->st->margin[1].v : 0) + (c->st->margin[3].u == L_PX ? c->st->margin[3].v : 0);
        if (row) {
            mx += z + m;
            if (b->st->flex_wrap) { if (a + m > mn) mn = a + m; }
            else mn += a + m;
        } else {
            if (z + m > mx) mx = z + m;
            if (a + m > mn) mn = a + m;
        }
        n++;
    }
    if (row && n > 1) { mx += b->st->col_gap * (n - 1); if (!b->st->flex_wrap) mn += b->st->col_gap * (n - 1); }
    *minw = mn; *maxw = mx;
}

/* Height of an item laid out at border-box width w (column flex / measuring). */
static float measure_h(page_t *pg, box_t *c, float w, float cbw, float cbh) {
    layout_box_with_width(pg, c, w, cbw, cbh);
    return c->h;
}

void layout_flex(page_t *pg, box_t *b, float cw, float sh) {
    const style_t *st = b->st;
    bool row = st->flex_dir <= 1, reverse = st->flex_dir == 1 || st->flex_dir == 3;
    float cx = b->b[3] + b->p[3], cy = b->b[0] + b->p[0];
    float main_avail = row ? cw : sh;          /* < 0: indefinite */
    float gap_main = row ? st->col_gap : st->row_gap;
    float gap_cross = row ? st->row_gap : st->col_gap;

    int n = 0;
    for (box_t *c = b->first; c; c = c->next) if (!c->is_abs) n++;
    if (!n) {
        float h = sh >= 0 ? sh : 0;
        b->h = h; return;
    }
    fitem_t *it = (fitem_t *)calloc((size_t)n, sizeof(fitem_t));
    if (!it) { b->h = 0; return; }
    int k = 0;
    for (box_t *c = b->first; c; c = c->next) {
        if (c->is_abs) { resolve_box_edges(c, cw); c->x = cx + c->m[3]; c->y = cy + c->m[0]; continue; }
        c->list_index = k;
        it[k++].b = c;
    }
    qsort(it, (size_t)n, sizeof(fitem_t), cmp_order);

    /* base sizes */
    for (int i = 0; i < n; i++) {
        fitem_t *f = &it[i]; box_t *c = f->b; const style_t *cs = c->st;
        resolve_box_edges(c, cw);
        if (row) {
            f->mm0 = c->m[3]; f->mm1 = c->m[1]; f->cm0 = c->m[0]; f->cm1 = c->m[2];
            f->auto0 = cs->margin[3].u == L_AUTO; f->auto1 = cs->margin[1].u == L_AUTO;
        } else {
            f->mm0 = c->m[0]; f->mm1 = c->m[2]; f->cm0 = c->m[3]; f->cm1 = c->m[1];
            f->auto0 = cs->margin[0].u == L_AUTO; f->auto1 = cs->margin[2].u == L_AUTO;
        }
        float pbm = row ? hpb(c) : vpb(c);
        bool bs = cs->box_sizing == BOX_BORDER;
        float base = -1;
        len_t basis = cs->basis;
        if (basis.u == L_PX || (basis.u == L_PCT && main_avail >= 0)) {
            base = len_px(basis, main_avail, 0) + (bs ? 0 : pbm);
        } else {
            len_t msz = row ? cs->width : cs->height;
            if (msz.u == L_PX || (msz.u == L_PCT && main_avail >= 0))
                base = len_px(msz, main_avail, 0) + (bs ? 0 : pbm);
        }
        float mn_c = 0, mx_c = 0;
        if (row) box_intrinsic(pg, c, &mn_c, &mx_c);
        if (base < 0) {
            if (row) {
                base = mx_c;
                if (main_avail >= 0 && base > main_avail - f->mm0 - f->mm1 && !st->flex_wrap) base = mx_c;
            } else {
                /* column: content height at the cross (stretched) width */
                float w = cw - f->cm0 - f->cm1;
                bool stretch = (cs->align_self == 255 ? st->align_items : cs->align_self) == JC_STRETCH;
                float sw = box_used_width(c, cw, NULL);
                if (sw >= 0) w = sw + hpb(c);
                else if (!stretch) { float a, z; box_intrinsic(pg, c, &a, &z); if (z < w) w = z; }
                base = measure_h(pg, c, w, cw, -1);
            }
        }
        /* clamps */
        len_t mnl = row ? cs->min_w : cs->min_h, mxl = row ? cs->max_w : cs->max_h;
        f->minm = 0; f->maxm = 1e9f;
        if (mnl.u == L_PX || (mnl.u == L_PCT && main_avail >= 0)) f->minm = len_px(mnl, main_avail, 0) + (bs ? 0 : pbm);
        else if (row && cs->overflow == OV_VISIBLE && c->kind != K_REPLACED) {
            /* automatic minimum size = min-content, capped by a definite size */
            float auto_min = mn_c;
            len_t msz = cs->width;
            if (msz.u == L_PX) { float spec = msz.v + (bs ? 0 : pbm); if (spec < auto_min) auto_min = spec; }
            f->minm = auto_min;
        }
        if (mxl.u == L_PX || (mxl.u == L_PCT && main_avail >= 0)) f->maxm = len_px(mxl, main_avail, 1e9f) + (bs ? 0 : pbm);
        if (f->minm < pbm) f->minm = pbm;
        f->base = base;
        f->hyp = base < f->minm ? f->minm : base > f->maxm ? f->maxm : base;
    }

    /* lines */
    int *line_start = (int *)malloc(sizeof(int) * (size_t)(n + 1));
    int nlines = 0;
    if (!line_start) { free(it); b->h = 0; return; }
    {
        int i = 0;
        while (i < n) {
            line_start[nlines++] = i;
            float used = 0; int cnt = 0;
            while (i < n) {
                float o = it[i].hyp + it[i].mm0 + it[i].mm1;
                if (st->flex_wrap && cnt > 0 && main_avail >= 0 && used + gap_main + o > main_avail + 0.5f) break;
                used += (cnt ? gap_main : 0) + o; cnt++; i++;
            }
        }
        line_start[nlines] = n;
    }

    float *line_cross = (float *)calloc((size_t)nlines, sizeof(float));
    float *line_main = (float *)calloc((size_t)nlines, sizeof(float));
    if (!line_cross || !line_main) { free(it); free(line_start); free(line_cross); free(line_main); b->h = 0; return; }

    for (int L = 0; L < nlines; L++) {
        int s = line_start[L], e = line_start[L + 1];
        int cnt = e - s;
        float sum = 0;
        for (int i = s; i < e; i++) { it[i].target = it[i].hyp; it[i].frozen = 0; sum += it[i].hyp + it[i].mm0 + it[i].mm1; }
        sum += gap_main * (cnt - 1);
        float container_main = main_avail;
        if (container_main < 0) container_main = sum;
        /* resolve flexible lengths (iterative freeze on clamp) */
        for (int iter = 0; iter < 5; iter++) {
            float used = gap_main * (cnt - 1), sum_grow = 0, sum_shrink = 0;
            for (int i = s; i < e; i++) {
                used += it[i].mm0 + it[i].mm1 + (it[i].frozen ? it[i].target : it[i].base);
                if (!it[i].frozen) { sum_grow += it[i].b->st->grow; sum_shrink += it[i].b->st->shrink * it[i].base; }
            }
            float free_sp = container_main - used;
            bool growing = free_sp > 0;
            if (growing && sum_grow <= 0) {
                for (int i = s; i < e; i++) if (!it[i].frozen) it[i].target = it[i].hyp;
                break;
            }
            if (!growing && sum_shrink <= 0) {
                for (int i = s; i < e; i++) if (!it[i].frozen) it[i].target = it[i].hyp;
                break;
            }
            if (growing && sum_grow < 1) free_sp *= sum_grow;
            bool clamped = false;
            for (int i = s; i < e; i++) {
                fitem_t *f = &it[i];
                if (f->frozen) continue;
                float t;
                if (growing) t = f->base + free_sp * f->b->st->grow / sum_grow;
                else t = f->base + free_sp * (f->b->st->shrink * f->base) / sum_shrink;
                float tc = t < f->minm ? f->minm : t > f->maxm ? f->maxm : t;
                if (tc != t) { f->frozen = 1; clamped = true; }
                f->target = tc;
            }
            if (!clamped) break;
        }
        /* lay out items at their main size to get cross sizes */
        float lc = 0, lm = gap_main * (cnt - 1);
        for (int i = s; i < e; i++) {
            fitem_t *f = &it[i]; box_t *c = f->b;
            if (row) {
                layout_box_with_width(pg, c, f->target, cw, sh);
                f->cross = c->h;
            } else {
                /* width: stretch / intrinsic */
                float w = cw - f->cm0 - f->cm1;
                u8 al = c->st->align_self == 255 ? st->align_items : c->st->align_self;
                float sw = box_used_width(c, cw, NULL);
                if (sw >= 0) w = sw + hpb(c);
                else if (al != JC_STRETCH) { float a, z; box_intrinsic(pg, c, &a, &z); if (z < w) w = z; }
                layout_box_with_width(pg, c, w, cw, -1);
                if (c->h < f->target) c->h = f->target;     /* grown in the main axis */
                else f->target = c->h;
                f->cross = c->w;
            }
            float oc = f->cross + f->cm0 + f->cm1;
            if (oc > lc) lc = oc;
            lm += f->target + f->mm0 + f->mm1;
        }
        if (!row && sh < 0) {}
        line_cross[L] = lc; line_main[L] = lm;
    }
    /* single-line container with definite cross size: line fills it */
    float cross_avail = row ? sh : cw;
    if (nlines == 1 && cross_avail >= 0 && !st->flex_wrap) line_cross[0] = cross_avail;
    if (!row && nlines == 1) line_cross[0] = cw;

    /* position */
    float cpos = 0;
    float total_cross = 0;
    for (int L = 0; L < nlines; L++) total_cross += line_cross[L] + (L ? gap_cross : 0);
    if (cross_avail >= 0 && nlines > 1 && st->align_content == JC_CENTER) cpos = (cross_avail - total_cross) / 2;
    else if (cross_avail >= 0 && nlines > 1 && st->align_content == JC_END) cpos = cross_avail - total_cross;
    for (int L = 0; L < nlines; L++) {
        int s = line_start[L], e = line_start[L + 1], cnt = e - s;
        float container_main = main_avail >= 0 ? main_avail : line_main[L];
        float free_sp = container_main - line_main[L];
        float pos = 0, between = gap_main;
        int nauto = 0;
        for (int i = s; i < e; i++) nauto += it[i].auto0 + it[i].auto1;
        float auto_sz = (nauto && free_sp > 0) ? free_sp / nauto : 0;
        if (!nauto && free_sp > 0) {
            u8 jc = st->justify;
            if (reverse) { if (jc == JC_START) jc = JC_END; else if (jc == JC_END) jc = JC_START; }
            switch (jc) {
            case JC_END: pos = free_sp; break;
            case JC_CENTER: pos = free_sp / 2; break;
            case JC_BETWEEN: if (cnt > 1) between += free_sp / (cnt - 1); break;
            case JC_AROUND: pos = free_sp / cnt / 2; between += free_sp / cnt; break;
            case JC_EVENLY: pos = free_sp / (cnt + 1); between += free_sp / (cnt + 1); break;
            default: break;
            }
        } else if (!nauto && free_sp < 0 && st->justify == JC_CENTER) pos = free_sp / 2;
        float lc = line_cross[L];
        for (int q = 0; q < cnt; q++) {
            int i = reverse ? e - 1 - q : s + q;
            fitem_t *f = &it[i]; box_t *c = f->b;
            float m0 = f->mm0 + (f->auto0 ? auto_sz : 0), m1 = f->mm1 + (f->auto1 ? auto_sz : 0);
            pos += m0;
            /* cross alignment */
            u8 al = c->st->align_self == 255 ? st->align_items : c->st->align_self;
            float cs_ = f->cross;
            bool cross_auto = row ? c->st->height.u == L_AUTO : c->st->width.u == L_AUTO;
            if (al == JC_STRETCH && cross_auto) {
                float want = lc - f->cm0 - f->cm1;
                if (row) {
                    if (want > c->h) {
                        /* re-lay out with a definite height so % heights and
                         * nested flex columns see the stretched size */
                        if (c->inner == IN_FLEX && want - c->h > 1) {
                            len_t saved = c->st->height;
                            c->st->height.u = L_PX;
                            c->st->height.v = want - (c->st->box_sizing == BOX_BORDER ? 0 : vpb(c));
                            layout_box_with_width(pg, c, f->target, cw, sh);
                            c->st->height = saved;
                        }
                        c->h = want;
                    }
                    cs_ = c->h;
                } else {
                    if (c->w < want) { layout_box_with_width(pg, c, want, cw, -1); if (c->h < f->target) c->h = f->target; }
                    cs_ = c->w;
                }
            }
            float cp = f->cm0;
            float slack = lc - cs_ - f->cm0 - f->cm1;
            bool c_auto0 = row ? c->st->margin[0].u == L_AUTO : c->st->margin[3].u == L_AUTO;
            bool c_auto1 = row ? c->st->margin[2].u == L_AUTO : c->st->margin[1].u == L_AUTO;
            if (c_auto0 && c_auto1) cp += slack > 0 ? slack / 2 : 0;
            else if (c_auto0) cp += slack > 0 ? slack : 0;
            else if (al == JC_CENTER) cp += slack / 2;
            else if (al == JC_END) cp += slack;
            if (row) { c->x = cx + pos; c->y = cy + cpos + cp; }
            else { c->y = cy + pos; c->x = cx + cpos + cp; }
            pos += (row ? c->w : c->h) + m1 + (q < cnt - 1 ? between : 0);
            if (c->baseline >= 0 && b->baseline < 0) b->baseline = c->y + c->baseline;
        }
        cpos += lc + gap_cross;
    }
    float content_h;
    if (row) content_h = total_cross;
    else {
        content_h = 0;
        for (int L = 0; L < nlines; L++) if (line_main[L] > content_h) content_h = line_main[L];
    }
    if (sh >= 0) content_h = sh;
    else {
        /* min/max height clamps applied by the caller (layout_block_in) */
    }
    b->h = content_h;
    free(it); free(line_start); free(line_cross); free(line_main);
}

/* ------------------------------------------------------------------ */
/*  Grid (columns template + auto-placement, auto rows)                */
/* ------------------------------------------------------------------ */

void layout_grid(page_t *pg, box_t *b, float cw) {
    const style_t *st = b->st;
    float cx = b->b[3] + b->p[3], cy = b->b[0] + b->p[0];
    float cg = st->col_gap, rg = st->row_gap;
    int nitems = 0;
    for (box_t *c = b->first; c; c = c->next) if (!c->is_abs) nitems++;
    const grid_tracks_t *gt = &st->grid_cols;
    int ncols = gt->n;
    len_t tracks[GRID_MAX_TRACKS]; float tmin[GRID_MAX_TRACKS];
    if (gt->auto_fill) {
        ncols = (int)((cw + cg) / (gt->fill_min + cg));
        if (ncols < 1) ncols = 1;
        if (ncols > GRID_MAX_TRACKS) ncols = GRID_MAX_TRACKS;
        if (gt->auto_fill == 2 && nitems > 0 && nitems < ncols) ncols = nitems;
        for (int i = 0; i < ncols; i++) { tracks[i].u = L_FR; tracks[i].v = 1; tracks[i].pc_px = 0; tmin[i] = gt->fill_min; }
    } else if (ncols == 0) {
        ncols = 1; tracks[0].u = L_FR; tracks[0].v = 1; tmin[0] = 0;
    } else {
        for (int i = 0; i < ncols; i++) { tracks[i] = gt->t[i]; tmin[i] = gt->tmin[i]; }
    }
    /* placement (row-major, column spans, explicit column start) */
    typedef struct { box_t *b; int col, row, span; } gi_t;
    gi_t *gi = (gi_t *)calloc((size_t)(nitems ? nitems : 1), sizeof(gi_t));
    if (!gi) { b->h = 0; return; }
    int r = 0, ccur = 0, k = 0, nrows = 0;
    for (box_t *c = b->first; c; c = c->next) {
        if (c->is_abs) { resolve_box_edges(c, cw); c->x = cx + c->m[3]; c->y = cy + c->m[0]; continue; }
        int span = c->st->grid_col_span; if (span > ncols) span = ncols; if (span < 1) span = 1;
        int start = c->st->grid_col_start;
        if (start > 0) {
            int col = start - 1; if (col >= ncols) col = ncols - 1;
            if (col + span > ncols) span = ncols - col;
            if (col < ccur) r++;
            ccur = col;
        } else if (ccur + span > ncols) { r++; ccur = 0; }
        gi[k].b = c; gi[k].col = ccur; gi[k].row = r; gi[k].span = span; k++;
        ccur += span;
        if (ccur >= ncols) { ccur = 0; r++; }
    }
    nrows = k ? gi[k - 1].row + 1 : 0;
    /* track sizing */
    float colw[GRID_MAX_TRACKS];
    float fixed = cg * (ncols - 1), fr_total = 0;
    for (int i = 0; i < ncols; i++) {
        if (tracks[i].u == L_PX) colw[i] = tracks[i].v;
        else if (tracks[i].u == L_PCT) colw[i] = len_px(tracks[i], cw, 0);
        else if (tracks[i].u == L_FR) { colw[i] = tmin[i]; fr_total += tracks[i].v; continue; }
        else {
            /* auto: max-content of single-span items in this column */
            float mx = 0, mnn = 0;
            for (int q = 0; q < k; q++) if (gi[q].col == i && gi[q].span == 1) {
                float a, z; box_intrinsic(pg, gi[q].b, &a, &z);
                float m = gi[q].b->m[1] + gi[q].b->m[3];
                if (z + m > mx) mx = z + m;
                if (a + m > mnn) mnn = a + m;
            }
            colw[i] = mx; tmin[i] = mnn;
        }
        fixed += colw[i];
    }
    float rem = cw - fixed;
    if (fr_total > 0) {
        float fr_px = rem > 0 ? rem / (fr_total < 1 ? 1 : fr_total) : 0;
        /* an fr track never goes below its minimum */
        for (int i = 0; i < ncols; i++) if (tracks[i].u == L_FR) {
            float w = fr_px * tracks[i].v;
            colw[i] = w > tmin[i] ? w : tmin[i];
        }
    } else if (rem < 0) {
        /* shrink auto tracks toward their minimum */
        float shrinkable = 0;
        for (int i = 0; i < ncols; i++) if (tracks[i].u == L_AUTO) shrinkable += colw[i] - tmin[i];
        if (shrinkable > 0) for (int i = 0; i < ncols; i++) if (tracks[i].u == L_AUTO) {
            float give = (colw[i] - tmin[i]) / shrinkable * (-rem);
            if (give > colw[i] - tmin[i]) give = colw[i] - tmin[i];
            colw[i] -= give;
        }
    } else if (rem > 0) {
        /* auto tracks share leftover space (justify-content: normal stretches) */
        int na = 0; for (int i = 0; i < ncols; i++) if (tracks[i].u == L_AUTO) na++;
        if (na) for (int i = 0; i < ncols; i++) if (tracks[i].u == L_AUTO) colw[i] += rem / na;
    }
    float colx[GRID_MAX_TRACKS + 1]; colx[0] = 0;
    for (int i = 0; i < ncols; i++) colx[i + 1] = colx[i] + colw[i] + cg;
    /* rows */
    float *rowh = (float *)calloc((size_t)(nrows ? nrows : 1), sizeof(float));
    if (!rowh) { free(gi); b->h = 0; return; }
    for (int q = 0; q < k; q++) {
        box_t *c = gi[q].b;
        resolve_box_edges(c, cw);
        float w = colx[gi[q].col + gi[q].span] - colx[gi[q].col] - cg - c->m[1] - c->m[3];
        float sw = box_used_width(c, w, NULL);
        u8 js = JC_STRETCH;
        if (sw >= 0) { w = sw + (c->st->box_sizing == BOX_BORDER ? 0 : hpb(c)); js = JC_START; }
        (void)js;
        layout_box_with_width(pg, c, w, w, -1);
        float h = c->h + c->m[0] + c->m[2];
        if (h > rowh[gi[q].row]) rowh[gi[q].row] = h;
    }
    float y = 0;
    float *rowy = (float *)calloc((size_t)(nrows ? nrows : 1), sizeof(float));
    for (int i = 0; i < nrows && rowy; i++) { rowy[i] = y; y += rowh[i] + (i < nrows - 1 ? rg : 0); }
    for (int q = 0; q < k && rowy; q++) {
        box_t *c = gi[q].b;
        u8 al = c->st->align_self == 255 ? st->align_items : c->st->align_self;
        float slack = rowh[gi[q].row] - c->h - c->m[0] - c->m[2];
        float dy = 0;
        if (al == JC_STRETCH && c->st->height.u == L_AUTO) c->h = rowh[gi[q].row] - c->m[0] - c->m[2];
        else if (al == JC_CENTER) dy = slack / 2;
        else if (al == JC_END) dy = slack;
        c->x = cx + colx[gi[q].col] + c->m[3];
        c->y = cy + rowy[gi[q].row] + c->m[0] + dy;
        if (c->baseline >= 0 && b->baseline < 0) b->baseline = c->y + c->baseline;
    }
    b->h = y;
    free(gi); free(rowh); free(rowy);
}
