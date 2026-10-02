/* SPDX-License-Identifier: MIT */
#include "uw_texmap.h"
#include <math.h>

void uw_gfx_texture_poly_perspective(const uw_fb *fb, const uw_rast_svert *v,
                                     int n, const uw_rast_texrec *tex,
                                     const uint8_t *texels, size_t n_texels) {
    uw_gfx_texture_poly_perspective_lit(fb, v, n, tex, texels, n_texels,
                                        NULL, NULL, NULL);
}

void uw_gfx_texture_poly_perspective_lit(const uw_fb *fb, const uw_rast_svert *v,
                                         int n, const uw_rast_texrec *tex,
                                         const uint8_t *texels, size_t n_texels,
                                         const uint8_t *shade,
                                         const uint8_t *light, long *overrun) {
    int i, y, ymin, ymax;
    if (!fb || !fb->pixels || !fb->row || fb->n_rows <= 0
        || !v || n < 3 || !tex || !texels) return;
    ymin = ymax = v[0].sy;
    for (i = 0; i < n; i++) {
        if (v[i].z <= 0) return; /* near clipping belongs to the rasteriser */
        if (v[i].sy < ymin) ymin = v[i].sy;
        if (v[i].sy > ymax) ymax = v[i].sy;
    }
    if (ymin == ymax) return;
    if (ymin < 0) ymin = 0;
    if (ymax >= fb->n_rows) ymax = fb->n_rows - 1;
    for (y = ymin; y <= ymax; y++) {
        double lx = 0, rx = 0, lq = 0, rq = 0;
        double lu = 0, ru = 0, lv = 0, rv = 0;
        double ls = 0, rs = 0;
        size_t row = fb->row[y], end;
        int hits = 0, x, first, last;
        if (row >= fb->size) continue;
        /* Bound each span to its row as well as the framebuffer. */
        end = y + 1 < fb->n_rows ? fb->row[y + 1] : fb->size;
        if (y + 1 == fb->n_rows && y > 0 && row > fb->row[y - 1]) {
            size_t width = row - fb->row[y - 1];
            if (width < end - row) end = row + width;
        }
        if (end > fb->size) end = fb->size;
        if (end <= row) continue;
        for (i = 0; i < n; i++) {
            const uw_rast_svert *a = &v[i], *b = &v[(i + 1) % n];
            double t, sx, q, u, vv, aq, bq, s = 0;
            if (a->sy == b->sy) continue;
            /* Evaluate shared edges in the same direction regardless of
             * polygon winding, avoiding different floating-point roundoff. */
            if (a->sy < b->sy) {
                const uw_rast_svert *tmp = a; a = b; b = tmp;
            }
            if (y < b->sy || y > a->sy) continue;
            t = (double)(y - a->sy) / (b->sy - a->sy);
            aq = 1.0 / a->z; bq = 1.0 / b->z;
            sx = a->sx + t * (b->sx - a->sx);
            q = aq + t * (bq - aq);
            u = a->u * aq + t * (b->u * bq - a->u * aq);
            vv = a->v * aq + t * (b->v * bq - a->v * aq);
            if (shade && light)
                s = shade[a - v] + t * (shade[b - v] - shade[a - v]);
            if (!hits || sx < lx) { lx = sx; lq = q; lu = u; lv = vv; ls = s; }
            if (!hits || sx > rx) { rx = sx; rq = q; ru = u; rv = vv; rs = s; }
            hits++;
        }
        if (hits < 2) continue;
        /* The original texture and lighting passes seed edges at x + 1/2.
         * Round both borders to the nearest pixel, inclusively. Rounding
         * inward leaves cracks at rounded subdivision vertices where a
         * short edge meets a neighbouring section's unsplit long edge. */
        first = (int)floor(lx + 0.5); last = (int)floor(rx + 0.5);
        if (first < 0) first = 0;
        if ((size_t)(last < 0 ? 0 : last) >= end - row)
            last = (int)(end - row - 1);
        for (x = first; x <= last; x++) {
            double t = rx > lx ? (x - lx) / (rx - lx) : 0;
            /* Rounded coverage can extend half a pixel beyond the face.
             * Sampling must stay on its edge rather than wrap UVs there. */
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            double q = lq + t * (rq - lq);
            /* Floor before wrapping, including negative coordinates. Absorb
             * roundoff at exact integer texel boundaries (notably corners). */
            uint16_t u = (uint16_t)(int32_t)floor((lu + t * (ru - lu)) / q + 1e-8);
            uint16_t vv = (uint16_t)(int32_t)floor((lv + t * (rv - lv)) / q + 1e-8);
            size_t off = (size_t)(vv & tex->v_mask) + (u >> 8);
            if (off < n_texels) {
                uint8_t pixel = texels[off];
                if (shade && light) {
                    /* Affine vertex lighting with the game's alternating
                     * quarter/three-quarter shade dither, anchored to the
                     * screen so adjacent sections use the same pattern. */
                    uint8_t level = (uint8_t)floor(ls + t * (rs - ls)
                                                  + ((x ^ y) & 1 ? 0.75 : 0.25));
                    if (level > 15) {
                        if (overrun) (*overrun)++;
                        level = level & 0x80 ? 0 : 15;
                    }
                    pixel = light[(unsigned)level * 256u + pixel];
                }
                fb->pixels[row + (size_t)x] = pixel;
            }
        }
    }
}

/* `idiv` with the graphics module's divide-error handler NOT armed: these
 * are the mapper's own divides and the module traps on overflow rather than
 * saturating, so a port that would overflow has already gone wrong. The
 * clamp keeps this defined in C and is asserted never to fire on real
 * geometry. */
static int16_t idiv16(int32_t num, int16_t den, int32_t *rem) {
    int32_t q;
    if (den == 0) { *rem = 0; return 0; }
    if (den == -1 && num == INT32_MIN) { *rem = 0; return 0; }
    q = num / den;
    *rem = num % den;
    if (q > 32767) q = 32767;
    if (q < -32768) q = -32768;
    return (int16_t)q;
}

int32_t uw_gfx_step16(int16_t delta, int16_t rows) {
    int32_t rem, half, rem2;
    int16_t q, f;

    /* `cwd; idiv cx` -- the whole part, truncated toward zero. */
    q = idiv16((int32_t)delta, rows, &rem);
    /* `xor ax,ax; sar dx,1; rcr ax,1` -- the remainder in the high word,
     * halved, so that the second `idiv` cannot overflow a word. */
    half = (int32_t)(((uint32_t)rem << 16) >> 1);
    if (rem < 0) half = (int32_t)((uint32_t)half | 0x80000000u);  /* SAR */
    f = idiv16(half, rows, &rem2);
    /* `cwd; shl ax,1; rcl dx,1` -- doubled back, the low word becoming the
     * fraction and the high word an extra whole step. */
    return (int32_t)(((uint32_t)(int32_t)q << 16) + (uint32_t)((int32_t)f * 2));
}

/* One edge of the walk. The original keeps these in data-segment words,
 * left and right, and its steps in the immediates of its
 * `add`/`adc` pairs. */
typedef struct {
    int32_t x, u, v;        /* 16.16 */
    int32_t dx, du, dv;
    int16_t last_y;         /* the y this edge is heading for */
    int     cursor;         /* the vertex it is heading for */
    int     forward;        /* which way round the ring */
} uw_edge;

static int16_t hi16(int32_t a) { return (int16_t)((uint32_t)a >> 16); }

/* One call of an edge stepper. `reload` distinguishes the routine's two
 * entry points: the affine mapper always calls the top, which reloads the
 * accumulators from the vertex, while the wall mapper's RESEEDS call eight
 * bytes in and keep whatever the previous edge left.
 * `u_steps` is false for the wall, whose u comes from the column table.
 *
 * Returns 0 when the ring is exhausted -- the `dec [0x7d1]; jz`. */
static int seed_edge2(uw_edge *e, const uw_rast_svert *v, int n,
                      int *left, int16_t row, int reload, int u_steps) {
    for (;;) {
        const uw_rast_svert *a = &v[e->cursor];
        const uw_rast_svert *b;
        int16_t rows;

        /* The accumulators take the vertex, and the fractions ONE HALF. */
        if (reload) {
            e->x = ((int32_t)a->sx << 16) | 0x8000;
            e->v = ((int32_t)a->v  << 16) | 0x8000;
            if (u_steps) e->u = ((int32_t)a->u << 16) | 0x8000;
        }
        /* A zero-row edge re-enters at the TOP, which reloads whether this
         * call did or not -- not at the reload's skip. */
        reload = 1;

        e->cursor = e->forward ? (e->cursor + 1) % n
                               : (e->cursor + n - 1) % n;
        if (--*left == 0) return 0;

        b = &v[e->cursor];
        e->last_y = b->sy;
        rows = (int16_t)((uint16_t)row - (uint16_t)b->sy);
        /* A horizontal edge contributes no rows; the original jumps back to
         * its own entry and takes the next vertex. */
        if (rows == 0) continue;

        /* The steps are the difference between the vertex the edge is
         * heading for and THE ACCUMULATOR, not the vertex it came from --
         * which is the same thing when the accumulator was just reloaded
         * and is not when the wall mapper kept it. */
        e->dx = uw_gfx_step16((int16_t)((uint16_t)b->sx
                                        - (uint16_t)hi16(e->x)), rows);
        if (u_steps)
            e->du = uw_gfx_step16((int16_t)((uint16_t)b->u
                                            - (uint16_t)hi16(e->u)), rows);
        e->dv = uw_gfx_step16((int16_t)((uint16_t)b->v
                                        - (uint16_t)hi16(e->v)), rows);
        return 1;
    }
}

static int seed_edge(uw_edge *e, const uw_rast_svert *v, int n,
                     int *left, int16_t row) {
    return seed_edge2(e, v, n, left, row, 1, 1);
}

static int reseed_edge(uw_edge *e, const uw_rast_svert *v, int n,
                       int *left, int16_t row) {
    return seed_edge2(e, v, n, left, row, 0, 0);
}

/* The walk, with the drawing optional. `fb` NULL walks without writing a
 * pixel; `stop_row` and `out` report the accumulators at the moment the walk
 * reaches that row, as the original holds them mid-polygon. */
static int run(const uw_fb *fb, const uw_rast_svert *v,
               int n, const uw_rast_texrec *tex,
               const uint8_t *texels, size_t n_texels,
               int stop_row, uw_gfx_edge_state *out) {
    uw_edge left, right;
    int remaining = n + 1;      /* the edges left */
    int start = 0, i;
    int16_t row;                /* the current row */

    if (n < 3) return 0;

    /* The vertex with the GREATEST y, strictly -- `cmp ax,[di+2]; jle`, so
     * the first of several equal ones wins. */
    for (i = 1; i < n; i++)
        if (v[i].sy > v[start].sy) start = i;

    row = v[start].sy;
    left.forward = 0;  left.cursor = start;
    right.forward = 1; right.cursor = start;

    if (!seed_edge(&left, v, n, &remaining, row)) return 0;
    if (!seed_edge(&right, v, n, &remaining, row)) return 0;

    for (;;) {
        int16_t lx, rx;

        if (out && row == stop_row) {
            out->lx = left.x;  out->lu = left.u;  out->lv = left.v;
            out->rx = right.x; out->ru = right.u; out->rv = right.v;
            out->row = row;
            out->remaining = remaining;
            return 1;
        }
        lx = hi16(left.x); rx = hi16(right.x);
        int16_t width = (int16_t)((uint16_t)rx - (uint16_t)lx);
        int32_t du = 0, dv = 0;
        int count;

        if (width != 0) {
            int32_t rem;
            int16_t q;
            /* u is 8.8 already, so it gets a whole-number step and its
             * fraction is the low byte it carries. */
            du = idiv16((int32_t)(int16_t)((uint16_t)hi16(right.u)
                                           - (uint16_t)hi16(left.u)), width,
                        &rem);
            /* v gets the same two-divide treatment the edges get. */
            q = idiv16((int32_t)(int16_t)((uint16_t)hi16(right.v)
                                          - (uint16_t)hi16(left.v)), width,
                       &rem);
            {
                int32_t half = (int32_t)(((uint32_t)rem << 16) >> 1);
                int32_t rem2;
                int16_t f;
                if (rem < 0) half = (int32_t)((uint32_t)half | 0x80000000u);
                f = idiv16(half, width, &rem2);
                dv = (int32_t)(((uint32_t)(int32_t)q << 16) + (uint32_t)((int32_t)f * 2));
            }
        }
        /* `inc cx; jg` -- the width is a pixel COUNT one greater than the
         * difference, and a negative one abandons the polygon. */
        count = width + 1;
        if (count <= 0) return 0;

        if (fb && texels && row >= 0 && row < fb->n_rows) {
            /* DX is u as 8.8 in ONE WORD, AX:BP is v as 16.16, and DI is a
             * word too in the original. Keep texture coordinates wrapping,
             * but widen the host destination for views larger than 64K. */
            uint16_t u = (uint16_t)hi16(left.u);
            uint32_t vv = (uint32_t)left.v;
            uint32_t di = fb->row[row] + (uint16_t)lx;
            int k;
            for (k = 0; k < count; k++) {
                uint32_t off = (uint32_t)((uint16_t)(vv >> 16) & tex->v_mask)
                             + (uint32_t)(u >> 8);
                if (di < fb->size && off < n_texels)
                    fb->pixels[di] = texels[off];
                di++;
                u = (uint16_t)(u + (uint16_t)du);
                vv += (uint32_t)dv;
            }
        }

        /* The per-row advance: six `add`/`adc` pairs. */
        row--;
        left.x = (int32_t)((uint32_t)left.x + (uint32_t)left.dx);
        left.u = (int32_t)((uint32_t)left.u + (uint32_t)left.du);
        left.v = (int32_t)((uint32_t)left.v + (uint32_t)left.dv);
        right.x = (int32_t)((uint32_t)right.x + (uint32_t)right.dx);
        right.u = (int32_t)((uint32_t)right.u + (uint32_t)right.du);
        right.v = (int32_t)((uint32_t)right.v + (uint32_t)right.dv);

        if (row <= left.last_y)
            if (!seed_edge(&left, v, n, &remaining, row)) return 0;
        if (row <= right.last_y)
            if (!seed_edge(&right, v, n, &remaining, row)) return 0;
    }
}

void uw_gfx_texture_poly_affine(const uw_fb *fb, const uw_rast_svert *v,
                                int n, const uw_rast_texrec *tex,
                                const uint8_t *texels, size_t n_texels) {
    if (!fb || !fb->pixels || !texels) return;
    (void)run(fb, v, n, tex, texels, n_texels, INT16_MIN, NULL);
}

int uw_gfx_affine_walk_to(const uw_rast_svert *v, int n, int stop_row,
                          uw_gfx_edge_state *out) {
    uw_rast_texrec t = {0, 0, 0, 0};
    return run(NULL, v, n, &t, NULL, 0, stop_row, out);
}

/* ---- gfx_texture_poly_wall --------------------------------- */

/* The linear fill: u stepped across the columns between
 * the two extreme vertices by one truncated divide. */
static int wall_ucol_linear(const uw_rast_svert *v, int lo, int hi,
                            uint8_t *ucol, int n_ucol) {
    int cols = (int)(int16_t)((uint16_t)v[hi].sx - (uint16_t)v[lo].sx);
    int x = v[lo].sx, i;
    int16_t u = v[lo].u;
    int16_t step;
    int32_t rem;
    if (cols <= 0) {
        if (x >= 0 && x + 2 < n_ucol) {
            ucol[x] = (uint8_t)((uint16_t)u >> 8);
            ucol[x + 2] = (uint8_t)((uint16_t)u >> 8);
        }
        return 1;
    }
    step = idiv16((int32_t)(int16_t)((uint16_t)v[hi].u - (uint16_t)v[lo].u),
                  (int16_t)cols, &rem);
    for (i = 0; i <= cols; i++) {
        if (x + i >= 0 && x + i < n_ucol)
            ucol[x + i] = (uint8_t)((uint16_t)u >> 8);
        u = (int16_t)((uint16_t)u + (uint16_t)step);
    }
    return cols + 1;
}



/* Fills `ucol[x]` with the texel column for screen column x. Transcribed
 * from the instructions. */
int uw_gfx_wall_ucol(const uw_rast_svert *v, int n, const uw_rast_proj *proj,
                     uint8_t *ucol, int n_ucol, uw_gfx_wall_setup *out) {
    int lo = 0, hi = 0, i;
    int16_t min_z, dz4_over_z;
    int32_t rem;

    if (n < 3 || !ucol) return -1;

    /* The leftmost and rightmost vertices -- `cmp dx,ax; jge` keeps the
     * FIRST of several equal ones on the right and the first on the left,
     * which is not the same rule twice over. */
    for (i = 1; i < n; i++) {
        if (v[i].sx > v[hi].sx) hi = i;
        if (v[i].sx < v[lo].sx) lo = i;
    }

    /* `4 * |z(hi) - z(lo)| / min z`, and 2 or less takes the affine path. */
    min_z = v[lo].z < v[hi].z ? v[lo].z : v[hi].z;
    {
        int32_t d = (int32_t)(int16_t)((uint16_t)v[hi].z - (uint16_t)v[lo].z);
        int32_t a = d < 0 ? -d : d;
        a = (int32_t)((uint32_t)a << 2);
        dz4_over_z = idiv16(a, min_z, &rem);
    }
    if (out) {
        out->min_x = v[lo].sx;
        out->max_x = v[hi].sx;
        out->quotient = dz4_over_z;
        out->perspective = dz4_over_z > 2;
        /* `mov bp,[0x7d8]`: the leftmost vertex's address in
         * the original's vertex ring, past its x -- the linear fill leaves it. */
        out->bp = (uint16_t)(0x659 + 14 * lo + 2);
    }

    if (dz4_over_z <= 2)
        return wall_ucol_linear(v, lo, hi, ucol, n_ucol);

    /* The perspective fill. */
    {
        int16_t du = (int16_t)((uint16_t)v[hi].u - (uint16_t)v[lo].u);
        /* `mov cl,ch; xor ch,ch` -- the u difference's HIGH BYTE, unsigned,
         * so a u that falls from left to right counts up to 255. */
        int texels = (int)(uint8_t)((uint16_t)du >> 8);
        uint32_t x_acc, z_acc;
        int32_t dx_step, dz_step;
        uint8_t cur = (uint8_t)((uint16_t)v[lo].u >> 8);   /* bl = [0x7ed] */
        uint8_t last = (uint8_t)((uint16_t)v[hi].u >> 8);  /* bh */
        int cursor = v[lo].sx, written = 0, max_x = v[hi].sx;
        int trap = 0;

        /* `je 0x661`: no u difference at all is the LINEAR path. */
        if (du == 0) {
            if (out) out->perspective = 0;
            return wall_ucol_linear(v, lo, hi, ucol, n_ucol);
        }
        x_acc = (uint32_t)(uint16_t)v[lo].x << 16;
        z_acc = (uint32_t)(uint16_t)v[lo].z << 16;
        dx_step = texels ? uw_gfx_step16((int16_t)((uint16_t)v[hi].x
                                                   - (uint16_t)v[lo].x),
                                         (int16_t)texels) : 0;
        dz_step = texels ? uw_gfx_step16((int16_t)((uint16_t)v[hi].z
                                                   - (uint16_t)v[lo].z),
                                         (int16_t)texels) : 0;

        /* `or cx,cx; jle 0x7a6` -- no whole texel between the two is the
         * `int 2` below. */
        if (texels <= 0) trap = 1;

        while (!trap) {
            int16_t col, rx;
            /* 0730: z steps; then x steps AND THE NEW x IS PROJECTED --
             * `adc ax,[0x7e6]; mov [0x7e2],ax; ...; imul [0x7f0]` multiplies
             * the AX the `adc` just wrote. This port used to project the x
             * from BEFORE the step, and said the new x was the mistake;
             * crit0's steep wall is what the instructions and the pixels
             * both say it is. */
            z_acc += (uint32_t)dz_step;
            x_acc += (uint32_t)dx_step;
            rx = (int16_t)(uint16_t)(x_acc >> 16);
            col = idiv16((int32_t)rx * (int32_t)proj->scale_x,
                         (int16_t)(uint16_t)(z_acc >> 16), &rem);
            col = (int16_t)((uint16_t)col + (uint16_t)proj->off_x);

            if (col < 0) goto next_texel;                 /* js 0x7a2 */
            if (col > max_x) {
                /* 076d: fill the cursor to the right edge, and draw. */
                int count = max_x - cursor;
                if (count >= 0)
                    for (i = 0; i <= count; i++)
                        if (cursor + i >= 0 && cursor + i < n_ucol) {
                            ucol[cursor + i] = cur; written++;
                        }
                goto done;
            }
            {
                /* 0781: `mov [0x7da],ax; sub ax,di; jl 0x7a2`. */
                int count = col - cursor;
                int from = cursor;
                cursor = col;
                if (count < 0) goto next_texel;
                for (i = 0; i <= count; i++)
                    if (from + i >= 0 && from + i < n_ucol) {
                        ucol[from + i] = cur; written++;
                    }
                /* `inc bl; mov [di-1],bl` -- the run's LAST column takes the
                 * NEXT texel -- and no wrap test on this path. */
                cur = (uint8_t)(cur + 1);
                if (col >= 0 && col < n_ucol) ucol[col] = cur;
                if ((int8_t)cur < (int8_t)last) continue;  /* cmp bl,bh; jl */
                goto done;            /* jmp 0x7c6: NOTHING fills the rest */
            }
        next_texel:
            cur = (uint8_t)(cur + 1);                     /* 07a2: inc bl */
            if (cur == 0) { trap = 1; break; }            /* jne, else int 2 */
            if ((int8_t)cur >= (int8_t)last) goto done;
        }

        /* 07a6: `int 2`, which DOS returns from, and then the only tail fill
         * in the routine: from the cursor to the right edge, or the other
         * way round, `rep stosb` of the difference -- not the difference
         * plus one. A normal end of the loop does not get here, so columns
         * past the last one projected keep whatever the previous wall face
         * left in the table. */
        {
            int a = max_x - cursor, from = cursor, count;
            if (a < 0) { from = max_x; a = cursor - max_x; }
            for (count = 0; count < a; count++)
                if (from + count >= 0 && from + count < n_ucol) {
                    ucol[from + count] = cur; written++;
                }
        }
    done:
        /* `mov bp,[0x7de]` and `adc bp,[0x7ea]` each texel: BP leaves as
         * the z accumulator's integer. */
        if (out) out->bp = (uint16_t)(z_acc >> 16);
        return written;
    }
}

void uw_gfx_texture_poly_wall(const uw_fb *fb, const uw_rast_svert *v, int n,
                              const uw_rast_texrec *tex,
                              const uint8_t *texels, size_t n_texels,
                              const uw_rast_proj *proj,
                              uint8_t *ucol, int n_ucol, int32_t *span_dv) {
    uw_edge left, right;
    uw_gfx_wall_setup setup;
    int remaining = n + 1;
    int start = 0, i;
    int16_t row;
    uint16_t bp;
    int32_t dv = span_dv ? *span_dv : 0;

    if (n < 3 || !fb || !fb->pixels || !texels || !ucol) return;
    if (uw_gfx_wall_ucol(v, n, proj, ucol, n_ucol, &setup) < 0) return;
    bp = setup.bp;

    for (i = 1; i < n; i++)
        if (v[i].sy > v[start].sy) start = i;

    row = v[start].sy;
    left.forward = 0;  left.cursor = start;
    right.forward = 1; right.cursor = start;
    if (!seed_edge2(&left, v, n, &remaining, row, 1, 0)) return;
    if (!seed_edge2(&right, v, n, &remaining, row, 1, 0)) return;

    for (;;) {
        int16_t lx = hi16(left.x), rx = hi16(right.x);
        int16_t width = (int16_t)((uint16_t)rx - (uint16_t)lx);
        int count;

        if (width != 0) {
            int32_t rem;
            int16_t q;
            bp = (uint16_t)hi16(left.v);                  /* 07f2 */
            q = idiv16((int32_t)(int16_t)((uint16_t)hi16(right.v)
                                                  - (uint16_t)hi16(left.v)),
                               width, &rem);
            int32_t half = (int32_t)(((uint32_t)rem << 16) >> 1);
            int32_t rem2;
            int16_t f;
            if (rem < 0) half = (int32_t)((uint32_t)half | 0x80000000u);
            f = idiv16(half, width, &rem2);
            dv = (int32_t)(((uint32_t)(int32_t)q << 16) + (uint32_t)((int32_t)f * 2));
        }
        count = width + 1;
        /* A BACKWARDS SPAN IS SKIPPED HERE, not fatal as in the affine one. */
        if (count > 0) {
            /* 081e: `mov dx,bp; mov bp,[0x7c1]` -- DX:BP is v, its integer
             * from BP whether or not 07f2 loaded it (FOUR above). */
            uint32_t vv = ((uint32_t)bp << 16) | (uint16_t)left.v;
            int draw = row >= 0 && row < fb->n_rows;
            uint32_t di = draw ? fb->row[row] + (uint16_t)lx : 0;
            int k;
            for (k = 0; k < count; k++) {
                int col = lx + k;
                if (draw && col >= 0 && col < n_ucol) {
                    uint32_t off = (uint32_t)((uint16_t)(vv >> 16) & tex->v_mask)
                                 + (uint32_t)ucol[col];
                    if (di < fb->size && off < n_texels)
                        fb->pixels[di] = texels[off];
                }
                di++;
                vv += (uint32_t)dv;
            }
            bp = (uint16_t)vv;
        }

        row--;
        left.x = (int32_t)((uint32_t)left.x + (uint32_t)left.dx);
        left.v = (int32_t)((uint32_t)left.v + (uint32_t)left.dv);
        right.x = (int32_t)((uint32_t)right.x + (uint32_t)right.dx);
        right.v = (int32_t)((uint32_t)right.v + (uint32_t)right.dv);

        /* AND A RESEEDED EDGE KEEPS ITS ACCUMULATORS -- the calls are eight
         * bytes into the stepper, past the reload the affine one runs. */
        if (row <= left.last_y)
            if (!reseed_edge(&left, v, n, &remaining, row)) break;
        if (row <= right.last_y)
            if (!reseed_edge(&right, v, n, &remaining, row)) break;
    }
    if (span_dv) *span_dv = dv;
}
