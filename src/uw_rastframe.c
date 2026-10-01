/* SPDX-License-Identifier: MIT */
/* See uw_rastframe.h. */
#include "uw_rastframe.h"
#include "uw_trig.h"

static uint16_t rw(const uint8_t *m, uint16_t at) {
    return (uint16_t)(m[at] | (m[(uint16_t)(at + 1)] << 8));
}
static int16_t rs(const uint8_t *m, uint16_t at) { return (int16_t)rw(m, at); }
static void ww(uint8_t *m, uint16_t at, uint16_t w) {
    m[at] = (uint8_t)w;
    m[(uint16_t)(at + 1)] = (uint8_t)(w >> 8);
}

/* `imul b; shl ax,1; rcl dx,1` -> dx. */
static int16_t fx(int16_t a, int16_t b) {
    return (int16_t)((uint32_t)((int32_t)a * b * 2) >> 16);
}

/* The saturating row product of rast_mat_x_vec and rast_vec_x_mat_bp: the
 * doubled 32-bit sum's high word, and on a signed overflow of the doubling
 * the opposite extreme, 0x8000 made 0x8001. */
static int16_t sat_row(int32_t sum, int *ovf) {
    int64_t twice = (int64_t)sum * 2;
    uint16_t hi = (uint16_t)((uint32_t)twice >> 16);
    *ovf = twice > INT32_MAX || twice < INT32_MIN;
    return (int16_t)hi;
}
static int16_t saturate(int16_t hi) {
    return (int16_t)(hi >= 0 ? 0x8001 : 0x7fff);
}

/* rast_mat_x_vec: M (nine words by rows, in `mm` at `bp`) times
 * v (three words in `vm` at `si`). */
static void mat_x_vec(const uint8_t *mm, uint16_t bp, const uint8_t *vm, uint16_t si,
                      int16_t out[3]) {
    int r, ovf;
    int16_t cx = 0, di;
    for (r = 0; r < 3; r++) {
        int32_t sum = (int32_t)rs(vm, si) * rs(mm, (uint16_t)(bp + r * 6))
                    + (int32_t)rs(vm, (uint16_t)(si + 2)) * rs(mm, (uint16_t)(bp + r * 6 + 2))
                    + (int32_t)rs(vm, (uint16_t)(si + 4)) * rs(mm, (uint16_t)(bp + r * 6 + 4));
        int16_t v = sat_row(sum, &ovf);
        if (r < 2) {
            if (ovf) v = saturate(v);
            if ((uint16_t)v == 0x8000) v = (int16_t)0x8001;
            if (r == 0) out[0] = v; else cx = v;
        } else {
            di = v;
            if (ovf) cx = saturate(cx);           /* 5bb8: the second result */
            if ((uint16_t)di == 0x8000) di = (int16_t)0x8001;
            out[2] = di;
        }
    }
    out[1] = cx;
}

/* rast_vec_x_mat_bp: row `bp` of M times the columns of S (nine
 * words in `sm` at `si`), with the same slip in the third term. */
static void row_x_mat(const uint8_t *mm, uint16_t bp, const uint8_t *sm, uint16_t si,
                      int16_t out[3]) {
    int c, ovf;
    int16_t cx = 0, di;
    for (c = 0; c < 3; c++) {
        int32_t sum = (int32_t)rs(mm, bp) * rs(sm, (uint16_t)(si + c * 2))
                    + (int32_t)rs(mm, (uint16_t)(bp + 2)) * rs(sm, (uint16_t)(si + 6 + c * 2))
                    + (int32_t)rs(mm, (uint16_t)(bp + 4)) * rs(sm, (uint16_t)(si + 12 + c * 2));
        int16_t v = sat_row(sum, &ovf);
        if (c < 2) {
            if (ovf) v = saturate(v);
            if ((uint16_t)v == 0x8000) v = (int16_t)0x8001;
            if (c == 0) out[0] = v; else cx = v;
        } else {
            di = v;
            if (ovf) cx = saturate(cx);
            if ((uint16_t)di == 0x8000) di = (int16_t)0x8001;
            out[2] = di;
        }
    }
    out[1] = cx;
}

/* gfx_sincos_lerp_lookup_far of a negated angle: sine in AX,
 * cosine in BX. */
static void sc_neg(int16_t angle, int16_t *s, int16_t *c) {
    uw_sincos_lerp((uint16_t)-angle, s, c);
}

/* rast_matrix_from_angles: nine words at SS:bp from the three
 * angles at SS:bp+0x12, +0x14, +0x16. */
static void matrix_from_angles(uw_rast_frame *f, uint16_t bp) {
    uint8_t *p = f->priv;
    int16_t s1, c1, s2, c2, s3, c3, bx, di, cx, si;
    sc_neg(rs(p, (uint16_t)(bp + 0x12)), &s1, &c1);   /* 0x196, 0x194 */
    sc_neg(rs(p, (uint16_t)(bp + 0x14)), &s2, &c2);   /* 0x192, 0x190 */
    sc_neg(rs(p, (uint16_t)(bp + 0x16)), &s3, &c3);   /* 0x18e, 0x18c */
    ww(f->rast, 0x194, (uint16_t)c1); ww(f->rast, 0x196, (uint16_t)s1);
    ww(f->rast, 0x190, (uint16_t)c2); ww(f->rast, 0x192, (uint16_t)s2);
    ww(f->rast, 0x18c, (uint16_t)c3); ww(f->rast, 0x18e, (uint16_t)s3);
    ww(p, (uint16_t)(bp + 0xa), (uint16_t)-s1);
    bx = fx(c2, c3);
    di = fx(s2, s3);
    ww(p, bp, (uint16_t)(fx(di, s1) + bx));
    ww(p, (uint16_t)(bp + 6), (uint16_t)-fx(s2, c1));
    cx = fx(c2, s3);
    si = fx(s2, c3);
    ww(p, (uint16_t)(bp + 0xc), (uint16_t)-(fx(si, s1) - cx));
    ww(p, (uint16_t)(bp + 2), (uint16_t)(si - fx(cx, s1)));
    ww(p, (uint16_t)(bp + 8), (uint16_t)fx(c2, c1));
    ww(p, (uint16_t)(bp + 0xe), (uint16_t)(fx(bx, s1) + di));
    ww(p, (uint16_t)(bp + 4), (uint16_t)-fx(s3, c1));
    ww(p, (uint16_t)(bp + 0x10), (uint16_t)fx(c3, c1));
}

/* rast_matrix_from_two_angles: nine words in the program at di
 * from the two angles at si. */
static void matrix_from_two_angles(uw_rast_frame *f, uint16_t si, uint16_t di) {
    uint8_t *r = f->rast;
    int16_t s1, c1, s3, c3;
    sc_neg(rs(r, si), &s1, &c1);
    sc_neg(rs(r, (uint16_t)(si + 2)), &s3, &c3);
    ww(r, 0x194, (uint16_t)c1); ww(r, 0x196, (uint16_t)s1);
    ww(r, 0x18c, (uint16_t)c3); ww(r, 0x18e, (uint16_t)s3);
    ww(r, di, (uint16_t)c3);
    ww(r, (uint16_t)(di + 2), (uint16_t)-fx(s3, s1));
    ww(r, (uint16_t)(di + 4), (uint16_t)-fx(s3, c1));
    ww(r, (uint16_t)(di + 6), 0);
    ww(r, (uint16_t)(di + 8), (uint16_t)c1);
    ww(r, (uint16_t)(di + 0xa), (uint16_t)-s1);
    ww(r, (uint16_t)(di + 0xc), (uint16_t)s3);
    ww(r, (uint16_t)(di + 0xe), (uint16_t)fx(c3, s1));
    ww(r, (uint16_t)(di + 0x10), (uint16_t)fx(c3, c1));
}

/* rast_cam_from_block. `si` points past the handler word. */
static void cam_from_block(uw_rast_frame *f, uint16_t si) {
    uint8_t *r = f->rast, *p = f->priv;
    uint16_t bp, flag;
    int16_t v[3];
    int k;

    ww(r, 0x2726, rw(r, si)); si = (uint16_t)(si + 2);
    bp = rw(r, si); si = (uint16_t)(si + 2);
    flag = rw(r, si); si = (uint16_t)(si + 2);
    if (flag == 0 && rw(p, 0x9b2) == 0) ww(r, 0x15c, (uint16_t)(bp - 4));
    if (rw(r, (uint16_t)(si + 6)) == 0) {
        uint16_t di;
        ww(r, (uint16_t)(si + 6), 1);
        matrix_from_two_angles(f, (uint16_t)(si + 8), (uint16_t)(si + 0xc));
        di = rw(r, (uint16_t)(si + 0x1e));
        if (di) {
            /* rast_cam_offset_scale: the eye offset is the
             * program matrix's third column scaled by di, negated. */
            ww(r, si, (uint16_t)-fx((int16_t)di, rs(r, (uint16_t)(si + 0x10))));
            ww(r, (uint16_t)(si + 2), (uint16_t)-fx((int16_t)di, rs(r, (uint16_t)(si + 0x16))));
            ww(r, (uint16_t)(si + 4), (uint16_t)-fx((int16_t)di, rs(r, (uint16_t)(si + 0x1c))));
        }
    }
    bp = (uint16_t)(bp + 0x12);
    if (rw(p, (uint16_t)(bp - 0x12)) != 0) matrix_from_angles(f, bp);
    mat_x_vec(p, bp, r, si, v);
    for (k = 0; k < 3; k++) {
        uint16_t at = (uint16_t)(bp - 0xc + k * 4);
        int32_t pos = (int32_t)((uint32_t)rw(p, at) | ((uint32_t)rw(p, (uint16_t)(at + 2)) << 16));
        int32_t cam = pos + v[k];
        ww(r, (uint16_t)(0x26ba + k * 4), (uint16_t)cam);
        ww(r, (uint16_t)(0x26bc + k * 4), (uint16_t)((uint32_t)cam >> 16));
    }
    si = (uint16_t)(si + 0xc);
    /* rast_basis_set_rows. */
    for (k = 0; k < 3; k++) {
        int16_t row[3];
        row_x_mat(p, (uint16_t)(bp + k * 6), r, si, row);
        ww(r, (uint16_t)(0x1602 + k * 6), (uint16_t)row[0]);
        ww(r, (uint16_t)(0x1604 + k * 6), (uint16_t)row[1]);
        ww(r, (uint16_t)(0x1606 + k * 6), (uint16_t)row[2]);
    }
}

static void scale_words(uint8_t *r, const uint16_t *at, int n, int16_t k) {
    int i;
    for (i = 0; i < n; i++) ww(r, at[i], (uint16_t)fx(rs(r, at[i]), k));
}

static int16_t bswap16(uint16_t w) { return (int16_t)((w >> 8) | (w << 8)); }

/* `idiv di` of (v << 15), 0x8000 made 0x8001. A quotient that does not fit
 * a word, or a zero divisor, faults into rast_div_saturate_handler,
 * which apply_projection installs as the divide handler just before: it
 * steps past the two-byte idiv and returns 0x7fff whatever the sign. A
 * forward row straight down an axis gives exactly 32768 and takes it. */
static int16_t div15(uw_rast_frame *f, int16_t v, uint16_t di) {
    int32_t q;
    if ((int16_t)di == 0) { f->saturated++; return 0x7fff; }
    q = ((int32_t)v * 32768) / (int16_t)di;
    if (q > 32767 || q < -32768) { f->saturated++; return 0x7fff; }
    if (q == -32768) q = -32767;
    return (int16_t)q;
}

/* rast_basis_apply_projection. */
static void apply_projection(uw_rast_frame *f) {
    uint8_t *r = f->rast;
    int16_t aspect = rs(r, 0x15f4), k;
    int32_t xs, ys;
    int16_t xhi, yhi;

    ww(r, 0x2728, 0x7fff);
    ww(r, 0x272a, 0x7fff);
    ww(r, 0x1614, rw(r, 0x1608));
    ww(r, 0x1616, rw(r, 0x160a));
    ww(r, 0x1618, rw(r, 0x160c));
    /* Match the physical viewport rather than the intermediate framebuffer.
     * Landscape windows retain vertical FOV and gain horizontal FOV. */
    if (f->output_width > 0 && f->output_height > 0) {
        xs = (int32_t)((uint32_t)f->output_width << 16);
        ys = (int32_t)((uint32_t)f->output_height << 16);
    } else if (aspect >= 0) {
        xs = (int32_t)((uint32_t)((int32_t)aspect * rs(r, 0x26b2)) << 1);
        ys = (int32_t)((uint32_t)rw(r, 0x26b0) << 16);
    } else {
        ys = (int32_t)((uint32_t)((int32_t)-aspect * rs(r, 0x26b0)) << 1);
        xs = (int32_t)((uint32_t)rw(r, 0x26b2) << 16);
    }
    xhi = (int16_t)((uint32_t)xs >> 16);
    yhi = (int16_t)((uint32_t)ys >> 16);
    if (xhi > yhi) {
        static const uint16_t col0[] = { 0x1602, 0x1608, 0x160e, 0x1616, 0x1618 };
        int16_t q = (int16_t)((int32_t)((uint32_t)ys >> 1) / xhi);
        ww(r, 0x2728, (uint16_t)q);
        scale_words(r, col0, 5, q);
    } else if (xhi < yhi) {
        static const uint16_t col1[] = { 0x1604, 0x160a, 0x1610, 0x1614, 0x1618 };
        int16_t q = (int16_t)((int32_t)((uint32_t)xs >> 1) / yhi);
        ww(r, 0x272a, (uint16_t)q);
        scale_words(r, col1, 5, q);
    }

    k = rs(r, 0x2726);
    if (k >= 0) {
        static const uint16_t col2[] = { 0x1606, 0x160c, 0x1612, 0x1616, 0x1614 };
        int32_t kk = (int32_t)k * k;
        int16_t a;
        ww(r, 0x2730, (uint16_t)k);
        scale_words(r, col2, 5, k);
        a = rs(r, 0x2728);
        ww(r, 0x272c, (uint16_t)a);
        ww(r, 0x2728, (uint16_t)bswap16(uw_isqrt32((uint16_t)((uint32_t)((int32_t)a * a + kk) >> 16))));
        a = rs(r, 0x272a);
        ww(r, 0x272e, (uint16_t)a);
        ww(r, 0x272a, (uint16_t)bswap16(uw_isqrt32((uint16_t)((uint32_t)((int32_t)a * a + kk) >> 16))));
    } else {
        static const uint16_t cols[] = { 0x1602, 0x1608, 0x160e, 0x1604, 0x160a, 0x1610, 0x1618 };
        int16_t n = (int16_t)-k, bp;
        uint32_t d;
        ww(r, 0x2730, 0x7fff);
        scale_words(r, cols, 7, n);
        d = (uint32_t)((int32_t)rs(r, 0x2728) * n) << 1;
        bp = (int16_t)(d >> 16);
        ww(r, 0x272c, (uint16_t)((d << 1) >> 16));
        ww(r, 0x2728, (uint16_t)bswap16(uw_isqrt32((uint16_t)(
            (uint16_t)((uint32_t)((int32_t)bp * bp) >> 16) + 0x3fff))));
        d = (uint32_t)((int32_t)rs(r, 0x272a) * n) << 1;
        bp = (int16_t)(d >> 16);
        ww(r, 0x272e, (uint16_t)((d << 1) >> 16));
        ww(r, 0x272a, (uint16_t)bswap16(uw_isqrt32((uint16_t)(
            (uint16_t)((uint32_t)((int32_t)bp * bp) >> 16) + 0x3fff))));
    }

    ww(f->priv, 0x4d5, 0x5bd1);
    /* The horizon: the forward row's x and z, normalised. */
    {
        int16_t m2 = rs(r, 0x1606), m8 = rs(r, 0x1612);
        uint32_t sum = (uint32_t)((int32_t)m2 * m2) + (uint32_t)((int32_t)m8 * m8);
        int16_t cc, ce;
        if ((sum >> 24) & 0xff) {
            uint16_t di = uw_isqrt32(sum);
            cc = div15(f, m2, di);
            ce = div15(f, m8, di);
        } else {
            int16_t m1 = rs(r, 0x1604), m7 = rs(r, 0x1610);
            uint32_t s2 = (uint32_t)((int32_t)m1 * m1) + (uint32_t)((int32_t)m7 * m7);
            uint16_t di = uw_isqrt32(s2);
            cc = div15(f, m1, di);
            ce = div15(f, m7, di);
            if (rs(r, 0x160c) >= 0) { cc = (int16_t)-cc; ce = (int16_t)-ce; }
        }
        ww(r, 0x26cc, (uint16_t)cc);
        ww(r, 0x26ce, (uint16_t)ce);
        {
            int32_t t = (int32_t)ce * rs(r, 0x1602) + (int32_t)(int16_t)-cc * rs(r, 0x160e);
            uint32_t u = (uint32_t)t << 1;
            ww(r, 0x26d0, (uint16_t)((u >> 16) + ((u >> 15) & 1)));
            t = (int32_t)ce * rs(r, 0x1604) + (int32_t)(int16_t)-cc * rs(r, 0x1610);
            u = (uint32_t)t << 1;
            ww(r, 0x26d2, (uint16_t)((u >> 16) + ((u >> 15) & 1)));
        }
    }
}

int uw_rast_frame_setup(uw_rast_frame *f) {
    uint8_t *r = f->rast;
    uint16_t block, left = 0, top = 0;
    uint16_t right = (uint16_t)(f->view_width - 1), bottom = (uint16_t)(f->view_height - 1);
    int ok = 1;

    /* rast_execute: the projection from the clip rectangle. */
    ww(r, 0x26b2, (uint16_t)((uint16_t)(right - left + 1) >> 1));
    ww(r, 0x26b0, (uint16_t)((uint16_t)(bottom - top + 1) >> 1));
    ww(r, 0x26b4, (uint16_t)((uint16_t)(right + left + 1) >> 1));
    ww(r, 0x26b6, (uint16_t)((uint16_t)(bottom + top) >> 1));
    ww(r, 0x15fa, (uint16_t)(rw(r, 0x15fa) + 1));
    r[0x2880] = 0;

    /* rast_camera_lerp: the camera program, then a move in
     * progress -- which needs a previous block, and no state has one. */
    ww(r, 0x15c, 0xffff);
    block = rw(r, 0x104);
    f->handler = rs(r, block);
    if (f->handler == 0) cam_from_block(f, (uint16_t)(block + 2));
    else { f->unsupported++; ok = 0; }
    if (rw(r, 0x106) != 0 && (rw(r, 0x106) != block || rw(r, 0x19a) != 0))
        f->unsupported++;
    /* 0bd4: the block becomes the previous one, and this frame's camera,
     * forward row and depth scale the target a later move starts from. */
    ww(r, 0x106, block);
    {
        int k;
        for (k = 0; k < 6; k++)
            ww(r, (uint16_t)(0x19c + k * 2), rw(r, (uint16_t)(0x26ba + k * 2)));
        ww(r, 0x1a8, rw(r, 0x1606));
        ww(r, 0x1aa, rw(r, 0x160c));
        ww(r, 0x1ac, rw(r, 0x1612));
        ww(r, 0x1ae, rw(r, 0x2726));
    }

    /* rast_patch_depth_axis. */
    {
        int16_t m5 = rs(r, 0x160c), m2 = rs(r, 0x1606), m8 = rs(r, 0x1612);
        int16_t sign;
        if ((m5 < 0 ? -m5 : m5) > 0x5a82) {
            f->depth_axis = 2;
            sign = m5;
        } else if ((m2 < 0 ? -m2 : m2) > (m8 < 0 ? -m8 : m8)) {
            f->depth_axis = 0;
            sign = m2;
        } else {
            f->depth_axis = 4;
            sign = m8;
        }
        f->depth_op = sign < 0 ? 0x03 : 0x2b;
    }
    apply_projection(f);
    return ok;
}
