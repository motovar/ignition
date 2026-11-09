/*
 * race/view.c - the race's picture each frame (0x436990): which camera
 * shows the race, the 3D through it, the HUD over it, and the fades and
 * screens around the race.
 *
 * The port draws the 3D on the GPU (race_draw_view) under the back
 * buffer, so what the original fills into the back buffer before the 3D
 * (the sky, track 2's band) is filled into the view instead.
 */

#include <math.h>
#include <string.h>

#include "lisa/sprite.h"
#include "prof.h"
#include "race/race.h"
#include "race/cars.h"
#include "race/contact.h"
#include "race/data.h"
#include "race/debug.h"
#include "race/frame.h"
#include "race/hud.h"
#include "race/init.h"
#include "race/results.h"
#include "race/misc.h"
#include "race/view.h"
#include "scene.h"
#include "soft3d/soft3d.h"
#include "startup.h"
#include "stubs.h"
#include "video.h"

s32 r_54f92c;                           /* the lens wobbles */
s32 r_54f934;                           /* the debug camera behind the car */
s32 r_4949bc;                           /* the replay camera's numbers on screen */
s32 r_601664;                           /* the place the TV camera follows */
s32 r_563bec;                           /* the replay camera's roll */
s32 r_563c14;                           /* the replay camera: 1 looks at a car, 2 rides with it */
s32 r_563c5c;                           /* the car it watches */
double r_563c48, r_563c40, r_563c50;    /* where it rides, from the car */
s32 r_54f9d8, r_54f9dc, r_54f9e0;       /* its last place, for a car that is gone */
s32 r_553098, r_5530a0, r_55309c;       /* and its last target */

/* A camera pose: place, then pitch and yaw in tenths of a degree. */
struct pose {
    double x, y, z, pitch, yaw;
};

/* 900 - the heading in tenths of a degree, into 0..3599. */
static double yaw_of(double heading)
{
    double y = heading * -572.957795130822 + 900.0;

    while (y < 0.0) {
        y += 3600.0;
    }
    while (y > 3599.0) {
        y -= 3600.0;
    }
    return y;
}

/* The pose of car i's chase camera, or looking back. */
static void chase_pose(int i, struct pose *p)
{
    const struct race_cam *cam = &g_race_cams[i];

    if (g_cars[i].look_back == 0) {
        p->x = cam->x;
        p->z = cam->z;
        p->yaw = cam->yaw;
    } else {
        p->x = cam->back_x;
        p->z = cam->back_z;
        p->yaw = cam->back_yaw;
    }
    p->y = cam->f38 + cam->height;
    p->pitch = cam->f48 + cam->f40 + cam->f30;
}

/* The debug camera: 150 behind car i and 100 up, looking down at 15
 * degrees. */
static void debug_pose(int i, struct pose *p)
{
    const struct car *c = &g_cars[i];

    p->x = cos(c->heading) * -150.0 + c->x;
    p->y = c->y + 100.0;
    p->z = sin(c->heading) * -150.0 + c->z;
    p->pitch = 150.0;
    p->yaw = yaw_of(c->heading);
}

/* One of the track's cameras over the finish, bobbing a little. */
static void finish_pose(int cam, double bob, struct pose *p)
{
    const float *f = &g_finish_cams[(g_track * 3 + cam) * 5];

    p->x = f[0];
    p->y = bob + f[1];
    p->z = f[2];
    p->pitch = f[3] * 180.0f * 3.1830988618379;
    p->yaw = f[4] * 180.0f * 3.1830988618379;
}

/* 0x438050
 * The TV camera's car: the best placed car still racing (in an
 * elimination race the worst placed that is not last), changed to only
 * after it has been the one for 20 frames. Returns that car's place,
 * Count + 1 for none. */
static s32 tv_pick(void)
{
    s32 best;
    int i, pick = g_view_car;

    if (g_race_mode == 3) {
        best = 1;
        for (i = 0; i < g_driver_count; i++) {
            if (g_cars[i].f528 == 0 && g_cars[i].grid > best) {
                best = g_cars[i].grid;
                pick = i;
            }
        }
        if (best == 1) {
            best = g_driver_count + 1;
        }
    } else {
        best = g_driver_count + 1;
        for (i = 0; i < g_driver_count; i++) {
            if (g_cars[i].f528 == 0 && g_cars[i].grid < best) {
                best = g_cars[i].grid;
                pick = i;
            }
        }
    }
    if (pick == g_view_car) {
        r_4949a8 = 0;
    } else if (++r_4949a8 > 20) {
        g_view_car = pick;
    }
    return best;
}

/* Whether the players are done: the one player, or both of two. */
static int players_finished(void)
{
    if (g_two_players == 0) {
        return g_cars[0].f528 == 1;
    }
    return g_two_players == 1 && g_cars[0].f528 == 1 && g_cars[1].f528 == 1;
}

static void put_scene(const struct pose *p, int cam)
{
    struct scene *sc = g_scene;

    sc->stage_lookat = 0;
    sc->x = p->x;
    sc->y = p->y;
    sc->z = p->z;
    sc->pitch = p->pitch;
    sc->yaw = p->yaw;
    sc->roll = 0.0;
    sc->target_roll = 0;
    sc->focal_x = g_race_cams[cam].f58;
    sc->focal_y = g_race_cams[cam].f5c;
    sc->near_ofs = (s32)g_race_cams[cam].f60;
}

/* The group of placements car i's segment shows (0x63c5d8). */
static s32 group_at(int i)
{
    s32 seg = g_cars[i].f364;
    s32 plc = seg >= 0 ? g_track_links[seg].plc : g_track_links[-seg].alt_plc;

    return g_plc_extra[plc].f0;
}

/* The start lights blink between 60 and 130. */
static int lights_on(void)
{
    return r_552e48 >= 60.0 && r_552e48 < 130.0 && (s32)(r_552e48 * 0.125) % 2 == 0;
}

/* The 3D through g_scene: the visible cells' objects, at the GPU's
 * resolution. */
static void draw_3d(int x0, int x1, int first)
{
    prof_begin(PROF_GATHER);
    {
        /* The view takes the drawable's shape (race_draw_view): wider
         * than 4:3, it sees past the original's cells at the sides. */
        int w, h;

        video_drawable_size(&w, &h);
        scene_set_reach(h > 0 ? (double)w / h / (4.0 / 3.0) * (x1 - x0) / g_screen_width : 1.0);
    }
    scene_gather_view();
    prof_end(PROF_GATHER);
    prof_begin(PROF_3D);
    race_draw_view(g_scene, g_scene_visible, g_scene->obj_count, x0, x1, first);
    prof_end(PROF_3D);
}

/* The view's place on the screen: the 3D is drawn over the whole
 * screen, so only the rows filled before it use the rectangle. */
static void fill_rows(const struct race_driver *d, int rows, int index)
{
    race_view_fill(d->view_y0, d->view_y0 + rows, index);
}

/* Track 2's band, and the sky behind the debug camera. */
static void fills(void)
{
    if (g_track == 2) {
        int band = g_screen_height / 200 * 25;

        fill_rows(&g_drivers[g_view_car], band, 0x72);
        if (g_two_players == 1 && r_552f58 == 0) {
            fill_rows(&g_drivers[1], band, 0x72);
        }
    }
    if (r_54f934 == 1) {
        const struct race_driver *d = &g_drivers[g_view_car];

        race_view_fill(d->view_y0, d->view_y1, (u8)g_sky_colour[g_track]);
        if (g_two_players == 1 && r_552f58 == 0) {
            race_view_fill(g_drivers[1].view_y0, g_drivers[1].view_y1, (u8)g_sky_colour[g_track]);
        }
    }
}

/* How far the scene gathers on some stretches of each track. */
static void gather_radius(void)
{
    s32 seg = g_cars[g_view_car].f364;
    s32 r = 0;

    switch (g_track) {
    case 5:
        r = seg > 27 && seg < 70 ? 30 : 0;
        break;
    case 3:
        r = seg > 0 && seg < 30 ? 25 : seg > 187 && seg < 197 ? 30 : 0;
        break;
    case 2:
        return;
    case 1:
        r = seg > -124 && seg < -113 ? 30 : 0;
        break;
    case 0:
        r = seg > 36 && seg < 46 ? 30 : seg > 124 && seg < 140 ? 35 : 0;
        break;
    case 4:
        r = seg > 6 && seg < 14 ? 35 : 0;
        break;
    default:
        return;
    }
    g_scene->gather_radius = r;
}

/* The replay camera (r_5532b0): at a place, or looking at a car, or
 * riding with it. */
static void replay_camera(void)
{
    struct scene *sc = g_scene;
    const struct car *c = &g_cars[r_563c5c];

    sc->target_roll = r_563bec;
    if (r_563c14 == 1) {
        sc->stage_lookat = 1;
        sc->target_x = (s32)c->x;
        sc->target_y = (s32)c->y;
        sc->target_z = (s32)c->z;
    }
    if (r_563c14 != 2) {
        return;
    }
    sc->stage_lookat = 1;
    if (c->f354 == 0 && c->f358 == 0 && c->f35c == 0) {
        sc->x = c->x + r_563c48;
        r_5532c0 = sc->x;
        sc->y = c->y + r_563c40;
        r_5532b8 = sc->y;
        sc->z = c->z + r_563c50;
        r_553288 = sc->z;
        sc->target_x = (s32)c->x;
        sc->target_y = (s32)c->y;
        sc->target_z = (s32)c->z;
        r_54f9d8 = (s32)sc->x;
        r_54f9dc = (s32)sc->y;
        r_54f9e0 = (s32)sc->z;
        /* The original saves target_x three times. */
        r_553098 = sc->target_x;
        r_5530a0 = sc->target_x;
        r_55309c = sc->target_x;
    } else {
        /* And puts x in all three. */
        sc->x = r_54f9d8;
        sc->y = r_54f9d8;
        sc->z = r_54f9d8;
        sc->target_x = r_553098;
        sc->target_y = r_5530a0;
        sc->target_z = r_55309c;
    }
}

/* The pieces over a full screen view's corners go to the drawable's
 * own when the 3D is a layer behind the screen (video_set_layer): it is
 * wider than the 4:3 screen the HUD is drawn in, and the pieces that
 * round its corners belong at its edge. 0 where that is not so. */
static int corners_on_drawable(void)
{
    const u8 *pieces[4];
    struct tex_desc d[4];
    int i;

    if (g_soft3d) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        if (g_position_icons[i] == NULL || tex_get_info(&d[i], g_position_icons[i]->tex) != 0) {
            return 0;
        }
        if (d[i].pixels == NULL || d[i].width != d[0].width || d[i].height != d[0].height
            || d[i].stride != d[0].stride) {
            return 0;
        }
        pieces[i] = d[i].pixels;
    }
    video_set_corners(pieces, d[0].width, d[0].height, d[0].stride);
    return 1;
}

/* The four corner pieces over a full screen view. */
static void corners(void)
{
    const s32 *r = g_position_icon_rect;
    s32 x1 = g_drivers[0].view_x1, y1 = g_drivers[0].view_y1;
    struct sprite_pos pos;

    if (corners_on_drawable()) {
        return;
    }
    pos.x = 0;
    pos.y = 0;
    sprite_draw(g_position_icons[0], &pos, NULL);
    pos.x = (x1 - r[2]) << 8;
    pos.y = 0;
    sprite_draw(g_position_icons[1], &pos, NULL);
    pos.x = 0;
    pos.y = (y1 - r[3]) << 8;
    sprite_draw(g_position_icons[2], &pos, NULL);
    pos.x = (x1 - r[2]) << 8;
    pos.y = (y1 - r[3]) << 8;
    sprite_draw(g_position_icons[3], &pos, NULL);
}

/* 0x436990 */
void race_view(double dt)
{
    struct pose one, two = { 0 };
    struct scene_desc *lights = &g_arrow_objs[g_driver_count];
    int split = g_two_players == 1 && r_552f58 == 0;

    if (r_54f92c == 1) {
        r_525e2c = (float)(r_525e2c + 0.1);
        g_scene->zoom = sin(r_525e2c);
    }
    back_guard_check("the frame's start");
    if (r_494990 == 1) {
        memset(g_back_buffer, 0, (size_t)g_screen_width * g_screen_height);
    }
    gather_radius();
    fills();
    back_guard_check("the fills");

    chase_pose(g_view_car, &one);
    if (r_54f934 == 1) {
        debug_pose(g_view_car, &one);
    }
    if (split) {
        chase_pose(1, &two);
        if (r_54f934 == 1) {
            debug_pose(1, &two);
        }
    }
    /* Two counters after the start, to 120. */
    if (r_527f40[0] >= 0 && r_527f40[0] < 120) {
        r_527f40[0] = (s32)(r_527f40[0] + dt);
    }
    if (r_527f40[1] >= 0 && r_527f40[1] < 120) {
        r_527f40[1] = (s32)(r_527f40[1] + dt);
    }
    if ((0.0 <= r_54f980 && r_54f980 < 120.0) || r_5530a8 == 7) {
        /* The players have just finished: time it, and each player's
         * own count to 25. */
        r_54f980 += dt;
        if (g_cars[0].f528 == 1 && g_cars[0].f5d4 < 25) {
            g_cars[0].f5d4 = (s32)(g_cars[0].f5d4 + dt);
        }
        if (g_cars[1].f528 == 1 && g_cars[1].f5d4 < 25) {
            g_cars[1].f5d4 = (s32)(g_cars[1].f5d4 + dt);
        }
    } else if (!(r_54f980 < 120.0) && players_finished()) {
        double bob = sin(g_race_clock * 0.05) * 8.0;

        if (r_563d20 != 3) {
            finish_pose(r_563d20, bob, &one);
            if (split) {
                finish_pose(0, bob, &two);
            }
        } else {
            r_601664 = tv_pick();
            if (g_driver_count - r_601664 == -1) {
                /* No one left racing: back to the first camera. */
                r_563d20 = 0;
                g_view_car = 0;
                finish_pose(0, bob, &one);
                if (split) {
                    finish_pose(0, bob, &two);
                }
            } else {
                const struct race_cam *cam = &g_race_cams[g_view_car];

                one.x = cam->back_x;
                one.z = cam->back_z;
                one.y = cam->f38 + cam->height;
                one.yaw = cam->back_yaw;
                one.pitch = cam->f48 + cam->f40 + cam->f30;
            }
        }
    }
    if (r_5532b0 == 1) {
        one.x = r_5532c0;
        one.y = r_5532b8;
        one.z = r_553288;
        one.pitch = r_6192f8 * 572.957795130822;
        one.yaw = r_619318 * 572.957795130822;
    }

    put_scene(&one, g_view_car);
    if (g_two_players == 0) {
        g_scene->center_x = g_screen_width / 2;
    } else if (g_two_players == 1) {
        g_scene->center_x = g_screen_width / 4 * 3;
    }
    if (r_552f58 == 1) {
        g_scene->center_x = g_screen_width / 2;
    }
    if (r_5532b0 == 1) {
        replay_camera();
    }
    if (lights_on() && lights->in_grid == 0) {
        scene_obj_link(lights);
    }
    g_scene_group = r_54f980 < 120.0 ? group_at(g_view_car) : 0;
    /* Split, the first player has the right half. */
    draw_3d(split ? g_screen_width / 2 : 0, g_screen_width, 1);
    back_guard_check("the 3D");
    if (lights->in_grid == 1) {
        scene_obj_unlink(lights);
    }
    if ((g_cars[0].f528 == 0 || r_54f980 <= 120.0) && r_5532b0 == 0) {
        prof_begin(PROF_HUD);
        race_hud(g_view_car, 0);
        prof_end(PROF_HUD);
        back_guard_check("the HUD");
    }
    if (r_4949b4) {
        race_debug_lines();
    }

    if (split) {
        /* The second player's view, in the left half. */
        put_scene(&two, 1);
        g_scene->center_x = g_screen_width / 4;
        if (lights_on() && lights[1].in_grid == 0) {
            scene_obj_link(&lights[1]);
        }
        g_scene_group = group_at(1);
        draw_3d(0, g_screen_width / 2, 0);
        back_guard_check("the second 3D");
        if (lights[1].in_grid == 1) {
            scene_obj_unlink(&lights[1]);
        }
        if (g_cars[1].f528 == 0 || r_54f980 <= 120.0) {
            race_hud(1, 1);
        }
    }
    race_replay_numbers();
    back_guard_check("the replay numbers");
    if (0.0 <= r_552e48 && r_552e48 < 100.0) {
        race_iris((s32)r_552e48);
        back_guard_check("the iris");
    }
    if (players_finished() && !(r_54f980 < 120.0) && r_563c60 == 0 && r_5532b0 == 0) {
        prof_begin(PROF_RESULTS);
        race_results();
        prof_end(PROF_RESULTS);
        back_guard_check("the results");
    }
    if (r_527f28 == 0 && (g_two_players == 0 || r_552f58 == 1)) {
        corners();
        back_guard_check("the corners");
    }
    if (r_563c60 == 1) {
        r_552f4c = (s32)(r_552f4c - dt);
        if (r_552f4c <= 0) {
            race_save_results();
            r_563d88 = 2;
        }
        race_iris(r_552f4c);
    }
    if (split) {
        int x;

        for (x = g_screen_width / 2 - 1; x < g_screen_width / 2 + 1; x++) {
            for (int y = 0; y < g_screen_height; y++) {
                g_back_buffer[y * g_screen_width + x] = 0;
            }
        }
    }
}
