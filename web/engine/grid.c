/* web/engine/grid.c -- CSS grid (common subset).
 *
 * Column template (px / % / fr / auto / minmax / repeat incl. auto-fill/fit),
 * named areas (grid-template-areas + grid-area), explicit line placement and
 * row-major auto placement over an occupancy map. Rows are auto-sized. */
#include "engine.h"

static inline float hpb(const box_t *b) { return b->p[1] + b->p[3] + b->b[1] + b->b[3]; }

#define GRID_MAX_ROWS 1024

typedef struct { box_t *b; int col, row, span, rspan; } gi_t;
typedef struct { char name[48]; int r0, c0, r1, c1; } garea_t;

/* Parse "a a|b c" into named rectangles; returns the column count. */
static int parse_area_rows(const char *s, garea_t *areas, int *nareas, int max_areas, int *nrows) {
    int row = 0, maxc = 0;
    *nareas = 0;
    const char *p = s;
    while (p && *p) {
        int col = 0;
        while (*p && *p != '|') {
            while (*p == ' ') p++;
            const char *w = p;
            while (*p && *p != ' ' && *p != '|') p++;
            size_t n = (size_t)(p - w);
            if (!n) continue;
            bool dot = w[0] == '.';                        /* "." / "..." = empty cell */
            if (!dot && n < 48) {
                int k;
                for (k = 0; k < *nareas; k++)
                    if (strlen(areas[k].name) == n && memcmp(areas[k].name, w, n) == 0) break;
                if (k == *nareas && *nareas < max_areas) {
                    memcpy(areas[k].name, w, n); areas[k].name[n] = 0;
                    areas[k].r0 = areas[k].r1 = row; areas[k].c0 = areas[k].c1 = col;
                    (*nareas)++;
                } else if (k < *nareas) {
                    if (row < areas[k].r0) areas[k].r0 = row;
                    if (row > areas[k].r1) areas[k].r1 = row;
                    if (col < areas[k].c0) areas[k].c0 = col;
                    if (col > areas[k].c1) areas[k].c1 = col;
                }
            }
            col++;
        }
        if (col > maxc) maxc = col;
        row++;
        if (*p == '|') p++;
    }
    *nrows = row;
    return maxc;
}

typedef struct { u32 *bits; int rows; } occ_t;   /* one bit per column (<= 24) per row */

static u32 span_mask(int c, int span) { return (span >= 32 ? 0xFFFFFFFFu : ((1u << span) - 1)) << c; }

static bool occ_free(occ_t *o, int r, int c, int span, int rspan) {
    for (int i = r; i < r + rspan; i++) {
        if (i >= GRID_MAX_ROWS) return false;
        if (o->bits[i] & span_mask(c, span)) return false;
    }
    return true;
}

static void occ_mark(occ_t *o, int r, int c, int span, int rspan) {
    for (int i = r; i < r + rspan && i < GRID_MAX_ROWS; i++) {
        o->bits[i] |= span_mask(c, span);
        if (i + 1 > o->rows) o->rows = i + 1;
    }
}

void layout_grid(page_t *pg, box_t *b, float cw) {
    const style_t *st = b->st;
    float cx = b->b[3] + b->p[3], cy = b->b[0] + b->p[0];
    float cg = st->col_gap, rg = st->row_gap;
    int nitems = 0;
    for (box_t *c = b->first; c; c = c->next) if (!c->is_abs) nitems++;

    garea_t areas[32]; int nareas = 0, area_rows = 0, area_cols = 0;
    if (st->grid_areas) area_cols = parse_area_rows(st->grid_areas, areas, &nareas, 32, &area_rows);

    /* tracks */
    const grid_tracks_t *gt = &st->grid_cols;
    int ncols = gt->n;
    len_t tracks[GRID_MAX_TRACKS]; float tmin[GRID_MAX_TRACKS];
    if (gt->auto_fill) {
        ncols = (int)((cw + cg) / (gt->fill_min + cg));
        if (ncols < 1) ncols = 1;
        if (ncols > GRID_MAX_TRACKS) ncols = GRID_MAX_TRACKS;
        if (gt->auto_fill == 2 && nitems > 0 && nitems < ncols) ncols = nitems;
        for (int i = 0; i < ncols; i++) { tracks[i].u = L_FR; tracks[i].v = 1; tracks[i].pc_px = 0; tmin[i] = gt->fill_min; }
    } else {
        for (int i = 0; i < ncols; i++) { tracks[i] = gt->t[i]; tmin[i] = gt->tmin[i]; }
    }
    /* implicit columns for named areas / explicit lines past the template */
    int want = area_cols;
    for (box_t *c = b->first; c; c = c->next)
        if (!c->is_abs && c->st->grid_col_start > 0 && c->st->grid_col_span < 999) {
            int need = c->st->grid_col_start - 1 + c->st->grid_col_span;
            if (need > want) want = need;
        }
    if (want > GRID_MAX_TRACKS) want = GRID_MAX_TRACKS;
    if (ncols == 0 && want == 0) { ncols = 1; tracks[0].u = L_FR; tracks[0].v = 1; tracks[0].pc_px = 0; tmin[0] = 0; }
    while (ncols < want) { tracks[ncols].u = L_AUTO; tracks[ncols].v = 0; tracks[ncols].pc_px = 0; tmin[ncols] = 0; ncols++; }

    /* placement */
    gi_t *gi = (gi_t *)calloc((size_t)(nitems ? nitems : 1), sizeof(gi_t));
    u32 *bits = (u32 *)calloc(GRID_MAX_ROWS, sizeof(u32));
    bool *placed = (bool *)calloc((size_t)(nitems ? nitems : 1), sizeof(bool));
    if (!gi || !bits || !placed) { free(gi); free(bits); free(placed); b->h = 0; return; }
    occ_t occ = { bits, 0 };
    int k = 0, idx = 0;
    /* pass 1: items with a definite row and column (named area / both lines) */
    for (box_t *c = b->first; c; c = c->next) {
        if (c->is_abs) continue;
        int me = idx++;
        const style_t *cs = c->st;
        int col = -1, row = -1, span = cs->grid_col_span, rspan = cs->grid_row_span;
        if (cs->grid_area) {
            for (int a = 0; a < nareas; a++) if (strcmp(areas[a].name, cs->grid_area) == 0) {
                row = areas[a].r0; col = areas[a].c0;
                span = areas[a].c1 - areas[a].c0 + 1; rspan = areas[a].r1 - areas[a].r0 + 1;
                break;
            }
        } else if (cs->grid_col_start > 0 && cs->grid_row_start > 0) {
            col = cs->grid_col_start - 1; row = cs->grid_row_start - 1;
        }
        if (col < 0 || row < 0) continue;
        if (col >= ncols) col = ncols - 1;
        if (span >= 999 || col + span > ncols) span = ncols - col;
        if (span < 1) span = 1;
        if (rspan < 1) rspan = 1;
        if (row + rspan > GRID_MAX_ROWS) continue;
        occ_mark(&occ, row, col, span, rspan);
        gi[k++] = (gi_t){ c, col, row, span, rspan };
        placed[me] = true;
    }
    /* pass 2: auto placement (row-major, sparse cursor) */
    int cr = 0, cc = 0;
    idx = 0;
    for (box_t *c = b->first; c; c = c->next) {
        if (c->is_abs) { resolve_box_edges(c, cw); c->x = cx + c->m[3]; c->y = cy + c->m[0]; continue; }
        if (placed[idx++]) continue;
        const style_t *cs = c->st;
        int span = cs->grid_col_span, rspan = cs->grid_row_span;
        if (span >= 999 || span > ncols) span = ncols;
        if (span < 1) span = 1;
        if (rspan < 1) rspan = 1;
        int col, row;
        if (cs->grid_col_start > 0) {
            col = cs->grid_col_start - 1;
            if (col + span > ncols) col = ncols - span;
            if (col < 0) col = 0;
            row = cr + (col < cc ? 1 : 0);
            while (row < GRID_MAX_ROWS - rspan && !occ_free(&occ, row, col, span, rspan)) row++;
            cr = row; cc = col + span;
        } else if (cs->grid_row_start > 0) {
            row = cs->grid_row_start - 1; col = 0;
            while (col + span <= ncols && !occ_free(&occ, row, col, span, rspan)) col++;
            if (col + span > ncols) col = 0;
        } else {
            row = cr; col = cc;
            for (;;) {
                if (col + span > ncols) { col = 0; row++; }
                if (row >= GRID_MAX_ROWS - rspan) break;
                if (occ_free(&occ, row, col, span, rspan)) break;
                col++;
            }
            cr = row; cc = col + span;
        }
        if (row >= GRID_MAX_ROWS - rspan) row = GRID_MAX_ROWS - rspan - 1;
        occ_mark(&occ, row, col, span, rspan);
        gi[k++] = (gi_t){ c, col, row, span, rspan };
    }
    free(placed);
    int nrows = occ.rows;

    /* column sizing */
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
                resolve_box_edges(gi[q].b, cw);
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
        for (int i = 0; i < ncols; i++) if (tracks[i].u == L_FR) {
            float w = fr_px * tracks[i].v;
            colw[i] = w > tmin[i] ? w : tmin[i];
        }
    } else if (rem < 0) {
        /* shrink auto tracks toward their min-content */
        float shrinkable = 0;
        for (int i = 0; i < ncols; i++) if (tracks[i].u == L_AUTO) shrinkable += colw[i] - tmin[i];
        if (shrinkable > 0) for (int i = 0; i < ncols; i++) if (tracks[i].u == L_AUTO) {
            float give = (colw[i] - tmin[i]) / shrinkable * (-rem);
            if (give > colw[i] - tmin[i]) give = colw[i] - tmin[i];
            colw[i] -= give;
        }
    } else if (rem > 0) {
        /* justify-content: normal stretches auto tracks */
        int na = 0;
        for (int i = 0; i < ncols; i++) if (tracks[i].u == L_AUTO) na++;
        if (na) for (int i = 0; i < ncols; i++) if (tracks[i].u == L_AUTO) colw[i] += rem / na;
    }
    float colx[GRID_MAX_TRACKS + 1]; colx[0] = 0;
    for (int i = 0; i < ncols; i++) colx[i + 1] = colx[i] + colw[i] + cg;

    /* rows: single-row items size their row; spanning items grow the last row */
    float *rowh = (float *)calloc((size_t)(nrows ? nrows : 1), sizeof(float));
    float *rowy = (float *)calloc((size_t)(nrows ? nrows : 1) + 1, sizeof(float));
    if (!rowh || !rowy) { free(gi); free(bits); free(rowh); free(rowy); b->h = 0; return; }
    for (int q = 0; q < k; q++) {
        box_t *c = gi[q].b;
        resolve_box_edges(c, cw);
        float w = colx[gi[q].col + gi[q].span] - colx[gi[q].col] - cg - c->m[1] - c->m[3];
        float sw = box_used_width(c, w, NULL);
        if (sw >= 0) w = sw + (c->st->box_sizing == BOX_BORDER ? 0 : hpb(c));
        if (w < 0) w = 0;
        layout_box_with_width(pg, c, w, w, -1);
        if (gi[q].rspan == 1) {
            float h = c->h + c->m[0] + c->m[2];
            if (h > rowh[gi[q].row]) rowh[gi[q].row] = h;
        }
    }
    for (int q = 0; q < k; q++) {
        if (gi[q].rspan == 1) continue;
        box_t *c = gi[q].b;
        int last = gi[q].row + gi[q].rspan - 1;
        if (last >= nrows) last = nrows - 1;
        float sum = rg * (last - gi[q].row);
        for (int r = gi[q].row; r <= last; r++) sum += rowh[r];
        float h = c->h + c->m[0] + c->m[2];
        if (h > sum) rowh[last] += h - sum;
    }
    float y = 0;
    for (int i = 0; i < nrows; i++) { rowy[i] = y; y += rowh[i] + (i < nrows - 1 ? rg : 0); }
    for (int q = 0; q < k; q++) {
        box_t *c = gi[q].b;
        int last = gi[q].row + gi[q].rspan - 1;
        if (last >= nrows) last = nrows - 1;
        float area_h = rowy[last] + rowh[last] - rowy[gi[q].row];
        u8 al = c->st->align_self == 255 ? st->align_items : c->st->align_self;
        float slack = area_h - c->h - c->m[0] - c->m[2];
        float dy = 0;
        if (al == JC_STRETCH && c->st->height.u == L_AUTO) c->h = area_h - c->m[0] - c->m[2];
        else if (al == JC_CENTER) dy = slack / 2;
        else if (al == JC_END) dy = slack;
        c->x = cx + colx[gi[q].col] + c->m[3];
        c->y = cy + rowy[gi[q].row] + c->m[0] + dy;
        if (c->baseline >= 0 && b->baseline < 0) b->baseline = c->y + c->baseline;
    }
    b->h = y;
    free(gi); free(bits); free(rowh); free(rowy);
}
