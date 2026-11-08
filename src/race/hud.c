/*
 * race/hud.c - the race's HUD (0x43ef30) and the 2D sprites it draws
 * (0x41e990, 0x41f410), drawn into the back buffer through the sprite
 * and font code. The needle and the boost bar are flat triangles the
 * original queues for its rasteriser (0x438030); here they go to
 * gl3d_raster over the back buffer.
 *
 * The HUD sits at the corners of the driver's view (drivers' view_x0..
 * view_y1); while the camera shakes (its f38) the whole HUD shakes with
 * it by a twenty-fifth of that.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "gl3d.h"
#include "lisa/font.h"
#include "lisa/gfx.h"
#include "lisa/sprite.h"
#include "platform/touch.h"
#include "race/race.h"
#include "race/cars.h"
#include "race/contact.h"
#include "race/data.h"
#include "race/hud.h"
#include "race/misc.h"
#include "race/init.h"
#include "startup.h"

extern s32 r_563c80[0x18], r_54f9d4, r_527f2c, r_527f28;   /* race/world.c, race/load.c */
extern s32 r_553068, r_527f24, r_563c38;
extern s32 r_5531c0, r_5531c4;
extern s32 r_6192a0[10], r_6192c8[10];
extern s32 *r_552f24;
extern double r_563d80;

struct sprite *g_hud_panels[4];
struct sprite *g_hud_needle_cap;
struct sprite *g_hud_gears[9];
struct sprite *g_hud_boost[3];

/* The boost for the touch pad's action button (platform/touch.h). */
float g_touch_boost = -1.0f;
int g_touch_boost_on;
struct sprite *g_hud_lights[4];
struct sprite *g_hud_signs16[16];
struct sprite *g_race_5286f4[6];
struct sprite *g_race_52870c[12];
struct sprite *g_hud_car_icons[12];
struct sprite *g_hud_knockout[10];
struct sprite *g_race_5287e8;
struct sprite *g_hud_bar[2];
struct sprite *g_hud_leader;
struct sprite *g_hud_places4[4];
struct sprite *g_race_5287b8[3];
struct sprite *g_race_5287c4[3];
struct sprite *g_race_5287d0[3];
struct sprite *g_race_5287dc[3];

/* The layout's tables and pictures (0x563d8c, 0x553770, 0x553780,
 * 0x563bdc, 0x563bd8, 0x563d50) and the pictures' widths (0x5285cc,
 * 0x563bf8). */
static const s32 *s_cuts, *s_text, *s_places;
static const u8 *s_panel_pic, *s_panel2_pic, *s_signs_pic;
static s32 s_panel_stride, s_signs_stride;

/* Where the confetti over the finishing place would go (0x552d90,
 * 0x552df0): worked out, never drawn. */
static s32 s_spark_x[20], s_spark_y[20];

/* The zoom of the finishing place (0x563d70). */
static struct sprite_xform s_zoom;

/* The race's fonts by role and layout: g_race_fonts runs time, place,
 * speed for the small layout, then the large, then the high. */
static int hud_font(int role)
{
    return g_race_fonts[role + 3 * r_54f9d4];
}

static struct sprite *cut(struct sprite *old, const u8 *base, int offset, int w, int h,
                          int stride, s32 hot_x, s32 hot_y)
{
    struct tex_desc d;

    memset(&d, 0, sizeof(d));
    d.width = w;
    d.height = h;
    d.pixels = (u8 *)base + offset;
    d.stride = stride;
    d.hot_x = hot_x;
    d.hot_y = hot_y;
    return sprite_create(&d, old);
}

/* A sprite of the layout's table entry k (x, y, w, h) from `pic`. */
static struct sprite *cut_entry(struct sprite *old, const u8 *pic, int k, int stride)
{
    const s32 *e = s_cuts + 4 * k;

    return cut(old, pic, e[1] * stride + e[0] + 0x34e, e[2], e[3], stride, 0, 0);
}

/* 0x41f3a0 */
void race_hud_layout(void)
{
    r_527f2c = g_screen_size != 0 ? 7 : 6;
    r_54f9d4 = r_563c80[g_two_players + 2 * (r_527f28 + 4 * g_screen_size)];
    if (r_54f9d4 >= 0 && r_54f9d4 <= 2) {
        s_text = g_hud_text + 8 * r_54f9d4;
    }
}

/* The layout's pictures, as both 0x41e990 and 0x41f410 begin. */
static void choose_pictures(void)
{
    static const s32 panel_w[3] = { 123, 227, 256 }, signs_w[3] = { 56, 112, 256 };
    int l = r_54f9d4;

    if (l < 0 || l > 2) {
        return;
    }
    s_panel_pic = g_race_pics[l == 2 ? 2 : l];
    s_panel2_pic = g_race_pics[l == 2 ? 3 : l];
    s_signs_pic = g_race_pics[4 + l];
    s_cuts = g_hud_cuts + 144 * l;
    s_places = g_hud_places + 28 * l;
    s_panel_stride = panel_w[l];
    s_signs_stride = signs_w[l];
}

/* The sprites that change with the layout. */
static void cut_layout(void)
{
    int k, row;

    g_hud_panels[0] = cut_entry(g_hud_panels[0], s_panel_pic, 0, s_panel_stride);
    g_hud_panels[1] = cut_entry(g_hud_panels[1], s_panel_pic, 1, s_panel_stride);
    g_hud_panels[2] = cut_entry(g_hud_panels[2], s_panel_pic, 2, s_panel_stride);
    g_hud_panels[3] = cut_entry(g_hud_panels[3], s_panel_pic, 3, s_panel_stride);
    g_hud_needle_cap = cut_entry(g_hud_needle_cap, s_panel_pic, 26, s_panel_stride);
    for (k = 0; k < 9; k++) {
        g_hud_gears[k] = cut_entry(g_hud_gears[k], s_panel2_pic, 4 + k, s_panel_stride);
    }
    for (k = 0; k < 3; k++) {
        g_hud_boost[k] = cut_entry(g_hud_boost[k], s_panel_pic, 27 + k, s_panel_stride);
    }
    /* The start lights are cut from the small layout's picture always,
     * one under the other. */
    for (k = 0; k < 4; k++) {
        const s32 *e = s_cuts + 4 * 33;

        g_hud_lights[k] = cut(g_hud_lights[k], g_race_pics[1],
                              (e[3] * k + e[1]) * 227 + e[0] + 0x34e, e[2], e[3], 227, 0, 0);
    }
    for (row = 0; row < 4; row++) {
        for (k = 0; k < 4; k++) {
            const s32 *e = s_cuts + 4 * 34;
            struct sprite **s = &g_hud_signs16[row * 4 + k];

            *s = cut(*s, s_signs_pic, (e[3] * row + e[1]) * s_signs_stride + e[2] * k + e[0] + 0x34e,
                     e[2], e[3], s_signs_stride, 0, 0);
        }
    }
}

/* 0x41f410 */
void race_hud_recut(void)
{
    choose_pictures();
    if (s_cuts != NULL) {
        cut_layout();
    }
}

/* 0x41e990 */
void race_hud_sprites(void)
{
    const u8 *pokal = g_race_pics[7], *n_pan = g_race_pics[1];
    int k, i;

    choose_pictures();
    race_hud_layout();
    if (s_cuts == NULL) {
        return;
    }
    cut_layout();
    for (k = 0; k < 6; k++) {
        g_race_5286f4[k] = cut(NULL, g_sysgfx, 0x5431 + k * 0x1800, 29, 24, 256, 0, 0);
    }
    for (i = 0; i < 3; i++) {
        for (k = 0; k < 4; k++) {
            g_race_52870c[i * 4 + k] = cut(NULL, g_sysgfx, 0x34e + i * 0x2c00 + k * 0x33, 0x33, 0x2c,
                                           256, 0, 0);
        }
    }
    for (i = 0; i < 3; i++) {
        for (k = 0; k < 4; k++) {
            g_hud_car_icons[i * 4 + k] = cut(NULL, n_pan, 0x7e98 + i * 12 + k * 0xf13, 12, 17, 227,
                                             0, 0);
        }
    }
    for (i = 0; i < 2; i++) {
        for (k = 0; k < 5; k++) {
            g_hud_knockout[i * 5 + k] = cut(NULL, n_pan, 0x3a1 + i * 0x1465 + k * 0x18, 0x18, 0x17,
                                            227, 0, 0);
        }
    }
    g_hud_bar[0] = cut(NULL, n_pan, 0x7e8f, 7, 120, 227, 0, 0);
    g_hud_bar[1] = cut(NULL, g_race_pics[3], 0x42f, 7, 300, 256, 0, 0);
    g_hud_leader = cut(NULL, n_pan, 0xbae4, 17, 12, 227, 0, 0);
    g_hud_places4[0] = cut(NULL, g_sysgfx, 0xc14e, 0x99, 0x32, 256, 0, 0);
    g_race_5287e8 = cut(NULL, g_sysgfx, 0x41a, 0x1c, 0x19, 256, 0, 0);
    g_hud_places4[1] = cut(NULL, g_sysgfx2, 0x3e7, 0x59, 0x54, 0xf2, 0x2c00, 0x2a00);
    g_hud_places4[2] = cut(NULL, g_sysgfx2, 0x567e, 0x59, 0x54, 0xf2, 0x2c00, 0x2a00);
    g_hud_places4[3] = cut(NULL, g_sysgfx2, 0x56d7, 0x59, 0x54, 0xf2, 0x2c00, 0x2a00);
    for (k = 0; k < 3; k++) {
        g_race_5287b8[k] = cut(NULL, pokal, 0x34e + k * 0x3d, 0x3d, 0x67, 0xcf, 0x1e00, 0x3300);
    }
    for (k = 0; k < 3; k++) {
        g_race_5287c4[k] = cut(NULL, pokal, 0x5697 + k * 0x3d, 0x3d, 0x5e, 0xcf, 0x1e00, 0x2f00);
    }
    /* The original first makes one more of the last cup, then puts the
     * next three over it; that sprite is never freed. */
    cut(NULL, pokal, 0x5697 + 2 * 0x3d, 0x3d, 0x5e, 0xcf, 0x1e00, 0x2f00);
    for (k = 0; k < 3; k++) {
        g_race_5287d0[k] = cut(NULL, pokal, 0xa299 + k * 0x45, 0x45, 0x88, 0xcf, 0x2200, 0x4400);
    }
    for (k = 0; k < 3; k++) {
        g_race_5287dc[k] = cut(NULL, pokal, 0x11091 + k * 0x3d, 0x3d, 0x5d, 0xcf, 0x1e00, 0x2e00);
    }
}

/* 0x4407c0 */
const char *race_time_text(double seconds)
{
    static char buf[32];                /* 0x4cd888 */
    s32 min = (s32)(seconds * 0.016666666666666666), whole;

    if (min > 99) {
        min = 99;
    }
    whole = (s32)seconds;
    snprintf(buf, sizeof(buf), "%.2d:%.2d:%.2d", min, whole - min * 60,
             (s32)((seconds - whole) * 100.0));
    return buf;
}

static void draw_at(const struct sprite *s, s32 x, s32 y, const struct sprite_xform *xf)
{
    struct sprite_pos pos;

    pos.x = x << 8;
    pos.y = y << 8;
    sprite_draw(s, &pos, xf);
}

static void flat(struct raster_cmd *c, float x0, float y0, float x1, float y1, float x2, float y2,
                 u8 colour)
{
    memset(c, 0, sizeof(*c));
    c->type = RASTER_FLAT;
    c->colour = colour;
    c->x[0] = x0;
    c->y[0] = y0;
    c->x[1] = x1;
    c->y[1] = y1;
    c->x[2] = x2;
    c->y[2] = y2;
}

static void raster(const struct raster_cmd *cmds, int n)
{
    gl3d_raster(g_back_buffer, g_screen_width, g_screen_height, g_screen_width, cmds, n);
}

/* The camera's shake, a twenty-fifth of its f38 either way at random. */
static double shake(int view)
{
    double s = g_race_cams[view].f38;

    if (s == 0.0 || r_494990 == 1) {
        return 0.0;
    }
    return race_rand() < 0.5 ? s * -0.04 : s * 0.04;
}

/* 0x440840
 * The speedometer: the dial, a needle at 2.7 degrees a percent of the
 * revs (eased by a quarter a frame, stopping at 270), its cap, and the
 * speed in km/h. */
static void race_dials(int view)
{
    const struct race_driver *d = &g_drivers[view];
    struct car *c = &g_cars[view];
    double s = shake(view);
    s32 x1, y1, ax, ay, bx, by, tx, ty;
    double cx, cy, r1, r2, a;
    struct raster_cmd cmd;
    char buf[16];

    if (s == 0.0) {
        x1 = d->view_x1;
        y1 = d->view_y1;
    } else {
        x1 = (s32)(d->view_x1 + s);
        y1 = (s32)(d->view_y1 + s);
    }
    draw_at(g_hud_panels[3], s_places[5] + x1, s_places[6] + y1, NULL);
    c->f290 = (c->rpm * 2.7 - c->f290) * 0.25 + c->f290;
    if (c->f290 > 270.0) {
        c->f290 = 270.0;
    }
    cx = s_places[9] + x1;
    cy = s_places[10] + y1;
    r1 = s_places[11];
    r2 = s_places[12];
    a = (c->f290 + 180.0) * 0.017453292519943334;
    ax = (s32)(cos(a) * r1 + cx);
    ay = (s32)(sin(a) * r1 + cy);
    a = (c->f290 + 90.0) * 0.017453292519943334;
    bx = (s32)(cos(a) * r2 + cx);
    by = (s32)(sin(a) * r2 + cy);
    a = c->f290 * 0.017453292519943334;
    tx = (s32)(cos(a) * r1 + cx);
    ty = (s32)(sin(a) * r1 + cy);
    g_gfx_color_table = g_level_pan;
    flat(&cmd, (float)ax, (float)ay, (float)bx, (float)by, (float)tx, (float)ty, 0xc4);
    raster(&cmd, 1);
    draw_at(g_hud_needle_cap, s_places[7] + x1, s_places[8] + y1, NULL);
    snprintf(buf, sizeof(buf), "%.3d", abs((s32)(g_car_phys[view].speed * 3.6)));
    text_draw(buf, hud_font(2), s_text[6] + d->view_x1, s_text[7] + d->view_y1);
}

/* The start: the lights at 50, 100, 150 and GO, with their beeps. */
static void start_lights(int view, s32 y0)
{
    const struct race_driver *d = &g_drivers[view];
    const struct car *c = &g_cars[view];
    double t = r_552e48;
    int k;

    if (50.0 <= t && t < 100.0) {
        k = 0;
    } else if (100.0 <= t && t < 150.0) {
        if (r_563c38 == 0) {
            race_sound_3d(0, 4, 0, 0, 0x10000, 0x55f0, 0, (float)(s32)c->rear_x,
                          (float)(s32)c->rear_z);
            r_563c38 = 1;
        }
        k = 1;
    } else if (150.0 <= t && t < 200.0) {
        if (r_563c38 == 1) {
            race_sound_3d(0, 4, 0, 0, 0x10000, 0x55f0, 0, (float)(s32)c->rear_x,
                          (float)(s32)c->rear_z);
            r_563c38 = 2;
        }
        k = 2;
    } else if (t == -1.0 && g_race_clock < 250.0) {
        if (r_563c38 == 2) {
            race_sound_3d(0, 4, 1, 0, 0x10000, 0x55f0, 0, (float)(s32)c->rear_x,
                          (float)(s32)c->rear_z);
            r_563c38 = 3;
        }
        k = 3;
    } else {
        return;
    }
    draw_at(g_hud_lights[k], (d->view_x1 - d->view_x0) / 2 - s_places[24] / 2 + d->view_x0,
            s_places[25] + y0, NULL);
}

/* The elimination race: the warning to the last car, and a bar with
 * each car's way round the lap. */
static void knockout_bar(int view)
{
    static const char *const last[6] = {
        "WARNING, YOU'RE LAST!", "SIE SIND LETZTER!", "ATTENZIONE, SEI ULTIMO",
        "AVISO, ERES EL ULTIMO", "VARNING, DU LIGGER SIST!", "ATTENTION, VOUS ETES DERNIER!"
    };
    const struct race_driver *d = &g_drivers[view];
    const struct car *c = &g_cars[view];
    double frac[RACE_DRIVERS];
    s32 h = g_screen_size == 0 ? 120 : 300;
    int i;

    if (c->grid == r_527f24 && c->f528 == 0 && !(r_494990 && g_race_net.mode == 2)) {
        char buf[0x64] = "";

        if (g_language >= 0 && g_language < 6) {
            snprintf(buf, sizeof(buf), "%s", last[g_language]);
        }
        if (view == 0 && !g_two_players) {
            race_message(buf, 0, 1, -1);
        } else if (view == 0 && g_two_players == 1) {
            race_message(buf, 0, 1, 0);
        } else if (view == 1 && g_two_players == 1) {
            race_message(buf, 0, 1, 1);
        }
    }
    draw_at(g_hud_bar[g_screen_size != 0], d->view_x0 + 8, d->view_y0 + 8, NULL);
    for (i = g_driver_count - 1; i >= 0; i--) {
        const struct car *o = &g_cars[i];

        frac[i] = g_race_clock >= 250.0 ? abs(o->f364) / (double)g_track_link_count : 0.0;
        if (o->f528 == 0) {
            s32 up = (s32)(h * frac[i]);

            draw_at(g_hud_car_icons[g_drivers[i].car], d->view_x0 + 5, d->view_y0 - up + h + 5, NULL);
            if (o->grid == 1) {
                draw_at(g_hud_leader, d->view_x0 + 19, d->view_y0 - up + h + 8, NULL);
            }
        }
    }
    for (i = g_driver_count - 1; i >= 0; i--) {
        const struct car *o = &g_cars[i];

        /* The original indexes with a negative f60c too; skipped here. */
        if (o->f528 == 1 && o->f60c < 10.0 && o->f60c >= 0.0 && o->f3a0 != 1) {
            draw_at(g_hud_knockout[(s32)o->f60c], d->view_x0,
                    d->view_y0 - (s32)(h * frac[i]) + h, NULL);
        }
    }
    if (view == 1 && g_cars[1].f528 == 0) {
        double f = g_race_clock >= 250.0 ? abs(g_cars[1].f364) / (double)g_track_link_count : 0.0;

        draw_at(g_hud_car_icons[g_drivers[1].car], d->view_x0 + 5,
                d->view_y0 - (s32)(h * f) + h + 5, NULL);
    }
}

/* The finishing place zooming in, its cheer, and where the confetti
 * would fly. */
static void finish_place(int view, s32 y0, s32 y1)
{
    const struct race_driver *d = &g_drivers[view];
    const struct car *c = &g_cars[view];
    s32 *cheered = view ? &r_5531c4 : &r_5531c0;
    s32 *spark = view ? r_6192c8 : r_6192a0;
    int k;

    if (!(r_54f980 >= 0.0) || r_527f40[view] >= 120) {
        return;
    }
    if (g_race_mode == 2 && g_driver_count <= 1) {
        return;
    }
    if (g_race_mode == 3 ? c->f3a0 >= 2 : c->f3a0 >= 4) {
        return;
    }
    if (c->f528 != 1) {
        return;
    }
    if (c->f5d4 > 0 && c->f3a0 < 4) {
        s_zoom.angle = 0.0f;
        s_zoom.scale = (float)(c->f5d4 * 0.04);
        draw_at(g_hud_places4[c->f3a0], (d->view_x1 - d->view_x0) / 2 + d->view_x0, (y1 - y0) / 2,
                c->f5d4 < 25 ? &s_zoom : NULL);
        if (spark[0] >= 100 && *cheered == 0) {
            race_sound_3d(0, 4, 5, 0, 0x10000, 0x55f0, 0, (float)(s32)c->rear_x,
                          (float)(s32)c->rear_z);
            *cheered = 1;
        }
    }
    for (k = 0; k < 6; k++) {
        if (spark[k] > 12 && r_527f40[view] < 40) {
            s32 i = view * 10 + k;

            s_spark_x[i] = (s32)((race_rand() * 90.0 - 45.0) * s_zoom.scale
                                 + (double)((d->view_x1 - d->view_x0) / 2 + d->view_x0));
            s_spark_y[i] = (s32)((race_rand() * 90.0 - 45.0) * s_zoom.scale
                                 + (double)((y1 - y0) / 2));
            spark[k] = spark[k] >= 100 ? (s32)(race_rand() * 12.0) : 0;
        }
        if (spark[k] < 13) {
            /* The original sets a half size and the place here, then
             * draws nothing. */
            s_zoom.scale = 0.5f;
            s_zoom.angle = 0.0f;
        }
    }
}

/* The boost lamp: off while charging, blinking slowly when full, fast
 * while it burns or is too low to use. */
static void boost_lamp(struct car *c, s32 x1, s32 y1)
{
    int k;

    if (c->f53c == 0 && c->f534 >= 20.0 && c->f534 < 100.0) {
        k = 1;
    } else if (c->f53c == 0 && c->f534 >= 100.0) {
        if (!(r_494990 && g_race_net.mode == 2) && ++c->f540 > 14) {
            c->f540 = 0;
        }
        k = c->f540 < 7 ? 2 : 1;
    } else if (c->f53c > 0 || c->f534 < 20.0) {
        if (!(r_494990 && g_race_net.mode == 2) && ++c->f540 > 1) {
            c->f540 = 0;
        }
        k = 1 + c->f540;
    } else {
        return;
    }
    draw_at(g_hud_boost[k], s_places[15] + x1, s_places[16] + y1, NULL);
}

/* WRONG WAY, or the road's hint sign, blinking for its first 24 ticks. */
static void signs(int view, s32 y0)
{
    const struct race_driver *d = &g_drivers[view];
    struct car *c = &g_cars[view];
    s32 x = (d->view_x1 - d->view_x0) / 2 - s_cuts[4 * 34 + 2] / 2 + d->view_x0;
    s32 plc = c->f364 >= 0 ? g_track_links[c->f364].plc : g_track_links[-c->f364].alt_plc;
    s32 hint;

    if (c->wrong_way > 25 && c->f368 > c->f364) {
        draw_at(g_hud_signs16[15], x, s_places[27] + y0, NULL);
        return;
    }
    hint = g_plc_extra[plc].f2;
    if (hint <= 0) {
        c->sign_ticks = 0;
        return;
    }
    c->sign_ticks++;
    if ((c->sign_ticks / 4) % 2 != 0 && c->sign_ticks < 24) {
        return;
    }
    draw_at(g_hud_signs16[g_hud_signs[g_mirror * 20 + (hint > 16 ? 16 : hint)]], x,
            s_places[27] + y0, NULL);
}

/* 0x43ef30 */
void race_hud(int view, int second)
{
    const struct race_driver *d = &g_drivers[view];
    struct car *c = &g_cars[view];
    double s = shake(view);
    s32 x0 = d->view_x0, y0 = d->view_y0, x1 = d->view_x1, y1 = d->view_y1;
    s32 tx;
    char buf[32];
    struct raster_cmd bar[2];
    double scale;

    (void)second;
    if (s_cuts == NULL || s_text == NULL) {
        return;
    }
    gfx_set_clip(d->view_x0, d->view_y0, d->view_x1, d->view_y1);
    if (s != 0.0) {
        x0 = (s32)(d->view_x0 + s);
        y0 = (s32)(d->view_y0 + s);
        x1 = (s32)(d->view_x1 + s);
        y1 = (s32)(d->view_y1 + s);
    }
    /* The time and the laps. */
    if (g_race_mode != 3) {
        tx = x0 + 7;
        draw_at(g_hud_panels[0], tx, y0 + 7, NULL);
        g_gfx_color_table = g_level_pan;
        text_draw(r_552e48 == -1.0 ? race_time_text(c->f3a4) : "--:--:--", hud_font(0), tx,
                  s_text[0] + y0);
        draw_at(g_hud_panels[1], tx, s_places[0] + y0, NULL);
        if (r_552e48 == -1.0) {
            text_draw(race_time_text(c->laps == 0 ? c->lap_time : c->f3bc), hud_font(0), tx,
                      s_text[1] + y0);
        } else {
            text_draw("--:--:--", hud_font(0), tx, s_text[1] + y0);
        }
        if (c->laps >= 1) {
            text_draw(race_time_text(c->laps == 1 ? c->lap_time : c->f3c4), hud_font(0), tx,
                      s_text[2] + y0);
        }
        if (c->laps >= 2) {
            text_draw(race_time_text(c->laps == 2 ? c->lap_time : c->f3cc), hud_font(0), tx,
                      s_text[3] + y0);
        }
    }
    /* The place. */
    if (g_driver_count > 1 || r_553068 == 1) {
        if (g_race_mode != 3) {
            draw_at(g_hud_panels[2], s_places[1] + x0, s_places[2] + y1, NULL);
            snprintf(buf, sizeof(buf), "%d", c->f528 ? c->f3a0 : c->grid);
        } else if (c->f528 == 0) {
            snprintf(buf, sizeof(buf), "%d/%d", c->grid, r_527f24);
        } else {
            snprintf(buf, sizeof(buf), "%d", c->grid);
        }
        text_draw(buf, hud_font(1), s_text[4] + x0, s_text[5] + y1);
    }
    race_dials(view);
    /* The gear, rolling toward its sprite at half a sprite a tick. */
    if ((double)(c->gear * 4) > c->f280) {
        c->f280 += r_563d80 * 0.5;
        if ((double)(c->gear * 4) < c->f280) {
            c->f280 = c->gear * 4;
        }
    }
    if ((double)(c->gear * 4) < c->f280) {
        c->f280 += r_563d80 * -0.5;
        if (!((double)(c->gear * 4) <= c->f280)) {
            c->f280 = c->gear * 4;
        }
    }
    draw_at(g_hud_gears[(s32)c->f280], s_places[3] + x1, s_places[4] + y1, NULL);
    /* The boost meter, a bar from its foot, 4.2, 2.1 or 1 to a pixel. */
    scale = r_54f9d4 == 0 ? 4.2 : r_54f9d4 == 1 ? 2.1 : 1.0;
    {
        s32 top = (s32)((double)(s_places[18] + y1) - c->f534 / scale);
        float l = (float)(s_places[17] + x1), r = (float)(s_places[19] + x1);
        float foot = (float)(s_places[18] + y1);

        flat(&bar[0], l, foot, l, (float)top, r, foot, 0xd7);
        flat(&bar[1], r, foot, l, (float)top, r, (float)top, 0xd7);
        raster(bar, 2);
    }
    draw_at(g_hud_boost[0], s_places[13] + x1, s_places[14] + y1, NULL);
    boost_lamp(c, x1, y1);
    if (view == 0) {
        g_touch_boost = (float)(c->f534 / 100.0);
        g_touch_boost_on = c->f53c != 0;
    }
    if (c->f528 == 0 && r_552e48 == -1.0) {
        signs(view, y0);
    }
    if (g_race_mode == 3) {
        knockout_bar(view);
    }
    start_lights(view, y0);
    finish_place(view, y0, y1);
    race_draw_messages();
}

/* 0x441250: the messages, centred on their view, one above another
 * from the view's foot, each blinking by its period. */
void race_draw_messages(void)
{
    struct race_msg *m = (struct race_msg *)r_552f24;
    int i, font = g_fonts_menu[r_527f2c], scale = race_box_scaled() ? 2 : 1;

    if (m == NULL) {
        return;
    }
    for (i = 0; i < 5; i++) {
        s32 x;

        /* A period of 0 makes the fmod NaN, which the original's
         * compare takes as off. */
        if (m[i].active != 1
            || !(fmod((double)m[i].age, (double)(m[i].blink * 2)) <= m[i].blink)) {
            continue;
        }
        if (m[i].view == -1) {
            x = g_screen_width / 2;
        } else {
            const struct race_driver *d = &g_drivers[m[i].view];

            x = (d->view_x1 - d->view_x0) / 2 + d->view_x0;
        }
        /* NATIVE draws them at the menus' size, as its boxes. */
        g_fonts[font].align = FONT_ALIGN_CENTER;
        text_draw_scaled(m[i].text, font, x,
                         (5 - r_527f2c) * (i + 1) * 8 * scale + g_drivers[0].view_y1, scale);
        g_fonts[font].align = FONT_ALIGN_LEFT;
    }
}

/* 0x441210: a message goes once it has been up its time. */
void race_age_messages(void)
{
    struct race_msg *m = (struct race_msg *)r_552f24;
    int i;

    if (m == NULL) {
        return;
    }
    for (i = 0; i < 5; i++) {
        if (m[i].active == 1 && m[i].life < ++m[i].age) {
            m[i].active = 0;
        }
    }
}

static void free_sprites(struct sprite **s, int n)
{
    int i;

    for (i = 0; i < n; i++) {
        if (s[i] != NULL) {
            sprite_create(NULL, s[i]);
            s[i] = NULL;
        }
    }
}

/* 0x420990: the HUD's sprites freed at the race's end. */
void race_hud_free(void)
{
    free_sprites(g_hud_panels, 4);
    free_sprites(&g_hud_needle_cap, 1);
    free_sprites(g_hud_gears, 9);
    free_sprites(g_hud_boost, 3);
    free_sprites(g_hud_lights, 4);
    free_sprites(g_hud_signs16, 16);
    free_sprites(g_race_5286f4, 6);
    free_sprites(g_race_52870c, 12);
    free_sprites(g_hud_knockout, 10);
    free_sprites(g_hud_car_icons, 12);
    free_sprites(g_hud_bar, 2);
    free_sprites(&g_hud_leader, 1);
    free_sprites(g_hud_places4, 4);
    free_sprites(&g_race_5287e8, 1);
    free_sprites(g_race_5287b8, 3);
    free_sprites(g_race_5287c4, 3);
    free_sprites(g_race_5287d0, 3);
    free_sprites(g_race_5287dc, 3);
}
