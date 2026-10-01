/* SPDX-License-Identifier: MIT */
#include "uw_sprite.h"
#include "uw_image.h"

#include <string.h>

/* ---- the art ------------------------------------------------------------ */

size_t uw_sprite_decode(const uint8_t *art, size_t art_len,
                        const uint8_t *auxpals, size_t auxpals_len,
                        const uint8_t *light, uint8_t level,
                        uint8_t *out, size_t cap, int *width, int *height) {
    int w, h;
    size_t px;

    if (!art || art_len < 6 || !out) return 0;
    w = art[1];
    h = art[2];
    px = (size_t)w * (size_t)h;
    if (width) *width = w;
    if (height) *height = h;

    if (art[0] == UW_GR_UNCOMPRESSED) {
        /* rast_gr_decode_type4: `call 0xcb` points BX at
         * LIGHT.DAT row DH -- whose `cmp dh,0xff` sets flags nobody reads --
         * then SI = BP (image + 4), a count word, and `lodsb; cs xlatb;
         * stosb` for each pixel. */
        size_t n = uw_u16(art + 4), k;
        for (k = 0; k < n && k < cap && 6 + k < art_len; k++) {
            uint8_t v = art[6 + k];
            out[k] = light ? light[(unsigned)(level > 15 ? 15 : level) * 256u + v]
                           : v;
        }
        return k < px ? k : px;
    }
    if (art[0] == UW_GR_RLE || art[0] == UW_GR_PACKED4) {
        /* emit_sprite's palette byte: 0x20 or more becomes (byte - 8) * 2,
         * then sixteen bytes a palette into the auxiliary palettes. */
        uint8_t b = art[3];
        uint8_t aux[UW_AUXPAL_SIZE];
        size_t off;
        int k;
        static uint8_t file[0x10000];
        uw_gr g;
        uw_gr_info info;

        if (b >= 0x20) b = (uint8_t)((uint8_t)(b - 8) << 1);
        off = (size_t)b << 4;
        if (!auxpals || off + UW_AUXPAL_SIZE > auxpals_len) return 0;
        /* rast_gr_xlat_build: each colour through LIGHT.DAT row
         * DH, or copied as it is when DH = 0xff. */
        for (k = 0; k < UW_AUXPAL_SIZE; k++) {
            uint8_t c = auxpals[off + (size_t)k];
            aux[k] = (level == 0xff || !light) ? c
                   : light[(unsigned)(level > 15 ? 15 : level) * 256u + c];
        }
        /* The .gr reader takes a file; a one-slot file around the art is
         * the same bytes at the same offsets. */
        /* An art id names a place, not a length: the image runs as far as
         * its own counts say. 64K is more than any retail image. */
        if (art_len > sizeof file - 7) art_len = sizeof file - 7;
        memset(file, 0, 7);
        file[0] = 1;
        file[1] = 1;
        file[3] = 7;
        memcpy(file + 7, art, art_len);
        g.file.data = file;
        g.file.size = art_len + 7;
        g.file.why = NULL;
        g.count = 1;
        if (!uw_gr_image(&g, 0, aux, out, cap, &info)) return 0;
        return px;
    }
    return 0;
}

/* ---- a creature frame ------------------------------------------------------ */

int uw_creature_find(const uint8_t *page, size_t len, uint16_t frame,
                     uint16_t dir, uint8_t variant, uw_creature_frame *out) {
    size_t di, at;
    uint16_t bp;
    uint8_t anim, n1, n2, n3, idx;

    if (!page || len < 4 || !out) return 0;
    /* 7ce8: `sub bp,ax` -- the frame word less the file's first frame. */
    bp = (uint16_t)(frame - page[0]);
    n1 = page[1];
    if ((size_t)bp + 2 >= len) return 0;
    anim = page[(size_t)bp + 2];
    if (anim == 0xff) return 0;                           /* 7cf6 */
    di = 2 + (size_t)n1;
    if (di >= len) return 0;
    n2 = page[di++];
    /* 7d02: eight per animation, `shl bp,3; add bp,bx` with BX the word. */
    at = di + (size_t)(uint16_t)(((uint16_t)anim << 3) + dir);
    if (at >= len) return 0;
    idx = page[at];
    if (idx == 0xff) idx = 0;
    di += (size_t)n2 * 8;
    if (di >= len) return 0;
    n3 = page[di++];
    if (n3 <= variant) return 0;                          /* 7d1e jbe */
    out->palette_at = di + (size_t)variant * 32;
    di += (size_t)n3 * 32;
    di++;                                  /* the frame count; unused here */
    di++;                                  /* and the byte after it */
    at = di + 2 * (size_t)idx;
    if (at + 2 > len) return 0;
    out->frame_at = uw_u16(page + at);
    if (out->frame_at + 7 > len) return 0;
    out->width = page[out->frame_at];
    out->height = page[out->frame_at + 1];
    out->hot_x = page[out->frame_at + 2];
    out->hot_y = page[out->frame_at + 3];
    out->type = page[out->frame_at + 4];
    return 1;
}

size_t uw_creature_decode(const uint8_t *page, size_t len,
                          const uw_creature_frame *f, const uint8_t *light,
                          uint8_t level, uint8_t *out, size_t cap) {
    static uint8_t code[1 << 16], vals[1 << 16];
    uint8_t pal[32];
    int bits = f->type == 6 ? 5 : f->type == 8 ? 4 : 0;
    int entries = f->type == 6 ? 32 : 16, k;
    size_t want = (size_t)f->width * (size_t)f->height, n, made, count;

    if (!bits || f->frame_at + 7 > len || f->palette_at + 32 > len) return 0;
    /* rast_gr_xlat_build with CX = 2 for type 6, 1 for type 8. */
    for (k = 0; k < entries; k++) {
        uint8_t c = page[f->palette_at + (size_t)k];
        pal[k] = (level == 0xff || !light) ? c
               : light[(unsigned)(level > 15 ? 15 : level) * 256u + c];
    }
    count = uw_u16(page + f->frame_at + 5);
    if (count > sizeof code) return 0;
    n = uw_bit_codes(page + f->frame_at + 7, len - f->frame_at - 7, count,
                     bits, code, sizeof code);
    made = uw_rle_decode(code, n, vals, want + 8 < sizeof vals ? want + 8
                                                           : sizeof vals,
                         want, NULL, NULL);
    if (made < want) return 0;
    for (n = 0; n < want && n < cap; n++)
        out[n] = pal[vals[n] & (entries - 1)];
    return want;
}

/* ---- the placement ------------------------------------------------------- */

/* `imul r/m8`: AL times a byte, both signed, into AX. */
static int16_t imul8(int16_t ax, int16_t byte) {
    return (int16_t)((int8_t)(uint8_t)ax * (int8_t)(uint8_t)byte);
}

/* `imul r/m16`, the high word. */
static int16_t hi_mul(int16_t a, int16_t b) {
    return (int16_t)(((int32_t)a * (int32_t)b) >> 16);
}

/* `imul; idiv bp` under rast_div_saturate_handler: an overflow
 * skips the divide with AX = 0x7fff and counts, and a count cancels the draw. */
static int16_t project(int16_t v, int16_t scale, int16_t z, int16_t off,
                       int *overflow) {
    int32_t num = (int32_t)v * (int32_t)scale, q;
    if (z == 0) { (*overflow)++; return (int16_t)(uint16_t)(0x7fff + (uint16_t)off); }
    q = num / z;
    if (q > 32767 || q < -32768) {
        (*overflow)++;
        return (int16_t)(uint16_t)(0x7fff + (uint16_t)off);
    }
    return (int16_t)(uint16_t)((uint16_t)q + (uint16_t)off);
}

int uw_rast_sprite_place(uw_rast_slots *s, int slot_off, const int16_t rec[7],
                         int16_t scale_x, int16_t m4, const uw_rast_proj *p,
                         uw_sprite_desc *out) {
    int shift = s->add_shift & 15, overflow = 0;
    int16_t dx, dy, ext_x, ext_y, x, y, z, sx, sy, sx2, sy2;
    int i = slot_off / 2;

    if (slot_off < 0 || slot_off + 6 > UW_RAST_SLOT_WINDOW) return 0;
    /* 6863..687d: the anchor, subtracted from x and added to y IN PLACE. */
    dx = hi_mul((int16_t)((uint16_t)imul8(rec[5], rec[3]) << shift), scale_x);
    s->x[i] = (int16_t)(uint16_t)((uint16_t)s->x[i] - (uint16_t)dx);
    dy = hi_mul((int16_t)((uint16_t)imul8(rec[6], rec[4]) << shift), m4);
    s->x[i + 1] = (int16_t)(uint16_t)((uint16_t)s->x[i + 1] + (uint16_t)dy);
    /* 6880..689c: the extents, from the image's own width and height. */
    ext_x = hi_mul((int16_t)((uint16_t)imul8(rec[1], rec[5]) << shift), scale_x);
    ext_y = hi_mul((int16_t)((uint16_t)imul8(rec[2], rec[6]) << shift), m4);

    x = s->x[i]; y = s->x[i + 1]; z = s->x[i + 2];
    if (uw_rast_outcode(x, y, z) & 0x82) return 0;       /* test al,0x82 */
    sx = project(x, p->scale_x, z, p->off_x, &overflow);
    sy = project(y, p->scale_y, z, p->off_y, &overflow);
    x = (int16_t)(uint16_t)((uint16_t)x + (uint16_t)ext_x);
    y = (int16_t)(uint16_t)((uint16_t)y + (uint16_t)ext_y);
    if (uw_rast_outcode(x, y, z) & 0x89) return 0;       /* test al,0x89 */
    sx2 = project(x, p->scale_x, z, p->off_x, &overflow);
    sy2 = project(y, p->scale_y, z, p->off_y, &overflow);

    out->src_offset = 0;
    out->x = sx;
    out->y = sy;
    out->width = (int16_t)(uint16_t)((uint16_t)sx2 - (uint16_t)sx);
    out->height = (int16_t)(uint16_t)((uint16_t)sy2 - (uint16_t)sy);
    out->src_w = (uint16_t)rec[1];
    out->src_h = (uint16_t)rec[2];
    return overflow == 0;                    /* the overflow word tested */
}

/* ---- gfx_draw_sprite_scaled ------------------------------------------------ */

#define MAP_MAX 0x200 + 0x100 + 2

long uw_gfx_draw_sprite_scaled(const uw_fb *fb, const uw_sprite_desc *d,
                               const uint8_t *pixels, size_t n_pixels,
                               const uw_clip_bounds *clip,
                               const uint8_t *xfer, long *translucent) {
    return uw_gfx_draw_sprite_rows(fb, d, pixels, n_pixels, clip, xfer, translucent, 1, 0);
}

long uw_gfx_draw_sprite_rows(const uw_fb *fb, const uw_sprite_desc *d,
                             const uint8_t *pixels, size_t n_pixels,
                             const uw_clip_bounds *clip,
                             const uint8_t *xfer, long *translucent,
                             int blitter, uint8_t fill) {
    uint16_t map[MAP_MAX], step, ystep, src_start = 0, acc = 0;
    uint8_t buf[MAP_MAX];
    int16_t cx = d->width, width, x = d->x, xclip = 0, rows, row, ax;
    int mapped = 0, r, i;
    uint8_t last;
    long written = 0;

    if (!fb || !fb->pixels || !pixels) return 0;
    if (cx <= 2 || d->height <= 2) return 0;              /* cmp ..,2; jg */

    /* 10b3: ((src_w - 1) << 8) / (w - 1), unsigned. */
    {
        uint32_t num = (uint32_t)(uint16_t)(d->src_w - 1) << 8;
        uint32_t q = num / (uint16_t)(cx - 1);
        step = (uint16_t)q;
        if (q > 0xffff || step == 0) return 0;
    }
    /* 10d5: the right clip lets ONE COLUMN past gfx_clip_right through --
     * `sub ax,[0x3df6]; dec ax; jle`. */
    width = cx;
    ax = (int16_t)(x + cx - 1 - clip->right - 1);
    if (ax > 0) {
        width = (int16_t)(cx - ax);
        if (width <= 0) return 0;
    }
    /* 10ed: the left clip moves the start, in destination columns for now. */
    ax = (int16_t)(x - clip->left);
    if (ax < 0) {
        ax = (int16_t)-ax;
        if (ax <= 0) return 0;
        xclip = ax;
        x = clip->left;
        width = (int16_t)(width - ax);
        if (width <= 0) return 0;
    }
    if (width >= 0x200) return 0;

    /* gfx_sprite_gen_row_scaler, as the map its code implements. */
    if (xclip) {
        int32_t p = (int32_t)xclip * (int32_t)(int16_t)step;   /* imul bx */
        src_start = (uint16_t)((uint32_t)p >> 8);              /* al=ah, ah=dl */
    }
    if (step >> 8) {
        /* 1f10: `movsb` for each pixel, then the whole part of the step
         * less the one movsb took as `inc si` or `add si,imm8`. */
        uint16_t dxa = 0, S = 0;
        for (i = 0; i < width; i++) {
            int8_t dh;
            map[mapped++] = S;
            S++;
            dxa = (uint16_t)((dxa & 0xff) + step);
            dh = (int8_t)(uint8_t)((dxa >> 8) - 1);
            if (dh == 0) continue;
            if (dh > 1) S = (uint16_t)(S + (uint16_t)(int16_t)dh);
            else        S++;
        }
    } else {
        /* 1ef7: `lodsb`, then one `stosb` per addition until the fraction
         * carries -- run lengths, possibly running past the width, which
         * the row copy never reads. */
        uint8_t bh = 0, bl = (uint8_t)step;
        uint16_t S = 0;
        int16_t bp = width;
        do {
            int n = 0, carry;
            do {
                unsigned t = (unsigned)bh + bl;
                carry = t > 0xff;
                bh = (uint8_t)t;
                n++;
            } while (!carry);
            for (i = 0; i < n && mapped < MAP_MAX; i++) map[mapped++] = S;
            S++;
            bp = (int16_t)(bp - n);
        } while (bp > 0 && mapped < MAP_MAX);
    }

    /* 1121: the vertical step, (src_h << 8) / h. */
    {
        uint32_t num = (uint32_t)(uint16_t)d->src_h << 8;
        uint32_t q = num / (uint16_t)d->height;
        if (q > 0xffff) return 0;
        ystep = (uint16_t)q;
    }
    rows = d->height;
    row = d->y;
    if (row > clip->bottom) {                               /* jle */
        int16_t ex = (int16_t)(row - clip->bottom);
        rows = (int16_t)(rows - ex);
        if (rows <= 0) return 0;
        acc = (uint16_t)((uint32_t)(uint16_t)ex * ystep);
        row = clip->bottom;
    }
    {
        int16_t top = (int16_t)(row - rows + 1);
        if (top < clip->top) {
            rows = (int16_t)(rows + (top - clip->top));
            if (rows <= 0) return 0;
        }
    }

    last = 0xff;
    for (r = 0; r < rows; r++, row--) {
        uint8_t sr = (uint8_t)(acc >> 8);
        uint32_t di;
        if (last != sr) {
            /* `mov al,dh; mul byte [0xdc2]` -- the stride is the width's low
             * byte -- plus the start the generator folded the clip into. */
            uint16_t base = (uint16_t)(d->src_offset + src_start
                                       + (uint16_t)(sr * (uint8_t)d->src_w));
            for (i = 0; i < mapped; i++) {
                size_t at = (uint16_t)(base + map[i]);
                buf[i] = at < n_pixels ? pixels[at] : 0;
            }
        }
        if (row >= 0 && row < fb->n_rows) {
            di = fb->row[row] + (uint16_t)x;
            /* gfx_row_copy_translucent: 0 skipped, 0xfb..0xff through
             * XFER.DAT row code - 0xfb of the pixel beneath -- except the
             * LAST pixel, where such a code is skipped. */
            for (i = 0; i < width; i++, di = (uint16_t)(di + 1)) {
                uint8_t v = i < mapped ? buf[i] : 0;
                if (v == 0 || di >= fb->size) continue;
                if (blitter == 2 || blitter == 3) {
                    /* gfx_row_fill_masked and gfx_row_copy_transparent: no
                     * code is translucent, the last pixel included */
                    fb->pixels[di] = blitter == 2 ? fill : v;
                } else if (v >= 0xfb) {
                    if (i == width - 1) continue;
                    if (!xfer) { if (translucent) (*translucent)++; continue; }
                    fb->pixels[di] = xfer[(unsigned)(v - 0xfb) * 256u
                                          + fb->pixels[di]];
                } else {
                    fb->pixels[di] = v;
                }
                written++;
            }
        }
        last = sr;
        acc = (uint16_t)(acc + ystep);
    }
    return written;
}
