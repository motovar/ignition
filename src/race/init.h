/*
 * race/init.h - the last stage of loading a race.
 */

#ifndef IGN_RACE_INIT_H
#define IGN_RACE_INIT_H

#include "net.h"
#include "structs/race_cam.h"
#include "types.h"

/* The collision grid (.SRF): 0x24 bytes of header, cells, spans of 24
 * bytes and two lists of span offsets. */
struct srf_cell {
    u32 list_b;         /* byte offset into list_b */
    u32 list_a;         /* byte offset into list_a */
    u16 count_a, count_b;
};

struct srf {
    u8 *file;           /* 0x4937bc */
    s32 ext_z, ext_x;   /* 0x4c53b4, 0x4c53d0 */
    s32 cell_h, cell_w; /* 0x4c53c8, 0x4c53b8 */
    s32 gw, gh;         /* 0x4c53cc, 0x4c53b0 */
    const struct srf_cell *cells;   /* 0x4c53d4 */
    const u8 *spans;    /* 0x4c53a8 */
    const u32 *list_a;  /* 0x4c53a4, byte offsets into spans */
    const u32 *list_b;  /* 0x4c53c0 */
};

extern struct srf g_srf;
extern struct race_cam *g_race_cams;
extern s32 g_weather;
extern double g_best_lap;

/* Globals the stages set, named by address until their use is known. */
extern s32 r_619308, r_5532b0;
extern double r_5532b8;
extern double r_553288, r_5532c0, r_6192f8, r_619318;
extern s32 r_563d28, r_563d2c, r_563d38, r_563d3c, r_552f30, r_552f34;
extern double r_525e30;
extern double r_525e38;
extern s32 r_525e48;
extern double r_54f9a0, r_563bd0, r_563d48;   /* the tick accumulators, reset by race_reset_state */

/* The race's state (0x41fe60 resets it). */
extern double g_race_clock;             /* 0x5285d8, ticks */
extern s32 g_view_car;                  /* 0x4949a4 */
extern s32 g_champ_points[11];          /* 0x563d00 */
extern s32 r_494990;                    /* paused */
extern s32 r_563d88;                    /* the race is over */
extern struct net_game g_race_net;      /* 0x552f10 (race/load.c): 4 racing, 2 and 3
                                           waiting for the network */
extern s32 r_54f97c, r_5532c8, r_553070, r_553280, r_5530b8, r_563da0, r_5daff0;
extern s32 r_5532b0, r_601674;
extern s32 r_5530e0;                    /* steps since the start */
extern s32 r_552f58;                    /* the screen went back to one view */
extern s32 r_563c60;                    /* the ghost stops recording */
extern double r_54f980;                 /* ticks since the players finished, -1 before */
extern float r_525e2c;
extern s32 r_527f40[11];                /* counts to 120 after the start, by player */
extern s32 r_5530a8;
extern s32 r_552f4c;                    /* the exit fade, from 100 */
extern double r_552e48;
extern s32 r_5285c0;                    /* the car being updated */

/* --autopilot: the computer drives the player's car (a test switch). */
extern int g_race_autopilot;

void race_pos_deltas(void);
int race_srf_load(const char *name);
void race_init_cameras(void);
void race_make_effects(void);
void race_clear_laps(void);
void race_reset_state(void);
void race_init_hud(void);
void race_layout_views(void);           /* 0x43e2a0 */
extern s32 g_view_w, g_view_h;

#endif /* IGN_RACE_INIT_H */
