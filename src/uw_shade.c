/* SPDX-License-Identifier: MIT */
#include "uw_shade.h"

#include <string.h>

/* ---- rast_draw_face_lit's vertex shade ------------------------ */

int16_t uw_rast_vertex_shade(const uw_rast_svert *v, const uw_light_params *p) {
    const int16_t c[3] = { v->x, v->y, v->z };
    uint16_t sum = 0, root = 0x40, ax;
    int i;

    for (i = 0; i < 3; i++) {
        /* `lodsw; sar ax,5; mov dx,ax; imul dx; add cx,ax` -- the low word. */
        int16_t a = (int16_t)(c[i] >> 5);
        sum = (uint16_t)(sum + (uint16_t)((int32_t)a * a));
    }
    for (i = 0; i < 4; i++) {
        /* `mov ax,bx; xor dx,dx; div cx; add cx,ax; rcr cx,1` -- the carry
         * of the add comes back in through the rotate, so the sum is 17
         * bits wide before the halving. */
        uint16_t q = (uint16_t)(sum / root);
        root = (uint16_t)(((uint32_t)root + q) >> 1);
    }
    /* `mov ax,cx; mov cx,[0x64c]; mul cl` -- eight bits by eight. */
    ax = (uint16_t)((uint16_t)(root & 0xff) * (uint16_t)(p->scale & 0xff));
    ax = (uint16_t)((ax >> 6) & 0xff);                 /* shr ax,6; xor ah,ah */
    ax = (uint16_t)(ax + (uint16_t)p->bias_a);         /* add ax,[0x650] */
    if ((int16_t)ax < 0) ax = 0;                       /* jns; xor ax,ax */
    ax = (uint16_t)(ax + (uint16_t)p->bias_b);         /* add ax,[0x64e] */
    if ((ax & 0xff) > 0x0f)                            /* cmp al,0xf; jbe */
        ax = (uint16_t)((ax & 0xff00) | 0x0f);
    return (int16_t)ax;
}

/* ---- gfx_polygon_shaded_to_spans ------------------------------ */

/* Ten bytes a row. */
typedef struct {
    int16_t row, x1, x2, s1, s2;
} span_rec;

#define MAX_ROWS 1024        /* the original's area is not bounded; see below */

/* The lighting table lookup, `mov bh,<shade>; mov bh,es:[bx+0x696e]`. */
static uint8_t lit(const uint8_t *light, uint8_t shade, uint8_t pixel,
                   long *overrun) {
    if (shade > 15) {
        /* Past LIGHT.DAT and into the rasteriser's code. */
        if (overrun) (*overrun)++;
        shade = (shade & 0x80) ? 0 : 15;
    }
    return light[(unsigned)shade * 256u + pixel];
}

/* Which span pair is installed, and the Gouraud one's state. */
typedef struct {
    int      gouraud;
    uint8_t  base;
    uint8_t *colour;        /* the flat fill's colour, or NULL */
} span_mode;

static long spans(const uw_fb *fb, const span_rec *r, int n,
                  const uint8_t *light, const span_mode *mode,
                  long *overrun) {
    long written = 0;
    int k;

    for (k = 0; k < n; k++) {
        uint16_t row = (uint16_t)r[k].row;
        int16_t x1 = r[k].x1, x2 = r[k].x2, s1 = r[k].s1, s2 = r[k].s2;
        int16_t count, left, sl, sr;
        uint32_t di;

        if (row & 0x8000) break;                       /* shl ax,1; jb ret */
        if ((int)row >= fb->n_rows) continue;          /* the table ends */
        /* `sub cx,dx; jge` -- else swap so the record reads left to right. */
        count = (int16_t)(uint16_t)((uint16_t)x2 - (uint16_t)x1);
        if (count < 0) {
            count = (int16_t)(0u - (uint16_t)count);
            left = x2; sl = s2; sr = s1;
        } else {
            left = x1; sl = s1; sr = s2;
        }
        di = fb->row[row] + (uint16_t)left;
        count = (int16_t)(count + 1);
        {
            int16_t delta = (int16_t)(uint16_t)((uint16_t)sr - (uint16_t)sl);
            if (delta == 0 && mode->gouraud) {
                /* LIGHT[shade][base] into the fill colour, and this
                 * record through gfx_fill_span_list -- its own x1 and x2,
                 * not the swapped pair, under the flat fill's rule. */
                uint8_t c = lit(light, (uint8_t)(sl & 0xff), mode->base,
                                overrun);
                int16_t fc = (int16_t)((uint16_t)r[k].x2 - (uint16_t)r[k].x1 + 1);
                uint32_t at = fb->row[row] + (uint16_t)r[k].x1;
                uint16_t cnt;
                if (mode->colour) *mode->colour = c;
                if (fc >= 0) {
                    cnt = (uint16_t)fc;
                } else {
                    at = (uint32_t)((int32_t)at + fc);
                    cnt = (uint16_t)(1 - fc);
                }
                while (cnt--) {
                    if (at < fb->size) { fb->pixels[at] = c; written++; }
                    at++;
                }
            } else if (delta == 0) {
                /* gfx_shaded_row_remap: one row of LIGHT.DAT. */
                uint8_t sh = (uint8_t)(sl & 0xff);
                int i;
                for (i = 0; i < (uint16_t)count; i++, di++) {
                    if (di >= fb->size) continue;
                    fb->pixels[di] = lit(light, sh, fb->pixels[di], overrun);
                    written++;
                }
            } else {
                /* the 8.8 step, on the magnitude:
                 * `cwd; idiv cx; mov bh,al; sub ax,ax; div cx; mov bl,ah`,
                 * negated (`neg bx`) when the delta was negative. */
                uint16_t mag = (uint16_t)(delta < 0 ? -delta : delta);
                uint16_t cnt = (uint16_t)count;
                uint16_t q = (uint16_t)(mag / cnt), rem = (uint16_t)(mag % cnt);
                uint16_t frac = (uint16_t)(((uint32_t)rem << 16) / cnt);
                uint16_t step = (uint16_t)(((q & 0xff) << 8) | (frac >> 8));
                uint16_t acc_a, acc_b, t;
                int i;
                if (delta < 0) step = (uint16_t)(0u - step);
                /* `mov bh,[b0a]; mov bl,0x80`, the step doubled,
                 * 0x40 either side, swapped on an odd row. */
                acc_a = (uint16_t)((((uint16_t)sl & 0xff) << 8) | 0x80);
                acc_b = (uint16_t)(acc_a + 0x40);
                acc_a = (uint16_t)(acc_a - 0x40);
                step = (uint16_t)(step << 1);
                if (row & 1) { t = acc_a; acc_a = acc_b; acc_b = t; }
                for (i = 0; i < cnt; i++, di++) {
                    uint16_t *acc = (i & 1) ? &acc_b : &acc_a;
                    if (di < fb->size) {
                        /* 0ff7 reads the base colour where 103d reads the
                         * pixel; the dither is the same. */
                        fb->pixels[di] = lit(light, (uint8_t)(*acc >> 8),
                                             mode->gouraud ? mode->base
                                                           : fb->pixels[di],
                                             overrun);
                        written++;
                    }
                    *acc = (uint16_t)(*acc + step);
                }
            }
        }
    }
    return written;
}

static long shade_polygon(const uw_fb *fb, uw_shade_vert *v, int n,
                          const uint8_t *light, const span_mode *mode,
                          long *overrun) {
    static span_rec rec[MAX_ROWS + 2];     /* rec[i + 1] is row ymin + i */
    uw_shade_vert ring[UW_CLIP_MAX + 1];
    int16_t ymax = -1000, ymin = 1000, bx_x = 0, bx_s = 0;
    int i, height;

    if (!fb || !fb->pixels || !light || n < 1 || n > UW_CLIP_MAX) return 0;

    /* 615c: close the ring BEFORE the clamp, so the copy keeps a negative y. */
    for (i = 0; i < n; i++) ring[i] = v[i];
    ring[n] = v[0];

    /* 6176: clamp negative y to zero in place, and find the extremes. */
    for (i = 0; i < n; i++) {
        if (ring[i].y < 0) { ring[i].y = 0; v[i].y = 0; }
        if (ring[i].y >= ymax) {                        /* cmp ax,bx; jl */
            ymax = ring[i].y;
            bx_x = ring[i].x;
            bx_s = ring[i].shade;
        }
        if (ring[i].y <= ymin) ymin = ring[i].y;        /* cmp ax,dx; jg */
    }
    height = ymax - ymin;
    if (height < 0 || height >= MAX_ROWS) {
        /* The original writes 10 * (ymax - ymin) bytes past the row table,
         * whatever that is. A projected face cannot get here. */
        if (overrun) (*overrun)++;
        return 0;
    }

    /* 619b: the bottom row from its vertex, every row above it -- and the
     * one above the top, rec[0] -- to the sentinels. */
    rec[height + 1].row = ymax;
    rec[height + 1].x1 = rec[height + 1].x2 = bx_x;
    rec[height + 1].s1 = rec[height + 1].s2 = bx_s;
    for (i = height; i >= 0; i--) {
        rec[i].x1 = 1000;
        rec[i].x2 = -1000;
    }

    /* 61e1: the edges. */
    for (i = 0; i < n; i++) {
        const uw_shade_vert *a = &ring[i], *b = &ring[i + 1];
        int16_t dy = (int16_t)(a->y - b->y);
        if (dy == 0) {
            /* 630a: a horizontal edge writes its own row, <= and >=. */
            int idx = b->y - ymin + 1;
            int16_t lx = a->x, ls = a->shade, hx = b->x, hs = b->shade;
            if (idx < 0 || idx > height + 1) { if (overrun) (*overrun)++; continue; }
            rec[idx].row = b->y;
            if (lx > hx) {
                int16_t t = lx; lx = hx; hx = t;
                t = ls; ls = hs; hs = t;
            }
            if (lx <= rec[idx].x1) { rec[idx].x1 = lx; rec[idx].s1 = ls; }
            if (hx >= rec[idx].x2) { rec[idx].x2 = hx; rec[idx].s2 = hs; }
            continue;
        }
        {
            /* The lower end (greater y) starts; the walk goes up. */
            const uw_shade_vert *lo = dy < 0 ? b : a, *hi = dy < 0 ? a : b;
            int16_t h = (int16_t)(lo->y - hi->y);
            int32_t xs = uw_gfx_step16((int16_t)(hi->x - lo->x), h);
            int32_t ss = uw_gfx_step16((int16_t)(hi->shade - lo->shade), h);
            uint32_t x = ((uint32_t)(uint16_t)lo->x << 16) | 0x8000u;
            uint32_t s = ((uint32_t)(uint16_t)lo->shade << 16) | 0x8000u;
            int16_t row = (int16_t)(lo->y - 1);
            int k;
            for (k = 0; k < h; k++, row--) {
                int idx = row - ymin + 1;
                int16_t xi, si;
                x += (uint32_t)xs;                      /* add bp; adc ax */
                s += (uint32_t)ss;
                xi = (int16_t)(uint16_t)(x >> 16);
                si = (int16_t)(uint16_t)(s >> 16);
                if (idx < 0 || idx > height + 1) { if (overrun) (*overrun)++; continue; }
                rec[idx].row = row;
                if (xi < rec[idx].x1) { rec[idx].x1 = xi; rec[idx].s1 = si; }
                if (xi > rec[idx].x2) { rec[idx].x2 = xi; rec[idx].s2 = si; }
            }
        }
    }

    /* 62dc: rows ymin..ymax, the terminator after the last. */
    return spans(fb, rec + 1, height + 1, light, mode, overrun);
}

long uw_gfx_shade_polygon_remap(const uw_fb *fb, uw_shade_vert *v, int n,
                                const uint8_t *light, long *overrun) {
    span_mode mode = { 0, 0, NULL };
    return shade_polygon(fb, v, n, light, &mode, overrun);
}

long uw_gfx_shade_polygon_gouraud(const uw_fb *fb, uw_shade_vert *v, int n,
                                  const uint8_t *light, uint8_t base,
                                  long *overrun, uint8_t *colour) {
    span_mode mode = { 1, base, colour };
    return shade_polygon(fb, v, n, light, &mode, overrun);
}

/* ---- gfx_draw_polygon_shaded's clip ---------------------------- */

static int16_t w16(int32_t v) {
    return (int16_t)(uint16_t)((uint32_t)v & 0xffffu);
}

/* 649c (x bounds, `axis` 0) and 64f0 (y bounds, `axis` 1), with the bias
 * already signed by the entry the pass installed. */
static uw_shade_vert shaded_cross(const uw_shade_vert *a, const uw_shade_vert *b,
                                  int axis, int16_t bound, int16_t bias,
                                  const uw_clip_rect *r) {
    int16_t num, run, den, val;
    int32_t den32;
    uw_shade_vert out;

    if (axis) {
        num = w16((int32_t)b->x - a->x);
        run = w16((int32_t)bound - a->y);
        den32 = (int32_t)b->y - a->y;
    } else {
        num = w16((int32_t)b->y - a->y);
        run = w16((int32_t)bound - a->x);
        den32 = (int32_t)b->x - a->x;
    }
    if (den32 > 32767 || den32 < -32768) {          /* jo: rcr cx; sar dx */
        den = (int16_t)(den32 >> 1);
        run = (int16_t)(run >> 1);
    } else {
        den = (int16_t)den32;
    }
    if (den == 0) den = 1;                           /* unreachable, as flat */
    val = w16((int32_t)num * run / den);
    val = w16((int32_t)val + (axis ? a->x : a->y));
    if (axis) {
        if (val > r->left && val < r->right) val = w16((int32_t)val + bias);
        out.x = val;
        out.y = bound;
    } else {
        if (val < r->bottom && val > r->top) val = w16((int32_t)val + bias);
        out.x = bound;
        out.y = val;
    }
    out.shade = w16((int32_t)w16((int32_t)b->shade - a->shade) * run / den
                    + a->shade);
    return out;
}

int uw_clip_polygon_shaded(uw_shade_vert *poly, int n, const uw_clip_rect *r) {
    static const struct { int axis; int keep_above, neg; int which; } pass[4] = {
        { 1, 0, 0, 3 },   /* y < bottom, 64f0 */
        { 1, 1, 1, 0 },   /* y > top,    64ec */
        { 0, 1, 0, 1 },   /* x > left,   649c */
        { 0, 0, 1, 2 },   /* x < right,  6498 */
    };
    uw_shade_vert buf[2 * (UW_CLIP_MAX_VERTS + 1) + 1];
    int i, j;

    if (n <= 0) return 0;
    if (n > UW_CLIP_MAX_VERTS) return -1;
    for (i = 0; i < 4; i++) {
        int16_t bound;
        int m = 0;
        if ((i & 1) == 0) {
            /* The pre-scan, inclusive, over the axis's pair of passes. */
            int all = 1;
            for (j = 0; j < n; j++) {
                int16_t c = pass[i].axis ? poly[j].y : poly[j].x;
                int16_t lo = pass[i].axis ? r->top : r->left;
                int16_t hi = pass[i].axis ? r->bottom : r->right;
                if (c > hi || c < lo) { all = 0; break; }
            }
            if (all) { i++; continue; }
        }
        bound = pass[i].which == 0 ? r->top : pass[i].which == 1 ? r->left
              : pass[i].which == 2 ? r->right : r->bottom;
        poly[n] = poly[0];
        for (j = 0; j < n; j++) {
            const uw_shade_vert *a = &poly[j], *b = &poly[j + 1];
            int16_t ca = pass[i].axis ? a->y : a->x;
            int16_t cb = pass[i].axis ? b->y : b->x;
            int ain = pass[i].keep_above ? ca > bound : ca < bound;
            int bin = pass[i].keep_above ? cb > bound : cb < bound;
            if (ain) buf[m++] = *a;
            if (ain != bin)
                buf[m++] = shaded_cross(a, b, pass[i].axis, bound,
                                        (int16_t)(ain ? 0
                                                  : pass[i].neg ? -1 : 1), r);
            if (m > UW_CLIP_MAX_VERTS) return -1;
        }
        if (m == 0) return 0;                        /* jcxz: nothing drawn */
        for (n = 0; n < m; n++) poly[n] = buf[n];
    }
    return n;
}

long uw_gfx_draw_polygon_shaded(const uw_fb *fb, uw_shade_vert *v, int n,
                                const uw_clip_rect *r, const uint8_t *light,
                                int gouraud, uint8_t base, long *overrun,
                                uint8_t *colour) {
    span_mode mode = { gouraud, base, colour };
    if (n < 0 || n > 99) return 0;
    if (n <= 2) return -1;
    n = uw_clip_polygon_shaded(v, n, r);
    if (n <= 0) return 0;
    return shade_polygon(fb, v, n, light, &mode, overrun);
}
