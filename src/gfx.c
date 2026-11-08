/*
 * gfx.c - the screen: colour indices through the palette (rnd.h).
 */

#include <string.h>

#include <stdlib.h>

#include "gfx.h"
#include "lisa/gfx.h"
#include "rnd.h"
#include "prof.h"

static unsigned s_index_tex;    /* R8, the surface */
static unsigned s_shade_levels, s_shade_table;   /* RG8 levels and the table, see shaded_layer */
static unsigned s_palette_tex;  /* 256 x 1 RGB8 */
static unsigned s_snap_tex;     /* R8, the screen as the 3D left it */
static int s_draw_w, s_draw_h;
static const u8 *s_snap_src;    /* what s_snap_tex holds */
static int s_snap_w, s_snap_h;
static unsigned s_overlay;      /* RGBA8, over the 3D */

void gfx_set_overlay(unsigned overlay)
{
    s_overlay = overlay;
}

/* The corner pieces: their pixels, and the textures they are drawn from
 * (made when the size changes, set when the pixels do). */
enum { CORNER_MAX = 32 };
static int s_corner_w, s_corner_h;
static u8 s_corner_px[4][CORNER_MAX * CORNER_MAX];
static const u8 s_corner_zero[CORNER_MAX * CORNER_MAX];
static unsigned s_corner_tex[4], s_corner_zero_tex;
static int s_corner_made_w, s_corner_made_h, s_corner_dirty;

void gfx_set_corners(const u8 *const pieces[4], int w, int h, int stride)
{
    u8 px[4][CORNER_MAX * CORNER_MAX];
    int i, y;

    if (pieces == NULL || w <= 0 || h <= 0 || w > CORNER_MAX || h > CORNER_MAX || stride < w) {
        s_corner_w = s_corner_h = 0;
        return;
    }
    for (i = 0; i < 4; i++) {
        for (y = 0; y < h; y++) {
            memcpy(&px[i][y * w], pieces[i] + (size_t)y * stride, (size_t)w);
        }
    }
    if (w != s_corner_w || h != s_corner_h || memcmp(px, s_corner_px, sizeof(px)) != 0) {
        memcpy(s_corner_px, px, sizeof(px));
        s_corner_w = w;
        s_corner_h = h;
        s_corner_dirty = 1;
    }
}

int gfx_init(void)
{
    if (!rnd_init()) {
        return 0;
    }
    s_index_tex = rnd_texture(RND_R8, 1, 1, NULL, 0);
    s_palette_tex = rnd_texture(RND_RGB8, 256, 1, NULL, 0);
    s_snap_tex = rnd_texture(RND_R8, 1, 1, NULL, 0);
    s_snap_src = NULL;
    return 1;
}

void gfx_shutdown(void)
{
    rnd_texture_free(s_shade_levels);
    rnd_texture_free(s_shade_table);
    s_shade_levels = s_shade_table = 0;
    {
        int i;

        for (i = 0; i < 4; i++) {
            rnd_texture_free(s_corner_tex[i]);
            s_corner_tex[i] = 0;
        }
        rnd_texture_free(s_corner_zero_tex);
        s_corner_zero_tex = 0;
        s_corner_w = s_corner_h = 0;
    }
    rnd_texture_free(s_index_tex);
    rnd_texture_free(s_palette_tex);
    rnd_texture_free(s_snap_tex);
    rnd_shutdown();
}

void gfx_begin_frame(int width, int height)
{
    static const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    s_draw_w = width;
    s_draw_h = height;
    rnd_bind_screen(width, height);
    rnd_clear(black, 1);
}

/* The rectangle a screen of `aspect` takes: the drawable's height,
 * pillarboxed, or letterboxed when the window is narrower. */
static void screen_rect(float aspect, float rect[4])
{
    float sw, sh = 1.0f;

    sw = aspect * (float)s_draw_h / (float)s_draw_w;
    if (sw > 1.0f) {
        sh = 1.0f / sw;
        sw = 1.0f;
    }
    rect[0] = -sw;
    rect[1] = -sh;
    rect[2] = sw;
    rect[3] = sh;
}

static void set_palette(const u8 *palette)
{
    rnd_texture_set(s_palette_tex, 256, 1, palette, 256);
}

void gfx_draw_screen(const u8 *pixels, int w, int h, int pitch,
                     const u8 *palette, float aspect)
{
    float rect[4];

    rnd_texture_set(s_index_tex, w, h, pixels, pitch);
    set_palette(palette);
    screen_rect(aspect, rect);
    rnd_use(RND_PRESENT);
    rnd_uniform(RND_U_RECT, rect);
    rnd_texture_unit(0, s_index_tex);
    rnd_texture_unit(1, s_palette_tex);
    rnd_depth(RND_DEPTH_OFF, 1);
    rnd_draw(RND_STRIP, 0, 4);
}

/* The corner pieces at the drawable's corners. `rect` is where the 2D
 * screen of `w` x `h` is, which gives the size of its pixels. */
static void draw_corners(const float rect[4], int w, int h)
{
    float dx, dy, cw, ch, r[4];
    int i;

    if (s_corner_w == 0 || w <= 0 || h <= 0) {
        return;
    }
    if (s_corner_zero_tex == 0 || s_corner_made_w != s_corner_w || s_corner_made_h != s_corner_h) {
        for (i = 0; i < 4; i++) {
            if (s_corner_tex[i] != 0) {
                rnd_texture_free(s_corner_tex[i]);
            }
            s_corner_tex[i] = rnd_texture(RND_R8, s_corner_w, s_corner_h, s_corner_px[i], s_corner_w);
        }
        if (s_corner_zero_tex != 0) {
            rnd_texture_free(s_corner_zero_tex);
        }
        s_corner_zero_tex = rnd_texture(RND_R8, s_corner_w, s_corner_h, s_corner_zero, s_corner_w);
        s_corner_made_w = s_corner_w;
        s_corner_made_h = s_corner_h;
        s_corner_dirty = 0;
    } else if (s_corner_dirty) {
        for (i = 0; i < 4; i++) {
            rnd_texture_set(s_corner_tex[i], s_corner_w, s_corner_h, s_corner_px[i], s_corner_w);
        }
        s_corner_dirty = 0;
    }
    dx = (rect[2] - rect[0]) / (float)w;
    dy = (rect[3] - rect[1]) / (float)h;
    cw = s_corner_w * dx;
    ch = s_corner_h * dy;
    rnd_use(RND_OVER);
    for (i = 0; i < 4; i++) {
        /* 0 top left, 1 top right, 2 bottom left, 3 bottom right. */
        r[0] = (i & 1) ? 1.0f - cw : -1.0f;
        r[2] = (i & 1) ? 1.0f : -1.0f + cw;
        r[1] = (i & 2) ? -1.0f : 1.0f - ch;
        r[3] = (i & 2) ? -1.0f + ch : 1.0f;
        rnd_uniform(RND_U_RECT, r);
        rnd_texture_unit(0, s_corner_tex[i]);
        rnd_texture_unit(1, s_palette_tex);
        rnd_texture_unit(2, s_corner_zero_tex);
        rnd_draw(RND_STRIP, 0, 4);
    }
}

/* The 3D, with the HUD's shade and the iris on it (lisa/gfx.h): its
 * index through the table's rows that the levels name, as the original
 * has them over the pixel under them. 0 when there are none. */
static int shaded_layer(unsigned layer, const float whole[4])
{
    int mw = g_gfx_shade_w, mh = g_gfx_shade_h;
    float size[2];

    if (g_gfx_shade == NULL || !g_gfx_shade_used || g_gfx_shade_table == NULL || mw <= 0
        || mh <= 0) {
        return 0;
    }
    if (s_shade_levels == 0) {
        s_shade_levels = rnd_texture(RND_RG8, mw, mh, g_gfx_shade, mw);
    } else {
        rnd_texture_set(s_shade_levels, mw, mh, g_gfx_shade, mw);
    }
    if (s_shade_table == 0) {
        s_shade_table = rnd_texture(RND_R8, 256, 256, g_gfx_shade_table, 256);
    } else {
        rnd_texture_set(s_shade_table, 256, 256, g_gfx_shade_table, 256);
    }
    /* The quad's u over the levels: the layer is the screen's columns and
     * ox_f on each side, the levels ox (a whole number) on each side. */
    size[0] = (float)((mw - 2 * g_gfx_shade_ox + 2 * g_gfx_shade_ox_f) / mw);
    size[1] = (float)((g_gfx_shade_ox - g_gfx_shade_ox_f) / mw);
    rnd_use(RND_SHADE);
    rnd_uniform(RND_U_RECT, whole);
    rnd_uniform(RND_U_SIZE, size);
    rnd_texture_unit(0, layer);
    rnd_texture_unit(1, s_palette_tex);
    rnd_texture_unit(2, s_shade_table);
    rnd_texture_unit(3, s_shade_levels);
    rnd_draw(RND_STRIP, 0, 4);
    return 1;
}

void gfx_draw_layers(unsigned layer, const u8 *pixels, const u8 *snapshot, int w, int h,
                     int pitch, const u8 *palette, float aspect)
{
    static const float whole[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
    float rect[4];

    prof_begin(PROF_H_PAL);
    set_palette(palette);
    prof_end(PROF_H_PAL);
    screen_rect(aspect, rect);
    prof_begin(PROF_H_3D);
    rnd_depth(RND_DEPTH_OFF, 1);

    /* The 3D, the whole drawable. */
    if (!shaded_layer(layer, whole)) {
        rnd_use(RND_PRESENT);
        rnd_uniform(RND_U_RECT, whole);
        rnd_texture_unit(0, layer);
        rnd_texture_unit(1, s_palette_tex);
        rnd_draw(RND_STRIP, 0, 4);
    }

    /* The 3D effects, blended over it. */
    if (s_overlay != 0) {
        rnd_use(RND_LAYER);
        rnd_uniform(RND_U_RECT, whole);
        rnd_texture_unit(0, s_overlay);
        rnd_blend(RND_BLEND_PREMUL);
        rnd_draw(RND_STRIP, 0, 4);
        rnd_blend(RND_BLEND_OFF);
    }

    prof_end(PROF_H_3D);
    prof_begin(PROF_H_TEX);
    /* What the 2D drawing changed, at the screen's place. */
    rnd_texture_set(s_index_tex, w, h, pixels, pitch);
    prof_end(PROF_H_TEX);
    prof_begin(PROF_H_SNAP);
    /* The snapshot is the same buffer, never written, in a race: it is
     * uploaded once (video_set_layer). */
    if (snapshot != s_snap_src || w != s_snap_w || h != s_snap_h) {
        rnd_texture_set(s_snap_tex, w, h, snapshot, pitch);
        s_snap_src = snapshot;
        s_snap_w = w;
        s_snap_h = h;
    }
    prof_end(PROF_H_SNAP);
    prof_begin(PROF_H_OVER);
    rnd_use(RND_OVER);
    rnd_uniform(RND_U_RECT, rect);
    rnd_texture_unit(0, s_index_tex);
    rnd_texture_unit(1, s_palette_tex);
    rnd_texture_unit(2, s_snap_tex);
    rnd_draw(RND_STRIP, 0, 4);
    draw_corners(rect, w, h);
    prof_end(PROF_H_OVER);
}

void gfx_read_rgb(int w, int h, u8 *rgb)
{
    rnd_read(0, 0, w, h, RND_RGB8, rgb, w * 3);
}

void gfx_draw_index_texture(unsigned tex, const u8 *palette, float aspect)
{
    float rect[4];

    set_palette(palette);
    screen_rect(aspect, rect);
    rnd_depth(RND_DEPTH_OFF, 1);
    rnd_scissor(0, 0, 0, 0, 0);
    rnd_use(RND_PRESENT);
    rnd_uniform(RND_U_RECT, rect);
    rnd_texture_unit(0, tex);
    rnd_texture_unit(1, s_palette_tex);
    rnd_draw(RND_STRIP, 0, 4);
}
