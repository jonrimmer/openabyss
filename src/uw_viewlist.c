/* SPDX-License-Identifier: MIT */
/* See uw_viewlist.h. */
#include "uw_viewlist.h"
#include "uw_trig.h"

#include <string.h>

/* The data segment, as the build has it so far: v->mem. */
static uint16_t dsw(const uw_vl *v, uint16_t at) {
    return (uint16_t)(v->mem[at] | (v->mem[(uint16_t)(at + 1)] << 8));
}
static int16_t dss(const uw_vl *v, uint16_t at) { return (int16_t)dsw(v, at); }
static uint8_t dsb(const uw_vl *v, uint16_t at) { return v->mem[at]; }
static int8_t dsc(const uw_vl *v, uint16_t at) { return (int8_t)v->mem[at]; }
static void setw(uw_vl *v, uint16_t at, uint16_t w) {
    v->mem[at] = (uint8_t)w;
    v->mem[(uint16_t)(at + 1)] = (uint8_t)(w >> 8);
}
static int16_t vs(const uw_vl *v, int at) {
    return (int16_t)(v->vstate[at] | (v->vstate[at + 1] << 8));
}

static void emit(uw_vl *v, uint16_t w) {
    if (v->list_overflow) return;
    if (v->ptr >= UW_VL_LIST_END - 2) {
        v->list_overflow = 1;
        return;
    }
    v->list[v->ptr] = (uint8_t)w;
    v->list[(uint16_t)(v->ptr + 1)] = (uint8_t)(w >> 8);
    v->ptr = (uint16_t)(v->ptr + 2);
}

/* A tile byte by index, 0 outside the map -- the original reads whatever
 * the far pointer reaches, which for the map's own border is solid rock. */
static uint8_t tile_byte(const uw_vl *v, long idx, int k) {
    if (idx < 0 || idx >= 64 * 64) return 0;
    return v->tiles[idx * 4 + k];
}

/* drawlist_emit_vertex: 0xb6, then `(x - 0x10) << 3` and
 * `y << 3` as a word, then `z << 1` and the vertex number's low byte. Returns
 * the number, which the caller keeps as a byte. */
static uint8_t vertex(uw_vl *v, uint8_t x, uint8_t y, uint8_t z) {
    uint8_t n = (uint8_t)v->vertex_count;
    emit(v, 0xb6);
    emit(v, (uint16_t)(((uint8_t)(y << 3) << 8) | (uint8_t)((uint8_t)(x - 0x10) << 3)));
    emit(v, (uint16_t)((n << 8) | (uint8_t)(z << 1)));
    v->vertex_count++;
    return n;
}

/* drawlist_emit_shape_plain, drawlist_emit_shaded_quad and
 * drawlist_emit_textured_face: a flat face. The
 * colour is `pick` while drawlist_object_count is non-zero -- the pick pass
 * paints ids, not colours -- and otherwise the first byte of art slot `art`
 * (gr_map_art_page), shaded as shade[((dist * light_scale) << 8) + texel]. */
static void flat_face(uw_vl *v, const uint8_t *quad, uint8_t dist, uint16_t art,
                      uint16_t pick) {
    uint16_t colour = pick;
    if (dsb(v, 0x311e) == 0) {
        uint8_t texel = 0;
        if (art < UW_VL_ART && v->art[art]) texel = v->art[art][0];
        else v->unsupported++;
        colour = v->shade ? v->shade[(uint16_t)(((uint16_t)(dist * v->light_scale) << 8)
                                                + texel) & 0xfff]
                          : 0;
        if (!v->shade) v->unsupported++;
    }
    v->mem[0x2e0a] = (uint8_t)colour;
    v->mem[0x2e0b] = (uint8_t)(colour >> 8);
    emit(v, 0x2e);
    emit(v, colour);
    emit(v, 0x7e);
    emit(v, 4);
    emit(v, (uint16_t)(quad[0] << 3));
    emit(v, (uint16_t)(quad[1] << 3));
    emit(v, (uint16_t)(quad[2] << 3));
    emit(v, (uint16_t)(quad[3] << 3));
}

/* drawlist_emit_shape_lod, the floor's and ceiling's emitter at
 * the top two detail settings. Distance at or past the word at 0x549 with
 * a quad hands off to the plain emitter, which this stage does not carry. */
static void shape_lod(uw_vl *v, const uint8_t *quad, uint8_t dist, uint8_t tex) {
    const uint8_t *fac = v->mem + v->facing_table;
    if ((int)dist >= dss(v, 0x549) && quad) {
        flat_face(v, quad, dist, (uint16_t)(tex + 0x6a), (uint16_t)(tex + 0xf0));
        return;
    }
    if ((int)dist >= dss(v, 0x547)) {
        tex = (uint8_t)(tex + 0x6a);
        v->lod_shade = v->light_scale;
        v->lod_scale = 0x100;
        v->lod_mask = 0xff;
        v->lod_step = 0x10;
    } else {
        v->lod_shade = 2;
        if (dist != 0 || dsb(v, 0x12b6) != 0x64)
            v->lod_shade = (uint16_t)(v->lod_shade + v->light_scale);
        v->lod_scale = 0x400;
        v->lod_step = 0x20;
        v->lod_mask = 0x3ff;
        tex = (uint8_t)(tex + 0x30);
    }
    emit(v, 0x3e);
    emit(v, v->lod_shade);
    emit(v, tex);
    emit(v, v->lod_scale);
    emit(v, 2);
    emit(v, (uint16_t)((v->rast[0xb000] | (v->rast[0xb001] << 8))
                       + v->lod_shade * 8));
    emit(v, v->lod_mask);
    if (quad) {
        emit(v, 0x36);
        emit(v, v->lod_shade);
        emit(v, (uint16_t)(quad[0] << 3)); emit(v, fac[3]);
        emit(v, (uint16_t)(quad[1] << 3)); emit(v, fac[2]);
        emit(v, (uint16_t)(quad[2] << 3)); emit(v, fac[1]);
        emit(v, (uint16_t)(quad[3] << 3)); emit(v, fac[0]);
    }
}

/* drawlist_emit_group_lod, the walls' emitter: the 0x3e header
 * goes out only while the budget (lod_budget) is under 1, the store of the
 * record's v_max always, then 0xa0 or 0xa2 with the four vertex numbers as
 * two words. */
static void group_lod(uw_vl *v, const uint8_t *quad, uint8_t dist,
                      uint8_t vsize, uint8_t tex) {
    int8_t was;
    if ((int)dist >= dss(v, 0x549) && quad) {
        flat_face(v, quad, dist, (uint16_t)(tex + 0x3a), (uint16_t)(tex + 0xc0));
        return;
    }
    if ((int)dist >= dss(v, 0x547)) {
        v->lod_shade = 0;
        if (dist != 0) v->lod_shade = v->light_scale;
        v->lod_scale = 0x100;
        v->lod_step = 0x10;
        v->lod_mask = (uint16_t)((uint16_t)(vsize << 6) - 1);
        tex = (uint8_t)(tex + 0x3a);
    } else {
        v->lod_shade = 4;
        if (dist != 0 || dsb(v, 0x12b6) != 0x64)
            v->lod_shade = (uint16_t)(v->lod_shade + v->light_scale);
        v->lod_scale = 0x1000;
        v->lod_step = 0x40;
        v->lod_mask = (uint16_t)((uint16_t)(vsize << 10) - 1);
    }
    was = v->lod_budget;
    v->lod_budget = (int8_t)(uint8_t)(v->lod_budget + 1);
    if (was < 1) {
        emit(v, 0x3e);
        emit(v, v->lod_shade);
        emit(v, tex);
        emit(v, v->lod_scale);
    }
    emit(v, 2);
    emit(v, (uint16_t)((v->rast[0xb000] | (v->rast[0xb001] << 8))
                       + v->lod_shade * 8));
    emit(v, v->lod_mask);
    if (quad) {
        emit(v, v->no_view_offset ? 0xa2 : 0xa0);
        emit(v, v->lod_shade);
        emit(v, (uint16_t)(quad[1] << 8 | quad[0]));
        emit(v, (uint16_t)(quad[3] << 8 | quad[2]));
    }
}

/* The three emitter pointers view_build_draw_list loads from 0x551,
 * 0x559 and 0x561 by the detail flag. */
static void shape_fn(uw_vl *v, uint16_t fn, const uint8_t *quad, uint8_t dist,
                     uint8_t tex) {
    if (fn == 0x0a4a) shape_lod(v, quad, dist, tex);
    else if (fn == 0x079a) flat_face(v, quad, dist, (uint16_t)(tex + 0x6a), (uint16_t)(tex + 0xf0));
    else if (fn == 0x0880) flat_face(v, quad, dist, (uint16_t)(tex + 0x6a), 0xfa);
    else v->unsupported++;
}

static void face_fn(uw_vl *v, uint16_t fn, const uint8_t *quad, uint8_t dist,
                    uint8_t vsize, uint8_t tex) {
    if (fn == 0x0c1b) group_lod(v, quad, dist, vsize, tex);
    else if (fn == 0x0964) flat_face(v, quad, dist, (uint16_t)(tex + 0x3a), (uint16_t)(tex + 0xc0));
    else v->unsupported++;
}

/* ======================================================================
 * The object pass. Objects are 16-bit offsets into the level segment, 0 the
 * null far pointer; every DS address below is the listing's.
 * ====================================================================== */

static uint8_t ob(const uw_vl *v, uint16_t p, int k) {
    return v->level[(uint16_t)(p + k)];
}
static uint16_t ow(const uw_vl *v, uint16_t p, int k) {
    return (uint16_t)(ob(v, p, k) | (ob(v, p, k + 1) << 8));
}
static uint16_t light_word(const uw_vl *v) {          /* [0x3172] * [0x54d] */
    return (uint16_t)(dsb(v, 0x3172) * v->light_scale);
}
static int detailed_pick(const uw_vl *v) {            /* ([0x311e] | [0x54b]) */
    return (dsb(v, 0x311e) | dsw(v, 0x54b)) != 0;
}
/* drawlist_word_addr: word n of the header before drawlist_base. */
static uint16_t word_addr(const uw_vl *v, int n) {
    return (uint16_t)(dsw(v, 0x237c) - 0x18 + n * 2);
}
static void store(uw_vl *v, int n, uint16_t w) {
    emit(v, 2);
    emit(v, word_addr(v, n));
    emit(v, w);
}

/* obj_ptr_from_index, obj_deref_link, obj_is_mobile
 * and obj_index_from_ptr. */
static uint16_t obj_ptr(const uw_vl *v, uint16_t idx) {
    if (idx == 0) return 0;
    if (idx < 0x100) return (uint16_t)(dsw(v, 0x272e) + idx * 0x1b);
    return (uint16_t)(dsw(v, 0x275a) + idx * 8 - 0x800);
}
static uint16_t obj_deref(const uw_vl *v, uint16_t link) {
    uint16_t idx;
    if (link == 0) return 0;
    idx = (uint16_t)(ow(v, link, 0) >> 6);
    if (idx == 0) return 0;
    if (idx < 0x100) return (uint16_t)(dsw(v, 0x272e) + idx * 0x1b);
    return (uint16_t)(dsw(v, 0x275a) + (idx - 0x100) * 8);
}
static int obj_is_mobile(const uw_vl *v, uint16_t p) {
    return p != 0 && p < dsw(v, 0x275a);
}
static uint16_t obj_index(const uw_vl *v, uint16_t p) {
    if (obj_is_mobile(v, p))
        return (uint16_t)(((long)p - dsw(v, 0x272e)) / 0x1b);
    return (uint16_t)(((long)p - dsw(v, 0x275a)) / 8 + 0x100);
}

/* drawlist_emit_label_ref for a label other than 0xa0: the
 * displacement ((target - here) - 1) * 2 in words from drawlist_base. A
 * label not yet defined gets a fixup the define patches; none is carried. */
static void label_ref(uw_vl *v, uint8_t label) {
    uint16_t pos = dsw(v, (uint16_t)(0x7856 + label * 2));
    long here = ((long)v->ptr - (long)dsw(v, 0x237c)) / 2;
    if (pos == 0xffff) {
        v->unsupported++;
        emit(v, 0);
        return;
    }
    emit(v, (uint16_t)(((long)pos - here - 1) * 2));
}

/* automap_sweep_list_op. The lists are a count word and up to
 * nine entries: 0x32a8 + step * 0x12 for each column, 0x3160 the carry and
 * 0x314e where the carry waits while the sweep changes halves. */
static void list_op(uw_vl *v, int8_t mode) {
    switch (mode) {
    case -10:
        memset(v->mem + 0x32a8, 0, 0x252);
        memset(v->mem + 0x314e, 0, 0x12);
        memset(v->mem + 0x3160, 0, 0x12);
        break;
    case 2:
        memset(v->mem + 0x314e, 0, 0x12);
        memset(v->mem + 0x3160, 0, 0x12);
        break;
    case 1:
        memcpy(v->mem + 0x314e, v->mem + 0x3160, 0x12);
        setw(v, 0x3160, 0);
        break;
    case 0:
        if (dsw(v, 0x314e) != 0) {
            if ((uint16_t)(dsw(v, 0x314e) + dsw(v, 0x3160)) >= 9)
                setw(v, 0x314e, (uint16_t)(9 - dsw(v, 0x3160) - 1));
            memmove(v->mem + (uint16_t)(0x3162 + dsw(v, 0x3160) * 2),
                    v->mem + 0x3150, (uint16_t)(dsw(v, 0x314e) * 2));
            setw(v, 0x3160, (uint16_t)(dsw(v, 0x3160) + dsw(v, 0x314e)));
        }
        break;
    default:
        break;
    }
    v->mem[0x32a6] = (uint8_t)mode;
}

/* drawlist_cell_from_object: the object's sub-tile position
 * turned by view_quadrant_rotation into cell bytes 1 (x) and 2 (y), its
 * height into byte 3. */
static void cell_from_object(uw_vl *v, uint16_t cell, uint16_t p) {
    int f = v->facing;
    uint16_t w2 = ow(v, p, 2);
    int xb = (w2 & 0xe000) >> 13, yb = (w2 & 0x1c00) >> 10;
    uint16_t a = (uint16_t)(f * 16 + xb * 2);
    uint16_t b = (uint16_t)(((f + 1) & 3) * 16 + yb * 2);
    v->mem[(uint16_t)(cell + 1)] = (uint8_t)(dsb(v, (uint16_t)(a + 0x6e6))
                                             + dsb(v, (uint16_t)(b + 0x6e6)));
    v->mem[(uint16_t)(cell + 2)] = (uint8_t)(dsb(v, (uint16_t)(a + 0x6e7))
                                             + dsb(v, (uint16_t)(b + 0x6e7)));
    v->mem[(uint16_t)(cell + 3)] = (uint8_t)(ob(v, p, 2) & 0x7f);
}

/* drawlist_depth_key, by the half of the row being swept. */
static void depth_key(uw_vl *v, uint16_t cell) {
    uint8_t *c = v->mem + cell;
    switch ((int8_t)dsb(v, 0x32a6)) {
    case 0: c[0] = (uint8_t)(c[2] << 1); break;
    case 1: c[0] = (uint8_t)(c[1] + c[2] + 1); break;
    case 2: c[0] = (uint8_t)(8 - c[1] + c[2]); break;
    default: break;
    }
}

#define ORDER(i) v->mem[(uint16_t)(0x3173 + (i))]
#define KEY(i)   ((int8_t)v->mem[(uint16_t)(0x31af + (int8_t)ORDER(i) * 4)])

/* drawlist_sort_range: a bubble sort of the order bytes, larger
 * keys first. */
static void sort_range(uw_vl *v, int lo, int hi) {
    int di, si;
    for (di = hi - 1; di >= lo; di--)
        for (si = lo; si <= di; si++)
            if (KEY(si) < KEY(si + 1)) {
                uint8_t t = ORDER(si);
                ORDER(si) = ORDER(si + 1);
                ORDER(si + 1) = t;
            }
}

/* drawlist_partition: the objects on one side of the pivot's
 * value fill the order from one end, the rest from the other, and the pivot
 * takes the place where they meet. `field` 0 compares heights. */
static void partition(uw_vl *v, int flag, int16_t *split, int pivot, int n,
                      int value, int field) {
    int pos[2], step[2], si;
    if (flag) { pos[0] = n - 1; step[0] = -1; pos[1] = 0; step[1] = 1; }
    else      { pos[0] = 0; step[0] = 1; pos[1] = n - 1; step[1] = -1; }
    for (si = 0; si < n; si++) {
        int val, d;
        if (si == pivot) continue;
        if (field == 0)
            val = ow(v, obj_ptr(v, dsw(v, (uint16_t)(0x3500 + si * 2))), 2) & 0x7f;
        else
            val = (int8_t)dsb(v, (uint16_t)(0x31af + si * 4 + field));
        d = val > value;
        ORDER(pos[d]) = (uint8_t)si;
        pos[d] += step[d];
    }
    if (pos[0] == pos[1]) {
        *split = (int16_t)pos[0];
        ORDER(*split) = (uint8_t)pivot;
    }
}

/* drawlist_partition_by_z: a bridge splits by height, the eye's
 * side of it last. */
static void partition_by_z(uw_vl *v, int pivot, int16_t *split, int n) {
    int z = ow(v, obj_ptr(v, dsw(v, (uint16_t)(0x3500 + pivot * 2))), 2) & 0x7f;
    int eye = ow(v, dsw(v, 0x7274), 2) & 0x7f;
    partition(v, eye < z, split, pivot, n, z, 0);
}

/* drawlist_partition_by_axis: a door frame splits along the axis
 * its heading crosses. */
static void partition_by_axis(uw_vl *v, int pivot, int16_t *split, int n) {
    uint16_t p = obj_ptr(v, dsw(v, (uint16_t)(0x3500 + pivot * 2)));
    int field, value, flag;
    if (((((ow(v, p, 2) & 0x380) >> 7) + v->facing * 2) & 3) == 0) {
        field = 2;
        value = (int8_t)dsb(v, (uint16_t)(0x31af + pivot * 4 + 2));
        flag = 1;
    } else {
        field = 1;
        value = (int8_t)dsb(v, (uint16_t)(0x31af + pivot * 4 + 1));
        if (dsb(v, 0x32a6) == 2) flag = 0;
        else if (dsb(v, 0x32a6) == 1) flag = 1;
        else flag = (vs(v, 0xa) >> 5) < value;
    }
    partition(v, flag, split, pivot, n, value, field);
}

static void emit_shape(uw_vl *v, uint8_t shape, uint16_t p, int8_t turn,
                       uint16_t tex);

/* drawlist_emit_shape_group: the frame and the panel of a door
 * or portcullis, the panel shifted by how far it is open, drawn in the order
 * the eye's side of it needs. */
static void emit_shape_group(uw_vl *v, uint8_t g, uint16_t p) {
    int16_t si = -1, b2, b4, b6 = 0;
    int8_t first = 0, stepd = 1, i;
    uint16_t w0 = ow(v, p, 0);
    uint16_t q = (uint16_t)((w0 & 0x1e00) >> 9);
    uint8_t heading2 = (uint8_t)(((ow(v, p, 2) & 0x380) >> 7) << 1);
    uint8_t wall = (uint8_t)(tile_byte(v, v->tile, 2) & 0x3f);
    uint16_t b000 = (uint16_t)(v->rast[0xb000] | (v->rast[0xb001] << 8));
    int shut_side = 0;

    v->mem[0x54f] = 1;
    if ((g & 7) == 6) {
        b4 = 0;
        si = dss(v, 0x34fc);
        b6 = (int16_t)(dsw(v, 0x34fc) - (q & 7) * 0x30);
        shut_side = 1;
        b2 = (int16_t)(0x400 - b6 - 0xd0);
        first = 1;
        stepd = -1;
        store(v, 5, (uint16_t)(q & 7));
        emit(v, 0x4c); emit(v, 0); emit(v, 0);
        emit(v, (uint16_t)(0xd0 - (q & 7) * 0x30));
        emit(v, 0x400);
    } else {
        if ((q & 7) || ((w0 & 0x1c0) >> 6) == 7)
            setw(v, 0x34fc, (uint16_t)(dsw(v, 0x34fc) - 0xc0));
        b4 = (int16_t)((q & 7) * ((int)(((w0 & 0x2000) >> 13) * 2) - 1));
        b2 = (int16_t)(0x400 - dsw(v, 0x34fc) - 0xd0);
        store(v, 5, (uint16_t)((uint16_t)b4 << 12));
    }
    emit(v, 0x4c); emit(v, 0); emit(v, 0); emit(v, (uint16_t)b2); emit(v, 0x800);
    (void)shut_side;   /* [bp-0xa]: computed against the Avatar's heading, never read */

    if ((q & 7) && si == -1) {
        int h = (int)((((ow(v, p, 2) & 0x380) >> 7) - v->facing * 2) & 7);
        int16_t dx0 = (int16_t)(dsw(v, 0x34fa) - (uint16_t)vs(v, 0xa));
        int16_t cx = (int16_t)(dsw(v, 0x34fe) - (uint16_t)vs(v, 0x12));
        int dd = 0;
        switch (h & 3) {
        case 0: dd = cx < 0; break;
        case 1: dd = (int16_t)-cx > dx0; break;
        case 2: dd = dx0 < 0; break;
        case 3: dd = dx0 < cx; break;
        }
        dd = h / 4 + (int)((w0 & 0x2000) >> 13) + dd;
        if (!(dd & 1)) { first = 1; stepd = -1; }
    }
    store(v, 3, (uint16_t)((q & 7) + (si >= 0)));

    for (i = first; i >= 0 && i <= 1; i = (int8_t)(i + stepd)) {
        if (i == 0) {
            if (v->lod_budget < 1)
                group_lod(v, NULL, dsb(v, 0x3172), (uint8_t)(dss(v, 0x34fc) >> 6), wall);
            emit(v, 2);
            emit(v, (uint16_t)(b000 + v->lod_shade * 8));
            emit(v, (uint16_t)((int16_t)((int16_t)(v->lod_step * v->lod_step) >> 8) * b2 - 1));
            store(v, 7, (uint16_t)(v->lod_step * v->lod_step - 1));
            store(v, 6, (uint16_t)(b000 + v->lod_shade * 8));
            if (dsb(v, 0x311e)) { emit(v, 0xae); emit(v, (uint16_t)(wall + 0xc0)); }
            emit(v, 0xb2);
            emit(v, v->lod_shade);
            emit_shape(v, 1, p, (int8_t)heading2, wall);
        } else {
            if (dsb(v, 0x311e)) { emit(v, 0xae); emit(v, (uint16_t)(dsb(v, 0x311e) - 1)); }
            if (si >= 0) {
                setw(v, 0x34fc, (uint16_t)si);
                emit_shape(v, 0xc, p, (int8_t)heading2, 0);
                setw(v, 0x34fc, (uint16_t)b6);
            } else if ((g & 7) == 7) {
                if (v->lod_budget < 1)
                    group_lod(v, NULL, dsb(v, 0x3172), (uint8_t)(dss(v, 0x34fc) >> 6), wall);
                emit(v, 0xb2);
                emit(v, v->lod_shade);
                emit(v, 2);
                emit(v, (uint16_t)(b000 + v->lod_shade * 8));
                emit(v, (uint16_t)(v->lod_step * v->lod_step - 1));
                emit_shape(v, 0xf, p, (int8_t)heading2, wall);
            } else {
                emit_shape(v, 0xe, p, (int8_t)heading2,
                           (uint16_t)(dsw(v, 0x15e2) + (g & 7) + 0x30));
            }
        }
    }
}

/* drawlist_emit_shape: a model by its entry in the table at
 * 0x60a -- flags, then up to three header store values -- with its
 * texture, its origin at the object and its turn, called through label
 * 0x60 + model. */
static void emit_shape(uw_vl *v, uint8_t shape, uint16_t p, int8_t turn,
                       uint16_t tex) {
    int16_t lab = -1;                                     /* [bp-0x6] */
    uint8_t di = dsb(v, (uint16_t)(0x60a + shape * 4));
    uint16_t rot, x = dsw(v, 0x34fa), y = dsw(v, 0x34fe), z;
    uint16_t b000 = (uint16_t)(v->rast[0xb000] | (v->rast[0xb001] << 8));
    int si;

    store(v, 10, light_word(v));
    if (di & 0x20) {
        /* gr_map_art_page(tex + 0x3a), and the first byte of the art. */
        uint16_t slot = (uint16_t)(tex + 0x3a);
        uint8_t px = 0;
        if (slot < UW_VL_ART && v->art[slot]) px = v->art[slot][0];
        else v->unsupported++;
        store(v, 0, px);
        store(v, 10, (uint16_t)(v->tile_type * v->light_scale));
    } else if (di & 0x80) {
        uint16_t ph = (uint16_t)((v->clock & 0x1ff) >> 6);
        v->mem[0x2e1c] = 1;
        if (ph & 4) ph = (uint16_t)(3 - (ph & 3));
        for (si = 0; si < (di & 7); si++)
            store(v, si, (uint16_t)(((ow(v, p, 6) >> 6) & 0x1ff) + ph
                                    + dsb(v, (uint16_t)(0x60b + shape * 4 + si))));
    } else {
        for (si = 0; si < (di & 7); si++)
            store(v, si, dsb(v, (uint16_t)(0x60b + shape * 4 + si)));
    }

    if (di & 0x10) {
        uint8_t b7 = dsb(v, (uint16_t)(0x60d + shape * 4));
        uint16_t q = (uint16_t)((ow(v, p, 0) & 0x1e00) >> 9);
        uint16_t d = (uint16_t)((b7 >> 5) + 1);
        if ((int16_t)tex < 0) {
            if (shape == 2) {
                if (q >= d) {
                    shape_lod(v, NULL, v->tile_type, (uint8_t)(q - d));
                    emit(v, 0xb2);
                    emit(v, v->lod_shade);
                    tex = 0xffff;
                } else {
                    v->mem[0x54f] = 2;
                    tex = (uint16_t)((b7 & 0x1f) + dsw(v, 0x15e2) + 0x10 + q % d);
                    store(v, 0xb, dsw(v, (uint16_t)(0x6e2 + q * 2)));
                    emit(v, 0xb2);
                    emit(v, 6);
                }
            } else {
                tex = (uint16_t)((b7 & 0x1f) + dsw(v, 0x15e2) + 0x10 + q % d);
            }
        }
        if ((int16_t)tex >= 0) {
            emit(v, 0xc0);
            emit(v, tex);
            emit(v, light_word(v));
        }
        v->lod_budget = (int8_t)0xe0;
    }

    if (di & 8) {
        uint16_t s = (uint16_t)(0x400 - dsw(v, 0x34fc));
        emit(v, 0x4c); emit(v, 0); emit(v, 0); emit(v, s); emit(v, 0x800);
        emit(v, 2); emit(v, (uint16_t)(b000 + 0x30)); emit(v, (uint16_t)(s * 2 - 1));
    }

    z = dsw(v, 0x34fc);
    emit(v, 0x18);
    emit(v, x); emit(v, (int16_t)x < 0 ? 0xffff : 0);
    emit(v, z); emit(v, (int16_t)z < 0 ? 0xffff : 0);
    emit(v, y); emit(v, (int16_t)y < 0 ? 0xffff : 0);

    if (turn < 0)
        rot = (uint16_t)(((((ow(v, p, 2) & 0x380) >> 7) + 8 - v->facing * 2) & 7) << 13);
    else
        rot = (uint16_t)(((turn + 0x10 - v->facing * 4) & 0xf) << 12);
    if ((di & 0x40) && !(di & 0x10)) {
        uint16_t e8 = 0;
        if (p < dsw(v, 0x275a)) {
            e8 = (uint16_t)((((ob(v, p, 0x14) & 0xf8) >> 3) - 0x10) * 0x266);
            rot = (uint16_t)(((((((ow(v, p, 2) & 0x380) >> 7) << 5)
                                + (ob(v, p, 0x18) & 0x1f) + 0x100 - v->facing * 64)
                               & 0xff)) << 8);
        }
        store(v, 5, e8);
    }
    if ((shape == 0x10 || shape == 0x11) && !detailed_pick(v))
        lab = 0;
    else if (dsb(v, 0x311e) == 0
             && ((dsb(v, dsw(v, 0x7270) + 0xb5) >> 4) & 0xf) == 1 && shape == 2)
        lab = 1;
    if (lab != -1) store(v, 8, (uint16_t)lab);
    if (rot) { emit(v, 0x50); emit(v, rot); }
    else emit(v, 0x12);
    label_ref(v, (uint8_t)(shape + 0x60));
    if (lab != -1) store(v, 8, (uint16_t)((lab + 1) & 1));
    emit(v, 0x18);
    for (si = 0; si < 6; si++) emit(v, 0);
}

/* drawlist_emit_object. */
static void emit_object(uw_vl *v, uint16_t p) {
    uint16_t w0 = ow(v, p, 0);
    uint16_t id;                                          /* [bp-0x4] */
    int kind;                                             /* [bp-0x2] */

    v->objects++;
    if (p == dsw(v, 0x7274)) return;                      /* the Avatar */
    if (w0 & 0x4000) return;
    if (dsb(v, 0x311e)) {
        uint8_t pk = dsb(v, 0x311e);
        setw(v, (uint16_t)(0x2e1c + pk * 2), (uint16_t)(v->tile + dsw(v, 0x32a0)));
        setw(v, (uint16_t)(0x2f9c + pk * 2), obj_index(v, p));
        emit(v, 0xae);
        emit(v, pk);
        pk++;
        v->mem[0x311e] = pk >= 0xc0 ? 1 : pk;
    }
    if (obj_is_mobile(v, p) && ((w0 & 0x1c0) >> 6) != 1) {
        uint16_t bx = ob(v, p, 0xb), bd = ob(v, p, 0xd), a = 0, c = 0;
        switch (v->facing) {
        case 0: c = bd; a = bx; break;
        case 1: c = bx; a = (uint16_t)(0xff - bd); break;
        case 2: a = (uint16_t)(0xff - bx); c = (uint16_t)(0xff - bd); break;
        case 3: c = (uint16_t)(0xff - bx); a = bd; break;
        default: break;
        }
        setw(v, 0x34fa, (uint16_t)((dsw(v, 0x34fa) & 0xff00) + a));
        setw(v, 0x34fe, (uint16_t)((dsw(v, 0x34fe) & 0xff00) + c));
    }
    kind = dsb(v, (uint16_t)((w0 & 0x1ff) * 11 + 0x5b77)) & 3;
    if (((w0 & 0x1c0) >> 6) == 7) {
        v->mem[0x2e1c] = 1;
        id = (uint16_t)(ob(v, p, 6) & 0x3f);
        if (kind == 0) id = id > 0 ? (uint16_t)(id + 0x1c0) : (uint16_t)(w0 & 0x1ff);
    } else {
        id = (uint16_t)(w0 & 0x1ff);
    }

    switch (kind) {
    case 2:                                               /* a model */
        id &= 0x3f;
        if (id >> 4) {
            int16_t s;
            id = (uint16_t)(id - 0x10);
            s = dss(v, (uint16_t)(0x682 + id * 2));
            if (s < 0 || id >= 0x20) return;
            emit_shape(v, (uint8_t)s, p, -1, 0xffff);
        } else {
            emit_shape_group(v, (uint8_t)id, p);
        }
        return;
    case 1: {                                             /* a creature */
        uint16_t frame = (uint16_t)(ob(v, p, 0x15) & 0x3f);
        uint16_t ang = (uint16_t)((uint16_t)(vs(v, 0x2c)
                                   + dsw(v, (uint16_t)(0x45c + v->facing * 2))) >> 11);
        uint16_t f8 = dsb(v, (uint16_t)(0x6c2 + (((((ow(v, p, 2) & 0x380) >> 7) << 2)
                                                  + 0x20 - ang) & 0x1f)));
        emit(v, 0x7a);
        emit(v, dsw(v, 0x34fa)); emit(v, dsw(v, 0x34fe)); emit(v, dsw(v, 0x34fc));
        emit(v, 0x7f8);
        if (frame >= 0x20) frame = (uint16_t)(((frame - 0x20) << 3) + f8 + 0x20);
        else if (frame != 0xc && ((f8 + 5) & 7) >= 3) frame = (uint16_t)(f8 + 0x20);
        emit(v, 0x5a);
        emit(v, (uint16_t)(id & 0x3f));
        emit(v, light_word(v));
        emit(v, frame);
        emit(v, (uint16_t)((ow(v, p, 0xb) & 0xf000) >> 12));
        emit(v, 0x7f8);
        return;
    }
    case 0:                                               /* a sprite */
        if ((id & 0x1e0) == 0xe0 && (id & 0x18)) id = 0xe0;
        emit(v, 0x7a);
        emit(v, dsw(v, 0x34fa)); emit(v, dsw(v, 0x34fe)); emit(v, dsw(v, 0x34fc));
        emit(v, 0x7f8);
        emit(v, 0x3a);
        emit(v, id);
        emit(v, light_word(v));
        emit(v, 0x7f8);
        return;
    default: {                                            /* 3: wall-textured */
        uint8_t tex = (uint8_t)(ob(v, p, 6) & 0x3f);
        int around;
        if (((w0 & 0x30) >> 4) == 3) {
            if (!detailed_pick(v)) store(v, 8, 0);
            emit_shape(v, 0x14, p, -1, (uint16_t)((id & 0xf) + dsw(v, 0x15e2)));
            if (!detailed_pick(v)) store(v, 8, 1);
            return;
        }
        v->lod_budget = (int8_t)0xe0;
        group_lod(v, NULL, v->tile_type, 4, tex);
        emit(v, 0xb2);
        emit(v, v->lod_shade);
        around = dsb(v, (uint16_t)(0x720c + tex * 2));
        if (around == 3 || around == 4) {
            v->mem[0x54f] = 3;
            around = 1;
        } else {
            around = around == 8 || around == 0xb;
        }
        if (around) around = !detailed_pick(v);
        if (around) store(v, 8, 0);
        emit_shape(v, 0x16, p, -1, tex);
        if (around) store(v, 8, 1);
        return;
    }
    }
}

/* drawlist_tile_objects. `link` is the far pointer to the word
 * whose top ten bits name the first object -- the tile's word at +2 -- or 0
 * when drawlist_flush_deferred calls it only to drain the lists. */
static void tile_objects(uw_vl *v, uint16_t link) {
    int chain = 0;                                        /* [bp-0x8] */
    uint16_t pivot = 0;                                   /* [bp-0x10] */
    int n = 0;                                            /* [bp-0x12] */
    int16_t split = -1;                                   /* [bp-0x14] */
    uint16_t slot = (uint16_t)(0x32a8 + v->step * 0x12);
    uint16_t k, p;

    /* What earlier tiles deferred to this one. */
    for (k = 1; dsw(v, slot) >= k && n < 9; k++, n++) {
        uint16_t e = dsw(v, (uint16_t)(slot + k * 2));
        uint16_t c = (uint16_t)(0x31af + n * 4);
        setw(v, (uint16_t)(0x3500 + n * 2), (uint16_t)(e & 0x3ff));
        cell_from_object(v, c, obj_ptr(v, (uint16_t)(e & 0x3ff)));
        if (e & 0x1000)
            v->mem[(uint16_t)(c + 1)] = (uint8_t)(v->mem[(uint16_t)(c + 1)]
                                                  + ((e & 0x2000) ? -8 : 8));
        if (e & 0x4000)
            v->mem[(uint16_t)(c + 2)] = (uint8_t)(v->mem[(uint16_t)(c + 2)] + 8);
        depth_key(v, c);
        if (e & 0x8000) v->mem[c]--;
    }
    if (dsw(v, 0x3160)) memcpy(v->mem + slot, v->mem + 0x3160, 0x12);
    else setw(v, slot, 0);
    setw(v, 0x3160, 0);

    for (p = obj_deref(v, link); p && chain < 0x3c; chain++) {
        uint16_t item = (uint16_t)(ow(v, p, 0) & 0x1ff);   /* [bp-0xc] */
        uint16_t c = (uint16_t)(0x31af + n * 4);          /* di */
        int kept = 0, deferred = 0;                       /* [bp-0x16], [bp-0x15] */
        if (item == 0x164 || (item >> 4) == 0x14 || item == 0x1cf) {
            pivot = (uint16_t)((item << 6) + n);
        } else if ((dsb(v, (uint16_t)(item * 11 + 0x5b6f)) >> 3) & 1) {
            uint16_t si = 0;
            int r = dsb(v, (uint16_t)(item * 11 + 0x5b6f)) & 7;
            kept = 1;
            cell_from_object(v, c, p);
            if ((int8_t)v->mem[(uint16_t)(c + 2)] - r < 0) si |= 0x4000;
            if (dsb(v, 0x32a6) == 1) r = -r;
            if (dsb(v, 0x32a6) != 0 && (((int8_t)v->mem[(uint16_t)(c + 1)] + r) & 0xfff8)) {
                si |= 0x1000;
                if (dsb(v, 0x32a6) == 2) si |= 0x2000;
            }
            if (si) {
                uint16_t dst;
                deferred = 1;
                si |= (uint16_t)((ow(v, link, 0) >> 6) & 0x3ff);
                if ((item & 0x1c0) == 0x1c0) si |= 0x8000;
                if ((si & 0x5000) == 0x5000) dst = 0x3160;
                else if (si & 0x4000) dst = slot;
                else if (si & 0x2000) dst = (uint16_t)(slot + 0x12);
                else dst = (uint16_t)(slot - 0x12);
                if (dsw(v, dst) >= 9) {
                    deferred = 0;
                } else {
                    setw(v, dst, (uint16_t)(dsw(v, dst) + 1));
                    setw(v, (uint16_t)(dst + dsw(v, dst) * 2), si);
                    v->deferred++;
                }
            }
        }
        if (!deferred) {
            if (!kept) cell_from_object(v, c, p);
            depth_key(v, c);
            if ((item & 0x1c0) == 0x1c0) v->mem[c]--;
            else if ((item & 0x1fe) == 0x16e) v->mem[c] = (uint8_t)(v->mem[c] + 0x20);
            setw(v, (uint16_t)(0x3500 + n * 2), (uint16_t)((ow(v, link, 0) >> 6) & 0x3ff));
            if (n < 0x3c) n++;
        }
        link = (uint16_t)(p + 4);
        p = obj_deref(v, link);
    }

    if (pivot != 0 && n > 1) {
        if (((int16_t)pivot >> 6) == 0x164) partition_by_z(v, pivot & 0x3f, &split, n);
        else partition_by_axis(v, pivot & 0x3f, &split, n);
        if (split > 1) sort_range(v, 0, split - 1);
        if (n - 2 > split) sort_range(v, split + 1, n - 1);
    } else {
        for (k = 0; k < n; k++) ORDER(k) = (uint8_t)k;
        if (n > 1) sort_range(v, 0, n - 1);
    }

    for (k = 0; k < n; k++) {
        int si = (int8_t)ORDER(k);
        uint16_t c = (uint16_t)(0x31af + si * 4);
        int8_t cx = (int8_t)v->mem[(uint16_t)(c + 1)], cy = (int8_t)v->mem[(uint16_t)(c + 2)];
        p = obj_ptr(v, dsw(v, (uint16_t)(0x3500 + si * 2)));
        setw(v, 0x34fa, (uint16_t)(((uint16_t)(v->step - 0x10) << 8) + (uint16_t)(cx * 32) + 0x10));
        setw(v, 0x34fe, (uint16_t)(((uint16_t)v->row << 8) + (uint16_t)(cy * 32) + 0x10));
        if (((ow(v, p, 0) & 0x1c0) >> 6) == 1 || !obj_is_mobile(v, p))
            setw(v, 0x34fc, (uint16_t)((ow(v, p, 2) & 0x7f) << 3));
        else
            setw(v, 0x34fc, ow(v, p, 0xf));
        if (dsb(v, 0x311e)) {
            int16_t a = (int16_t)((cy / 8) * dss(v, 0x32a4));
            a = (int16_t)(a + ((cx + 0x40) / 8 - 8) * dss(v, 0x32a2));
            setw(v, 0x32a0, (uint16_t)a);
        } else {
            int16_t d, sum, a;
            uint8_t l;
            d = (int16_t)((int16_t)(dsw(v, 0x34fa) - ((uint16_t)vs(v, 0xa) & 0xff)) >> 5);
            sum = (int16_t)(d * d);
            d = (int16_t)((int16_t)(dsw(v, 0x34fe) - ((uint16_t)vs(v, 0x12) & 0xff)) >> 5);
            sum = (int16_t)(sum + d * d);
            d = (int16_t)((int16_t)(dsw(v, 0x34fc) - (uint16_t)vs(v, 0xe)) >> 5);
            sum = (int16_t)(sum + d * d);
            d = sum > 0 ? (int16_t)uw_isqrt32((uint32_t)(int32_t)sum) : 0;
            a = (int16_t)((int16_t)(d * (int16_t)(v->lprm[0] | (v->lprm[1] << 8))) >> 6);
            a = (int16_t)(a + (int16_t)(v->lprm[4] | (v->lprm[5] << 8)));
            if (a < 0) a = 0;
            l = (uint8_t)((uint8_t)a + v->lprm[2]);
            v->mem[0x3172] = l > 0xe ? 0xe : l;
        }
        emit_object(v, p);
    }
}

/* drawlist_flush_deferred: a tile not drawn still drains. */
static void flush_deferred(uw_vl *v) {
    if (!v->level) return;
    if (dsw(v, (uint16_t)(0x32a8 + v->step * 0x12)) > 0) tile_objects(v, 0);
    if (dsw(v, 0x3160) > 0) tile_objects(v, 0);
}

/* automap_mark_tile. `cursor` is the tile's two bytes in
 * view_column_array, `tile` its index in the map and `am` its automap byte. */
static void mark_tile(uw_vl *v, uint16_t cursor, long tile, uint16_t am_idx) {
    uint16_t col = dsb(v, cursor);                         /* [bp-0x8] */
    uint8_t col1 = dsb(v, (uint16_t)(cursor + 1));
    uint8_t *am = &v->automap[am_idx & 0xfff];
    uint8_t t0 = tile_byte(v, tile, 0), t1 = tile_byte(v, tile, 1);
    uint8_t t2 = tile_byte(v, tile, 2);
    uint8_t height, code, corner, quad[4], wall;
    uint16_t hb;                                           /* [bp-0xc] */
    int visible;
    uint16_t bit;

    if (!(col & 0x80)) {
        if (*am == 0) {
            *am = dsb(v, (uint16_t)(0x5f7 + (t0 & 0xf)));
            v->newly_seen++;
        }
        flush_deferred(v);
        return;
    }
    v->vertex_count = 200;
    height = (uint8_t)((t0 >> 4) & 0xf);
    v->tile_type = (uint8_t)(col1 & 0xf);
    if (v->tile_type < 8) {
        code = (uint8_t)((t0 & 0xf)
                         | dsb(v, (uint16_t)(0x7192 + (((t1 >> 2) & 0xf) << 1))));
    } else {
        code = *am;
        if (code == 0) code = dsb(v, (uint16_t)(0x5f7 + (t0 & 0xf)));
    }
    corner = ((col & 0x44) == 4) ? (uint8_t)(col & 3) : 4;  /* [bp-0x9] */
    hb = (uint16_t)(0x589 + corner * 4);

    /* The floor: a flat tile when the eye is above it, a slope when the eye
     * is on its face's side -- two 16-bit products summed with a wrap and a
     * third added with the sign flag's own judgement (`add; jge`). */
    if (corner == 4) {
        visible = vs(v, 0xe) > dss(v, (uint16_t)(0x1992 + height * 2));
    } else {
        int16_t a = (int16_t)((uint16_t)((uint16_t)(v->step - 0x10) << 8)
                              - (uint16_t)vs(v, 0xa));
        int16_t b = (int16_t)((uint16_t)((uint16_t)v->row << 8)
                              - (uint16_t)vs(v, 0x12));
        int16_t c = (int16_t)(dsw(v, (uint16_t)(0x1992 + (height + dsb(v, hb)) * 2))
                              - (uint16_t)vs(v, 0xe));
        int16_t p1 = (int16_t)(a * dsc(v, (uint16_t)(corner * 3 + 0x5eb)));
        int16_t p2 = (int16_t)(b * dsc(v, (uint16_t)(corner * 3 + 0x5ed)));
        int16_t p3 = (int16_t)(c * dsc(v, (uint16_t)(corner * 3 + 0x5ec)));
        int16_t s12 = (int16_t)((uint16_t)p1 + (uint16_t)p2);
        visible = (int32_t)s12 + p3 < 0;
    }
    v->lod_budget = (int8_t)0xe0;
    if (visible) {
        quad[0] = vertex(v, (uint8_t)v->step, (uint8_t)(v->row + 1),
                         (uint8_t)(height + dsb(v, (uint16_t)(hb + 2))));
        quad[1] = vertex(v, (uint8_t)(v->step + 1), (uint8_t)(v->row + 1),
                         (uint8_t)(height + dsb(v, (uint16_t)(hb + 3))));
        quad[2] = vertex(v, (uint8_t)(v->step + 1), (uint8_t)v->row,
                         (uint8_t)(height + dsb(v, (uint16_t)(hb + 1))));
        quad[3] = vertex(v, (uint8_t)v->step, (uint8_t)v->row,
                         (uint8_t)(height + dsb(v, hb)));
        shape_fn(v, v->shape_ptr, quad, v->tile_type, (uint8_t)((t1 >> 2) & 0xf));
    }
    /* The ceiling, unless the eye is above 0x3f4. */
    if (!(vs(v, 0xe) > 0x3f4)) {
        quad[0] = vertex(v, (uint8_t)v->step, (uint8_t)v->row, 0x10);
        quad[1] = vertex(v, (uint8_t)(v->step + 1), (uint8_t)v->row, 0x10);
        quad[2] = vertex(v, (uint8_t)(v->step + 1), (uint8_t)(v->row + 1), 0x10);
        quad[3] = vertex(v, (uint8_t)v->step, (uint8_t)(v->row + 1), 0x10);
        shape_fn(v, v->quad_ptr, quad, v->tile_type, 9);
    }

    /* Three walls, for column bits 0x20, 0x10 and 0x08; column byte 1's
     * matching bit (0x40, 0x20, 0x10) says a neighbour stands there, whose
     * height and type choose the wall's top. */
    bit = 0x40;
    v->lod_budget = 0;
    for (wall = 0; wall < 3; wall++) {
        uint16_t prev = bit;                               /* [bp-0x12] */
        uint16_t di = (uint16_t)(0x5c1 + wall * 6);
        uint8_t top1, top2, vsize;
        bit = (uint16_t)((int16_t)bit >> 1);
        if (!(col & bit)) continue;
        if (prev & col1) {
            int16_t nstep = dss(v, (uint16_t)(0x444 + v->facing * 6
                                              + dsb(v, (uint16_t)(0x607 + wall)) * 2));
            long nidx = tile + nstep;
            uint8_t n0 = tile_byte(v, nidx, 0);
            uint8_t nh = (uint8_t)((n0 >> 4) & 0xf);
            uint8_t ntype = dsb(v, (uint16_t)(v->facing * 16 + (n0 & 0xf) + 0x464));
            uint8_t nt = (dsb(v, (uint16_t)(0x1d8a + ntype)) & 0x20) == 0x20
                       ? (uint8_t)(ntype + 0xfa) : 4;      /* [bp-0x1c] */
            uint8_t d = (uint8_t)(nh + dsb(v, (uint16_t)(wall * 5 + nt + 0x5b2)));
            d = (uint8_t)(d - height);
            vsize = (uint8_t)(d - dsb(v, (uint16_t)(wall * 5 + corner + 0x5a3)));
            top2 = (uint8_t)(nh + dsb(v, (uint16_t)(nt * 4
                                    + dsb(v, (uint16_t)(0x5a0 + wall)) + 0x589)));
            top1 = (uint8_t)(nh + dsb(v, (uint16_t)(nt * 4
                                    + dsb(v, (uint16_t)(0x59d + wall)) + 0x589)));
        } else {
            top1 = top2 = 0x10;
            vsize = (uint8_t)(0x10 - height);
            vsize = (uint8_t)(vsize - dsb(v, (uint16_t)(wall * 5 + corner + 0x5a3)));
        }
        quad[0] = vertex(v, (uint8_t)(v->step + dsb(v, di)),
                         (uint8_t)(v->row + dsb(v, (uint16_t)(di + 1))), top1);
        quad[1] = vertex(v, (uint8_t)(v->step + dsb(v, (uint16_t)(di + 3))),
                         (uint8_t)(v->row + dsb(v, (uint16_t)(di + 4))), top2);
        quad[2] = vertex(v, (uint8_t)(v->step + dsb(v, (uint16_t)(di + 3))),
                         (uint8_t)(v->row + dsb(v, (uint16_t)(di + 4))),
                         (uint8_t)(height + dsb(v, (uint16_t)(hb + dsb(v, (uint16_t)(di + 5))))));
        quad[3] = vertex(v, (uint8_t)(v->step + dsb(v, di)),
                         (uint8_t)(v->row + dsb(v, (uint16_t)(di + 1))),
                         (uint8_t)(height + dsb(v, (uint16_t)(hb + dsb(v, (uint16_t)(di + 2))))));
        face_fn(v, v->face_ptr, quad, v->tile_type, vsize, (uint8_t)(t2 & 0x3f));
    }

    /* The diagonal wall of a diagonal tile, when the eye is on its face's
     * side. */
    if ((col & 0x44) == 0x44) {
        uint16_t e = (uint16_t)((col & 3) * 6 + 0x5d3);     /* [bp-0xe] */
        int16_t a = (int16_t)((uint16_t)((uint16_t)(v->step + dsc(v, e) - 0x10) << 8)
                              - (uint16_t)vs(v, 0xa));
        int16_t b = (int16_t)((uint16_t)((uint16_t)(v->row + dsc(v, (uint16_t)(e + 1))) << 8)
                              - (uint16_t)vs(v, 0x12));
        int16_t p1 = (int16_t)(a * dsc(v, (uint16_t)(e + 4)));
        int16_t p2 = (int16_t)(b * dsc(v, (uint16_t)(e + 5)));
        if ((int32_t)p1 + p2 < 0) {
            quad[0] = vertex(v, (uint8_t)(v->step + dsb(v, e)),
                             (uint8_t)(v->row + dsb(v, (uint16_t)(e + 1))), 0x10);
            quad[1] = vertex(v, (uint8_t)(v->step + dsb(v, (uint16_t)(e + 2))),
                             (uint8_t)(v->row + dsb(v, (uint16_t)(e + 3))), 0x10);
            quad[2] = vertex(v, (uint8_t)(v->step + dsb(v, (uint16_t)(e + 2))),
                             (uint8_t)(v->row + dsb(v, (uint16_t)(e + 3))), height);
            quad[3] = vertex(v, (uint8_t)(v->step + dsb(v, e)),
                             (uint8_t)(v->row + dsb(v, (uint16_t)(e + 1))), height);
            face_fn(v, v->face_ptr, quad, v->tile_type, (uint8_t)(0x10 - height),
                    (uint8_t)(t2 & 0x3f));
        }
    }

    /* drawlist_tile_objects from the tile's word at +2, whose
     * upper ten bits start the chain; what the emitters drew goes into the
     * automap byte's top two bits. */
    if (v->level)
        tile_objects(v, (uint16_t)(v->tiles_origin + tile * 4 + 2));
    else if ((tile_byte(v, tile, 2) | (tile_byte(v, tile, 3) << 8)) >> 6)
        v->object_tiles++;
    if (dsb(v, 0x54f)) {
        if (v->tile_type < 8) code = (uint8_t)(code | (dsb(v, 0x54f) << 6));
        v->mem[0x54f] = 0;
    }
    if (dsb(v, 0x546)) *am = code;
}

/* automap_view_sweep. */
static void sweep(uw_vl *v) {
    int16_t di = dss(v, (uint16_t)(0x444 + v->facing * 6));
    int16_t rs = dss(v, (uint16_t)(0x446 + v->facing * 6));  /* [bp-0xc] */
    uint16_t cursor0 = (uint16_t)(0x2890 + dss(v, 0x2cf2) * 0x42);   /* [bp-0x2] */
    int16_t last = dss(v, 0x2cf2);
    long tile0 = ((long)(uint16_t)dsw(v, 0x2cfa) - v->tiles_origin) / 4;
    long rowtile;                                              /* [bp-0x6] */
    int16_t am0;                                               /* [bp-0x8] */

    v->lod_budget = (int8_t)0xe0;
    v->facing_table = (uint16_t)(0x579 + v->facing * 4);
    /* tilemap_origin_ptr and automap_tile_end_ptr:
     * tile (0,0) and tile (63,63) in view_tilemap_seg -- what the pick
     * (view_pick_object) turns drawlist_object_x back into a tile with */
    setw(v, 0x3120, (uint16_t)v->tiles_origin);
    setw(v, 0x3122, dsw(v, 0x2cfc));
    setw(v, 0x3128, (uint16_t)(v->tiles_origin + 0x3fff * 4));
    setw(v, 0x312a, dsw(v, 0x2cfc));
    rowtile = tile0 + (long)(int16_t)(last * rs) - (long)di * 16;
    /* `rt_ldiv(ptr - origin, 4)` on the far pointer, then 0xc000 added past
     * 0x2000: the 16-bit pointer arithmetic above can wrap below the map's
     * first tile, and this puts the index back below zero, where the
     * `& 0xf000` test keeps it out. As a signed index it simply is. */
    am0 = (int16_t)rowtile;
    list_op(v, -10);
    for (v->row = last; v->row >= 0; v->row--) {
        uint16_t am, cur;
        list_op(v, 2);
        for (v->step = 0; v->step < 0x10; v->step++) {
            cur = (uint16_t)(cursor0 + v->step * 2);
            v->tile = rowtile + (long)v->step * di;
            am = (uint16_t)(am0 + v->step * di);
            if (!(am & 0xf000)) mark_tile(v, cur, v->tile, am);
        }
        list_op(v, 1);
        for (v->step = 0x20; v->step > 0x10; v->step--) {
            cur = (uint16_t)(cursor0 + v->step * 2);
            v->tile = rowtile + (long)v->step * di;
            am = (uint16_t)(am0 + v->step * di);
            if (!(am & 0xf000)) mark_tile(v, cur, v->tile, am);
        }
        list_op(v, 0);
        cur = (uint16_t)(cursor0 + v->step * 2);
        v->tile = rowtile + (long)v->step * di;
        am = (uint16_t)(am0 + v->step * di);
        if (!(am & 0xf000)) mark_tile(v, cur, v->tile, am);
        emit(v, 0xb0);
        cursor0 = (uint16_t)(cursor0 - 0x42);
        rowtile -= rs;
        am0 = (int16_t)(am0 - rs);
    }
    /* The near row's unseen automap marks. */
    for (v->step = 0; v->step < 0x21; v->step++) {
        uint16_t am = (uint16_t)(am0 + v->step * di);
        long t = rowtile + (long)v->step * di;
        if (!(am & 0xf000) && v->automap[am] == 0)
            v->automap[am] = dsb(v, (uint16_t)(0x5f7 + (tile_byte(v, t, 0) & 0xf)));
    }
}

uint16_t uw_vl_build(uw_vl *v) {
    uint16_t detail;
    v->list_overflow = 0;
    memcpy(v->mem, v->ds, sizeof v->mem);
    if (v->spans && v->level) {
        if (v->spans >= 2) {
            memcpy(v->camera, v->vstate, sizeof v->camera);
            if (uw_vl_setup_frame(v, v->camera)) v->vstate = v->camera;
            else v->unsupported++;
        }
        uw_vl_light_map(v, dss(v, 0x735a));
        uw_vl_spans(v);
    }
    detail = v->pick ? 0 : dsw(v, 0x54b);
    if (v->pick) {
        v->mem[0x311e] = 1;                 /* drawlist_object_count */
    } else {
        v->mem[0x2e1c] = 0;                 /* drawlist_object_x */
        v->mem[0x311e] = 0;
    }
    v->ptr = UW_VL_LIST0;
    v->facing = (int)(int8_t)dsb(v, 0x2cf4);
    v->shape_ptr = dsw(v, (uint16_t)(0x551 + detail * 4));
    v->quad_ptr = dsw(v, (uint16_t)(0x559 + detail * 4));
    v->face_ptr = dsw(v, (uint16_t)(0x561 + detail * 4));

    /* view_build_draw_list: the header. */
    v->no_view_offset = (vs(v, 0x28) == 0 && vs(v, 0x2a) == 0);
    emit(v, 0x38);
    v->here_pos = v->ptr;                   /* drawlist_emit_label_ref(0xa0, 1) */
    emit(v, 0);
    emit(v, 0); emit(v, 0x2200); emit(v, 0); emit(v, 0); emit(v, 0x400);
    emit(v, 0); emit(v, 0); emit(v, 0x1100); emit(v, 0); emit(v, 0);
    emit(v, 0x3300);
    emit(v, 2); emit(v, 0x2920 + 9 * 2); emit(v, (uint16_t)(v->pick != 0));
    emit(v, 2); emit(v, 0x2920 + 8 * 2); emit(v, (uint16_t)(v->pick || detail == 0));
    emit(v, 2); emit(v, 0x2920 + 4 * 2);
    v->light_scale = dsw(v, 0x54d);
    if (v->pick) {                         /* drawlist_begin_frame: unlit */
        emit(v, 0);
    } else if (dsw(v, 0x7278) == 9) {      /* current_level: the sweep is unlit */
        emit(v, 0);
        v->light_scale = 0;
    } else {
        emit(v, (uint16_t)(int16_t)(int8_t)dsb(v, 0x550));
    }
    emit(v, 0xd0);
    emit(v, (uint16_t)v->no_view_offset);
    sweep(v);

    /* drawlist_define_label(0xa0): ((here - there) - arg) * 2 in words. */
    {
        uint16_t w = (uint16_t)(((v->ptr - v->here_pos) / 2 - 1) * 2);
        v->list[v->here_pos] = (uint8_t)w;
        v->list[(uint16_t)(v->here_pos + 1)] = (uint8_t)(w >> 8);
    }
    emit(v, 0);
    return v->ptr;
}
