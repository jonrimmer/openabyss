/* SPDX-License-Identifier: MIT */
/* The draw list's PRODUCER: view_build_draw_list, its
 * automap_view_sweep and the per-tile automap_mark_tile
 * with the detail slider's emitters.
 *
 * BEFORE THE LIST (src/uw_viewspan.c): view_place_camera and
 * view_setup_frame put the eye at the player's position and turn it into the
 * facing quadrant's frame; view_build_light_map, when the shading changes,
 * rings the eye with a light by distance; and view_setup_frustum with
 * view_traverse_spans walks two edges out from the eye, row by row, marking
 * which cells of view_column_array show which faces. So a list comes from the
 * level block, the player's position, the shades record and 6aac's tables.
 *
 * WHAT A FRAME'S LIST IS. A header -- 0x38 with a forward reference to label
 * 0xa0 (the list's end) and the world's extents, three stores into the list
 * header words 9, 8 and 4, and select_shader -- then the SWEEP: the rows of
 * the 33-tile-wide grid view_traverse_spans marked, from the far row in, each
 * row walked from its left end to the middle, from its right end back to the
 * middle, then the middle tile, with an automap_mark (0xb0) after every row.
 * A tile the column array marks visible (bit 7) emits its floor, its ceiling
 * unless the eye is too high, up to three walls against its neighbours and
 * a diagonal, each as four vertices (0xb6, numbered from 200 per tile) and a
 * face through one of three emitter pointers the detail slider chose; then
 * its objects. A tile it does not draw still drains what an earlier tile
 * deferred to it. The caller defines label 0xa0 and writes the terminator.
 *
 * THE OBJECT PASS. Each
 * visible tile walks its object chain into a list of up to sixty, turns each
 * object's position into a sub-tile cell and a depth key for the half of the
 * row the sweep is in, and sorts the list far to near -- around a pivot when
 * a bridge or a door frame stands there, so what is behind it draws first.
 * An object whose radius (the property table's low three bits) reaches into
 * a tile the sweep has not reached yet is DEFERRED: into the same column of
 * the next row, the next tile of this half, or a carry list for both, and
 * drawn from there -- which is how a long object is not overdrawn by the
 * floor of the tile it overhangs. Then drawlist_emit_object: a sprite (0x3a),
 * an animated creature sprite (0x5a), a model with its store-immediates,
 * texture and origin (0x18 around a call through label 0x60 + model), a
 * model group (doors, portcullises: frame, panel, the pair drawn in the order
 * the eye needs), or a wall-textured one.
 *
 * THE LOW DETAIL SETTINGS draw far floors, ceilings and walls as flat faces
 * (drawlist_emit_shape_plain, drawlist_emit_shaded_quad and
 * drawlist_emit_textured_face): one colour, the texture's first texel shaded
 * through the shade table by the cell's light, and the four corners.
 *
 * WHAT THIS DOES NOT CARRY: a reference to a shape label the list header
 * never defined, and an art slot the frame did not load. Both are COUNTED,
 * and a list compared against the original's will part from it at the first
 * one.
 *
 * Addresses are data-segment offsets and list addresses. */
#ifndef UW_VIEWLIST_H
#define UW_VIEWLIST_H

#include "uw.h"

#define UW_VL_LIST0 0x73f2         /* where every captured live list begins */
#define UW_VL_ART   116            /* art slots gr_map_art_page indexes */

typedef struct {
    /* ---- what the producer reads ------------------------------------- */
    const uint8_t *ds;             /* 6aac, 64K: the tables and view words */
    const uint8_t *rast;           /* 5723, 64K: the word at 0xb000 */
    const uint8_t *tiles;          /* the level's 64 * 64 four-byte tiles */
    uint16_t       tiles_origin;   /* their far offset */
    const uint8_t *vstate;         /* the view_state record, 0x40 bytes */
    const uint8_t *level;          /* the level block's 64K segment: the tiles
                                    * at tiles_origin and both object arrays,
                                    * addressed by the data segment's two
                                    * array pointers; NULL skips the object
                                    * pass */
    uint16_t       clock;          /* the tick counter's low word */
    const uint8_t *lprm;           /* the distance-light ramp */
    const uint8_t *art[UW_VL_ART]; /* each art slot's pixels, NULL if unloaded */
    const uint8_t *shade;          /* the 16 x 256 shade table */
    uint16_t       frustum_half_angle; /* host override; zero: original 0x2040 */
    int            spans;          /* compute view_column_array -- the light
                                    * map from the shades radius, then
                                    * uw_vl_spans as view_render does --
                                    * rather than take it from ds; 2 also
                                    * places the camera first, into camera[],
                                    * and reads the view state from there */
    uint8_t        camera[0x40];   /* the view state spans == 2 computes */
    int            pick;           /* build the pick map's list instead, as
                                    * view_refresh_pick_map does through
                                    * drawlist_begin_frame: the
                                    * object counter from 1, so objects are
                                    * painted their ids (opcode 0xae) and
                                    * faces their surface codes; list flags 9
                                    * and 8 set and 4, the light, clear; the
                                    * detail-0 emitters whatever the setting */

    /* ---- what it writes ----------------------------------------------- */
    uint8_t        list[0x10000];  /* addressed as 5723 */
    uint16_t       ptr;            /* draw_list_ptr */
    uint8_t        automap[0x1000];/* automap_tiles, marked */
    long           newly_seen;     /* automap_newly_seen */

    /* ---- the data segment as the build leaves it ---------------------- */
    uint8_t        mem[0x10000];   /* ds, copied: the deferral lists, cells,
                                    * sort order and pick tables live here */

    /* ---- the sweep's working state (the listing's globals) ------------ */
    int            step, row;      /* view_sweep_step, view_sweep_row */
    long           tile;           /* the sweep's current tile, by index */
    int            vertex_count;
    uint8_t        tile_type;      /* the column byte's low nibble */
    int8_t         lod_budget;
    uint16_t       lod_shade, lod_scale, lod_step, lod_mask;
    uint16_t       here_pos;       /* label 0xa0's placeholder */
    int            facing;         /* view_facing */
    uint16_t       facing_table;   /* drawlist_facing_table_ptr */
    uint16_t       shape_ptr, quad_ptr, face_ptr;   /* the three emitter pointers */
    int            no_view_offset;
    uint16_t       light_scale;    /* zeroed for level 9's sweep */

    /* ---- what it did ---------------------------------------------------- */
    long           objects;        /* drawlist_emit_object calls */
    long           deferred;       /* objects put on a deferral list */

    /* ---- what it did not do ------------------------------------------- */
    long           object_tiles;   /* tiles whose chain was skipped (no level) */
    long           unsupported;    /* an emitter or label this does not carry */
} uw_vl;

/* view_setup_frustum and view_traverse_spans over
 * v->mem: the edge records, view_column_array's faces bytes and
 * byte 1's neighbour bits (its light nibble, which view_build_light_map
 * wrote, is read and kept), and the last row. src/uw_viewspan.c. */
void uw_vl_spans(uw_vl *v);

/* view_place_camera and view_setup_frame: the view
 * state record `vs` (0x40 bytes, its other fields kept) from the player's
 * position globals in v->mem, and the eye's tile pointer, facing and
 * quadrant table there. Returns 0 for a camera following an object other
 * than the tracked one, which is not carried. */
int uw_vl_setup_frame(uw_vl *v, uint8_t *vs);

/* view_build_light_map over v->mem: byte 1 of every cell of
 * view_column_array, from v->lprm and a radius (shades.dat's). */
void uw_vl_light_map(uw_vl *v, int radius);
/* The same over any data segment `mem`, from the six-byte ramp. */
void uw_light_map_build(uint8_t *mem, int radius, const uint8_t *lprm);

/* view_build_draw_list, then drawlist_define_label(0xa0) and the terminating
 * word, as view_rebuild_and_draw writes them. Returns the address one past
 * the last word written. The caller fills the inputs and `automap` first. */
uint16_t uw_vl_build(uw_vl *v);

#endif
