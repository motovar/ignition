/*
 * platform/touch.c - the on-screen pad. See platform_touch.h.
 *
 * The buttons are rings drawn with rnd.h's OVER program: a small index
 * texture (0 outside the ring, so OVER leaves those pixels alone), a
 * palette of its own and an empty snapshot. A held button is filled.
 */

#include <string.h>

#include "platform/common.h"
#include "platform/touch.h"
#include "rnd.h"

/* In units of a sixth of the screen's height from the bottom left
 * (x < 0: from the right edge). */
static const struct button {
    float x, y, r;
    u32 pad;
} s_buttons[] = {
    { 1.0f, 1.9f, 0.55f, PAD_LEFT },
    { 3.0f, 1.9f, 0.55f, PAD_RIGHT },
    { 2.0f, 2.9f, 0.55f, PAD_UP },
    { 2.0f, 0.9f, 0.55f, PAD_DOWN },
    { -1.1f, 1.4f, 0.7f, PAD_A },
    { -2.6f, 1.0f, 0.55f, PAD_X },
    { -1.1f, 2.9f, 0.55f, PAD_B },
    { -2.6f, 2.4f, 0.55f, PAD_Y },
    { -0.6f, 5.5f, 0.35f, PAD_START },
    { 0.6f, 5.5f, 0.35f, PAD_SELECT },
};

enum { BUTTONS = sizeof(s_buttons) / sizeof(s_buttons[0]), FINGERS = 10, SIZE = 64 };

static int s_seen;
static struct { long id; float x, y; int down; } s_fingers[FINGERS];
static unsigned s_ring, s_lit, s_empty, s_palette;

static void place(const struct button *b, int w, int h, float *cx, float *cy, float *r)
{
    float unit = (float)h / 6.0f;

    *cx = b->x >= 0.0f ? b->x * unit : (float)w + b->x * unit;
    *cy = (float)h - b->y * unit;
    *r = b->r * unit;
}

void touch_finger(long id, float x, float y, int down)
{
    int f, free_slot = -1;

    s_seen = 1;
    for (f = 0; f < FINGERS; f++) {
        if (s_fingers[f].down && s_fingers[f].id == id) {
            break;
        }
        if (!s_fingers[f].down && free_slot < 0) {
            free_slot = f;
        }
    }
    if (f == FINGERS) {
        if (!down || free_slot < 0) {
            return;
        }
        f = free_slot;
    }
    s_fingers[f].id = id;
    s_fingers[f].x = x;
    s_fingers[f].y = y;
    s_fingers[f].down = down;
}

static u32 held(int w, int h)
{
    u32 b = 0;
    int i, f;

    for (f = 0; f < FINGERS; f++) {
        if (!s_fingers[f].down) {
            continue;
        }
        for (i = 0; i < BUTTONS; i++) {
            float cx, cy, r, dx, dy;

            place(&s_buttons[i], w, h, &cx, &cy, &r);
            dx = s_fingers[f].x - cx;
            dy = s_fingers[f].y - cy;
            /* A little slack around each button. */
            if (dx * dx + dy * dy < r * r * 1.4f) {
                b |= s_buttons[i].pad;
            }
        }
    }
    return b;
}

u32 touch_buttons(int w, int h)
{
    return s_seen ? held(w, h) : 0;
}

static int make_textures(void)
{
    static u8 ring[SIZE * SIZE], lit[SIZE * SIZE], empty[SIZE * SIZE];
    static u8 palette[256 * 3];
    int x, y;

    if (s_ring != 0) {
        return 1;
    }
    for (y = 0; y < SIZE; y++) {
        for (x = 0; x < SIZE; x++) {
            float dx = (float)x + 0.5f - SIZE / 2.0f, dy = (float)y + 0.5f - SIZE / 2.0f;
            float d2 = (dx * dx + dy * dy) / (SIZE * SIZE / 4.0f);
            u8 v = d2 > 1.0f ? 0 : d2 > 0.78f ? 1 : 0;

            ring[y * SIZE + x] = v;
            lit[y * SIZE + x] = d2 > 1.0f ? 0 : v != 0 ? 1 : 2;
        }
    }
    memset(palette + 3, 235, 3);
    memset(palette + 6, 150, 3);
    s_ring = rnd_texture(RND_R8, SIZE, SIZE, ring, 0);
    s_lit = rnd_texture(RND_R8, SIZE, SIZE, lit, 0);
    s_empty = rnd_texture(RND_R8, SIZE, SIZE, empty, 0);
    s_palette = rnd_texture(RND_RGB8, 256, 1, palette, 256);
    return s_ring != 0;
}

void touch_draw(int w, int h)
{
    u32 on;
    int i;

    if (!s_seen || w <= 0 || h <= 0 || !make_textures()) {
        return;
    }
    on = held(w, h);
    rnd_bind_screen(w, h);
    /* Depth test off, but depth writes stay on, as everywhere else:
     * glClear honours the depth mask, so leaving it off here would stop
     * the next frame's depth clears and black out the 3D picture. */
    rnd_depth(RND_DEPTH_OFF, 1);
    rnd_scissor(0, 0, 0, 0, 0);
    rnd_cull(RND_CULL_OFF);
    rnd_use(RND_OVER);
    rnd_texture_unit(1, s_palette);
    rnd_texture_unit(2, s_empty);
    for (i = 0; i < BUTTONS; i++) {
        float cx, cy, r, rect[4];

        place(&s_buttons[i], w, h, &cx, &cy, &r);
        rect[0] = (cx - r) / (float)w * 2.0f - 1.0f;
        rect[1] = 1.0f - (cy + r) / (float)h * 2.0f;
        rect[2] = (cx + r) / (float)w * 2.0f - 1.0f;
        rect[3] = 1.0f - (cy - r) / (float)h * 2.0f;
        rnd_uniform(RND_U_RECT, rect);
        rnd_texture_unit(0, (on & s_buttons[i].pad) ? s_lit : s_ring);
        rnd_draw(RND_STRIP, 0, 4);
    }
}
