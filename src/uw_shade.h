/* SPDX-License-Identifier: MIT */
/* The graphics module's shaded polygon path, as the rasteriser uses it to
 * LIGHT a textured face.
 *
 * In the original rendering paths a textured face is drawn twice. The
 * mapper (uw_texmap.h) writes raw texels;
 * then, when the draw list has set the lighting flag, rast_draw_face_lit
 * gives each projected vertex a shade from its distance and hands the same
 * polygon to gfx_polygon_shaded_to_spans with the span pair pointed at the
 * two REMAP handlers, which replace every pixel already in the buffer with
 * LIGHT.DAT[shade][pixel]. The host's non-affine mapper instead writes lit
 * texels in one scan so border coverage cannot disagree between passes.
 *
 * The lighting table is 16 rows of 256 -- in the rasteriser's own code
 * segment -- loaded from LIGHT.DAT, or MONO.DAT at shade level 5. The
 * caller passes those 4096 bytes. */
#ifndef UW_SHADE_H
#define UW_SHADE_H

#include "uw.h"
#include "uw_rast.h"
#include "uw_texmap.h"
#include "uw_clip.h"

/* gfx_clip_buf_a's six-byte vertex: screen x, screen y, shade. */
typedef struct {
    int16_t x, y, shade;
} uw_shade_vert;

/* ---- rast_draw_face_lit: a shade per projected vertex --------
 *
 * From the descriptor's camera-space x, y and z (+8, +0xa, +0xc):
 *
 *     d2    = sum of (c >> 5)^2, the LOW WORDS of three imul, in 16 bits
 *     d     = four Newton steps from 0x40: `div cx; add cx,ax; rcr cx,1`
 *     shade = ((d & 0xff) * (scale & 0xff)) >> 6, low byte
 *             + bias_a, floored at 0 (`jns`)
 *             + bias_b, then `cmp al,0xf; jbe` -- 15 at most, by its LOW
 *             byte, the word stored whole
 *
 * `scale`, `bias_a` and `bias_b` are the graphics module's words, set per
 * light level. The multiply is 8-bit
 * (`mul cl`), so only the square root's low byte takes part. */
typedef struct {
    uint16_t scale;
    int16_t  bias_a;    /* added first and floored at zero */
    int16_t  bias_b;    /* added after */
} uw_light_params;

int16_t uw_rast_vertex_shade(const uw_rast_svert *v, const uw_light_params *p);

/* ---- gfx_polygon_shaded_to_spans and gfx_span_list_shaded ----
 *
 * The rasteriser enters THREE BYTES IN, past the call that packs
 * separate shade and vertex arrays -- it has already built the six-byte
 * vertices -- and past the viewport clip, which the shaded draw entry runs
 * and this one does not.
 *
 * SCAN CONVERSION. The ring is closed by copying vertex 0 after the
 * last. Every vertex with a NEGATIVE y is set to 0 in the buffer. The
 * bottom row (greatest y; the LAST vertex holding it on a tie) is seeded
 * with that vertex's x and shade on both sides; every row above it down to
 * one below the top is seeded x1 = 1000, x2 = -1000. Then each edge that is
 * not horizontal is walked UP from its lower end: x and shade are 16.16
 * accumulators starting at the lower vertex with fractions of 0x8000, each
 * stepped BEFORE it is used (the steps are the two-halves divide,
 * uw_gfx_step16), so the lower end's own row is never written by its edges.
 * A row takes a new x1 (and its shade) when x < x1 and a new x2 when x > x2,
 * strictly. A horizontal edge writes its own row directly, and there the
 * comparisons are <= and >=.
 *
 * THE SPANS. Five words a record -- row, x1, x2, shade at x1, shade
 * at x2 -- swapped so x1 is the left. Equal shades take the FLAT handler:
 * every pixel becomes LIGHT[shade & 0xff][pixel]. Unequal ones take the
 * GOURAUD handler with an 8.8 step of (right - left) / count -- `idiv` for
 * the whole part, `div` of the remainder for the fraction, computed on the
 * magnitude and negated -- and TWO accumulators, the left shade + 0.5 with
 * 0x40 taken off one and added to the other, which alternate pixel by pixel
 * and each step by twice the step. The row's low bit decides which of the
 * two the first pixel uses. That is the dither.
 *
 * `light` is the 4096-byte table. A shade outside 0..15 would read past it
 * into the rasteriser's code in the original; here it is clamped and counted
 * in `*overrun`, which may be NULL. `v` is modified (the y clamp), as the
 * original's buffer is. Returns the number of pixels remapped. */
long uw_gfx_shade_polygon_remap(const uw_fb *fb, uw_shade_vert *v, int n,
                                const uint8_t *light, long *overrun);

/* ---- the Gouraud pair ---------------------------------------------------
 *
 * set_shaded_gouraud (0xd6) points the two span slots at these, and every
 * lit textured face points them back at the remap pair. The scan conversion
 * and the dither are the remap path's; what differs is the SOURCE byte,
 * which is the base colour set_vertex_shade put in place instead of the
 * pixel already there. And a span whose two shades are equal is not a loop
 * at all: the handler looks up LIGHT[shade][base] into the module's draw
 * colour, sets bit 15 of the NEXT record's first word -- which is
 * this record's left shade -- and calls gfx_fill_span_list on this record as
 * if it were a six-byte one, so that span follows the flat fill's rule for a
 * reversed pair on its UNSWAPPED x1 and x2.
 *
 * `base` is that colour. `colour`, which may be NULL, receives the module's
 * draw colour as the last flat span left it. Returns the pixels written. */
long uw_gfx_shade_polygon_gouraud(const uw_fb *fb, uw_shade_vert *v, int n,
                                  const uint8_t *light, uint8_t base,
                                  long *overrun, uint8_t *colour);

/* ---- gfx_draw_polygon_shaded and its clip ------------------
 *
 * The shaded twin of gfx_draw_polygon: over 99 vertices nothing, two or
 * fewer a line or a point this does not draw (-1), and otherwise the
 * viewport clip on six-byte vertices and then the scan. The clip is
 * gfx_clip_polygon_viewport's -- the same four passes in the same order,
 * the same strict tests, the same +/-1 bias gated on the other axis and the
 * same halving of a denominator that overflows -- with the shade of a new
 * vertex `a.shade + (b.shade - a.shade) * run / den`, truncated, over the
 * halved run and den when they were halved. Between the edge's own two ends,
 * unlike the rasteriser's 3-D passes (uw_rast_clip_plane_shaded).
 *
 * `v` must have room for UW_CLIP_MAX_VERTS + 1 entries. `gouraud` picks the
 * span pair: 1 for 0f2b/0ff7, 0 for the remap pair. */
int  uw_clip_polygon_shaded(uw_shade_vert *v, int n, const uw_clip_rect *r);
long uw_gfx_draw_polygon_shaded(const uw_fb *fb, uw_shade_vert *v, int n,
                                const uw_clip_rect *r, const uint8_t *light,
                                int gouraud, uint8_t base, long *overrun,
                                uint8_t *colour);

#endif
