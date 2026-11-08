/*
 * gfx.h - Lisa 2 2D draw target: the 8 bit buffer that text and sprites
 * are drawn into, its clip rectangle, and the colour table that combines
 * a source pixel with the pixel under it.
 *
 * The original keeps the target twice, for the generic driver
 * (0x4ba6f8, 0x50ebf0..0x50ed38) and for the rasteriser (0x63f2b0..
 * 0x63f2d4), with the clip also in 24.8 fixed point. One copy serves
 * here.
 */

#ifndef IGN_LISA_GFX_H
#define IGN_LISA_GFX_H

#include "types.h"

struct gfx_target {
    u8 *base;       /* 0x4ba6f8, 0x63f2d4 */
    s32 pitch;      /* 0x50ebf4, 0x63f2c8 */
    s32 width;      /* 0x50ed2c */
    s32 height;     /* 0x50ed38 */
    s32 bpp;        /* 0x50ed34 */
    s32 clip_x0;    /* 0x50ed20, 0x63f2c0 */
    s32 clip_y0;    /* 0x50ed24, 0x63f2b8 */
    s32 clip_x1;    /* 0x50ed28, 0x63f2bc, exclusive */
    s32 clip_y1;    /* 0x50ed30, 0x63f2b4, exclusive */
};

extern struct gfx_target g_gfx_target;

/* 256 x 256 bytes: the result of drawing source colour s over
 * destination colour d is table[s << 8 | d]. */
extern u8 *g_gfx_color_table;   /* 0x63f2d8 */

/* 0x456cc0, through the table entry 0x50ebb0. Sets the target and a
 * clip rectangle covering all of it. Returns 1. */
int set_target(u8 *base, int pitch, int width, int height, int bpp);

/* 0x457020, through 0x50ebb4. Returns 0, leaving the clip alone, when
 * the rectangle is empty or not inside the target. */
int gfx_set_clip(int x0, int y0, int x1, int y1);

/* The port's 3D that is drawn apart, a layer (video_set_layer), has the
 * HUD in a buffer that is clear (0) where the 3D shows. The original
 * draws it over the 3D in the same buffer, and the colour table (the
 * shade, table[level << 8 | pixel]) makes of the pixel under it
 * table[level << 8 | pixel]. Here that pixel is the 3D's, which is only
 * on the GPU: g_gfx_shade keeps the levels instead, two bytes for each
 * pixel of the layer (the HUD's shade, then the iris's), and the
 * layer's draw puts the 3D's index through those rows of the table.
 * It is g_gfx_shade_w wide, g_gfx_shade_ox columns of it before the
 * target's first and as many after its last. NULL when there is none. */
extern u8 *g_gfx_shade;
extern int g_gfx_shade_w, g_gfx_shade_h, g_gfx_shade_ox, g_gfx_shade_used;
extern double g_gfx_shade_ox_f;      /* the columns the 3D has past the screen, exactly */
extern const u8 *g_gfx_shade_base;    /* the target it is for */
extern const u8 *g_gfx_shade_table;   /* the shade table */

void gfx_shade_begin(int w, int h, double ox, const u8 *base, const u8 *table);
void gfx_shade_end(void);
/* The row `level` of the table over x, y of the target (x may be outside
 * it by ox). `second` is the iris's. */
void gfx_shade_set(int x, int y, int level, int second);

#endif /* IGN_LISA_GFX_H */
