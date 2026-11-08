/*
 * race/init.c - the last stage of loading a race (0x418c40): the moving
 * objects' paths, the collision grid, the cameras, the pools of objects
 * the effects use, and the race's state.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anim_tex.h"
#include "app.h"
#include "canvas.h"
#include "cd.h"
#include "game.h"
#include "input.h"
#include "lisa/file.h"
#include "lisa/log.h"
#include "lisa/mem.h"
#include "race/race.h"
#include "race/cars.h"
#include "race/hud.h"
#include "race/ai.h"
#include "race/fx.h"
#include "race/init.h"
#include "race/misc.h"
#include "scene.h"
#include "net.h"
#include "stubs.h"
#include "lisa/gfx.h"
#include "race/frame.h"
#include "startup.h"
#include "video.h"

struct race_cam *g_race_cams;               /* 0x5daff4, one a driver */
struct srf g_srf;                           /* 0x4937bc, 0x4c53a4.. */
struct scene_desc *g_particle_objs;         /* 0x563cec, 700 */
struct scene_desc *g_part_objs;             /* 0x54f994, 200 */
s32 g_part_meshes[200][200];                /* 0x528800 */
s32 g_particle_mesh[2] = { 0, 0 };          /* 0x5dfe70: empty until used */
s32 g_weather;                              /* 0x553774: 1 rain, 2 snow, 3 */
s32 *g_weather_meshes[2];                   /* 0x563c34, 0x563c30 */
struct scene_desc *g_weather_objs;          /* 0x553030, 9 a view */
s32 g_weather_voices[2];                    /* 0x525e50 */
double g_best_lap;                          /* 0x54f9b0 */
s32 r_563cf4, r_5532f0;

/* 0x4356d0
 * A moving placement's path, positions at first, becomes each frame's
 * step to the next: x, y and z less the next frame's, the last less
 * the first, so the path loops. */
void race_pos_deltas(void)
{
    int i, k;

    for (i = 0; i < g_level_plc[0]; i++) {
        s32 rel = g_level_pos[i];
        s32 *block, *f, first[3];

        if (rel == -1) {
            continue;
        }
        block = g_level_pos + rel + i;
        f = block + 2;
        first[0] = f[0];
        first[1] = f[1];
        first[2] = f[2];
        for (k = 0; k < block[0] - 1; k++) {
            f[6 * k] -= f[6 * k + 6];
            f[6 * k + 1] -= f[6 * k + 7];
            f[6 * k + 2] -= f[6 * k + 8];
        }
        f[6 * k] -= first[0];
        f[6 * k + 1] -= first[1];
        f[6 * k + 2] -= first[2];
    }
}

/* 0x412670
 * The original turns the file's offsets into pointers in place; they
 * stay offsets here, resolved by the accessors in race_init.h. */
int race_srf_load(const char *name)
{
    const s32 *h;

    g_srf.file = file_load(name);
    if (g_srf.file == NULL) {
        return 0;
    }
    h = (const s32 *)g_srf.file;
    g_srf.ext_z = h[0];
    g_srf.ext_x = h[1];
    g_srf.cell_h = h[2];
    g_srf.cell_w = h[3];
    g_srf.gw = h[4];
    g_srf.gh = h[5];
    g_srf.cells = (const struct srf_cell *)(g_srf.file + 0x24);
    g_srf.spans = g_srf.file + 0x24 + 12 * g_srf.gw * g_srf.gh;
    g_srf.list_a = (const u32 *)(g_srf.spans + 24 * h[6]);
    g_srf.list_b = g_srf.list_a + h[7];
    return 1;
}

/* 0x41e740: a chase camera behind each car. */
void race_init_cameras(void)
{
    int i;

    g_race_cams = mem_calloc(g_race_pool, (u32)g_driver_count * sizeof(*g_race_cams));
    g_race_bytes += g_driver_count * (s32)sizeof(*g_race_cams);
    if (g_race_cams == NULL) {
        mem_out_of_memory();
    }
    for (i = 0; i < g_driver_count; i++) {
        struct race_cam *cam = &g_race_cams[i];
        const struct car *c = &g_cars[i];

        cam->f50 = 400;
        cam->dist = 400;
        cam->f40 = 420.0;
        cam->f48 = 450.0;
        cam->height = c->y + 1400.0;
        cam->heading = c->heading;
        cam->f58 = 200;
        cam->f5c = 164;
        cam->f60 = 25.0;
        cam->f38 = 0;
        cam->fc0 = 0;
        cam->x = cos(cam->heading) * cam->dist * -0.5 + c->x;
        cam->y = c->y;
        cam->z = sin(cam->heading) * cam->dist * -0.5 + c->z;
        cam->fa8 = c->heading;
        cam->fb0 = c->f110;
    }
    r_5532b8 = 0;
    r_553288 = 25600.0;
    r_5532c0 = 25600.0;
    r_6192f8 = 0;
    r_619318 = 0;
    r_619308 = 0;
    r_5532b0 = 0;
}

static void *pool_calloc(size_t size)
{
    void *p = mem_calloc(g_race_pool, (u32)size);

    g_race_bytes += (s32)size;
    if (p == NULL) {
        mem_out_of_memory();
    }
    return p;
}

/* 0x41ce20: objects the effects take as they need them. */
static void make_particles(void)
{
    int i;

    g_particle_objs = pool_calloc(700 * sizeof(*g_particle_objs));
    for (i = 0; i < 700; i++) {
        if (scene_obj_create(&g_particle_objs[i], g_particle_mesh, 0, 0, 0, -1, 0) == -1) {
            log_printf("Maximum of objects reached!\n");
            exit(1);
        }
    }
    r_5532f0 = 0;
}

/* 0x41cea0: rain on CANADA, snow on AUSTRIA, and USA's, each view
 * with nine objects whose meshes the weather fills each frame. */
static void make_weather(void)
{
    int v, k;

    r_563cf4 = 0;
    g_weather = 0;
    if (g_track == 0) {
        g_weather = 1;
    }
    if (g_track == 4) {
        g_weather = 2;
    }
    if (g_track == 1) {
        g_weather = 3;
    }
    if (g_weather == 1 || g_weather == 2) {
        g_weather_meshes[0] = pool_calloc(0x9c40);
        g_weather_meshes[1] = pool_calloc(0x9c40);
        g_race_bytes -= 2 * 0x9c40;
        for (v = 0; v < g_views; v++) {
            g_weather_meshes[v][0] = 0;
            g_weather_meshes[v][1] = 0;
        }
        g_weather_objs = pool_calloc((size_t)g_views * 9 * sizeof(*g_weather_objs));
        for (v = 0; v < g_views; v++) {
            if (v > 1) {
                log_printf("Error, does not support more than 2 cameras");
                exit(1);
            }
            for (k = 0; k < 9; k++) {
                struct scene_desc *d = &g_weather_objs[9 * v + k];

                if (scene_obj_create(d, g_weather_meshes[v], 2, 200, 0, 1000, 0) == -1) {
                    log_printf("Error while creating the weather");
                    exit(1);
                }
                d->x = (s32)g_cars[0].x;
                d->y = (s32)g_cars[0].y;
                d->z = (s32)g_cars[0].z;
                d->ax = d->ay = d->az = 0;
                scene_obj_link(d);
            }
        }
        r_563d28 = r_563d2c = r_563d38 = r_563d3c = 0;
        r_552f30 = r_552f34 = 0;
        r_525e30 = 0;
        r_525e38 = 0;
    }
    if (g_weather == 1 || g_weather == 2 || g_weather == 3) {
        for (v = 0; v < g_views; v++) {
            if (g_weather == 1) {
                g_weather_voices[v] = race_sound_3d(1, 0, 0, -1, 0, 0, 0, 0.0f, 0.0f);
            }
            if (g_weather == 3) {
                g_weather_voices[v] = race_sound_3d(1, 0, 2, -1, 0, 0, 0, 0.0f, 0.0f);
            }
        }
    }
}

/* 0x41cd70 */
void race_make_effects(void)
{
    int i;

    make_particles();
    g_part_objs = pool_calloc(200 * sizeof(*g_part_objs));
    for (i = 0; i < 200; i++) {
        g_effects[i].type = 0;
        if (scene_obj_create(&g_part_objs[i], g_part_meshes[i], 1, 0, 0, -1, 0) == -1) {
            log_printf("Error while creating the world");
            exit(1);
        }
    }
    make_weather();
}

/* 0x420090: an animation for each placement of type 250, over the
 * track's texture slot 0. */
static void race_animated_textures(void)
{
    int i;

    anim_tex_free();
    for (i = 0; i < g_level_plc[0]; i++) {
        const struct plc_entry *e = plc_at(g_level_plc, i);

        if (e->type == 250) {
            anim_tex_add(g_level_msh + e->mesh, 0);
        }
    }
}

/* 0x41f8f0: the cars' lap timing cleared, the track's best lap from
 * the settings. */
void race_clear_laps(void)
{
    int i;

    for (i = 0; i < g_driver_count; i++) {
        struct car *c = &g_cars[i];

        memcpy(&g_best_lap, g_race_cfg + 0x20f + 120 * g_track, sizeof(g_best_lap));
        c->f3b4 = 0;
        c->f3a4 = 0;
        c->f3bc = 0;
        c->f3c4 = 0;
        c->f3cc = 0;
    }
    r_525e48 = g_frame_time;
}

s32 r_619308, r_5532b0;
double r_5532b8;                            /* the replay camera's y */
double r_553288, r_5532c0, r_6192f8, r_619318;
s32 r_563d28, r_563d2c, r_563d38, r_563d3c, r_552f30, r_552f34;
double r_525e30;
double r_525e38;                            /* the second view's snow sway */
s32 r_525e48;

double g_race_clock;                        /* 0x5285d8 */
s32 g_view_car;                             /* 0x4949a4, the car the camera follows */
s32 g_champ_points[RACE_DRIVERS];           /* 0x563d00 */
s32 r_6192a0[10], r_6192c8[10];
s32 *g_ghost_buffer;                        /* 0x563d7c */
s32 r_553280, r_553070, r_5530a8, r_563c60, r_619310, r_563c38;
float r_525e2c;                             /* the wobbling lens's phase */
s32 r_527f40[11];                          /* 0x527f40, by car */
s32 r_5285d0, r_5db03c, r_5530e0, r_494990, r_563c04, r_552f58, r_54f97c, r_563ce0;
s32 r_5531b8, r_54f988, r_563da0, r_5daff0, r_601674, r_5531c0, r_5531c4;
s32 r_5532c8, r_563c64, r_552f4c, r_54f978, r_563d44, r_527f24, r_5530b8, r_552f54;
s32 r_54f9d0, r_5db038, r_6036f0[8], r_553010[8];
double r_54f980, r_54f948, r_54f9a0, r_552f68, r_563bd0, r_563d48;

/* 0x41fe60 */
void race_reset_state(void)
{
    int i;

    r_553280 = r_553070 = r_5530a8 = r_563c60 = 0;
    r_54f980 = -1.0;
    r_527f40[0] = r_527f40[1] = -1;
    g_race_clock = 0;
    r_619310 = g_track;
    r_563c38 = r_5285d0 = 0;
    r_54f948 = r_54f9a0 = 0;
    r_5db03c = 0;
    g_view_car = 0;
    r_5530e0 = r_494990 = r_563c04 = r_552f58 = 0;
    r_552f68 = 0;
    r_54f97c = r_563ce0 = r_5531b8 = r_54f988 = r_563da0 = r_5daff0 = 0;
    r_525e2c = 0;
    r_601674 = r_5531c0 = r_5531c4 = 0;
    r_563bd0 = r_563d48 = 0;
    r_5532c8 = 0;
    r_563c64 = -1;
    r_552f4c = 100;
    r_54f978 = 1;
    /* The championship's first race starts the points again. */
    if (g_race_mode == 0 && race_cfg(0x0f3) == 0) {
        for (i = 0; i < g_driver_count; i++) {
            g_champ_points[i] = 0;
        }
    }
    memset(r_6036f0, 0, sizeof(r_6036f0));
    memset(r_553010, 0, sizeof(r_553010));
    r_563d44 = 0;
    r_527f24 = g_driver_count;
    if (g_race_mode == 2) {
        r_5530b8 = 0;
        r_552f54 = 0;
        g_ghost_buffer = pool_calloc(0x49d40);
    }
    if (g_race_mode != 3) {
        r_54f9d0 = 0;
        r_5db038 = 0;
    } else {
        r_5db038 = 0;
        r_54f9d0 = g_driver_count - 1;
    }
    for (i = 0; i < 10; i++) {
        r_6192a0[i] = 100;
        r_6192c8[i] = 100;
    }
    /* Joysticks for the players whose control is 1 or 3. */
    if ((g_drivers[0].f08 == 1 || g_drivers[0].f08 == 3)
        && (g_drivers[1].f08 == 1 || g_drivers[1].f08 == 3)) {
        g_joysticks[0].enabled = 1;
        g_joysticks[1].enabled = 1;
    } else {
        g_joysticks[0].enabled = 1;
        g_joysticks[1].enabled = 0;
    }
}

s32 *r_552f24;                              /* 0x552f24, 0x258 bytes */
s32 r_498478, r_49847c;
s32 r_563c18, r_563d88, r_54f9a8, r_5532ac, r_563ce4;
double r_552e48;
s32 r_5531d0[0x2c];                         /* 0x5531d0..0x553280 */

/* 0x418c40 */
void race_load_world(void)
{
    char name[64];

    race_pos_deltas();
    race_load_ai_track();
    race_progress(0x4b);
    memset(r_5531d0, 0, sizeof(r_5531d0));
    race_make_effects();
    race_progress(0x4e);
    race_hud_sprites();
    race_progress(0x51);
    race_reset_state();
    race_progress(0x54);
    race_animated_textures();
    race_progress(0x5a);
    race_load_ghost();
    race_progress(0x5f);
    snprintf(name, sizeof(name), "LEVELS\\%s%s.SRF", g_track_dir, g_track_name);
    race_srf_load(name);
    race_progress(0x64);
    race_init_hud();
    r_552f24 = pool_calloc(0x258);
    r_563c18 = 0;
    r_563d88 = 0;
    if (g_race_net.mode != 2) {
        g_race_net.stage = 2;
        net_clear_answers();
    } else {
        g_race_net.stage = 4;
        r_552e48 = 0;
    }
    r_54f97c = 0;
    r_54f9a8 = 0;
    r_5532ac = 0;
    r_563ce4 = 0;
    if (g_race_net.stage == 4) {
        cd_read_status();
        if (g_cd_present == 1) {
            cd_read_tracks();
            cd_play_track(g_race_cd_track);
        }
    }
    race_frame_time();
    race_clear_laps();
    /* For the HUD's outline and the iris, which go through g_level_pan over
     * the clear back buffer (colour 0): what the track's palette and table
     * make of it. Colour 0's value, and what 0, 1, 2, 4, 8, 16, 32, 64, 128
     * and 255 shade colour 0 to. */
    if (g_level_col != NULL && g_level_pan != NULL) {
        static const int levels[10] = { 0, 1, 2, 4, 8, 16, 32, 64, 128, 255 };
        char line[160];
        int k, len;

        len = snprintf(line, sizeof(line), "race palette: track %d, colour 0 = %d %d %d, 1 = %d %d %d, pan[l][0] =",
                       g_track, g_level_col[8], g_level_col[9], g_level_col[10], g_level_col[11],
                       g_level_col[12], g_level_col[13]);
        for (k = 0; k < 10 && len < (int)sizeof(line) - 6; k++) {
            len += snprintf(line + len, sizeof(line) - len, " %d", g_level_pan[levels[k] << 8]);
        }
        log_printf("%s\
", line);
    }
    /* A port's test switch: the computer drives the player's car. Not
     * through the original's autopilot (f604), which it turns on after
     * the finish and which counts no laps, but as one of its drivers. */
    if (g_race_autopilot) {
        g_drivers[0].control = 1;
    }
}

int g_race_autopilot;

s32 g_view_w, g_view_h;                     /* 0x5285c4, 0x552f50 */

/* The view's share of the screen for each SCREEN SIZE (0x4949f0). */
static const double s_view_scale[4][2] = {
    { 1.0, 1.0 }, { 0.8, 1.0 }, { 0.6, 1.0 }, { 0.5, 0.83 }
};

/* 0x43e2a0: each driver's view: the screen's middle, or with two
 * players the right half for the first and the left for the second. */
void race_layout_views(void)
{
    int w = g_screen_width, h = g_screen_height, split = g_two_players && !r_552f58;
    int i;

    g_view_w = (s32)(w * s_view_scale[r_527f28][0] * (split ? 0.5 : 1.0));
    g_view_h = (s32)(h * s_view_scale[r_527f28][1]);
    /* The screen's scale from 320 x 200 in 16.16 (the effects' sizes).
     * A screen wider than 4:3 (NATIVE) scales as its 4:3 width, so its
     * sprites keep 320 x 200's shape. */
    r_552d80 = (s32)((w * 3 > h * 4 ? h * 4 / 3 : w) * 204.796875);
    r_552f5c = (s32)(h * 327.675);
    for (i = 0; i < g_driver_count; i++) {
        struct race_driver *d = &g_drivers[i];

        if (split) {
            d->view_x0 = w / 4 * 3 - g_view_w / 2 + 1;
            d->view_y0 = h / 2 - g_view_h / 2;
            d->view_x1 = w / 4 * 3 + g_view_w / 2;
            d->view_y1 = g_view_h / 2 + h / 2;
            g_race_cams[i].f58 = g_race_cams[0].f58;
            g_race_cams[i].f5c = g_race_cams[0].f5c;
            g_race_cams[i].f60 = g_race_cams[0].f60;
        } else {
            d->view_x0 = w / 2 - g_view_w / 2;
            d->view_y0 = h / 2 - g_view_h / 2;
            d->view_x1 = g_view_w / 2 + w / 2;
            d->view_y1 = g_view_h / 2 + h / 2;
        }
    }
    if (split) {
        struct race_driver *d = &g_drivers[1];

        d->view_x0 = w / 4 - g_view_w / 2;
        d->view_y0 = h / 2 - g_view_h / 2;
        d->view_x1 = g_view_w / 2 + w / 4 - 1;
        d->view_y1 = g_view_h / 2 + h / 2;
    }
    scene_set_screen(split ? w / 2 : w, h);
    r_498478 = r_552d80 * 500;
    r_49847c = r_552f5c * 500;
}

/* 0x43dea0: the race's video mode from RESOLUTION, the cameras' focal
 * lengths and near distance for it, and the views. The original also
 * darkens the palette for the rain here (0x445f20). */
/* NATIVE (a port addition): the window's size over a whole scale of 1
 * or 2, 2 once the window is at least twice 600 rows. It is never
 * smaller than 800 x 600's height where the window allows, nor larger
 * than the 2D buffers. */
static void native_screen(s32 *w, s32 *h)
{
    int dw = 0, dh = 0, scale;

    video_drawable_size(&dw, &dh);
    if (dw < 320 || dh < 200) {
        *w = 800;
        *h = 600;
        return;
    }
    scale = dh >= 1200 ? 2 : 1;
    *w = dw / scale;
    *h = dh / scale;
    if (*w > SCREEN_MAX_W) {
        *w = SCREEN_MAX_W;
    }
    if (*h > SCREEN_MAX_H) {
        *h = SCREEN_MAX_H;
    }
}

void race_init_hud(void)
{
    static const s32 modes[3][2] = { { 320, 200 }, { 640, 480 }, { 800, 600 } };
    static const s32 focal[3][2] = { { 200, 164 }, { 425, 425 }, { 550, 550 } };
    static const double near_d[3] = { 25.0, 10.0, 10.0 };
    int size = g_screen_size, i;
    s32 w = modes[size][0], h = modes[size][1], fx = focal[size][0], fy = focal[size][1];

    /* The loading screen's surface (NATIVE) is done with. */
    canvas_unregister(g_back_buffer);

    if (race_cfg(0x10b) == CANVAS_NATIVE && size == 2) {
        /* 800 x 600's sprites and focal length, the focal scaled to the
         * height so the view sees as much up and down. */
        native_screen(&w, &h);
        fx = fy = (s32)(550.0 * h / 600.0 + 0.5);
    }
    g_video_mode.width = w;
    g_video_mode.height = h;
    g_video_mode.bpp = 8;
    g_video_mode.flag = 1;
    if (!video_set_mode()) {
        snprintf(g_error_text, sizeof(g_error_text), "Cannot use this graphics mode.\n");
        g_fatal_error = 1;
        return;
    }
    g_screen_width = w;
    g_screen_height = h;
    for (i = 0; i < g_driver_count; i++) {
        g_race_cams[i].f58 = fx;
        g_race_cams[i].f5c = fy;
        g_race_cams[i].f60 = near_d[size];
    }
    video_set_palette(g_level_col + 8);
    set_target(g_back_buffer, g_screen_width, g_screen_width, g_screen_height, 8);
    memset(g_back_buffer, 0, (size_t)g_screen_width * g_screen_height);
    race_layout_views();
    race_frame_time();
}
