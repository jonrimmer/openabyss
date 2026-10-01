/* SPDX-License-Identifier: MIT */
/* The rasteriser's frame setup: what rast_execute derives before it runs a
 * draw list.
 *
 *   1. the projection -- scale and centre -- from the
 *      clip rectangle view_present sets around the list, (0, height - 1,
 *      width - 1, 0);
 *   2. rast_camera_lerp, which is first a CAMERA PROGRAM: the
 *      block at ds:[0x104] starts with a word indexing the handler table
 *      after it, and the handler reads the rest of the block with `lodsw`.
 *      Handler 0, rast_cam_from_block, is the one every state
 *      runs: a depth scale, a pointer to the view-state block in the graphics
 *      module's private segment, a flag, an eye offset and the program's own
 *      matrix. The block's matrix is rebuilt from its three angles
 *      (rast_matrix_from_angles), the camera is the block's 32-bit position
 *      plus that matrix times the eye offset (rast_mat_x_vec), and the basis
 *      is the block's matrix times the program's (rast_basis_set_rows);
 *   3. rast_patch_depth_axis: which basis column is depth, for
 *      the self-modified cull test in set_origin_or_skip and call_block;
 *   4. rast_basis_apply_projection: the aspect and depth scales
 *      folded into the basis, the frustum words and the horizon terms.
 *
 * Every 16.16 product here is the original's `imul; shl ax,1; rcl dx,1` --
 * the high word of the doubled product -- and every matrix product saturates
 * as rast_mat_x_vec does, including the slip in its third row, whose
 * overflow saturates the SECOND result.
 *
 * Offsets are the rasteriser's data segment unless marked SS, the
 * private segment the list runs on. */
#ifndef UW_RASTFRAME_H
#define UW_RASTFRAME_H

#include "uw.h"

typedef struct {
    /* ---- inputs, as the frame found them; the setup writes into both ---- */
    uint8_t  rast[0x10000];     /* the data segment: the camera program, the aspect word */
    uint8_t  priv[0x10000];     /* the private segment (SS): the view-state block */
    int16_t  view_width, view_height;
    int      output_width, output_height; /* host aspect override; zero: original */

    /* ---- what it found ------------------------------------------------ */
    int      handler;           /* the camera block's first word */
    int      depth_axis;        /* rast_patch_depth_axis: 0, 2 or 4 */
    uint8_t  depth_op;          /* 0x2b (sub) or 0x03 (add) */
    long     unsupported;       /* a handler or a camera move not carried */
    long     saturated;         /* divides the INT 0 handler answered */
} uw_rast_frame;

/* Run the setup over f->rast and f->priv. Returns 0 when the camera block's
 * handler is not carried (the basis and camera are then left as they were). */
int uw_rast_frame_setup(uw_rast_frame *f);

#endif
