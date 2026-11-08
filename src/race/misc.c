/*
 * race/misc.c - the race frame's smaller steps: the hot keys and the
 * pause menu, the joystick, the Y/N box, the network's messages, the
 * keyframed track objects, the ground effects' dispatch, the ghost
 * (loading it and playing it back), the iris and the end of race save.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "anim_tex.h"
#include "canvas.h"
#include "cd.h"
#include "game.h"
#include "gl3d.h"
#include "input.h"
#include "lisa/file.h"
#include "lisa/font.h"
#include "lisa/gfx.h"
#include "lisa/log.h"
#include "lisa/mem.h"
#include "lisa/sprite.h"
#include "menu/pages.h"
#include "menu/panels.h"
#include "platform/touch.h"
#include "race/race.h"
#include "race/fx.h"
#include "race/camera.h"
#include "race/cars.h"
#include "race/ai.h"
#include "race/contact.h"
#include "race/control.h"
#include "race/data.h"
#include "race/frame.h"
#include "race/hud.h"
#include "race/init.h"
#include "race/misc.h"
#include "race/phys.h"
#include "scene.h"
#include "../sound.h"
#include "startup.h"
#include "stubs.h"
#include "video.h"

extern s32 r_563c04, r_54f988, r_54f978, r_563c18, r_5532ac, r_563ce4, r_54f9a8;
extern s32 r_6036f0[8], r_552f54, r_563c60, r_5530a8, r_54f9d0, r_5db038, r_5daff8;
extern s32 *g_ghost_buffer;
extern s32 r_619310;                     /* the furthest track reached, at the start */
extern s32 g_race_5285c8;               /* 0x5285c8: the Y/N box is up */

s32 r_49499c;                           /* the joystick's gear button held: 1 up, 2 down */
s32 r_4949cc, r_4949d0;                 /* the joystick's menu push: x, y */
s32 r_4949d4, r_4949d8;                 /* and the last frame's */

struct scene_desc *g_ghost_objs;        /* 0x525e40: the body and 4 wheels */
s32 g_ghost_car;                        /* 0x563d30, the ghost's car model */
char g_ghost_driver[0x20];              /* 0x601680 */
static s32 s_ghost_mesh[2000];          /* 0x6016a0 */
static s32 s_ghost_wheels[4][0x208];    /* 0x525ea0 */

static int language(void)
{
    return race_cfg(0x593);
}

static void cfg_set(int offset, s32 v)
{
    memcpy(g_race_cfg + offset, &v, sizeof(v));
}

static const char *pause_text(int item)
{
    return g_pause_text[language() * 7 + item];
}

static void clear_back_buffer(void)
{
    memset(g_back_buffer, 0, (size_t)g_screen_width * g_screen_height);
}

/* Dwords in a mesh: two counts, the vertices, then 11 a face. */
static int mesh_dwords(const s32 *m)
{
    return m[0] * 3 + m[1] * 11 + 2;
}

/* 0x42bc20: the ground effects under car r_5285c0 (race/fx.c). */
void race_ground_effects(void)
{
    const struct car *c = &g_cars[r_5285c0];

    if (c->f354 != 0 || c->f358 != 0 || c->f35c != 0) {
        return;
    }
    race_skids();
    race_spray();
    race_landing_dust();
    race_dust_trail();
}

/* 0x417d20: what the network says: the host quit (0x64) or restarts
 * (0x65), or player r_54f9a8 quit (0x66), who is then out. */
void race_net_messages(void)
{
    char text[100];
    int lang = language();

    if (r_54f97c == 0x64 && r_5532ac == 0) {
        race_message(g_net_host_quit[lang], 80, 8, -1);
        r_5532ac = 1;
    }
    if (r_54f97c == 0x65 && r_563ce4 == 0) {
        race_message(g_net_host_restart[lang], 80, 8, -1);
        r_563ce4 = 1;
    }
    if (r_54f97c == 0x66 && r_5532ac == 0 && r_6036f0[r_54f9a8] == 0) {
        snprintf(text, sizeof(text), g_net_player_quit[lang], g_drivers[r_54f9a8].name);
        race_message(text, 80, 8, -1);
        g_cars[r_54f9a8].f528 = 1;
        g_cars[r_54f9a8].laps = 0;
        if (r_54f9a8 == 0) {
            r_5532ac = 1;
        }
        r_6036f0[r_54f9a8] = 1;
    }
}

/* 0x4357a0
 * The keyframed placements: each with keys in g_level_pos steps a key
 * a tick, moving by the key's delta and taking its angles. The keys
 * are at pos[pos[i] + i]: a count, the current key, then six dwords a
 * key (dx, dy, dz subtracted, then the angles in tenths of a degree). */
void race_animate_objects(void)
{
    int i;

    for (i = 0; i < g_level_plc[0]; i++) {
        struct scene_desc *d = &g_world[i];
        s32 *k, *f;

        if (g_level_pos[i] == -1 || d->in_grid != 1) {
            continue;
        }
        k = &g_level_pos[g_level_pos[i] + i];
        if (k[1] == k[0]) {
            k[1] = 0;
        }
        f = &k[2 + k[1] * 6];
        d->x -= f[0];
        d->y -= f[1];
        d->z -= f[2];
        d->ax = f[3];
        d->ay = f[4];
        d->az = f[5];
        if (d->ax >= 3600) {
            d->ax -= 3600;
        }
        if (d->ay >= 3600) {
            d->ay -= 3600;
        }
        if (d->az >= 3600) {
            d->az -= 3600;
        }
        if (scene_obj_move(d) != 0) {
            log_printf("FEL VID LI_MOVEOBJECT ANIM OBJ\n");
        }
        k[1]++;
    }
}

/* 0x434580
 * Whether the player is in a stretch of track where the weather stops
 * (under cover): rain on track 0 between segments 45 and 120, the third
 * weather on track 1 within 30 of the start, snow on track 4 between 28
 * and 100. r_552f30 is the first player's, r_552f34 the second's. */
void race_weather_zone(void)
{
    int i = r_5285c0;
    s32 seg = g_cars[i].f364, a = seg < 0 ? -seg : seg;
    int in;

    if (g_weather == 1 && g_track == 0) {
        in = seg > 45 && seg < 120;
    } else if (g_weather == 3 && g_track == 1) {
        in = a > 0 && a < 30;
    } else if (g_weather == 2 && g_track == 4) {
        in = a > 28 && a < 100;
    } else {
        return;
    }
    if (i == 0) {
        r_552f30 = in;
    } else if (i == 1 && g_two_players == 1) {
        r_552f34 = in;
    }
}

/* A ghost mesh: the car's or a wheel's, its faces made see-through
 * (type 20, shade 1). Faces of 11 dwords become 5, in place. */
static void ghostify(s32 *m)
{
    s32 *faces = m + 2 + m[0] * 3;
    int k;

    for (k = 0; k < m[1]; k++) {
        const s32 *src = faces + 11 * k;
        s32 *dst = faces + 5 * k;
        s32 a = src[1], b = src[2], c = src[3];

        dst[0] = 20;
        dst[1] = a;
        dst[2] = b;
        dst[3] = c;
        dst[4] = 1;
    }
}

static void ghost_pose(struct scene_desc *d, const struct car *c)
{
    d->x = (s32)c->x;
    d->y = (s32)c->y;
    d->z = (s32)c->z;
    d->ax = (s32)(c->f108 * 572.957795130822);
    d->ay = (s32)(c->heading * 572.957795130822);
    d->az = (s32)(c->f110 * 572.957795130822);
}

/* 0x420240
 * The ghost for a time trial: GHOSTS\<track>.GST holds the car (s16),
 * the track (s16), the time in hundredths (s16), the driver's name from
 * +6, and 21600 steps of seven s16 from +10. The ghost's car is drawn
 * see-through. A ghost of another track is not used, and recording is
 * off. */
void race_load_ghost(void)
{
    int pool = mem_pool_create(0);
    u8 *file = mem_calloc(pool, 0x49d4c);
    char name[64];
    int w, k;

    if (g_race_mode != 2) {
        mem_pool_destroy(pool);
        return;
    }
    if (r_553068 == 1) {
        snprintf(name, sizeof(name), "GHOSTS\\%s.GST", g_ghost_name);
        if (file_read_at(name, file, 0x49d4c, 0) != 1) {
            r_553068 = 0;
            r_525e44 = 3000000;
        }
    }
    if (r_553068 != 1) {
        mem_pool_destroy(pool);
        return;
    }
    {
        s16 v[3];

        memcpy(v, file, sizeof(v));
        g_ghost_car = v[0];
        r_525e44 = v[2];
        /* sprintf("%s") in the original, unbounded. */
        snprintf(g_ghost_driver, sizeof(g_ghost_driver), "%s", (const char *)file + 6);
        if (v[1] != g_track) {
            r_5530b8 = 1;
            mem_pool_destroy(pool);
            return;
        }
    }
    memcpy(g_ghost_buffer, file + 10, 0x49d40);
    g_ghost_objs = mem_calloc(g_race_pool, 0xa0 / 0x20 * sizeof(*g_ghost_objs));
    g_race_bytes += 0xa0;
    if (g_ghost_objs == NULL) {
        mem_out_of_memory();
    }
    ghost_pose(&g_ghost_objs[0], &g_cars[0]);
    {
        const s32 *m = g_car_meshes[g_ghost_car];

        if (mesh_dwords(m) > 2000) {
            log_printf("TO LITTLE CAR MSH MEMORY!!\n");
            exit(1);
        }
        memcpy(s_ghost_mesh, m, (size_t)mesh_dwords(m) * sizeof(s32));
        ghostify(s_ghost_mesh);
    }
    if (scene_obj_create(&g_ghost_objs[0], s_ghost_mesh, 0, 110, 0, -1, 0) != 0) {
        log_printf("Error while creating ghostcar\n");
        exit(1);
    }
    for (w = 0; w < 4; w++) {
        const s32 *m = g_wheel_meshes[g_ghost_car * 4 + w];
        s32 *dst = s_ghost_wheels[w];

        if (mesh_dwords(m) > 0x208) {
            log_printf("TO LITTLE WHEEL MSH MEMORY!!\n");
            exit(1);
        }
        memcpy(dst, m, (size_t)mesh_dwords(m) * sizeof(s32));
        for (k = 0; k < m[0]; k++) {
            dst[2 + 3 * k] += g_wheel_dx[g_ghost_car * 4 + w];
            dst[4 + 3 * k] += g_wheel_dz[g_ghost_car * 4 + w];
        }
        ghostify(dst);
        ghost_pose(&g_ghost_objs[1 + w], &g_cars[0]);
        if (scene_obj_create(&g_ghost_objs[1 + w], dst, 1, 200, 0, -1, 0) != 0) {
            log_printf("Error while creating ghost wheel\n");
            exit(1);
        }
    }
    mem_pool_destroy(pool);
}

/* 0x445940
 * The ghost at the recording's step: its five objects moved there, the
 * car's faces pulsing between shades 46 and 52. */
void race_play_ghost(void)
{
    const s16 *f;
    s32 x, y, z, ax, ay, az, shade;
    int k, w;

    if (r_563c60 == 1 || g_ghost_objs == NULL) {
        return;
    }
    f = (const s16 *)g_ghost_buffer + r_552f54 * 7;
    x = (s32)(double)(f[0] + 25600);
    y = f[1];
    z = (s32)(double)(f[2] + 25600);
    ax = (s32)(f[3] * 0.0009765625 * 57.295779513082195 * 10.0);
    ay = (s32)(f[4] * 0.0009765625 * 57.295779513082195 * 10.0);
    az = (s32)(f[5] * 0.0009765625 * 57.295779513082195 * 10.0);
    shade = (s32)(sin(g_race_clock * 0.3333333333333333) * 3.0) + 49;
    for (k = 0; k < s_ghost_mesh[1]; k++) {
        s_ghost_mesh[2 + 3 * s_ghost_mesh[0] + 5 * k + 4] = shade;
    }
    shade = (s32)(sin((g_race_clock + 9.0) * 0.3333333333333333) * 3.0) + 49;
    for (w = 0; w < 4; w++) {
        const s32 *m = g_wheel_meshes[g_ghost_car * 4 + w];

        for (k = 0; k < m[1]; k++) {
            s_ghost_wheels[w][2 + 3 * m[0] + 5 * k + 4] = shade;
        }
    }
    for (k = 0; k < 5; k++) {
        struct scene_desc *d = &g_ghost_objs[k];

        d->x = x;
        d->y = y;
        d->z = z;
        d->ax = ax;
        d->ay = ay;
        d->az = az;
        if (d->in_grid == 0) {
            scene_obj_link(d);
        } else if (scene_obj_move(d) != 0) {
            log_printf(k == 0 ? "FEL VID LI_MOVEOBJECT HANDLE GHOST\n"
                              : "FEL VID LI_MOVEOBJECT HANDLE GHOST WHEEL\n");
        }
    }
}

/* g_cone_table[x * 800 + 400 + c] (0x4200e0), worked out so a screen
 * wider than the table's 800 columns, or a circle wider than its 405
 * rows, has the same edge: 0 well inside the circle, then 1 .. 5 over
 * its last pixels, and 20 outside. */
static u8 cone_level(int x, int c)
{
    int k;

    for (k = 5; k >= 0; k--) {
        if (c >= k - x && c < x - k) {
            return (u8)(5 - k);
        }
    }
    return 20;
}

/* 0x438210
 * The iris: a circle of radius step * 4 (of 320) about the screen's
 * middle; outside it the back buffer goes through MENU.TAB's darker
 * rows, from the cone table. The 3D under the back buffer's colour 0
 * is darkened by the shade of the level (lisa/gfx.h). */
void race_iris(int step)
{
    int s = g_screen_width / 320, w = g_screen_width, h = g_screen_height;
    int r = (s32)(step * 4 * s), y = (s32)(-100.0 * s);
    double end = s * 300.0;

    for (; y < end; y++) {
        int half = h / 2, x = 0, col;

        if (half - r < y && half + r > y) {
            double a = asin(((double)(r - half + y) / (r * 2.0) - 0.5) * 2.0);

            x = (s32)(cos(a) * w / h * r * 0.75);
        }
        if (x < 0) {
            x = 0;
        }
        if (x > w / 2 + 4 || y < 0 || y >= h) {
            continue;
        }
        /* With a layered 3D the iris is the rows of the table over it
         * too, for the clear pixels of the screen and the columns the 3D
         * has past it (lisa/gfx.h). */
        for (col = -(g_gfx_shade != NULL ? g_gfx_shade_ox : 0);
             col < w + (g_gfx_shade != NULL ? g_gfx_shade_ox : 0); col++) {
            u8 level = cone_level(x, col - w / 2);

            if (col < 0 || col >= w) {
                gfx_shade_set(col, y, level, 1);
            } else {
                u8 *p = &g_back_buffer[y * w + col];

                if (g_gfx_shade != NULL && *p == 0) {
                    gfx_shade_set(col, y, level, 1);
                } else {
                    *p = g_level_pan[level << 8 | *p];
                }
            }
        }
    }
}

/* 0x4225d0: the grid shuffled for the next race (not in a championship,
 * and only off the network), and the settings saved. */
static void race_save_settings(void)
{
    int i;

    if (g_race_mode != 0 && g_driver_count > 1 && g_race_net.mode == 2) {
        for (i = 0; i < g_driver_count; i++) {
            int a = (s32)(race_rand() * g_driver_count);
            int b = (s32)(race_rand() * g_driver_count);
            s32 t = race_cfg(0x133 + 4 * a);

            cfg_set(0x133 + 4 * a, race_cfg(0x133 + 4 * b));
            cfg_set(0x133 + 4 * b, t);
        }
    }
    cfg_set(0x107, r_563c6c);
    /* The port's RESOLUTION has a fourth value, NATIVE (3), that the
     * race plays as 2: it is kept. */
    if (!(race_cfg(0x10b) > 2 && g_screen_size == 2)) {
        cfg_set(0x10b, g_screen_size);
    }
    file_write("ign_win.btz", g_race_cfg, 0x597);
}

/* 0x4222b0
 * The race's end: a time trial faster than the ghost saves the player
 * as the new ghost (the rest of the recording repeating its last step);
 * a championship race gives the points and the next grid, the worst
 * placed in front. */
void race_save_results(void)
{
    s16 *g = (s16 *)g_ghost_buffer;
    int i;

    if (g_race_mode == 2 && r_5530b8 == 0 && (double)r_525e44 * 0.01 > g_cars[0].f3a4) {
        int pool, frame = r_552f54;
        u8 *file;
        s16 v[3];
        char name[64];

        /* The original reads before the recording when it is empty. */
        if (frame > 0 && frame < 21600) {
            for (i = frame; i < 21600; i++) {
                /* Six of the seven: the progress is left as it was. */
                memcpy(&g[i * 7], &g[(frame - 1) * 7], 6 * sizeof(s16));
            }
        }
        pool = mem_pool_create(0);
        file = mem_calloc(pool, 0x49d4c);
        v[0] = (s16)g_drivers[0].car;
        v[1] = (s16)g_track;
        v[2] = (s16)(s32)(g_cars[0].f3a4 * 100.0);
        memcpy(file, v, sizeof(v));
        snprintf((char *)file + 6, 0x49d4c - 6, "%s", g_drivers[0].name);
        memcpy(file + 10, g, 0x49d40);
        snprintf(name, sizeof(name), "GHOSTS\\%s.GST", g_ghost_name);
        if (file_write(name, file, 0x49d4c) == 0) {
            /* The original exits here. */
            log_printf("Error while saving ghostcar!\n");
        }
        mem_pool_destroy(pool);
    }
    if (g_race_mode == 0 && r_619310 > g_track) {
        s32 done = race_cfg(0xf3) + 1;

        cfg_set(0xf3, done > 6 ? 6 : done);
        for (i = 0; i < g_driver_count; i++) {
            g_champ_points[i] += g_place_points[g_cars[i].f3a0];
        }
        for (i = 0; i < g_driver_count; i++) {
            cfg_set(0x133 + 4 * i, 6 - g_cars[i].f3a0);
        }
    }
    race_save_settings();
}

/* The pause menu's restart is on: off the network (or hosting), in a
 * race that is not being left, with restarts left, and before the last
 * of the results. */
static int restart_allowed(void)
{
    return (g_race_net.mode == 2 || g_race_net.mode == 3) && r_563da0 != 1 && r_5daff0 != 1
        && g_race_tries > 0 && r_5530a8 <= 5;
}

/* 320 x 480 of the race buffer, for the boxes over the screen. */
static u8 *box_buffer(void)
{
    u8 *b = g_race_buffer + 0x25800;

    set_target(b, 320, 320, 480, 8);
    g_gfx_color_table = g_identity_table;
    memset(b, 0, 320 * 100);
    return b;
}

/* NATIVE's race screen is the window at half scale (or whole), the
 * menus' 320 x 200 at twice that, 1.2 times as tall: the boxes are
 * blown up to the menus' size, each pixel to 2 x 2.4. */
int race_box_scaled(void)
{
    return g_screen_width > 800 || g_screen_height > 600;
}

static void box_done(u8 *b, int rows, int dy)
{
    if (race_box_scaled()) {
        int w = 640, h = (int)(rows * 2.4 + 0.5), x0 = g_screen_width / 2 - 320;
        int y0 = g_screen_height / 2 - (int)(dy * 2.4 + 0.5), x, y;

        for (y = 0; y < h; y++) {
            const u8 *src = b + (int)(y / 2.4) * 320;
            u8 *dst;

            if (y0 + y < 0 || y0 + y >= g_screen_height) {
                continue;
            }
            dst = g_back_buffer + (y0 + y) * g_screen_width;
            for (x = 0; x < w; x++) {
                if (x0 + x >= 0 && x0 + x < g_screen_width) {
                    u8 sp = src[x / 2];

                    /* Over a layered 3D's clear buffer: its pixel's row
                     * of the table, on the GPU (lisa/gfx.h). */
                    if (g_gfx_shade != NULL && g_gfx_shade_table == g_level_pan
                        && dst[x0 + x] == 0 && sp != 0) {
                        gfx_shade_set(x0 + x, y0 + y, sp, 0);
                    } else {
                        dst[x0 + x] = g_level_pan[sp << 8 | dst[x0 + x]];
                    }
                }
            }
        }
    } else {
        blend_rect(b, 0, 0, 320, rows, g_back_buffer, g_screen_width / 2 - 160,
                   g_screen_height / 2 - dy, g_level_pan, 320, g_screen_width);
    }
    set_target(g_back_buffer, g_screen_width, g_screen_width, g_screen_height, 8);
    g_gfx_color_table = g_level_pan;
}

/* 0x4383b0
 * The pause menu: CONTINUE, RESTART, QUIT and the CD track, the chosen
 * one in the bright font, RESTART greyed when it is off; in a
 * championship a mark for each restart left. Drawn on panels in a
 * buffer, then blended over the screen's middle. */
void race_pause_menu(void)
{
    u8 *b = box_buffer();
    int on = restart_allowed(), lit = on ? 1 : 4, dim = on ? 0 : 4;
    int item = r_563c04, cd_w, val_w, k;
    const int bright = g_fonts_menu[1], plain = g_fonts_menu[0], value = g_fonts_menu[3];
    char text[16];
    const char *cd;

    panel_add(0x39, 0, text_width(pause_text(0), bright), 0, 0);
    panel_add(0x39, 1, text_width(pause_text(1), bright), 0, 0);
    panel_add(0x39, 2, text_width(pause_text(2), bright), 0, 0);
    cd_w = text_width(pause_text(3), bright);
    panel_add(0x39, 3, cd_w, 0, 0);
    panels_draw(b, 320);
    panels_clear();
    {
        s32 setting = race_cfg(0xf7);

        if (setting == 0) {
            cd = pause_text(5);
        } else if (setting == -1) {
            cd = pause_text(6);
        } else {
            /* The original measures a fixed "00". */
            cd = "00";
        }
        val_w = text_width(cd, value);
        if (setting == 0) {
            snprintf(text, sizeof(text), "%s", pause_text(5));
        } else if (setting == -1) {
            snprintf(text, sizeof(text), "%s", pause_text(6));
        } else {
            snprintf(text, sizeof(text), "%d", g_race_cd_track);
        }
    }
    panel_add(cd_w + 0x57, 3, val_w, 0, 1);
    panels_draw(b, 320);
    panels_clear();
    text_draw(pause_text(0), item == 0 ? bright : plain, 0x46, 0x0c);
    text_draw(pause_text(1), item == 1 ? g_fonts_menu[lit] : g_fonts_menu[dim], 0x46, 0x1f);
    text_draw(pause_text(2), item == 2 ? bright : plain, 0x46, 0x32);
    text_draw(pause_text(3), item == 3 ? bright : plain, 0x46, 0x45);
    g_fonts[value].align = FONT_ALIGN_CENTER;
    text_draw(text, value, val_w / 2 + cd_w + 100, 0x45);
    g_fonts[value].align = FONT_ALIGN_LEFT;
    if (g_race_mode == 0) {
        int w = text_width(pause_text(1), bright);

        for (k = 0; k < g_race_tries; k++) {
            struct sprite_pos pos;

            pos.x = (w + 0x67 + 0x1a * k) << 8;
            pos.y = 0x1b00;
            sprite_draw(g_race_5287e8, &pos, NULL);
        }
    }
    box_done(b, 100, 50);
}

/* 0x438880: the Y/N box, RESTART? or QUIT?. */
void race_ask_box(void)
{
    u8 *b = box_buffer();
    const int font = g_fonts_menu[1];
    const char *text = NULL;
    int w = 0;

    if (r_601674 == 2) {
        text = g_ask_restart[language()];
    } else if (r_601674 == 1) {
        text = g_ask_quit[language()];
    }
    if (text != NULL) {
        w = text_width(text, font);
    }
    panel_add(0x94 - w / 2, 0, w, 0, 0);
    panels_draw(b, 320);
    panels_clear();
    g_fonts[font].align = FONT_ALIGN_CENTER;
    if (text != NULL) {
        text_draw(text, font, 0xa0, 0x0c);
    }
    g_fonts[font].align = FONT_ALIGN_LEFT;
    box_done(b, 0x22, 15);
}

/* The CD track the setting picks: the track's own, a random one, or a
 * number. */
static void cd_pick(void)
{
    s32 setting = race_cfg(0xf7);

    if (setting == 0) {
        g_race_cd_track = g_track_cd[g_track];
        if (g_race_cd_track > g_cd_track_count) {
            g_race_cd_track = g_cd_track_count;
        }
    } else if (setting == -1) {
        g_race_cd_track = (s32)(race_rand() * g_cd_track_count + 1.0);
    } else {
        g_race_cd_track = setting;
    }
    cd_play_track(g_race_cd_track);
}

static void cd_prev(void)
{
    cd_read_status();
    if (g_cd_present != 1) {
        return;
    }
    cd_read_tracks();
    if (race_cfg(0xf7) > -1) {
        cfg_set(0xf7, race_cfg(0xf7) - 1);
    }
    cd_pick();
}

static void cd_next(void)
{
    cd_read_status();
    if (g_cd_present != 1) {
        return;
    }
    cd_read_tracks();
    if (race_cfg(0xf7) < g_cd_track_count) {
        cfg_set(0xf7, race_cfg(0xf7) + 1);
    }
    cd_pick();
}

/* P and Esc: pause or go on. */
static void toggle_pause(void)
{
    r_494990 ^= 1;
    if (r_494990 != 0 && r_5daff8 == 1 && g_race_net.mode == 2) {
        sound_stop_all();
    }
    race_frame_time();
}

/* A key going down this frame, once: r_563c18 holds the last. */
static int edge(int key, int *any)
{
    if (input_key_down(key) != 1) {
        return 0;
    }
    *any = 1;
    return r_563c18 != key;
}

/* Enter. */
static void enter_key(void)
{
    if (r_494990 == 1) {
        if (r_563c04 == 0) {
            r_494990 = 0;
            if (r_527f28 != 0) {
                clear_back_buffer();
            }
        } else if (r_563c04 == 1) {
            if (g_race_mode != 0 || g_race_tries > 0 || r_563da0 != 0 || r_5daff0 != 0) {
                r_601674 = 2;
                g_race_5285c8 = 1;
            }
        } else if (r_563c04 == 2) {
            r_601674 = 1;
            g_race_5285c8 = 1;
        }
        return;
    }
    /* The results' stages. */
    if (r_5530a8 == 8) {
        g_race_again = 0;
        r_563c60 = 1;
    } else if (r_5530a8 == 7 && r_563c60 == 0 && g_race_mode == 0 && r_601674 == 0) {
        if (r_563d68 == 1) {
            if (g_race_tries > 0 && r_54f988 == 0) {
                if (r_563da0 == 0) {
                    g_race_again = 1;
                    r_563da0 = 1;
                    g_race_tries--;
                }
            } else {
                g_race_again = 0;
                r_563c60 = 1;
            }
        } else {
            g_race_again = 0;
            r_563c60 = 1;
        }
    } else if (r_5530a8 == 5 && r_563c60 == 0 && g_race_mode == 0) {
        if (r_563d68 == 1) {
            g_race_again = 1;
            r_563c60 = 1;
        } else {
            r_5530a8 = g_two_players == 0 ? 6 : 8;
        }
    } else if (r_5530a8 == 3 && g_race_mode == 0) {
        if (g_cars[0].f3a0 >= 4 && (g_two_players != 1 || g_cars[1].f3a0 >= 4)) {
            r_5530a8 = 6;
        } else {
            int i;

            r_553070 = 0;
            r_54f9d0 = g_driver_count - 1;
            r_563d20++;
            r_5530a8 = 4;
            r_5db038 = 0x13f;
            if (r_563d20 > 3) {
                r_563d20 = 0;
            }
            race_positions();
            for (i = 0; i < g_driver_count; i++) {
                if (g_cars[i].f528 == 0) {
                    g_cars[i].f528 = 1;
                    g_cars[i].f3a0 = g_cars[i].grid;
                }
            }
        }
    } else if (r_5530a8 == 1 && g_race_mode == 0) {
        r_553070 = 0;
        r_5530a8 = 2;
        r_553280 = 6;
        if (++r_563d20 > 3) {
            r_563d20 = 0;
        }
    }
}

/* The pause menu's choice moves; RESTART is skipped when it is off,
 * and on a network client always. */
static void menu_up(void)
{
    if (--r_563c04 < 0) {
        r_563c04 = 3;
    }
    if (g_race_net.mode == 4) {
        if (r_563c04 == 1) {
            r_563c04 = 0;
        }
        return;
    }
    if (r_563c04 == 1 && !(r_563da0 != 1 && r_5daff0 != 1 && g_race_tries > 0 && r_5530a8 <= 5)) {
        r_563c04 = 0;
    }
}

static void menu_down(void)
{
    if (++r_563c04 > 3) {
        r_563c04 = 0;
    }
    if (g_race_net.mode == 4) {
        if (r_563c04 == 1) {
            r_563c04 = 2;
        }
        return;
    }
    if (r_563c04 == 1 && !(r_563da0 != 1 && r_5daff0 != 1 && g_race_tries > 0 && r_5530a8 <= 5)) {
        r_563c04 = 2;
    }
}

/* The Y key in each language: Y, J, S, S, J, O. */
static int yes_key(void)
{
    static const u8 keys[6] = { 0x15, 0x24, 0x1f, 0x1f, 0x24, 0x18 };
    int lang = language();

    return lang >= 0 && lang < 6 && input_key_down(keys[lang]) == 1;
}

/* The Y/N box's answer. */
static void answer(int *any)
{
    if (yes_key()) {
        if (g_race_net.stage > 1 && g_race_net.stage < 4) {
            r_563d88 = 1;
        }
        if (r_601674 == 0) {
            *any = 1;
            if (g_race_net.mode != 3) {
                g_race_again = 1;
                r_563c60 = 1;
            } else {
                r_54f97c = 0x65;
                r_563ce4 = 1;
            }
        } else if (r_601674 == 2) {
            g_race_again = 1;
            if (g_race_net.mode == 3) {
                r_54f97c = 0x65;
            }
            if (g_race_mode == 0) {
                r_5daff0 = 1;
                g_race_tries--;
            } else if (g_race_net.mode != 3) {
                r_563d88 = 1;
            }
        } else if (r_601674 == 1) {
            g_race_again = 0;
            if (g_race_net.mode == 3) {
                r_54f97c = 0x64;
            } else if (g_race_net.mode == 4) {
                r_54f97c = 0x66;
                r_54f9a8 = 0;
            } else {
                r_563d88 = 1;
            }
        }
    }
    /* N: latched by r_563c18 but never sets it, and only some of its
     * branches count as a key. */
    if (input_key_down(0x31) == 1 && r_563c18 != 0x31) {
        if (r_601674 == 0) {
            *any = 1;
            if (g_race_net.mode != 3) {
                g_race_again = 2;
                r_563c60 = 1;
            } else {
                r_54f97c = 0x64;
                r_5532ac = 1;
            }
        } else {
            r_601674 = 0;
            g_race_5285c8 = 0;
        }
    }
}

/* 0x420eb0
 * The race's keys: P and Esc pause, F12 a screenshot, Num+ and Num-
 * the screen size, = and - the view's size (before the finish), Enter
 * the pause menu's choice or the results' next stage, Tab the camera at
 * the finish, ` the results overlay; in the pause menu Up, Down and on
 * the CD track Left and Right; on the results' retry Up and Down; and
 * the Y/N box's Y and N. A key acts once until it is let go. */
void race_hot_keys(void)
{
    int any = 0;

    if (edge(0x19, &any)) {
        toggle_pause();
        r_563c04 = 0;
        r_601674 = 0;
        if (r_494990 == 0 && r_527f28 != 0) {
            clear_back_buffer();
        }
    }
    if (input_key_down(0x19) == 1) {
        r_563c18 = 0x19;
    }
    if (edge(0x58, &any)) {
        r_563c18 = 0x58;
        STUB(0x446040, "race_screenshot");
    }
    if (edge(0x4e, &any)) {
        r_563c18 = 0x4e;
        if (++g_screen_size > 2) {
            g_screen_size = 2;
        } else {
            race_init_hud();
            race_hud_layout();
            race_hud_recut();
        }
    }
    if (edge(0x4a, &any)) {
        r_563c18 = 0x4a;
        if (--g_screen_size < 0) {
            g_screen_size = 0;
        } else {
            race_init_hud();
            race_hud_layout();
            race_hud_recut();
        }
    }
    if (edge(0x0d, &any) && r_54f980 == -1.0) {
        r_563c18 = 0x0d;
        if (--r_527f28 < 0) {
            r_527f28 = 0;
        } else {
            race_layout_views();
            race_hud_layout();
            race_hud_recut();
        }
        r_563c6c = r_527f28;
    }
    if (edge(0x0c, &any) && r_54f980 == -1.0) {
        r_563c18 = 0x0c;
        if (++r_527f28 > 3) {
            r_527f28 = 3;
        } else {
            race_layout_views();
            race_hud_layout();
            race_hud_recut();
        }
        r_563c6c = r_527f28;
    }
    if (input_key_down(0x01) == 1) {
        any = 1;
        if (r_601674 == 0) {
            if (g_race_5285c8 == 1) {
                g_race_again = 2;
                r_563c60 = 1;
            } else if (r_563c18 != 1) {
                toggle_pause();
                r_601674 = 0;
                r_563c04 = 0;
                if (r_494990 == 0 && r_527f28 != 0) {
                    clear_back_buffer();
                }
            }
        } else {
            r_601674 = 0;
            g_race_5285c8 = 0;
        }
        r_563c18 = 1;
    }
    if ((input_key_down(0x1c) == 1 || input_key_down(0x9c) == 1)) {
        any = 1;
        if (r_563c18 != 0x1c) {
            r_563c18 = 0x1c;
            enter_key();
        }
    }
    if (edge(0x0f, &any)) {
        r_563c18 = 0x0f;
        if (r_563c60 == 0 && ++r_563d20 > 3) {
            r_563d20 = 0;
        }
    }
    if (edge(0x29, &any)) {
        r_563c18 = 0x29;
        r_54f978 ^= 1;
    }
    if (r_494990 == 1 && r_601674 == 0) {
        if (input_key_down(0xc8) == 1) {
            any = 1;
            if (r_563c18 != 0xc8) {
                r_563c18 = 0xc8;
                menu_up();
            }
        } else if (input_key_down(0xd0) == 1) {
            any = 1;
            if (r_563c18 != 0xd0) {
                r_563c18 = 0xd0;
                menu_down();
            }
        } else if (input_key_down(0xcb) == 1) {
            any = 1;
            if (r_563c18 != 0xcb) {
                r_563c18 = 0xcb;
                if (r_563c04 == 3) {
                    cd_prev();
                }
            }
        } else if (input_key_down(0xcd) == 1) {
            any = 1;
            if (r_563c18 != 0xcd) {
                r_563c18 = 0xcd;
                if (r_563c04 == 3) {
                    cd_next();
                }
            }
        }
    }
    if (r_5530a8 == 7 && r_601674 == 0 && r_494990 == 0 && r_563c60 == 0 && g_race_mode == 0) {
        if (edge(0xc8, &any)) {
            r_563c18 = 0xc8;
            if (--r_54f988 < 0) {
                r_54f988 = 1;
            }
        }
        if (edge(0xd0, &any)) {
            r_563c18 = 0xd0;
            if (++r_54f988 > 1) {
                r_54f988 = 0;
            }
        }
    }
    if (g_race_5285c8 == 1) {
        answer(&any);
    }
    if (!any) {
        r_563c18 = 0;
    }
}

/* The joystick's push this frame, for the menus: -1, 0 or 1 a way,
 * kept until the stick is back in the middle. */
static void joy_menu_push(const struct joystick *js)
{
    if (js->x >= 25 && r_4949d4 == 0) {
        r_4949cc = -1;
    }
    if (js->x <= -25 && r_4949d4 == 0) {
        r_4949cc = 1;
    }
    if (js->x >= -25 && js->x <= 25 && r_4949d4 != 0) {
        r_4949cc = 0;
    }
    if (js->y >= 25 && r_4949d8 == 0) {
        r_4949d0 = -1;
    }
    if (js->y <= -25 && r_4949d8 == 0) {
        r_4949d0 = 1;
    }
    if (js->y >= -25 && js->y <= 25 && r_4949d8 != 0) {
        r_4949d0 = 0;
    }
}

/* The joystick's gear buttons, with a manual gearbox. */
static void joy_gears(int i)
{
    struct car *c = &g_cars[i];
    const struct joystick *js = &g_joysticks[0];
    int sound = i == 0 || (g_two_players == 1 && i == 1);

    if (js->buttons[1] == 1 && r_49499c != 2 && c->gear > 0) {
        r_49499c = 2;
        c->gear--;
        if (sound) {
            race_sound_3d(0, 4, 2, 0, 0xd000, 0x55f0, 0, (float)(s32)c->rear_x, (float)(s32)c->rear_z);
        }
    }
    if (js->buttons[0] == 1) {
        if (r_49499c != 1 && c->gear < 2) {
            r_49499c = 1;
            c->gear++;
            if (sound) {
                race_sound_3d(0, 4, 2, 0, 0xd000, 0x55f0, 0, (float)(s32)c->rear_x,
                              (float)(s32)c->rear_z);
            }
        }
    } else if (js->buttons[1] != 1) {
        r_49499c = 0;
    }
}

/* 0x441390
 * The joystick (or pad, or wheel: the driver's f08 is 1, 2 or 3).
 * Racing: steering from x on a curve, throttle and brake from y (or the
 * buttons on a wheel), the action and look back on the buttons (or y),
 * the gears on the buttons with a manual gearbox. Paused: the pause
 * menu by the stick and the first button. On the results: the retry.
 * The second player reads the second joystick when both use one. */
void race_joystick(void)
{
    int i = r_5285c0, j = 0, type;
    s32 accel = 0, brake = 0, steer;
    const struct joystick *js;
    struct car *c = &g_cars[i];

    joystick_read();
    if (r_494990 == 0 && r_5530a8 == 0) {
        s32 d0 = g_drivers[0].f08, d1 = g_drivers[1].f08;

        if ((d0 == 1 || d0 == 3) && (d1 == 1 || d1 == 3) && i == 1) {
            j = 1;
        }
        js = &g_joysticks[j];
        if (js->x >= 0) {
            steer = (s32)(cos(js->x * 0.01 * 1.5707963267949) * -100.0 + 100.0);
        } else {
            steer = (s32)(cos(js->x * 0.01 * 1.5707963267949) * 100.0 - 100.0);
        }
        type = g_drivers[i].f08;
        if (type == 1) {
            accel = js->y > 0 ? 0 : -js->y;
            brake = js->y > 0 ? js->y : 0;
        } else if (type == 2) {
            accel = -js->y;
            brake = js->y;
        } else if (type == 3) {
            accel = (s8)js->buttons[0] * 100;
            brake = (s8)js->buttons[1] * 100;
        }
        if (accel < 0) {
            accel = 0;
        }
        if (brake < 0) {
            brake = 0;
        }
        if ((g_two_players == 0 && i == 0)
            || (g_two_players == 1 && (i == 0 || i == 1)
                && ((d0 > 0 && d1 == 0) || (d0 == 0 && d1 > 0) || (d0 == 3 && d1 == 3)))) {
            int alive = c->f354 == 0 && c->f35c == 0 && c->f358 == 0;

            if (type > 0 && type != 3) {
                c->action = js->buttons[0] == 1 && alive;
                if (c->f3a4 < 0.25 && c->action == 1 && c->last_action == 0 && c->f53c == 0) {
                    c->action = 2;
                }
                if (js->buttons[1] == 1 && c->look_back == 0) {
                    c->look_back = 1;
                } else if (js->buttons[1] == 0 && c->look_back == 1) {
                    c->look_back = 0;
                }
            } else if (type == 3) {
                c->action = js->y < -50 && alive;
                if (c->f3a4 < 0.25 && c->action == 1 && c->last_action == 0 && c->f53c == 0) {
                    c->action = 2;
                }
                if (js->y > 50 && c->look_back == 0) {
                    c->look_back = 1;
                } else if (js->y < 50 && c->look_back == 1) {
                    c->look_back = 0;
                }
            }
        }
        c->last_action = c->action;
        if (g_drivers[i].f0c == 0 && g_drivers[i].f08 == 2) {
            joy_gears(i);
        }
        if (g_drivers[i].f0c == 1 && c->gear != 0) {
            race_gearbox(i);
        }
        if (g_drivers[i].f18 == 1 && brake == 0) {
            accel = 100;
        }
        if (c->f558 > 0) {
            steer = -steer;
        }
        if (g_mirror == 3) {
            steer = -steer;
        }
        race_control(accel, brake, steer, c->action);
    } else if (r_494990 == 1) {
        js = &g_joysticks[0];
        joy_menu_push(js);
        if (r_4949d0 == 1 && r_4949d8 == 0) {
            if (--r_563c04 < 0) {
                r_563c04 = 3;
            }
            if (g_race_net.mode == 4 && r_563c04 == 1) {
                r_563c04 = 0;
            }
        } else if (r_4949d0 == -1 && r_4949d8 == 0) {
            if (++r_563c04 > 3) {
                r_563c04 = 0;
            }
            if (g_race_net.mode == 4 && r_563c04 == 1) {
                r_563c04 = 2;
            }
        } else if (r_4949cc == 1 && r_4949d4 == 0) {
            cd_prev();
        } else if (r_4949cc == -1 && r_4949d4 == 0) {
            cd_next();
        }
        r_4949d4 = r_4949cc;
        r_4949d8 = r_4949d0;
        if (js->buttons[0] == 1) {
            if (r_563c04 == 0) {
                r_494990 = 0;
                if (r_527f28 != 0) {
                    clear_back_buffer();
                }
            } else if (r_563c04 == 1) {
                if (g_race_mode != 0 || g_race_tries > 0 || r_563da0 != 0 || r_5daff0 != 0) {
                    r_601674 = 2;
                    g_race_5285c8 = 1;
                }
            } else if (r_563c04 == 2) {
                r_601674 = 1;
                g_race_5285c8 = 1;
            }
        }
    }
    if (r_5530a8 != 7) {
        return;
    }
    if (r_494990 == 0) {
        if (r_563c60 != 0) {
            return;
        }
        if (g_race_mode == 0) {
            js = &g_joysticks[0];
            joy_menu_push(js);
            if (r_4949d0 == 1 && r_4949d8 == 0 && r_563c18 != 0xc8) {
                r_563c18 = 0xc8;
                if (--r_54f988 < 0) {
                    r_54f988 = 1;
                }
            }
            if (r_4949d0 == -1 && r_4949d8 == 0 && r_563c18 != 0xd0) {
                r_563c18 = 0xd0;
                if (++r_54f988 > 1) {
                    r_54f988 = 0;
                }
            }
            r_4949d4 = r_4949cc;
            r_4949d8 = r_4949d0;
        }
    }
    if (r_563c60 == 0 && g_race_mode == 0 && g_joysticks[0].buttons[0] == 1 && r_563d68 == 1) {
        if (g_race_tries > 0 && r_54f988 == 0) {
            if (r_563da0 == 0) {
                g_race_again = 1;
                r_563da0 = 1;
                g_race_tries--;
            }
        } else if (r_563da0 == 0) {
            g_race_again = 0;
            r_563c60 = 1;
        }
    }
}

/* 0x420870: frees everything a race loaded, so the next one can load. */
void race_end(void)
{
    int i;

    video_set_palette(startup_black_palette());
    memset(g_back_buffer, 0, 0x1d4c0 * 4);
    present_back_buffer();
    cd_stop_play();
    if (g_srf.file != NULL) {           /* 0x4127a0 */
        mem_free(0, g_srf.file);
        g_srf.file = NULL;
    }
    race_hud_free();
    race_free_ai_track();
    scene_free();
    sound_close();
    race_save_settings();
    mem_free(0, g_level_pic);
    g_level_pic = NULL;
    for (i = 0; i < 9; i++) {
        font_unload(g_race_fonts[i]);
    }
    anim_tex_free();
    file_write("ign_win.btz", g_race_cfg, 0x597);
    memset(g_back_buffer, 0, 0x1d4c0 * 4);
    present_back_buffer();
    mem_pool_destroy(g_race_pool);
    /* The port's: the GPU's copies of the race's textures, tables and
     * meshes are kept by their addresses, which the next race's may
     * reuse. */
    gl3d_flush();
    canvas_forget_tables();
    /* The next race presents once before it draws its view: not this
     * race's last view. */
    video_set_layer(0, NULL);
    gfx_shade_end();
    g_touch_boost = -1.0f;
    g_touch_boost_on = 0;
}
