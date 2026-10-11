/* web/engine/table.c -- automatic table layout (CSS 2.1 auto algorithm, simplified). */
#include "engine.h"

static inline float hpb(const box_t *b) { return b->p[1] + b->p[3] + b->b[1] + b->b[3]; }
static inline float vpb(const box_t *b) { return b->p[0] + b->p[2] + b->b[0] + b->b[2]; }

#define TMAX_COLS 64

typedef struct { box_t *cell; int row, col, span, rspan; } tcell_t;
typedef struct { box_t *row; box_t *group; } trow_t;

typedef struct {
    tcell_t *cells; int ncells, capcells;
    trow_t  *rows;  int nrows, caprows;
    int ncols;
    box_t *caption;
} tgrid_t;

static void add_row(tgrid_t *g, box_t *row, box_t *group) {
    if (g->nrows == g->caprows) {
        int nc = g->caprows ? g->caprows * 2 : 16;
        trow_t *nr = (trow_t *)realloc(g->rows, (size_t)nc * sizeof(trow_t));
        if (!nr) return;
        g->rows = nr; g->caprows = nc;
    }
    g->rows[g->nrows++] = (trow_t){ row, group };
}

static void add_cell(tgrid_t *g, box_t *cell, int row, int col, int span, int rspan) {
    if (g->ncells == g->capcells) {
        int nc = g->capcells ? g->capcells * 2 : 64;
        tcell_t *n = (tcell_t *)realloc(g->cells, (size_t)nc * sizeof(tcell_t));
        if (!n) return;
        g->cells = n; g->capcells = nc;
    }
    g->cells[g->ncells++] = (tcell_t){ cell, row, col, span, rspan };
}

static void collect_rows(tgrid_t *g, box_t *t) {
    for (box_t *c = t->first; c; c = c->next) {
        if (c->is_abs) continue;
        if (c->st->display == D_TABLE_CAPTION) { if (!g->caption) g->caption = c; continue; }
        if (c->st->display == D_TABLE_COLUMN) continue;
        if (c->inner == IN_ROWGROUP) {
            for (box_t *r = c->first; r; r = r->next) if (!r->is_abs) add_row(g, r, c);
            continue;
        }
        add_row(g, c, NULL);
    }
}

static void build_grid(tgrid_t *g) {
    int pending[TMAX_COLS] = {0};      /* rowspan occupancy */
    g->ncols = 0;
    for (int r = 0; r < g->nrows; r++) {
        box_t *row = g->rows[r].row;
        int col = 0;
        bool row_is_cell = row->inner != IN_ROW;
        box_t *first = row_is_cell ? row : row->first;
        for (box_t *c = first; c; c = c->next) {
            if (!row_is_cell && c->is_abs) continue;
            while (col < TMAX_COLS && pending[col] > 0) col++;
            if (col >= TMAX_COLS) break;
            int span = c->colspan > 0 ? c->colspan : 1;
            int rspan = c->rowspan > 0 ? c->rowspan : 1;
            if (col + span > TMAX_COLS) span = TMAX_COLS - col;
            add_cell(g, c, r, col, span, rspan);
            if (rspan > 1) for (int k = 0; k < span; k++) pending[col + k] = rspan;
            col += span;
            if (col > g->ncols) g->ncols = col;
            if (row_is_cell) break;
        }
        for (int k = 0; k < TMAX_COLS; k++) if (pending[k] > 0) pending[k]--;
    }
}

static void column_widths(page_t *pg, tgrid_t *g, float *mn, float *mx, float *pct, float avail_hint) {
    for (int i = 0; i < g->ncols; i++) { mn[i] = 0; mx[i] = 0; pct[i] = 0; }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < g->ncells; i++) {
            tcell_t *tc = &g->cells[i];
            if ((pass == 0) != (tc->span == 1)) continue;
            box_t *c = tc->cell;
            float a, z;
            box_intrinsic(pg, c, &a, &z);
            if (c->st->width.u == L_PCT) {
                if (tc->span == 1 && c->st->width.v > pct[tc->col]) pct[tc->col] = c->st->width.v;
            } else if (c->st->width.u == L_PX) {
                float w = c->st->width.v + (c->st->box_sizing == BOX_BORDER ? 0 : hpb(c));
                if (w > a) a = w;
                z = a;
            }
            (void)avail_hint;
            if (tc->span == 1) {
                if (a > mn[tc->col]) mn[tc->col] = a;
                if (z > mx[tc->col]) mx[tc->col] = z;
            } else {
                float smn = 0, smx = 0;
                for (int k = 0; k < tc->span; k++) { smn += mn[tc->col + k]; smx += mx[tc->col + k]; }
                if (a > smn) { float d = (a - smn) / tc->span; for (int k = 0; k < tc->span; k++) mn[tc->col + k] += d; }
                if (z > smx) { float d = (z - smx) / tc->span; for (int k = 0; k < tc->span; k++) mx[tc->col + k] += d; }
            }
        }
    }
    for (int i = 0; i < g->ncols; i++) if (mx[i] < mn[i]) mx[i] = mn[i];
}

void table_intrinsic(page_t *pg, box_t *b, float *minw, float *maxw) {
    tgrid_t g = {0};
    collect_rows(&g, b);
    build_grid(&g);
    float mn[TMAX_COLS], mx[TMAX_COLS], pct[TMAX_COLS];
    column_widths(pg, &g, mn, mx, pct, -1);
    float sp = b->st->border_collapse ? 0 : b->st->border_spacing;
    float a = sp * (g.ncols + 1), z = a;
    for (int i = 0; i < g.ncols; i++) { a += mn[i]; z += mx[i]; }
    *minw = a; *maxw = z;
    free(g.cells); free(g.rows);
}

void layout_table(page_t *pg, box_t *t, float avail) {
    resolve_box_edges(t, avail);
    tgrid_t g = {0};
    collect_rows(&g, t);
    build_grid(&g);
    float sp = t->st->border_collapse ? 0 : t->st->border_spacing;
    float pb = hpb(t);
    float mn[TMAX_COLS], mx[TMAX_COLS], pct[TMAX_COLS], cw[TMAX_COLS];
    int nc = g.ncols;
    column_widths(pg, &g, mn, mx, pct, avail);
    float smin = 0, smax = 0;
    for (int i = 0; i < nc; i++) { smin += mn[i]; smax += mx[i]; }
    float spacing = sp * (nc + 1);
    /* table border-box width */
    float W;
    bool auto_w = true;
    if (t->st->width.u == L_PX || t->st->width.u == L_PCT) {
        W = len_px(t->st->width, avail, avail);
        if (t->st->box_sizing != BOX_BORDER) W += pb;
        auto_w = false;
    } else {
        W = smax + spacing + pb;
        if (W > avail) W = avail;
    }
    if (W < smin + spacing + pb) W = smin + spacing + pb;
    float room = W - spacing - pb;
    /* percentage columns first */
    float used = 0; int npct = 0;
    for (int i = 0; i < nc; i++) if (pct[i] > 0) {
        cw[i] = room * pct[i] / 100.0f;
        if (cw[i] < mn[i]) cw[i] = mn[i];
        used += cw[i]; npct++;
    }
    float rest = room - used, rmin = 0, rmax = 0;
    for (int i = 0; i < nc; i++) if (pct[i] <= 0) { rmin += mn[i]; rmax += mx[i]; }
    for (int i = 0; i < nc; i++) {
        if (pct[i] > 0) continue;
        if (rmax <= rest) {
            float extra = rest - rmax;
            if (auto_w && npct == 0) extra = 0;
            if (rmax > 0) cw[i] = mx[i] + extra * (mx[i] / rmax);
            else cw[i] = (nc - npct) ? rest / (nc - npct) : 0;
        } else if (rmin >= rest || rmax <= rmin) cw[i] = mn[i];
        else cw[i] = mn[i] + (mx[i] - mn[i]) * (rest - rmin) / (rmax - rmin);
    }
    if (auto_w) {
        float sum = spacing + pb;
        for (int i = 0; i < nc; i++) sum += cw[i];
        if (sum > W) W = sum; else W = sum;
    }
    t->w = W;
    float colx[TMAX_COLS + 1];
    colx[0] = t->b[3] + t->p[3] + sp;
    for (int i = 0; i < nc; i++) colx[i + 1] = colx[i] + cw[i] + sp;

    /* caption */
    float y = t->b[0] + t->p[0];
    if (g.caption) {
        box_t *c = g.caption;
        resolve_box_edges(c, W);
        layout_box_with_width(pg, c, W - pb - c->m[1] - c->m[3], W, -1);
        c->x = t->b[3] + t->p[3] + c->m[3]; c->y = y + c->m[0];
        y += c->h + c->m[0] + c->m[2];
    }
    y += sp;
    /* lay out cells, compute row heights */
    float *rh = (float *)calloc((size_t)(g.nrows ? g.nrows : 1), sizeof(float));
    float *ry = (float *)calloc((size_t)(g.nrows ? g.nrows : 1), sizeof(float));
    if (!rh || !ry) { free(rh); free(ry); free(g.cells); free(g.rows); t->h = y; return; }
    for (int r = 0; r < g.nrows; r++) {
        box_t *row = g.rows[r].row;
        resolve_box_edges(row, W);
        if (row->st->height.u == L_PX) rh[r] = row->st->height.v;
    }
    for (int i = 0; i < g.ncells; i++) {
        tcell_t *tc = &g.cells[i];
        box_t *c = tc->cell;
        float w = colx[tc->col + tc->span] - colx[tc->col] - sp;
        resolve_box_edges(c, w);
        layout_box_with_width(pg, c, w, w, -1);
        float h = c->h;
        if (c->st->height.u == L_PX) {
            float sh = c->st->height.v + (c->st->box_sizing == BOX_BORDER ? 0 : vpb(c));
            if (sh > h) h = sh;
        }
        if (tc->rspan == 1 && h > rh[tc->row]) rh[tc->row] = h;
    }
    /* rowspan cells: grow the last spanned row if needed */
    for (int i = 0; i < g.ncells; i++) {
        tcell_t *tc = &g.cells[i];
        if (tc->rspan <= 1) continue;
        int last = tc->row + tc->rspan - 1; if (last >= g.nrows) last = g.nrows - 1;
        float span_h = 0;
        for (int r = tc->row; r <= last; r++) span_h += rh[r] + (r < last ? sp : 0);
        if (tc->cell->h > span_h) rh[last] += tc->cell->h - span_h;
    }
    for (int r = 0; r < g.nrows; r++) { ry[r] = y; y += rh[r] + sp; }
    /* position rows, groups, cells */
    box_t *cur_group = NULL; float group_y0 = 0;
    for (int r = 0; r < g.nrows; r++) {
        box_t *row = g.rows[r].row, *grp = g.rows[r].group;
        if (grp && grp != cur_group) {
            cur_group = grp; group_y0 = ry[r];
            grp->x = 0; grp->y = ry[r]; grp->w = W;
            int last = r; while (last + 1 < g.nrows && g.rows[last + 1].group == grp) last++;
            grp->h = ry[last] + rh[last] - ry[r];
        }
        float oy = grp ? group_y0 : 0;
        if (row->inner == IN_ROW) {
            row->x = colx[0] - (grp ? 0 : 0); row->y = ry[r] - oy;
            row->w = colx[nc] - colx[0] - sp; row->h = rh[r];
            row->x = colx[0];
        }
    }
    for (int i = 0; i < g.ncells; i++) {
        tcell_t *tc = &g.cells[i];
        box_t *c = tc->cell;
        int last = tc->row + tc->rspan - 1; if (last >= g.nrows) last = g.nrows - 1;
        float h = ry[last] + rh[last] - ry[tc->row];
        float content_h = c->h;
        /* vertical alignment */
        float slack = h - content_h;
        u8 va = c->st->vertical_align;
        float dy = va == VA_TOP ? 0 : va == VA_BOTTOM ? slack : (va == VA_MIDDLE ? slack / 2 : 0);
        if (va == VA_BASELINE) dy = 0;
        shift_contents(c, dy);
        c->h = h;
        box_t *row = g.rows[tc->row].row;
        box_t *grp = g.rows[tc->row].group;
        if (row->inner == IN_ROW && c->parent == row) {
            c->x = colx[tc->col] - row->x;
            c->y = 0;
        } else {
            /* cell standing in for a row */
            float oy = grp ? grp->y : 0;
            c->x = colx[tc->col]; c->y = ry[tc->row] - oy;
        }
    }
    y += t->b[2] + t->p[2];
    float th = y;
    if (t->st->height.u == L_PX) { float sh = t->st->height.v + (t->st->box_sizing == BOX_BORDER ? 0 : vpb(t)); if (sh > th) th = sh; }
    t->h = th;
    t->baseline = g.ncells ? g.cells[0].cell->baseline + (g.rows[0].group ? g.rows[0].group->y : 0) + (g.rows[0].row->inner == IN_ROW ? g.rows[0].row->y : 0) : -1;
    free(rh); free(ry); free(g.cells); free(g.rows);
}
