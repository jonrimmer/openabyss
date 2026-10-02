/* SPDX-License-Identifier: MIT */
/* The graphics module's texture mappers.
 *
 * rast_draw_face hands its projected vertices to one of two routines through
 * a far pointer, and which one is the draw list's choice: the
 * emit_poly*_shader1da opcodes and select_shader(0) pick the
 * AFFINE mapper; the *_shader545 opcodes and select_shader(non-zero) pick
 * the wall mapper, which is perspective-correct in u. This header is the
 * first of the two.
 *
 * Both are hand-written assembly that PATCHES ITS OWN IMMEDIATES: an edge's
 * per-row steps are written into the `add`/`adc` instructions that apply
 * them, and the row at which an edge ends is written into the `cmp` that
 * tests for it. A port keeps the same quantities in variables; what it must
 * not do is round them differently, because every one of them is a truncated
 * 8086 divide and the pixel a wall edge lands on is the difference.
 */
#ifndef UW_TEXMAP_H
#define UW_TEXMAP_H

#include "uw.h"
#include "uw_rast.h"

/* ---- what the mapper is given ------------------------------------------
 *
 * The vertices are rast_draw_face's own output -- uw_rast_svert, fourteen
 * bytes -- of which the mapper reads the screen x and y and
 * the u and v. The texture comes through the shape record,
 * whose four words the mapper reads as width, v_max, SEGMENT and MASK:
 *
 *     width - 1      (read, and not used by the affine path)
 *     the segment    -- a pointer here
 *     the mask       -- patched into `and si,imm` in the inner
 *                       loop, which is what turns a v into a row
 *
 * The framebuffer is ES and a ROW TABLE: one word
 * per row, the offset of that row's first byte. The host widens these to
 * support larger viewports. It is a table rather than a
 * multiply because the module draws into both a 320-wide page and a narrower
 * viewport buffer, and the table is how the caller says which.
 */
typedef struct {
    uint8_t       *pixels;      /* ES, the destination */
    size_t         size;        /* its length, for the bounds check */
    const uint32_t *row;        /* host offsets can exceed the original 64K segment */
    int            n_rows;
} uw_fb;

/* ---- gfx_texture_poly_affine -------------------------------
 *
 * THE WALK. It finds the vertex with the GREATEST y and steps two edges out
 * of it -- gfx_tex_edge_back_affine backward around the ring for
 * the left, gfx_tex_edge_fwd_affine forward for the right -- one
 * row at a time with y DECREASING, re-seeding an edge when the row reaches
 * the vertex it was heading for, and stopping when the two have between them
 * consumed n+1 vertices.
 *
 * EVERY ACCUMULATOR IS 16.16 AND STARTS AT ONE HALF. `mov ax,0x8000` into
 * the three fraction words is the rounding: an edge seeded at exactly the
 * vertex would step a pixel late on one side and early on the other.
 *
 * AND EVERY STEP IS A TRUNCATED DIVIDE DONE IN TWO HALVES, because a 32-bit
 * dividend would overflow `idiv`: `delta / rows` is the integer part, then
 * the remainder is shifted into the high word, HALVED, divided again and
 * doubled -- `xor ax,ax; sar dx,1; rcr ax,1; idiv cx; cwd; shl ax,1;
 * rcl dx,1` -- and whatever came out of the top of that is added back to the
 * integer part. The result is `(delta << 16) / rows` to within the truncation
 * the original does, and NOT the same number as computing it in 32 bits.
 *
 * THE SPAN. u is 8.8 in one word, so it gets an integer step only and its
 * fraction is the low byte; v is 16.16 and gets the full treatment, seeded
 * from the LEFT EDGE's fraction. The inner loop is
 * `si = (v_high & mask) + (u >> 8); movsb` -- no divide, no multiply, one
 * byte a pixel.
 *
 * A ZERO-WIDTH SPAN STILL DRAWS ONE PIXEL. `jz` skips the step divides and
 * lands on the `inc cx` that turns the width into a pixel count, so the
 * single pixel is drawn with whatever steps the previous span left behind.
 * A NEGATIVE width abandons the polygon.
 */
void uw_gfx_texture_poly_affine(const uw_fb *fb, const uw_rast_svert *v,
                                int n, const uw_rast_texrec *tex,
                                const uint8_t *texels, size_t n_texels);

/* Convex, clipped faces with positive camera-space z. Interpolates 1/z,
 * u/z and v/z in screen space, then divides for each texel. Coordinates
 * retain the original 8.8 u and packed-row v texture layout. */
void uw_gfx_texture_poly_perspective(const uw_fb *fb, const uw_rast_svert *v,
                                     int n, const uw_rast_texrec *tex,
                                     const uint8_t *texels, size_t n_texels);

/* The same coverage with lighting applied to each sampled texel before it
 * is written. `shade` has n vertex levels and `light` is the 4096-byte
 * remap table; either may be NULL for an unlit face. This avoids separate
 * texture and lighting scans disagreeing at rounded polygon borders. */
void uw_gfx_texture_poly_perspective_lit(const uw_fb *fb, const uw_rast_svert *v,
                                         int n, const uw_rast_texrec *tex,
                                         const uint8_t *texels, size_t n_texels,
                                         const uint8_t *shade,
                                         const uint8_t *light, long *overrun);

/* The two halves of one step, exposed because they are what the property
 * tests hold: `uw_gfx_step16` is the two-divide 16.16 step above. */
int32_t uw_gfx_step16(int16_t delta, int16_t rows);

/* The six edge accumulators, which the module keeps in data-segment words as
 * (integer, fraction) pairs -- the same thing as a 16.16 long, and the
 * same thing this holds. */
typedef struct {
    int32_t lx, lu, lv;      /* the left edge */
    int32_t rx, ru, rv;      /* the right edge */
    int16_t row;             /* the current row */
    int     remaining;       /* the edges left */
} uw_gfx_edge_state;

/* Walks without drawing and reports the accumulators on reaching `stop_row`.
 * Returns 0 if the polygon ended first. */
int uw_gfx_affine_walk_to(const uw_rast_svert *v, int n, int stop_row,
                          uw_gfx_edge_state *out);

/* ---- gfx_texture_poly_wall ---------------------------------
 *
 * The other shader, and the one UW2 kept. Same entry contract, and the same
 * edge walk in outline -- but three things differ, and each of them is the
 * kind of difference a port written from the affine one would carry over
 * wrongly.
 *
 * ONE: u DOES NOT STEP. Before any row is drawn the routine builds a table
 * of ONE BYTE PER SCREEN COLUMN, and the inner loop reads the
 * column's texel out of it: `lodsb ss:si; xlat bx; stosb`, where BX is
 * `v & mask`. The table is what makes u perspective-correct: it walks the
 * face TEXEL by texel between its leftmost and rightmost vertices, stepping
 * the camera-space x and z linearly, projecting each step through the
 * rasteriser's own scale and centre to a screen column,
 * and filling every column up to there with that texel. Exact for a
 * vertical face, which is what a wall is.
 *
 * It only does that when the face is deep enough to need it:
 * `4 * |z1 - z0| / min(z0, z1) > 2`. Otherwise the table is filled linearly
 * between the two extreme vertices' u, which is the affine answer.
 *
 * TWO: A RESEEDED EDGE KEEPS ITS ACCUMULATORS. The affine mapper calls its
 * stepper at the top, which reloads x, u and v from the vertex; this one
 * calls eight bytes into
 * gfx_tex_edge_fwd_wall and gfx_tex_edge_back_wall
 * -- PAST THE RELOAD. Only
 * the steps are recomputed. So the accumulated rounding carries across a
 * vertex here and does not there, and a port that reseeds both alike drifts
 * on every polygon with more than two edges a side.
 *
 * THREE: A BACKWARDS SPAN IS SKIPPED, NOT FATAL. `inc cx; jg` falls to the
 * per-row advance rather than abandoning the polygon, which is what the
 * affine one does.
 *
 * FOUR: A ROW ONE PIXEL WIDE TAKES ITS v FROM A STALE REGISTER. The span
 * loads v's integer into BP (`mov bp,[0x7bd]`) on the path that
 * computes the span's v step, which a zero width jumps past; the row
 * then takes `mov dx,bp` regardless. So a one-pixel row -- a face's first,
 * where both edges leave the same vertex, or a sliver -- reads the texel
 * row from whatever BP held: after the column fill, the address of the
 * leftmost vertex's y in the vertex ring on the linear path, or the
 * z accumulator's integer on the perspective one; after a drawn row, the
 * low word the span's v accumulator ended on. Nothing overwrites that pixel
 * when the face is the last of a near wall's four quads, and the pixels at
 * the quads' shared corners in every coherent scene are this texel. The
 * span's v step lives in the code segment and outlives the call:
 * `span_dv` carries it from face to face, and may be NULL.
 */
void uw_gfx_texture_poly_wall(const uw_fb *fb, const uw_rast_svert *v, int n,
                              const uw_rast_texrec *tex,
                              const uint8_t *texels, size_t n_texels,
                              const uw_rast_proj *proj,
                              uint8_t *ucol, int n_ucol, int32_t *span_dv);

/* What the routine works out before it fills anything: the extreme
 * vertices' screen x and the `4 * |dz| / min z` quotient, which the
 * original keeps in its own code segment. */
typedef struct {
    int16_t min_x, max_x;
    int16_t quotient;
    int     perspective;     /* whether `cmp ax,2; jg` was taken */
    uint16_t bp;             /* BP as the fill leaves it, for a one-pixel row */
} uw_gfx_wall_setup;

/* The per-column texel table on its own, because it is the whole of what
 * makes this mapper different and it can be checked without a framebuffer.
 * `ucol[x]` is the texel column for screen column x. Returns the number of
 * columns it wrote to, or -1 if the face was rejected. `out` may be NULL. */
int uw_gfx_wall_ucol(const uw_rast_svert *v, int n, const uw_rast_proj *proj,
                     uint8_t *ucol, int n_ucol, uw_gfx_wall_setup *out);

#endif
