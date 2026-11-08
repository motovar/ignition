/*
 * gfx.c - Lisa 2 2D draw target.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lisa/gfx.h"

struct gfx_target g_gfx_target;
u8 *g_gfx_color_table;      /* 0x63f2d8 */

/* 0x456f60 */
static int gfx_set_target(u8 *base, int pitch, int width, int height, int bpp)
{
    struct gfx_target *t = &g_gfx_target;

    t->base = base;
    t->pitch = pitch;
    t->width = width;
    t->height = height;
    t->bpp = bpp;
    t->clip_x0 = 0;
    t->clip_y0 = 0;
    t->clip_x1 = width;
    t->clip_y1 = height;
    return 1;
}

/* 0x456cc0 */
int set_target(u8 *base, int pitch, int width, int height, int bpp)
{
    return gfx_set_target(base, pitch, width, height, bpp);
}

/* 0x457020 */
int gfx_set_clip(int x0, int y0, int x1, int y1)
{
    struct gfx_target *t = &g_gfx_target;

    if (x0 < 0 || y0 < 0 || x1 <= x0 || y1 <= y0
        || x1 > t->width || y1 > t->height) {
        return 0;
    }
    t->clip_x0 = x0;
    t->clip_y0 = y0;
    t->clip_x1 = x1;
    t->clip_y1 = y1;
    return 1;
}

/* The shade of the layered 3D, see gfx.h. */
u8 *g_gfx_shade;
int g_gfx_shade_w, g_gfx_shade_h, g_gfx_shade_ox, g_gfx_shade_used;
double g_gfx_shade_ox_f;
const u8 *g_gfx_shade_base;
const u8 *g_gfx_shade_table;
static u8 *s_shade_mem;
static size_t s_shade_cap;

void gfx_shade_begin(int w, int h, double oxf, const u8 *base, const u8 *table)
{
    size_t n;
    int ox = oxf > 0 ? (int)ceil(oxf) : 0;

    if (oxf < 0) {
        oxf = 0;
    }
    n = (size_t)(w + 2 * ox) * (size_t)h * 2;
    if (n > s_shade_cap) {
        free(s_shade_mem);
        s_shade_mem = malloc(n);
        s_shade_cap = s_shade_mem != NULL ? n : 0;
    }
    if (s_shade_mem == NULL) {
        g_gfx_shade = NULL;
        return;
    }
    memset(s_shade_mem, 0, n);
    g_gfx_shade = s_shade_mem;
    g_gfx_shade_w = w + 2 * ox;
    g_gfx_shade_h = h;
    g_gfx_shade_ox = ox;
    g_gfx_shade_ox_f = oxf;
    g_gfx_shade_base = base;
    g_gfx_shade_table = table;
    g_gfx_shade_used = 0;
}

void gfx_shade_end(void)
{
    g_gfx_shade = NULL;
    g_gfx_shade_table = NULL;
    g_gfx_shade_used = 0;
}

void gfx_shade_set(int x, int y, int level, int second)
{
    x += g_gfx_shade_ox;
    if (g_gfx_shade == NULL || level <= 0 || level > 255 || y < 0 || y >= g_gfx_shade_h
        || x < 0 || x >= g_gfx_shade_w) {
        return;
    }
    g_gfx_shade[((size_t)y * g_gfx_shade_w + x) * 2 + (second ? 1 : 0)] = (u8)level;
    g_gfx_shade_used = 1;
}
