/* SPDX-License-Identifier: MIT */
/* See uw_poly.h. gfx_draw_polygon, gfx_polygon_to_spans
 * and gfx_fill_span_list. */
#include "uw_poly.h"

void uw_spans_init(uw_spans *s) {
    int i;
    for (i = 0; i < UW_SPAN_ROWS; i++) {
        s->row[i] = (uint16_t)(i + UW_SPAN_Y_MIN);
        s->left[i] = 0;
        s->right[i] = 0;
    }
    s->row[-1 - UW_SPAN_Y_MIN] = 0x7fff;
    s->term = UW_SPAN_Y_MIN;
    s->x_left = s->x_right = 0;
    s->overrun = 0;
}

void uw_spans_load(uw_spans *s, const uint8_t *g) {
    int i;
    uw_spans_init(s);
    for (i = 0; i < UW_SPAN_ROWS; i++) {
        unsigned at = (unsigned)(0x4976 - 6 * (i + UW_SPAN_Y_MIN)) & 0xffff;
        s->row[i] = (uint16_t)(g[at] | (g[at + 1] << 8));
        s->left[i] = (int16_t)(g[at + 2] | (g[at + 3] << 8));
        s->right[i] = (int16_t)(g[at + 4] | (g[at + 5] << 8));
    }
    {
        unsigned t = (unsigned)(g[0x49aa] | (g[0x49ab] << 8));
        s->term = (0x4976 - (int)t) / 6;
    }
    s->x_left = (int16_t)(g[0x4a06] | (g[0x4a07] << 8));
    s->x_right = (int16_t)(g[0x4a08] | (g[0x4a09] << 8));
}

static int in_table(int y) {
    return y >= UW_SPAN_Y_MIN && y < UW_SPAN_Y_MIN + UW_SPAN_ROWS;
}

static void put(uw_spans *s, int y, int right, int16_t x) {
    if (!in_table(y)) { s->overrun++; return; }
    if (right) s->right[y - UW_SPAN_Y_MIN] = x;
    else       s->left[y - UW_SPAN_Y_MIN] = x;
}

/* gfx_polygon_edge_dda and the 31-step block it jumps into.
 * `*y` is the record `di` is at, as a row, and moves down one per write. */
static void edge(uw_spans *s, int *y, int right, int16_t x0, int16_t x1,
                 int16_t y0, int16_t y1) {
    int16_t dy, total, step = 0, x = x0;
    uint16_t frac = 0, acc = 0;
    int i;

    if (y0 <= y1) return;                     /* sub bx,dx; jle */
    dy = (int16_t)((uint16_t)y0 - (uint16_t)y1);
    put(s, (*y)--, right, x0);                /* stosw; add di,4 */
    total = (int16_t)((uint16_t)x1 - (uint16_t)x0);
    if (total != 0) {
        /* cwd; idiv bp -- then the remainder, `sar dx,1; rcr ax,1` making
         * it r * 0x8000, divided again; `cwd; shl ax,1; rcl dx,1` doubles
         * the fraction and borrows one from the integer part when it is
         * negative. */
        int16_t q = (int16_t)(total / dy), r = (int16_t)(total % dy);
        int32_t f = ((int32_t)r * 32768) / dy;
        frac = (uint16_t)((uint32_t)(f * 2) & 0xffff);
        step = (int16_t)(q - (f < 0 ? 1 : 0));
        acc = 0x7fff;
    }
    for (i = 1; i < dy; i++) {
        uint32_t t = (uint32_t)acc + frac;    /* add dx,cx */
        acc = (uint16_t)t;
        x = (int16_t)((uint16_t)x + (uint16_t)step
                      + (uint16_t)(t >> 16)); /* adc ax,bx */
        put(s, (*y)--, right, x);
    }
}

int uw_gfx_polygon_to_spans(uw_spans *s, const uw_pt *v, int n) {
    int top = 0, k, cnt, yl, yr, i;
    int16_t maxy = -999, lo, hi;

    if (in_table(s->term)) s->row[s->term - UW_SPAN_Y_MIN] &= 0x7fff;
    if (n < 1) return maxy;

    /* `cmp di,ax; jl` from di = 0xfc19: the FIRST vertex of the greatest y. */
    for (k = 0; k < n; k++)
        if (maxy < v[k].y) { maxy = v[k].y; top = k; }

    /* gfx_polygon_left_edges: backward, wrapping from vertex 0 to n - 1. */
    yl = maxy;
    for (k = top, cnt = n;;) {
        int p = k > 0 ? k - 1 : n - 1;
        if (v[k].y < v[p].y) break;           /* cmp bx,dx; jl ret */
        s->x_left = v[p].x;
        edge(s, &yl, 0, v[k].x, v[p].x, v[k].y, v[p].y);
        k = p;
        if (--cnt <= 0) break;                /* dec [0x49b8]; jg */
    }
    /* gfx_polygon_right_edges: forward. */
    yr = maxy;
    for (k = top, cnt = n;;) {
        int q = k + 1 < n ? k + 1 : 0;
        if (v[k].y < v[q].y) break;
        s->x_right = v[q].x;
        edge(s, &yr, 1, v[k].x, v[q].x, v[k].y, v[q].y);
        k = q;
        if (--cnt <= 0) break;
    }

    /* The last row, at the record after the RIGHT chain's last write. If
     * that chain wrote nothing the polygon is one row, and its extent is
     * the least and greatest x of every vertex; otherwise it is the two
     * chains' last far ends. Sorted, which no other row is. */
    if (yr == maxy) {
        int16_t mn = 9999, mx = -9999;
        for (i = 0; i < n; i++) {
            if (v[i].x <= mn) mn = v[i].x;    /* cmp ax,bx; jg -- ties take */
            if (v[i].x >= mx) mx = v[i].x;
        }
        lo = mx; hi = mn;
    } else {
        lo = s->x_left; hi = s->x_right;
    }
    if (lo > hi) { int16_t t = lo; lo = hi; hi = t; }
    put(s, yr, 0, lo);
    put(s, yr, 1, hi);
    s->term = yr - 1;
    if (in_table(s->term)) s->row[s->term - UW_SPAN_Y_MIN] |= 0x8000;
    else s->overrun++;
    return maxy;
}

long uw_gfx_fill_span_list(const uw_fb *fb, uw_spans *s, int y,
                           uint8_t colour) {
    long n = 0;
    if (!fb || !fb->pixels || !fb->row) return 0;
    for (;; y--) {
        uint16_t w, count;
        uint32_t base, at;
        int16_t l, r, c;
        if (!in_table(y)) { s->overrun++; break; }
        w = s->row[y - UW_SPAN_Y_MIN];
        if (w & 0x8000) break;                /* shl ax,1; jb */
        if ((int)w >= fb->n_rows) { s->overrun++; break; }
        base = fb->row[w];
        l = s->left[y - UW_SPAN_Y_MIN];
        r = s->right[y - UW_SPAN_Y_MIN];
        at = base + (uint16_t)l;
        c = (int16_t)((uint16_t)r - (uint16_t)l + 1);  /* sub; neg; inc */
        if (c >= 0) {
            count = (uint16_t)c;
        } else {                               /* js: from right + 1 */
            at = (uint32_t)((int32_t)at + c);
            count = (uint16_t)(1 - c);
        }
        while (count--) {
            if (at < fb->size) { fb->pixels[at] = colour; n++; }
            at++;
        }
    }
    return n;
}

long uw_gfx_draw_polygon(const uw_fb *fb, uw_spans *s, uw_pt *v, int n,
                         const uw_clip_rect *r, uint8_t colour) {
    int y;
    if (n < 0 || n > 99) return 0;
    if (n <= 2) return -1;
    n = uw_clip_polygon(v, n, r);
    if (n <= 0) return 0;
    y = uw_gfx_polygon_to_spans(s, v, n);
    return uw_gfx_fill_span_list(fb, s, y, colour);
}

/* ---- gfx_polygon_fill_general -------------------------------- */

typedef struct {
    int16_t top, x, xb, bottom, step;   /* +0, +2, +4, +6, +8 */
    int     next;                       /* +0xa, an index, -1 for none */
} gedge;

#define GF_ROWS   512          /* rows -64 .. 447: markers and bucket heads */
#define GF_ROW0   64
#define GF_EDGES  (2 * UW_CLIP_MAX_VERTS + 4)
#define GF_SPANS  2048

typedef struct {
    int16_t row, x1, x2;
} gspan;

static int16_t sar5(int16_t v) { return (int16_t)(v >> 5); }

long uw_gfx_polygon_fill_general(const uw_fb *fb, uw_pt *v, int n,
                                 const uw_clip_rect *r, uint8_t colour) {
    static gedge e[GF_EDGES];
    static gspan sp[GF_SPANS];
    static uint8_t marker[GF_ROWS];
    static int bucket[GF_ROWS];
    int act[GF_EDGES];
    int ne = 0, ns = 0, na = 0, i, ch, k;
    int16_t ax, bx, bp, row, top = 0;
    long written = 0;

    n = uw_clip_polygon_unbiased(v, n, r);
    if (n <= 0) return 0;
    for (k = 0; k < 4; k++) v[n + k] = v[k];       /* rep movsw, 8 words */

    /* 2e9d: the edge table, from vertex 1 round to vertex n (= 0). */
    i = 1;
    ch = n;
    while (ch > 0) {
        ax = v[i].x;
        bx = v[i].y;
        if (bx == v[i + 1].y) {
            /* 2f2c: horizontal, judged by the rows either side of the run. */
            bp = v[i - 1].y;
            for (;;) {
                int16_t dx = (int16_t)((uint16_t)bx - (uint16_t)v[i + 2].y);
                if (dx == 0) {                      /* 2f23: still flat */
                    i++;
                    if (--ch == 0) goto built;
                    continue;
                }
                if ((((int16_t)((uint16_t)bx - (uint16_t)bp)) ^ dx) >= 0) {
                    /* the same side both ways: an extremum, one flat edge */
                    if (ne < GF_EDGES) {
                        gedge *g = &e[ne++];
                        g->x = g->xb = (int16_t)((uint16_t)ax << 5);
                        g->top = g->bottom = bx;
                        g->step = 0;
                    }
                } else if (dx > 0) {
                    /* stepping sideways on the way down: two */
                    int j;
                    for (j = 0; j < 2 && ne < GF_EDGES; j++) {
                        gedge *g = &e[ne++];
                        g->x = g->xb = (int16_t)((uint16_t)(j ? v[i + 1].x : ax) << 5);
                        g->top = g->bottom = bx;
                        g->step = 0;
                    }
                }
                break;
            }
        } else if (ne < GF_EDGES) {
            gedge *g = &e[ne++];
            int16_t a;
            if (bx < v[i + 1].y) {                 /* 2eb5: rising */
                g->xb = (int16_t)((uint16_t)ax << 5);
                g->bottom = bx;
                g->x = (int16_t)((uint16_t)v[i + 1].x << 5);
                bp = v[i + 1].y;
                g->top = bp;
                a = (int16_t)((uint16_t)g->xb - (uint16_t)g->x);
                g->step = a ? (int16_t)(a / (int16_t)((uint16_t)bp - (uint16_t)bx)) : 0;
                g->x = (int16_t)((uint16_t)g->x - (uint16_t)g->step);
                if (bp <= v[i + 2].y) g->top--;
            } else {                               /* 2ee4: falling */
                g->x = (int16_t)((uint16_t)ax << 5);
                g->top = bx;
                g->xb = (int16_t)((uint16_t)v[i + 1].x << 5);
                bp = v[i + 1].y;
                g->bottom = bp;
                a = (int16_t)((uint16_t)g->xb - (uint16_t)g->x);
                g->step = a ? (int16_t)(a / (int16_t)((uint16_t)bx - (uint16_t)bp)) : 0;
                g->x = (int16_t)((uint16_t)g->x - (uint16_t)g->step);
                if (!(bp < v[i + 2].y)) g->bottom++;
            }
        }
        i++;
        ch--;
    }
built:
    if (ne < 2) return 0;                          /* 2fa8 */

    /* 2fae: bucket each edge at its top row, mark the row after its bottom. */
    for (k = 0; k < GF_ROWS; k++) { marker[k] = 0; bucket[k] = -1; }
    for (k = 0; k < ne; k++) {
        int t = e[k].top + GF_ROW0, b = e[k].bottom - 1 + GF_ROW0;
        if (t < 0 || t >= GF_ROWS || b < 0 || b >= GF_ROWS) return 0;
        marker[t] = 1;
        if (top <= e[k].top) top = e[k].top;      /* from 0: cmp ax,bx; jg */
        e[k].next = bucket[t];
        bucket[t] = k;
        marker[b] = 1;
    }
    row = top;

    for (;;) {
        int keep = 0, b;
        /* 2fe6: drop the edges whose bottom the row has passed. */
        for (k = 0; k < na; k++)
            if (row >= e[act[k]].bottom) act[keep++] = act[k];
        na = keep;
        if (row + GF_ROW0 < 0 || row + GF_ROW0 >= GF_ROWS) break;
        marker[row + GF_ROW0] = 0;
        for (b = bucket[row + GF_ROW0]; b >= 0 && na < GF_EDGES; b = e[b].next)
            act[na++] = b;
        bucket[row + GF_ROW0] = -1;
        if (na == 0) break;                       /* 3038 */

        if (na == 2) {
            int lo = act[0], hi = act[1];
            int16_t t = (int16_t)((uint16_t)e[lo].x + (uint16_t)e[lo].step
                                  - (uint16_t)e[hi].step);
            int keep_order = (t != e[hi].x) ? t < e[hi].x : e[lo].xb < e[hi].xb;
            if (!keep_order) { int tt = lo; lo = hi; hi = tt; }
            if (e[lo].xb <= e[hi].xb) {
                /* 306e: the fast path. */
                int16_t dx, bpx, sl, sr;
                int crossing;
                act[0] = lo; act[1] = hi;
                dx = (int16_t)((uint16_t)e[lo].x - 0x10);
                sl = e[lo].step;
                sr = e[hi].step;
                bpx = (int16_t)((uint16_t)e[hi].x + 0x10);
                crossing = sl > sr;               /* cmp si,ax; jle -> B */
                for (;;) {
                    dx = (int16_t)((uint16_t)dx + (uint16_t)sl);
                    bpx = (int16_t)((uint16_t)bpx + (uint16_t)sr);
                    if (ns < GF_SPANS) {
                        sp[ns].row = row; sp[ns].x1 = sar5(dx); sp[ns].x2 = sar5(bpx);
                        ns++;
                    }
                    row--;
                    if (row + GF_ROW0 < 0 || marker[row + GF_ROW0]) break;
                    if (crossing && dx > bpx) {
                        int16_t tt = dx; dx = bpx; bpx = tt;
                        tt = sl; sl = sr; sr = tt;
                        k = act[0]; act[0] = act[1]; act[1] = k;
                        dx = (int16_t)((uint16_t)dx - 0x20);
                        bpx = (int16_t)((uint16_t)bpx + 0x20);
                        crossing = 0;
                    }
                }
                e[act[0]].x = (int16_t)((uint16_t)dx + 0x10);
                e[act[1]].x = (int16_t)((uint16_t)bpx - 0x10);
                continue;
            }
            /* the two cross before they end: the general path */
        } else if (na & 1) {
            /* 3143: an odd count spans the least to the greatest. */
            int16_t mx = (int16_t)0x8000, mn = 0x7fff;
            for (k = 0; k < na; k++) {
                gedge *g = &e[act[k]];
                g->x = (int16_t)((uint16_t)g->x + (uint16_t)g->step);
                if (mx <= g->x) mx = g->x;
                if (mn >= g->x) mn = g->x;
            }
            if (ns < GF_SPANS) {
                sp[ns].row = row;
                sp[ns].x1 = sar5((int16_t)((uint16_t)mn - 0x10));
                sp[ns].x2 = sar5((int16_t)((uint16_t)mx + 0x10));
                ns++;
            }
            row--;
            continue;
        }
        /* 318e: bubble-sort by next x, span in pairs, row after row until
         * a marked one. */
        for (;;) {
            int dh = na - 1, swapped;
            do {
                swapped = 0;
                for (k = 0; k < dh; k++) {
                    gedge *a = &e[act[k]], *c = &e[act[k + 1]];
                    int16_t t = (int16_t)((uint16_t)a->x + (uint16_t)a->step
                                          - (uint16_t)c->step);
                    if (!(t < c->x)) {
                        int tt = act[k]; act[k] = act[k + 1]; act[k + 1] = tt;
                        swapped = 1;
                    }
                }
                if (!swapped) break;
            } while (--dh > 0);
            for (k = 0; k + 1 < na; k += 2) {
                gedge *a = &e[act[k]], *c = &e[act[k + 1]];
                a->x = (int16_t)((uint16_t)a->x + (uint16_t)a->step);
                c->x = (int16_t)((uint16_t)c->x + (uint16_t)c->step);
                if (ns < GF_SPANS) {
                    sp[ns].row = row;
                    sp[ns].x1 = sar5((int16_t)((uint16_t)a->x - 0x10));
                    sp[ns].x2 = sar5((int16_t)((uint16_t)c->x + 0x10));
                    ns++;
                }
            }
            row--;
            if (row + GF_ROW0 < 0 || marker[row + GF_ROW0]) break;
        }
    }

    /* 320d: the list, terminated, through gfx_fill_span_list. */
    if (!fb || !fb->pixels || !fb->row) return 0;
    for (k = 0; k < ns; k++) {
        uint16_t w = (uint16_t)sp[k].row, cnt;
        uint32_t at;
        int16_t c;
        if (w & 0x8000) break;                    /* shl ax,1; jb */
        if ((int)w >= fb->n_rows) break;
        at = fb->row[w] + (uint16_t)sp[k].x1;
        c = (int16_t)((uint16_t)sp[k].x2 - (uint16_t)sp[k].x1 + 1);
        if (c >= 0) {
            cnt = (uint16_t)c;
        } else {
            at = (uint32_t)((int32_t)at + c);
            cnt = (uint16_t)(1 - c);
        }
        while (cnt--) {
            if (at < fb->size) { fb->pixels[at] = colour; written++; }
            at++;
        }
    }
    return written;
}
