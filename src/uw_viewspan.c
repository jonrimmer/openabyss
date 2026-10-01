/* SPDX-License-Identifier: MIT */
/* view_setup_frustum and view_traverse_spans with their
 * edge walkers: what fills view_column_array before
 * automap_view_sweep reads it. See
 * uw_viewlist.h for where it sits and what holds it to the original.
 *
 * THE TRAVERSAL. Two edge records of 0x11 bytes,
 * linked through their low nibbles from a head byte (0xf ends
 * the chain), start in the eye's tile with directions a little over an
 * eighth of a turn either side of the view (gfx_sincos_lerp of heading
 * +/- 0x2040, over 16). Each row, view_step_edge walks each edge along its
 * direction to the row's far boundary, sliding it sideways tile by tile
 * while the tiles let it; view_resolve_edge_pair walks a copy of the left
 * edge across to the right one, writing every cell it crosses through
 * view_edge_try_step -- the faces byte from view_face_table by the rotated
 * tile type and where the cell sits against the eye, the neighbour bits in
 * byte 1's high nibble -- and view_edges_next_row takes both a row deeper,
 * pulled in past cells whose light nibble says they are unlit. Cells outside
 * the span are zeroed. It ends when the pair crosses, or at row 16.
 *
 * Record layout, by offset: +0 link nibble and bit 7 (the edge's side),
 * +1 dx and +3 dy (words), +5 column (signed, 0 at the eye), +6 x within the
 * tile, +7 row, +8 y within the tile, +9 the tile's far pointer, +0xd the
 * cell pointer, +0xf the cell the span starts from. */
#include "uw_viewlist.h"
#include "uw_trig.h"

#include <string.h>

static uint16_t mw(const uw_vl *v, uint16_t at) {
    return (uint16_t)(v->mem[at] | (v->mem[(uint16_t)(at + 1)] << 8));
}
static int16_t ms(const uw_vl *v, uint16_t at) { return (int16_t)mw(v, at); }
static uint8_t mb(const uw_vl *v, uint16_t at) { return v->mem[at]; }
static int8_t mc(const uw_vl *v, uint16_t at) { return (int8_t)v->mem[at]; }
static void setw(uw_vl *v, uint16_t at, uint16_t w) {
    v->mem[at] = (uint8_t)w;
    v->mem[(uint16_t)(at + 1)] = (uint8_t)(w >> 8);
}
static void setb(uw_vl *v, uint16_t at, uint8_t b) { v->mem[at] = b; }
static int16_t vsw(const uw_vl *v, int at) {
    return (int16_t)(v->vstate[at] | (v->vstate[at + 1] << 8));
}

/* The byte a record's tile pointer names, and the rotated tile type
 * (view_tile_type_rotated) of the tile `steps` * stride from it. */
static uint8_t tile_at(const uw_vl *v, uint16_t e, int16_t delta) {
    return v->level[(uint16_t)(mw(v, (uint16_t)(e + 9)) + delta)];
}
static uint8_t rotated(const uw_vl *v, uint8_t tile) {
    return mb(v, (uint16_t)(v->facing * 16 + (tile & 0xf) + 0x464));
}
static int16_t stride(const uw_vl *v, int k) {             /* view_step_offsets */
    return ms(v, (uint16_t)(0x444 + v->facing * 6 + k * 2));
}
static int abs16(int x) { return x < 0 ? -x : x; }

/* view_edge_step_forward and view_edge_step_back. */
static void step_forward(uw_vl *v, uint16_t e) {
    setb(v, (uint16_t)(e + 5), (uint8_t)(mb(v, (uint16_t)(e + 5)) + 1));
    setw(v, (uint16_t)(e + 9), (uint16_t)(mw(v, (uint16_t)(e + 9)) + stride(v, 0) * 4));
    setw(v, (uint16_t)(e + 0xd), (uint16_t)(mw(v, (uint16_t)(e + 0xd)) + 2));
}
static void step_back(uw_vl *v, uint16_t e) {
    setb(v, (uint16_t)(e + 5), (uint8_t)(mb(v, (uint16_t)(e + 5)) - 1));
    setw(v, (uint16_t)(e + 9), (uint16_t)(mw(v, (uint16_t)(e + 9)) - stride(v, 0) * 4));
    setw(v, (uint16_t)(e + 0xd), (uint16_t)(mw(v, (uint16_t)(e + 0xd)) - 2));
}

/* One wall of view_edge_try_step: the neighbour `delta` away hides the face
 * `face` unless it stands higher, in which case byte 1 gets `seen`. */
static void neighbour(const uw_vl *v, uint16_t e, int16_t delta, uint8_t di,
                      uint8_t flag, uint8_t kind, uint16_t face, uint16_t seen,
                      uint16_t *b2, uint16_t *b4) {
    uint8_t n = tile_at(v, e, delta);
    uint16_t nt = rotated(v, n);
    uint8_t nf = mb(v, (uint16_t)(0x1d8a + nt));
    uint16_t a, b;
    if (nf & flag) return;
    a = (uint16_t)(((n >> 4) & 0xf) + ((nf & 0x20) == 0x20) - (nt == kind));
    b = (uint16_t)(((tile_at(v, e, 0) >> 4) & 0xf) + (di == kind)
                   + (nt == di && di != 1));
    if (a > b) *b4 = (uint16_t)(*b4 + seen);
    else *b2 = (uint16_t)(*b2 - face);
}

/* view_edge_try_step: write the cell the edge stands in, and
 * with a direction, step it a column that way if the tile allows. */
static int try_step(uw_vl *v, uint16_t e, int8_t dir, int8_t want) {
    uint16_t cell = mw(v, (uint16_t)(e + 0xd));
    uint16_t b4 = (uint16_t)(mb(v, (uint16_t)(cell + 1)) & 0xf);
    uint8_t di = (uint8_t)rotated(v, tile_at(v, e, 0));
    uint16_t b6 = 0, b2;
    int8_t col = mc(v, (uint16_t)(e + 5)), row = mc(v, (uint16_t)(e + 7));
    uint8_t df = mb(v, (uint16_t)(0x1d8a + di));

    if (col != 0) {
        b6 = (uint16_t)((col > 0) + 1);
        if (abs16(col) > abs16(row)) b6 = (uint16_t)(b6 + 2);
        if (abs16(col) == abs16(row)) b6 = (uint16_t)(b6 + 4);
    }
    b2 = mb(v, (uint16_t)(0x4a5 + di * 7 + b6));
    if (b2 == 0) {
        setb(v, cell, 0);
        return 0;
    }
    if (b2 & 0x10)
        neighbour(v, e, (int16_t)(stride(v, 1) * 4), di, 8, 6, 0x10, 0x20, &b2, &b4);
    if (b2 & 0x20)
        neighbour(v, e, (int16_t)(stride(v, 0) * 4), di, 2, 8, 0x20, 0x40, &b2, &b4);
    if (b2 & 0x08)
        neighbour(v, e, (int16_t)(-stride(v, 0) * 4), di, 4, 9, 0x08, 0x10, &b2, &b4);
    setb(v, cell, (uint8_t)b2);
    setb(v, (uint16_t)(cell + 1), (uint8_t)b4);
    if (dir == 0) return 0;

    {
        uint8_t ff = mb(v, (uint16_t)(0x1d8a + rotated(v, tile_at(v, e, (int16_t)(stride(v, 1) * 4)))));
        int go = 0;
        if ((ff & 8) == want
            && ms(v, (uint16_t)(0x535 + (want == 8) * 2)) == (df & 0x10))
            go = 1;
        else if ((df & 1) == 1
                 && ms(v, (uint16_t)(0x535 + (want == 0) * 2)) == (df & 0x10))
            go = 1;
        if (!go) return 0;
    }
    if (dir == 1) {
        step_forward(v, e);
        setb(v, (uint16_t)(e + 6), 0);
    } else {
        step_back(v, e);
        setb(v, (uint16_t)(e + 6), 0xff);
    }
    return 1;
}

/* One side of view_edges_next_row: pull the edge in past unlit cells, aim it
 * from the eye unless it is still inside the eye's own reach, and take it a
 * row deeper. Returns 0 when the pair has crossed. */
static int next_row_side(uw_vl *v, uint16_t e, uint16_t l, uint16_t r, int left) {
    int16_t vx = vsw(v, 0xa), vy = vsw(v, 0x12);
    while ((mb(v, (uint16_t)(mw(v, (uint16_t)(e + 0xd)) + 0x43)) & 0xf) == 0xf) {
        if (left) { step_forward(v, e); setb(v, (uint16_t)(e + 6), 0); }
        else { step_back(v, e); setb(v, (uint16_t)(e + 6), 0xff); }
        try_step(v, e, 0, 0);
        if (mc(v, (uint16_t)(l + 5)) > mc(v, (uint16_t)(r + 5))) return 0;
    }
    if (!(mc(v, (uint16_t)(e + 7)) <= 1
          && abs16((int16_t)(mb(v, (uint16_t)(e + 6)) - vx))
             + abs16((int16_t)(mb(v, (uint16_t)(e + 8)) - vy)) <= 0x10)) {
        int16_t dx = (int16_t)((uint16_t)(mc(v, (uint16_t)(e + 5)) * 256)
                               + mb(v, (uint16_t)(e + 6)) - (uint16_t)vx);
        int16_t adj = (int16_t)(abs16(dx) / 0x32 + 2);
        setw(v, (uint16_t)(e + 1), (uint16_t)(left ? dx - adj : dx + adj));
        setw(v, (uint16_t)(e + 3), (uint16_t)((uint16_t)(mc(v, (uint16_t)(e + 7)) * 256)
                                              - (uint16_t)vy - (left ? 0 : 1)));
    }
    return 1;
}

/* view_edges_next_row. */
static int next_row(uw_vl *v, uint16_t si, uint16_t di) {
    setb(v, (uint16_t)(si + 7), (uint8_t)(mb(v, (uint16_t)(si + 7)) + 1));
    if (mc(v, (uint16_t)(si + 7)) > 0x10) return 0;
    if (!next_row_side(v, si, si, di, 1)) return 0;
    setw(v, (uint16_t)(si + 9), (uint16_t)(mw(v, (uint16_t)(si + 9)) + stride(v, 1) * 4));
    setw(v, (uint16_t)(si + 0xd), (uint16_t)(mw(v, (uint16_t)(si + 0xd)) + 0x42));
    setb(v, (uint16_t)(si + 8), 0);
    setb(v, (uint16_t)(di + 7), (uint8_t)(mb(v, (uint16_t)(di + 7)) + 1));
    if (!next_row_side(v, di, si, di, 0)) return 0;
    setb(v, (uint16_t)(di + 8), 0);
    setw(v, (uint16_t)(di + 0xd), (uint16_t)(mw(v, (uint16_t)(di + 0xd)) + 0x42));
    setw(v, (uint16_t)(di + 9), (uint16_t)(mw(v, (uint16_t)(di + 9)) + stride(v, 1) * 4));
    return 1;
}

/* Whether the edge's direction reaches the row's far boundary before the
 * column's: (dx * sign) * (0x100 - y) > room * dy, at 32 bits. */
static int crosses_row_first(const uw_vl *v, uint16_t e, int di, int16_t room) {
    int32_t t1 = (int32_t)ms(v, (uint16_t)(e + 1)) * ms(v, (uint16_t)(0x541 + di * 2));
    int32_t t2 = (int32_t)room * ms(v, (uint16_t)(e + 3));
    t1 *= (int32_t)(0x100 - mb(v, (uint16_t)(e + 8)));
    return t1 > t2;
}

/* The span-start bookkeeping: an edge whose side matches its direction
 * marks the cell it stands in. */
static int side_matches(const uw_vl *v, uint16_t e, int di) {
    return ((di << 7) ^ ((int)(int16_t)(int8_t)mb(v, e) & 0x80)) == 0;
}

/* view_step_edge. */
static void step_edge(uw_vl *v, uint16_t si) {
    int di = ms(v, (uint16_t)(si + 1)) >= 0;
    int16_t room = di == 1 ? (int16_t)(0x100 - mb(v, (uint16_t)(si + 6)))
                           : (int16_t)mb(v, (uint16_t)(si + 6));
    int sgn = ms(v, (uint16_t)(0x541 + di * 2));
    int other = (di + 1) % 2;
    int first;

    if (side_matches(v, si, di)) setw(v, (uint16_t)(si + 0xf), mw(v, (uint16_t)(si + 0xd)));
    if (ms(v, (uint16_t)(si + 3)) == 0) first = 1;
    else if (ms(v, (uint16_t)(si + 1)) == 0) first = 0;
    else first = crosses_row_first(v, si, di, room);

    while (first) {
        uint8_t bt = rotated(v, tile_at(v, si, 0));
        uint16_t mask = mw(v, (uint16_t)(0x539 + di * 2));
        int16_t nd = (int16_t)(sgn * stride(v, 0) * 4);
        if ((mb(v, (uint16_t)(0x1d8a + (int8_t)bt)) & mask)
            || (mb(v, (uint16_t)(0x1d8a + rotated(v, tile_at(v, si, nd))))
                & mw(v, (uint16_t)(0x539 + other * 2)))) {
            /* 0xed3: a wall in the way. */
            setb(v, (uint16_t)(si + 8), 0xff);
            if ((mb(v, (uint16_t)(0x1d8a + (int8_t)bt)) & mask) == mask
                && ms(v, (uint16_t)(0x53d + di * 2)) == (int8_t)bt)
                setb(v, (uint16_t)(si + 6), (uint8_t)(other * 0xff));
            else
                setb(v, (uint16_t)(si + 6), (uint8_t)(di * 0xff));
            if (!side_matches(v, si, di))
                setw(v, (uint16_t)(si + 0xf), mw(v, (uint16_t)(si + 0xd)));
            return;
        }
        {
            int32_t num = (int32_t)room * ms(v, (uint16_t)(si + 3));
            int32_t den = (int32_t)ms(v, (uint16_t)(si + 1)) * sgn;
            int32_t q = den ? num / den : 0;
            setb(v, (uint16_t)(si + 8), (uint8_t)(mb(v, (uint16_t)(si + 8)) + (uint8_t)q));
        }
        setb(v, (uint16_t)(si + 6), (uint8_t)(other * 0xff));
        room = 0x100;
        if (side_matches(v, si, di)) try_step(v, si, 0, 0);
        if (sgn == 1) step_forward(v, si); else step_back(v, si);
        if ((mb(v, (uint16_t)(mw(v, (uint16_t)(si + 0xd)) + 1)) & 0xf) == 0xf
            || abs16(mc(v, (uint16_t)(si + 5))) > 0x10) {
            if (ms(v, (uint16_t)(0x541 + other * 2)) == 1) step_forward(v, si);
            else step_back(v, si);
            setb(v, (uint16_t)(si + 6), (uint8_t)(di * 0xff));
            setb(v, (uint16_t)(si + 8), 0xff);
            if (!side_matches(v, si, di))
                setw(v, (uint16_t)(si + 0xf), mw(v, (uint16_t)(si + 0xd)));
            return;
        }
        if (ms(v, (uint16_t)(si + 3)) == 0) first = 1;
        else first = crosses_row_first(v, si, di, room);
    }

    /* 0xfa2: the row's far boundary comes first. */
    {
        int32_t num = (int32_t)ms(v, (uint16_t)(si + 1)) * sgn;
        int32_t den = ms(v, (uint16_t)(si + 3));
        int32_t q;
        num *= (int32_t)(0xff - mb(v, (uint16_t)(si + 8)));
        q = den ? num / den : 0;
        setb(v, (uint16_t)(si + 6),
             (uint8_t)(mb(v, (uint16_t)(si + 6)) + (uint8_t)((int16_t)q * sgn)));
    }
    setb(v, (uint16_t)(si + 8), 0xff);
    if (!side_matches(v, si, di))
        setw(v, (uint16_t)(si + 0xf), mw(v, (uint16_t)(si + 0xd)));
}

static uint16_t rec(const uw_vl *v, uint16_t link_at) {
    return (uint16_t)(0x2d00 + (mb(v, link_at) & 0xf) * 0x11);
}
static int colpos(const uw_vl *v, uint16_t e) {
    return (int16_t)((uint16_t)(mc(v, (uint16_t)(e + 5)) * 256) + mb(v, (uint16_t)(e + 6)));
}

/* view_resolve_edge_pair. */
static void resolve_pair(uw_vl *v, uint16_t *pp, uint16_t *pcell) {
    uint16_t di = rec(v, *pp), si = rec(v, di);
    uint8_t local[0x11];
    uint16_t lo = 0xffe0;                /* the walking copy, see below */
    uint8_t saved[0x11];

    *pcell = (uint16_t)(mw(v, (uint16_t)(si + 0xf)) + 2);
    while (try_step(v, di, 1, 8)) {
        if (colpos(v, di) > colpos(v, si)) {
            setb(v, *pp, (uint8_t)((mb(v, *pp) & 0xf0) + (mb(v, si) & 0xf)));
            setb(v, di, 0);
            setb(v, si, 0);
            return;
        }
    }
    if (mc(v, (uint16_t)(di + 5)) < mc(v, (uint16_t)(si + 5)))
        while (try_step(v, si, -1, 8)) {}
    memcpy(local, v->mem + di, sizeof local);
    if (!next_row(v, di, si)) {
        setb(v, *pp, (uint8_t)((mb(v, *pp) & 0xf0) + (mb(v, si) & 0xf)));
        setb(v, di, 0);
        setb(v, si, 0);
        return;
    }
    *pp = si;
    if (mb(v, (uint16_t)(di + 5)) == mb(v, (uint16_t)(si + 5))) return;

    /* Walk the copy of the left edge across the row to the right edge,
     * writing each cell. The copy lives on the original's stack; here it
     * borrows the top of the data segment, which nothing in the build reads. */
    memcpy(saved, v->mem + lo, sizeof saved);
    memcpy(v->mem + lo, local, sizeof local);
    step_forward(v, lo);
    while (mc(v, (uint16_t)(si + 5)) > mc(v, (uint16_t)(lo + 5))) {
        while (try_step(v, lo, 1, 0) && mc(v, (uint16_t)(si + 5)) > mc(v, (uint16_t)(lo + 5))) {}
        if (!(mc(v, (uint16_t)(si + 5)) > mc(v, (uint16_t)(lo + 5)))) break;
        step_forward(v, lo);
        while (try_step(v, lo, 1, 8) && mc(v, (uint16_t)(si + 5)) > mc(v, (uint16_t)(lo + 5))) {}
        step_forward(v, lo);
    }
    memcpy(v->mem + lo, saved, sizeof saved);
}

/* view_setup_frustum: both edges in the eye's tile, at the eye's
 * position within it, aimed 0x2040 either side of the view. */
static void setup_frustum(uw_vl *v) {
    int16_t s, c;
    uint16_t e, half = v->frustum_half_angle ? v->frustum_half_angle : 0x2040;
    if ((v->level[mw(v, 0x2cfa)] & 0xf) == 0) {         /* the eye is in rock */
        setb(v, 0x2e02, 0xf);
        return;
    }
    setb(v, 0x2e02, 0);
    for (e = 0x2d00; e <= 0x2d11; e = (uint16_t)(e + 0x11)) {
        setb(v, e, e == 0x2d00 ? 0x81 : 0x0f);
        setb(v, (uint16_t)(e + 5), 0);
        setb(v, (uint16_t)(e + 7), 0);
        setb(v, (uint16_t)(e + 6), v->vstate[0xa]);
        setb(v, (uint16_t)(e + 8), v->vstate[0x12]);
        setw(v, (uint16_t)(e + 9), mw(v, 0x2cfa));
        setw(v, (uint16_t)(e + 0xb), mw(v, 0x2cfc));
        setw(v, (uint16_t)(e + 0xd), 0x28b0);
    }
    uw_sincos_lerp((uint16_t)(vsw(v, 0x2c) + half), &s, &c);
    setw(v, 0x2d12, (uint16_t)(s >> 4));
    setw(v, 0x2d14, (uint16_t)(c >> 4));
    uw_sincos_lerp((uint16_t)(vsw(v, 0x2c) - half), &s, &c);
    setw(v, 0x2d01, (uint16_t)(s >> 4));
    setw(v, 0x2d03, (uint16_t)(c >> 4));
}

void uw_vl_spans(uw_vl *v) {
    uint16_t cell = 0x2890;
    v->facing = (int)(int8_t)mb(v, 0x2cf4);
    setup_frustum(v);
    /* view_traverse_spans. */
    setw(v, 0x2cf2, 0xffff);
    do {
        uint16_t p, end;
        setw(v, 0x2cf2, (uint16_t)(mw(v, 0x2cf2) + 1));
        for (p = 0x2e02; (mb(v, p) & 0xf) != 0xf; ) {
            uint16_t e = rec(v, p);
            step_edge(v, e);
            p = e;
        }
        p = 0x2e02;
        end = (uint16_t)(cell + 0x42);
        while ((mb(v, p) & 0xf) != 0xf) {
            while (mw(v, (uint16_t)(0x2d0f + (int8_t)mb(v, p) * 0x11)) > cell) {
                setb(v, cell, 0);
                cell = (uint16_t)(cell + 2);
            }
            resolve_pair(v, &p, &cell);
        }
        while (cell < end) {
            setb(v, cell, 0);
            cell = (uint16_t)(cell + 2);
        }
    } while (mb(v, 0x2e02) != 0xf);
}


/* view_build_light_map: the light each cell's distance from the
 * eye gets, into byte 1 whole -- a ramp by distance in the grid's own tile
 * units from shades.dat's record, unlit (0xf) past
 * the radius. shade_set_level calls it with the record's radius,
 * whenever the level's shading changes; nothing calls it per frame, which is
 * why the traversal keeps the nibble it finds. */
void uw_vl_light_map(uw_vl *v, int radius) {
    uw_light_map_build(v->mem, radius, v->lprm);
}

void uw_light_map_build(uint8_t *mem, int radius, const uint8_t *lprm) {
    uint8_t tab[16];
    int si, di;
    int16_t k = (int16_t)(lprm[0] | (lprm[1] << 8));
    int16_t add = (int16_t)(lprm[2] | (lprm[3] << 8));
    int16_t floor0 = (int16_t)(lprm[4] | (lprm[5] << 8));
    if (radius >= 0x10) return;
    for (si = 0; si < 16; si++) {
        int16_t a, d, l;
        if (si > radius) {
            tab[si] = 0xf;
            continue;
        }
        a = (int16_t)(si * 8);
        a = (int16_t)(a * a);
        a = (int16_t)(a << 1);
        d = (int16_t)uw_isqrt32((uint32_t)(int32_t)a);
        l = (int16_t)((int16_t)(d * k) >> 6);
        l = (int16_t)(l + floor0);
        if (l < 0) l = 0;
        l = (int16_t)(l + add);
        if (l > 0xe) l = 0xe;
        tab[si] = (uint8_t)l;
    }
    for (di = 0; di < 17; di++)
        for (si = 0; si < 33; si++) {
            int16_t sq = (int16_t)((16 - si) * (16 - si) + di * di);
            uint16_t r = uw_isqrt32((uint32_t)(int32_t)sq);
            mem[(uint16_t)(0x2891 + di * 0x42 + si * 2)] =
                (int16_t)r > radius ? 0xf : tab[r];
        }
}

/* view_place_camera, the two branches that read no object: the
 * camera following the tracked object -- the eye at the player's position,
 * 0xa4 above the feet, with the step sway added when its flag
 * is set -- and the free camera. A camera following
 * another object is counted and left as the state has it. Then
 * view_setup_frame: the eye's tile, the facing quadrant, and the
 * eye's position within its tile turned into the quadrant's frame, in place. */
static void setvs(uint8_t *vs, int at, uint16_t w) {
    vs[at] = (uint8_t)w;
    vs[at + 1] = (uint8_t)(w >> 8);
}
static int16_t getvs(const uint8_t *vs, int at) {
    return (int16_t)(vs[at] | (vs[at + 1] << 8));
}

int uw_vl_setup_frame(uw_vl *v, uint8_t *vs) {
    int16_t tx, ty, x, y, t;
    uint16_t di;
    int f;

    if (mw(v, 0x2e12) == mw(v, 0x7276) && mw(v, 0x2e10) == mw(v, 0x7274)) {
        setvs(vs, 0xa, mw(v, 0x2780));
        setvs(vs, 0x12, mw(v, 0x2782));
        setvs(vs, 0xe, (uint16_t)(mw(v, 0x2784) + 0xa4));
        setvs(vs, 0x2c, mw(v, 0x727a));
        setvs(vs, 0x28, mw(v, 0x3588));
        setvs(vs, 0x2a, mw(v, 0x358a));
        if (mb(v, 0x3578)) {
            setvs(vs, 0xe, (uint16_t)(getvs(vs, 0xe) + ms(v, 0x3580)));
            if (getvs(vs, 0xe) > 1000) setvs(vs, 0xe, 1000);
            setvs(vs, 0x2c, (uint16_t)(getvs(vs, 0x2c) + ms(v, 0x3582)));
            setvs(vs, 0x28, (uint16_t)(getvs(vs, 0x28) + ms(v, 0x3584)));
            setvs(vs, 0x2a, (uint16_t)(getvs(vs, 0x2a) + ms(v, 0x3586)));
        }
    } else if ((mw(v, 0x2e10) | mw(v, 0x2e12)) == 0) {
        setvs(vs, 0xa, mw(v, 0x3592));
        setvs(vs, 0xe, mw(v, 0x3596));
        setvs(vs, 0x12, mw(v, 0x3594));
        setvs(vs, 0x2c, mw(v, 0x358c));
        setvs(vs, 0x28, mw(v, 0x358e));
        setvs(vs, 0x2a, mw(v, 0x3590));
    } else {
        return 0;
    }

    tx = (int16_t)(getvs(vs, 0xa) >> 8);
    ty = (int16_t)(getvs(vs, 0x12) >> 8);
    setw(v, 0x248e, (uint16_t)tx);
    setw(v, 0x2490, (uint16_t)ty);
    /* tile_ptr_from_xy: a far NULL off the map. */
    if ((((uint16_t)tx & 0xffc0) + ((uint16_t)ty & 0xffc0)) == 0) {
        setw(v, 0x2cfa, (uint16_t)(mw(v, 0x19b4) + ((uint16_t)tx + (uint16_t)ty * 0x40) * 4));
        setw(v, 0x2cfc, mw(v, 0x19b6));
    } else {
        setw(v, 0x2cfa, 0);
        setw(v, 0x2cfc, 0);
    }
    di = (uint16_t)((uint16_t)getvs(vs, 0x2c) >> 13);
    f = (int)(((di + 1) & 7) >> 1);
    setw(v, 0x2e00, (uint16_t)(f * 16 + 0x464));
    setb(v, 0x2e03, (uint8_t)(di % 2));
    setb(v, 0x2cf4, (uint8_t)f);
    x = (int16_t)(getvs(vs, 0xa) & 0xff);
    y = (int16_t)(getvs(vs, 0x12) & 0xff);
    switch (f) {
    case 1: t = x; x = (int16_t)(0xff - y); y = t; break;
    case 2: x = (int16_t)(0xff - x); y = (int16_t)(0xff - y); break;
    case 3: t = x; x = y; y = (int16_t)(0xff - t); break;
    default: break;
    }
    setvs(vs, 0xa, (uint16_t)x);
    setvs(vs, 0x12, (uint16_t)y);
    setvs(vs, 0x2c, (uint16_t)(getvs(vs, 0x2c) - mw(v, (uint16_t)(0x45c + f * 2))));
    return 1;
}
