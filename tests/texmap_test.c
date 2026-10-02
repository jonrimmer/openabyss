/* SPDX-License-Identifier: MIT */
#include "uw_texmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
    exit(1); } } while (0)

static void check_adjacent_sections(void) {
    uint8_t pixels[16 * 16], texels[64 * 64];
    uint8_t light[16 * 256], shade[5] = {5, 5, 5, 5, 5};
    uint32_t rows[16];
    uw_fb fb = {pixels, sizeof pixels, rows, 16};
    uw_rast_texrec tex = {64, 4032, 0, 4032};
    /* One section has a single long edge; its neighbour has a projected,
     * rounded subdivision vertex on the same edge (a T junction). */
    uw_rast_svert left[4] = {
        {0, 2, 0, 0, 0, 0, 100}, {2, 2, 0, 0, 0, 0, 100},
        {9, 14, 0, 0, 0, 0, 400}, {0, 14, 0, 0, 0, 0, 400}
    };
    uw_rast_svert right[5] = {
        {2, 2, 0, 0, 0, 0, 100}, {15, 2, 0, 0, 0, 0, 100},
        {15, 14, 0, 0, 0, 0, 400}, {9, 14, 0, 0, 0, 0, 400},
        {6, 8, 0, 0, 0, 0, 160}
    };
    int pass, i, x, y;
    for (i = 0; i < 16; i++) rows[i] = (uint32_t)i * 16;
    for (i = 0; i < 4096; i++) light[i] = (uint8_t)((i & 255) / 2);
    for (pass = 0; pass < 4; pass++) {
        memset(pixels, 255, sizeof pixels);
        memset(texels, 42, sizeof texels);
        uw_gfx_texture_poly_perspective(&fb, left, 4, &tex, texels, sizeof texels);
        memset(texels, 43, sizeof texels);
        uw_gfx_texture_poly_perspective(&fb, right, 5, &tex, texels, sizeof texels);
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++) {
                int inside = pass % 2 ? x >= 2 && x <= 14 : y >= 2 && y <= 14;
                CHECK((pixels[y * 16 + x] != 255) == inside);
            }
        memset(pixels, 255, sizeof pixels);
        memset(texels, 42, sizeof texels);
        uw_gfx_texture_poly_perspective_lit(&fb, left, 4, &tex, texels,
                                            sizeof texels, shade, light, NULL);
        memset(texels, 43, sizeof texels);
        uw_gfx_texture_poly_perspective_lit(&fb, right, 5, &tex, texels,
                                            sizeof texels, shade, light, NULL);
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++) {
                int inside = pass % 2 ? x >= 2 && x <= 14 : y >= 2 && y <= 14;
                CHECK(inside ? pixels[y * 16 + x] == 21 || pixels[y * 16 + x] == 22
                             : pixels[y * 16 + x] == 255);
            }
        /* Exercise the same junction for floors as well as walls, and
         * independently projected midpoints rounded in either direction. */
        for (i = 0; i < 4; i++) {
            int16_t t = left[i].sx; left[i].sx = left[i].sy; left[i].sy = t;
        }
        for (i = 0; i < 5; i++) {
            int16_t t = right[i].sx; right[i].sx = right[i].sy; right[i].sy = t;
        }
        if (pass == 1) right[4].sx = 5;
    }
}

static void check_rounded_border_texels(void) {
    uint8_t pixels[16 * 16], texels[64 * 64];
    uint32_t rows[16];
    uw_fb fb = {pixels, sizeof pixels, rows, 16};
    uw_rast_texrec tex = {64, 4032, 0, 4032};
    uw_rast_svert face[4] = {
        {2, 2, 0, 0, 0, 0, 100}, {10, 2, 16128, 0, 0, 0, 100},
        {12, 10, 16128, 4032, 0, 0, 400}, {4, 10, 0, 4032, 0, 0, 400}
    };
    int i;
    for (i = 0; i < 16; i++) rows[i] = (uint32_t)i * 16;
    for (i = 0; i < 4096; i++) texels[i] = (uint8_t)(i % 64);
    memset(pixels, 255, sizeof pixels);
    uw_gfx_texture_poly_perspective(&fb, face, 4, &tex, texels, sizeof texels);
    /* Rounded spans reach past x = 2.25 and x = 10.75 respectively.
     * They must sample the border, not extrapolate into a wrapped texel. */
    CHECK(pixels[3 * 16 + 2] == 0);
    CHECK(pixels[5 * 16 + 11] == 63);
    CHECK(pixels[3 * 16 + 1] == 255);
    CHECK(pixels[5 * 16 + 12] == 255);
}

static void check_lit_moving_edges(void) {
    uint8_t pixels[16 * 16 + 16], coverage[sizeof pixels];
    uint8_t texels[64 * 64], light[16 * 256], shade[4] = {5, 5, 5, 5};
    uint32_t rows[16];
    uw_fb fb = {pixels, sizeof pixels, rows, 16};
    uw_rast_texrec tex = {64, 4032, 0, 4032};
    uw_rast_svert face[4] = {
        {2, 2, 0, 0, 0, 0, 100}, {8, 2, 16128, 0, 0, 0, 100},
        {8, 8, 16128, 4032, 0, 0, 400}, {2, 8, 0, 4032, 0, 0, 400}
    };
    int i, dx, dy, transpose;
    long overrun = 0;
    for (i = 0; i < 16; i++) rows[i] = (uint32_t)i * 16;
    /* Reapplying lighting halves the colour again: this detects both
     * missed lighting (200) and lighting applied twice (50). */
    for (i = 0; i < 4096; i++) light[i] = (uint8_t)((i & 255) / 2);
    memset(texels, 200, sizeof texels);
    for (dx = -2; dx <= 5; dx++) for (dy = 2; dy <= 12; dy++) {
        face[2].sx = (int16_t)(8 + dx); face[3].sx = (int16_t)(2 + dx);
        face[2].sy = face[3].sy = (int16_t)(2 + dy);
        for (transpose = 0; transpose < 2; transpose++) {
            memset(pixels, 77, sizeof pixels);
            uw_gfx_texture_poly_perspective(&fb, face, 4, &tex, texels, sizeof texels);
            memcpy(coverage, pixels, sizeof pixels);
            memset(pixels, 77, sizeof pixels);
            uw_gfx_texture_poly_perspective_lit(&fb, face, 4, &tex, texels,
                                                sizeof texels, shade, light, &overrun);
            for (i = 0; i < (int)sizeof pixels; i++)
                CHECK(pixels[i] == (coverage[i] == 200 ? 100 : 77));
            /* Turn wall edges into floor edges for the next pass. */
            for (i = 0; i < 4; i++) {
                int16_t t = face[i].sx; face[i].sx = face[i].sy; face[i].sy = t;
            }
        }
    }
    CHECK(overrun == 0);

    /* Varying vertex lighting includes both rounded borders, and never
     * touches the neighbouring face's colour. */
    face[0].sx = 2; face[1].sx = 10; face[2].sx = 12; face[3].sx = 4;
    face[2].sy = face[3].sy = 10;
    shade[0] = shade[3] = 0; shade[1] = shade[2] = 15;
    for (i = 0; i < 4096; i++) light[i] = (uint8_t)(100 + i / 256);
    memset(pixels, 77, sizeof pixels);
    uw_gfx_texture_poly_perspective_lit(&fb, face, 4, &tex, texels,
                                        sizeof texels, shade, light, &overrun);
    CHECK(pixels[3 * 16 + 2] == 100);
    CHECK(pixels[5 * 16 + 11] == 115);
    CHECK(pixels[3 * 16 + 1] == 77);
    CHECK(pixels[5 * 16 + 12] == 77);
    CHECK(overrun == 0);
}

int main(void) {
    uint8_t pixels[16 * 16 + 16], texels[64 * 64];
    uint32_t rows[16];
    uw_fb fb = {pixels, sizeof pixels, rows, 16};
    uw_rast_texrec tex = {64, 4032, 0, 4032};
    uw_rast_svert quad[4] = {
        {2, 2, 0, 0, 0, 0, 100},
        {10, 2, 16128, 0, 0, 0, 400},
        {10, 10, 16128, 4032, 0, 0, 400},
        {2, 10, 0, 4032, 0, 0, 100}
    }, reversed[4];
    uint8_t forward[sizeof pixels];
    int i;
    for (i = 0; i < 16; i++) rows[i] = (uint32_t)i * 16;
    for (i = 0; i < 4096; i++) texels[i] = (uint8_t)(i % 64);
    memset(pixels, 255, sizeof pixels);
    uw_gfx_texture_poly_perspective(&fb, quad, 4, &tex, texels, sizeof texels);
    /* Halfway across: (0/100 + 63/400) / (1/100 + 1/400) = 12.6. */
    CHECK(pixels[6 * 16 + 6] == 12);
    CHECK(pixels[6 * 16 + 2] == 0);
    CHECK(pixels[6 * 16 + 10] == 63);
    CHECK(pixels[0] == 255);
    memcpy(forward, pixels, sizeof pixels);
    for (i = 0; i < 4; i++) reversed[i] = quad[3 - i];
    memset(pixels, 255, sizeof pixels);
    uw_gfx_texture_poly_perspective(&fb, reversed, 4, &tex, texels, sizeof texels);
    CHECK(memcmp(forward, pixels, sizeof pixels) == 0);

    /* Equal depth must give linear texture coordinates. */
    for (i = 0; i < 4; i++) quad[i].z = 100;
    uw_gfx_texture_poly_perspective(&fb, quad, 4, &tex, texels, sizeof texels);
    CHECK(pixels[6 * 16 + 6] == 31);

    /* Correct v too, with depth changing vertically as on a floor. */
    for (i = 0; i < 4096; i++) texels[i] = (uint8_t)(i / 64);
    quad[2].z = quad[3].z = 400;
    uw_gfx_texture_poly_perspective(&fb, quad, 4, &tex, texels, sizeof texels);
    CHECK(pixels[6 * 16 + 6] == 12);
    CHECK(pixels[2 * 16 + 6] == 0);
    CHECK(pixels[10 * 16 + 6] == 63);

    /* Faces extending past every edge cannot spill into other rows or
     * the unused tail of a framebuffer allocation. */
    for (i = 0; i < 4; i++) {
        quad[i].sx = (int16_t)(quad[i].sx == 2 ? -8 : 24);
        quad[i].sy = (int16_t)(quad[i].sy == 2 ? -8 : 24);
        quad[i].z = 100;
    }
    memset(texels, 42, sizeof texels);
    memset(pixels, 255, sizeof pixels);
    uw_gfx_texture_poly_perspective(&fb, quad, 4, &tex, texels, sizeof texels);
    for (i = 0; i < 256; i++) CHECK(pixels[i] == 42);
    for (i = 256; i < (int)sizeof pixels; i++) CHECK(pixels[i] == 255);
    memcpy(forward, pixels, sizeof pixels);
    quad[0].z = 0;
    uw_gfx_texture_poly_perspective(&fb, quad, 4, &tex, texels, sizeof texels);
    CHECK(memcmp(forward, pixels, sizeof pixels) == 0);
    quad[0].z = 100;
    for (i = 0; i < 4; i++) quad[i].sy = 6;
    uw_gfx_texture_poly_perspective(&fb, quad, 4, &tex, texels, sizeof texels);
    CHECK(memcmp(forward, pixels, sizeof pixels) == 0);
    check_adjacent_sections();
    check_rounded_border_texels();
    check_lit_moving_edges();
    puts("Perspective texture mapping checks passed");
    return 0;
}
