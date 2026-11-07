/*
 * race/load.c - the first stage of loading a race (0x418610..0x41b360):
 * the settings, the level's files, the textures, the HUD's fonts and
 * pictures, and the loading screen.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canvas.h"
#include "cd.h"
#include "game.h"
#include "lisa/file.h"
#include "lisa/font.h"
#include "lisa/gfx.h"
#include "lisa/log.h"
#include "lisa/mem.h"
#include "lisa/sprite.h"
#include "menu/pages.h"
#include "menu/panels.h"
#include "race/sound.h"
#include "race/race.h"
#include "race/cars.h"
#include "race/init.h"
#include "soft3d/soft3d.h"
#include "startup.h"
#include "stubs.h"
#include "video.h"

u8 g_race_cfg[0x5a0];                   /* 0x6393a0 */
int g_race_pool;                        /* 0x55307c */
s32 g_race_bytes;                       /* 0x494980 */
u8 *g_race_buffer;                      /* 0x563d40 */
struct race_driver *g_drivers;          /* 0x552ffc */
s32 g_driver_count;                     /* 0x6192f0 */
s32 g_race_mode;                        /* 0x527f6c */
s32 g_track;                            /* 0x552fc4 */
s32 g_two_players;                      /* 0x55306c */
s32 g_mirror;                           /* 0x563ce8 */
s32 g_views;                            /* 0x54f98c */
s32 g_race_cd_track;                    /* 0x552e54 */
s32 g_race_grid[RACE_DRIVERS];          /* 0x4972b0 */
char g_track_dir[16];                   /* 0x553790 */
char g_track_name[16];                  /* 0x552f40 */
char g_ghost_name[16];                  /* 0x5532a0 */

u8 *g_level_col;                        /* 0x54f900 */
u8 *g_level_pan;                        /* 0x5dfe64 */
u8 *g_level_pic;                        /* 0x525e94 */
u8 *g_level_shd;                        /* 0x527f74 */
u8 *g_level_tab;                        /* 0x563be4 */
s32 *g_level_pos;                       /* 0x553064 */
s32 *g_level_plc;                       /* 0x54f9cc */
s32 *g_cars_plc;                        /* 0x552fc8 */
s32 *g_level_msh;                       /* 0x525e60 */
s32 *g_cars_msh;                        /* 0x527f34 */
u8 *g_race_tex[RACE_TEX_SLOTS];         /* 0x5530f0 */
int g_race_tex_pages[RACE_TEX_SLOTS];

u8 *g_race_fnt;                         /* 0x525e58 */
int g_race_fonts[9];                    /* 0x528624.. */
u8 *g_race_pics[8];

/* What the settings give the rest of the race; named by address until
 * their use is known. */
s32 r_527f28, r_563c6c;                 /* 3 - screen size */
struct net_game g_race_net;             /* 0x552f10, the network game */
s32 r_553068, r_527f78, r_525e68, r_552e58, r_563be0, r_553084;
s32 r_54f958, r_527f20, r_5daff8;
s32 g_race_sfx_volume;                  /* 0x601668, 0..100 */
s32 g_race_cd_volume;                   /* 0x527f30 */

/* Championship tracks by difficulty and race (0x4984a0), and each
 * track's CD track (0x497eb8). */
static const s32 s_championship[4][7] = {
    { 0, 1, 2, 3, 4, 0, 0 }, { 3, 2, 0, 4, 1, 5, 0 },
    { 4, 3, 5, 1, 0, 2, 6 }, { 5, 0, 4, 6, 2, 1, 3 },
};
static const s32 s_track_cd[8] = { 2, 3, 4, 5, 6, 6, 7, 0 };

/* The track names by language (0x494ae0) and "LOADING" (0x4953b8). */
static const char *const s_track_titles[7] = {
    "MOOSEJAW FALLS", "GOLD RUSH", "SNAKE ISLAND", "LOST RUINS",
    "YODEL PEAKS", "CAPE THOR", "TOKYO BULLET",
};
static const char *const s_loading[6] = {
    "LOADING", "LADE...", "CARICAMENTO", "CARGANDO", "LADDAR", "CHARGEMENT",
};

/* The folder, name and ghost name of each track (0x4198d0). */
static const char *const s_tracks[7][3] = {
    { "CANADA\\", "CANADA", "MOOSEJAW" },
    { "USA\\", "USA", "GOLDRUSH" },
    { "CARIB\\", "CARIB", "SNAKEISL" },
    { "BRAZIL\\", "BRAZIL", "LOSTRUIN" },
    { "AUSTRIA\\", "AUSTRIA", "YODELPEA" },
    { "ICELAND\\", "ICELAND", "CAPETHOR" },
    { "JAPAN\\", "JAPAN", "TOKYOBUL" },
};

static int language(void)
{
    return race_cfg(0x593);
}

/* The original's pattern for every file: open, size, allocate from the
 * race's pool with `slack` bytes to align it to `align`, read; a file
 * that is missing ends the game. */
static u8 *load(const char *name, u32 slack, u32 align, long *size_out)
{
    long size = file_size(name);
    u8 *buf;

    if (size < 0) {
        log_printf("Error while trying to read %s\n", name);
        fprintf(stderr, "ignition: cannot read %s (track %d)\n", name, (int)g_track);
        exit(1);
    }
    buf = mem_alloc(g_race_pool, (u32)size + slack);
    g_race_bytes += (s32)(size + slack);
    if (buf == NULL) {
        fprintf(stderr, "ignition: out of memory loading %s (%ld bytes)\n", name, size);
        mem_out_of_memory();
    }
    if (align > 1) {
        buf = (u8 *)(((uintptr_t)buf + align - 1) & ~(uintptr_t)(align - 1));
    }
    file_read_at(name, buf, (u32)size, 0);
    if (size_out != NULL) {
        *size_out = size;
    }
    return buf;
}

static u8 *load_level(const char *ext, u32 slack, u32 align)
{
    char name[64];

    snprintf(name, sizeof(name), "LEVELS\\%s%s.%s", g_track_dir, g_track_name, ext);
    return load(name, slack, align, NULL);
}

/* 0x4198d0 */
static void track_names(void)
{
    if (g_track >= 0 && g_track < 7) {
        snprintf(g_track_dir, sizeof(g_track_dir), "%s", s_tracks[g_track][0]);
        snprintf(g_track_name, sizeof(g_track_name), "%s", s_tracks[g_track][1]);
        snprintf(g_ghost_name, sizeof(g_ghost_name), "%s", s_tracks[g_track][2]);
    }
}

/* 0x419260: the settings file into the race's globals and drivers. */
static void read_settings(void)
{
    long size = file_size("ign_win.btz");
    int i, humans;

    if (size < 0) {
        log_printf("Error while trying to read %s\n", "ign_win.btz");
        exit(1);
    }
    if (size > (long)sizeof(g_race_cfg)) {
        size = sizeof(g_race_cfg);
    }
    if (!file_read_at("ign_win.btz", g_race_cfg, (u32)size, 0)) {
        log_printf("LOAD ERROR\n");
        exit(1);
    }
    if (race_cfg(0x008) >= 0 && race_cfg(0x008) <= 3) {
        g_race_mode = race_cfg(0x008);
    }
    if (g_race_mode != 0) {
        g_track = race_cfg(0x0f3);
    } else {
        g_track = s_championship[race_cfg(0x00c) & 3][race_cfg(0x0f3) % 7];
    }
    if (g_track < 0 || g_track >= 7) {
        /* A track the tables do not know (the settings file is read as
         * it is): names left empty would make every level file missing,
         * and s_track_cd would be read out of bounds. */
        fprintf(stderr, "ignition: track %d is not one of the seven, using 0\n", (int)g_track);
        log_printf("track %d out of range\n", (int)g_track);
        g_track = 0;
    }
    track_names();

    g_driver_count = race_cfg(0x153);
    if (g_driver_count < 1 || g_driver_count > RACE_DRIVERS) {
        fprintf(stderr, "ignition: driver count %d out of range\n", (int)g_driver_count);
        log_printf("driver count %d out of range\n", (int)g_driver_count);
        g_driver_count = g_driver_count < 1 ? 1 : RACE_DRIVERS;
    }
    /* --soft3d draws at the original's resolutions only: the front
     * end's (--res's), and 800 x 600 for NATIVE. */
    if (g_soft3d) {
        s32 res = canvas_resolution() > 2 ? 2 : canvas_resolution();

        memcpy(g_race_cfg + 0x10b, &res, sizeof(res));
    }
    /* The port's NATIVE (3) takes the 800 x 600 layout. */
    g_screen_size = race_cfg(0x10b) > 2 ? 2 : race_cfg(0x10b);
    if (g_screen_size < 0) {
        g_screen_size = 0;      /* indexes the video mode tables */
    }
    r_527f28 = r_563c6c = race_cfg(0x107);
    if (r_527f28 < 0 || r_527f28 > 3) {
        log_printf("view size %d out of range\n", (int)r_527f28);
        r_527f28 = r_563c6c = r_527f28 < 0 ? 0 : 3;
    }
    g_two_players = race_cfg(0x004);
    if (g_two_players != 0 && g_two_players != 1) {
        log_printf("players %d out of range\n", (int)g_two_players);
        g_two_players = 0;
    }
    g_race_net.mode = race_cfg(0x197);
    r_553068 = race_cfg(0x010);
    r_527f78 = race_cfg(0x15b);
    r_525e68 = race_cfg(0x10f);
    r_552e58 = race_cfg(0x113);
    r_563be0 = race_cfg(0x117);
    g_mirror = race_cfg(0x00c);
    r_553084 = race_cfg(0x014);
    if (r_553068 == 1 && g_race_mode != 2) {
        r_553068 = 0;
    }
    if (g_race_mode == 0) {
        r_553084 = 1;
    }
    if (race_cfg(0x0f7) == 0) {
        g_race_cd_track = s_track_cd[g_track];
        if (g_race_cd_track > g_cd_track_count) {
            g_race_cd_track = g_cd_track_count;
        }
    } else if (race_cfg(0x0f7) == -1) {
        g_race_cd_track = (s32)(rand() / (RAND_MAX + 1.0) * g_cd_track_count + 1.0);
    } else {
        g_race_cd_track = race_cfg(0x0f7);
    }
    r_54f958 = 6;
    r_527f20 = 100;
    g_race_sfx_volume = race_cfg(0x0ff) * 10;
    g_race_cd_volume = race_cfg(0x0fb) * 10;
    r_5daff8 = g_race_sfx_volume != 0;

    for (i = 0; i < g_driver_count && i < RACE_DRIVERS; i++) {
        struct race_driver *d = &g_drivers[i];
        const u8 *keys = g_race_cfg + 0x17f + 8 * i;

        d->car = race_cfg(0x01c + 4 * i);
        if (d->car < 0 || d->car >= 11) {
            /* g_car_meshes has 11 models; the settings file is read as it is. */
            log_printf("driver %d: car %d out of range\n", i, (int)d->car);
            d->car = 0;
        }
        d->f0c = race_cfg(0x048 + 4 * i);
        d->control = race_cfg(0x15f + 4 * i);
        d->f14 = 30;
        d->f18 = 0;
        g_race_grid[i] = race_cfg(0x133 + 4 * i);
        snprintf(d->name, sizeof(d->name), "%s", (const char *)g_race_cfg + 0x06c + 15 * i);
        if (i == 0 || (i == 1 && g_two_players == 1)) {
            /* Left and right swap on a mirrored track. */
            d->keys[0] = g_mirror < 3 ? keys[0] : keys[1];
            d->keys[1] = g_mirror < 3 ? keys[1] : keys[0];
            memcpy(d->keys + 2, keys + 2, 6);
            d->f08 = race_cfg(0x123 + 4 * i);
            d->f18 = race_cfg(0x12b + 4 * i);
        }
    }
    for (humans = 0, i = 0; i < g_driver_count && i < RACE_DRIVERS; i++) {
        if (g_drivers[i].control != 1) {
            humans++;
        }
    }
    if (humans > 1) {
        r_553068 = 0;
    }
    g_views = g_two_players + 1;
}

/* In NATIVE the loading screen is the port's own: the back buffer is a
 * GPU surface with the menus' margins, on black instead of the level's
 * 320 x 200 picture, with the same panels and bar at the menus' scale.
 * It is drawn whole for each step of the bar. */
static int native_loading(void)
{
    return canvas_hardware() && canvas_resolution() == CANVAS_NATIVE;
}

static void draw_loading(int percent);

/* The race buffer's pitch for the loading panels: 320 as the original,
 * where a panel past the right edge wraps into the next row's start;
 * NATIVE's screen has room for it, so the panels get 640. */
static int panel_pitch(void)
{
    return canvas_is_gpu(g_back_buffer) ? 640 : 0x140;
}

/* How wide the panels are blended in: the screen, and NATIVE's right
 * margin. */
static int panel_width(void)
{
    float mx, my;

    canvas_extent(g_back_buffer, &mx, &my);
    return g_screen_width + (int)mx < panel_pitch() ? g_screen_width + (int)mx : panel_pitch();
}

static void progress_bar(int percent)
{
    int w = g_screen_width, third = w / 3, len, pitch = panel_pitch();

    len = (int)((text_width(s_loading[language() % 6], g_fonts_menu[1]) + 20) * percent
                * 0.011899999999999999);
    blend_rect(g_race_buffer, 0, 0x32, third + len, 0x4b, g_back_buffer, 0, 4, g_level_pan,
               pitch, w);
    blend_rect(g_race_buffer, third + len, 0x64, pitch == 0x140 ? 0x140 : panel_width(), 0x7d,
               g_back_buffer, third + len, 4, g_level_pan, pitch, w);
}

/* 0x418a60: the loading bar, `percent` of the way. */
void race_progress(int percent)
{
    int w = g_screen_width, row, x;

    if (canvas_is_gpu(g_back_buffer)) {
        draw_loading(percent);
        present_back_buffer();
        return;
    }
    for (row = 0; row < 27; row++) {
        for (x = 0x68; x < 0x127; x++) {
            g_back_buffer[row * w + x] = g_level_pic[0x34e + row * w + x];
        }
    }
    progress_bar(percent);
    present_back_buffer();
}

/* 0x418610 */
static void race_target(void)
{
    g_screen_width = 320;
    g_screen_height = 200;
    set_target(g_back_buffer, 320, 320, 200, 8);
}

/* 0x418650: the loading screen: the level's picture, the track's name
 * and "LOADING" on panels, blended in. */
static void draw_loading(int percent)
{
    int w = g_screen_width, h = g_screen_height, third = w / 3, i;
    const s32 *r = g_position_icon_rect;
    struct sprite_pos pos;
    const char *title = s_track_titles[g_track % 7];
    const char *loading = s_loading[language() % 6];
    int title_w, loading_w, gpu = canvas_is_gpu(g_back_buffer), mx = 0, my = 0;

    if (gpu) {
        float fx, fy;

        canvas_extent(g_back_buffer, &fx, &fy);
        mx = (int)fx;
        my = (int)fy;
        canvas_clear(g_back_buffer, 0, (size_t)w * h);
    } else {
        memcpy(g_back_buffer, g_level_pic + 0x34e, (size_t)w * h);
    }
    video_set_palette(g_level_col + 8);
    g_gfx_color_table = g_level_pan;
    set_target(g_back_buffer, w, w, h, 8);
    pos.x = -mx << 8;
    pos.y = -my << 8;
    sprite_draw(g_position_icons[0], &pos, NULL);
    pos.x = (w - r[2] + mx) << 8;
    sprite_draw(g_position_icons[1], &pos, NULL);
    pos.x = -mx << 8;
    pos.y = (h - r[3] + my) << 8;
    sprite_draw(g_position_icons[2], &pos, NULL);
    pos.x = (w - r[2] + mx) << 8;
    sprite_draw(g_position_icons[3], &pos, NULL);
    memset(g_race_buffer, 0, 0x75300);

    g_gfx_color_table = g_identity_table;
    title_w = text_width(title, g_fonts_menu[1]);
    panels_clear();
    panel_add(third, 0, title_w, 0x12, 0);
    panels_draw(g_race_buffer, panel_pitch());
    panels_clear();
    set_target(g_race_buffer, panel_pitch(), panel_pitch(), 0x1e0, 8);
    text_draw(title, g_fonts_menu[1], third + 13, 0x1e);
    if (gpu) {
        canvas_blend_rect(g_race_buffer, 0, 0, panel_width(), h, g_back_buffer, 0, 0, g_level_pan,
                          panel_pitch());
    } else {
        for (i = 0; i < w * h; i++) {
            g_back_buffer[i] = g_level_pan[g_race_buffer[i] << 8 | g_back_buffer[i]];
        }
    }
    memset(g_race_buffer, 0, (size_t)panel_pitch() * h);

    loading_w = text_width(loading, g_fonts_menu[1]);
    panel_add(third, 0, loading_w, 0x2d, 1);
    panel_add(third, 1, title_w, 0x2d, 1);
    panels_draw(g_race_buffer, panel_pitch());
    panels_clear();
    panel_add(third, 0, loading_w, 0x5f, 0);
    panel_add(third, 1, title_w, 0x5f, 0);
    panels_draw(g_race_buffer, panel_pitch());
    panels_clear();
    g_gfx_color_table = g_identity_table;
    text_draw(loading, g_fonts_menu[3], third + 13, 0x39);
    text_draw(loading, g_fonts_menu[1], third + 13, 0x6b);

    g_gfx_color_table = g_level_pan;
    set_target(g_back_buffer, w, w, h, 8);
    pos.x = 0x2c00;
    pos.y = 0x100;
    sprite_draw(g_bolt_icon, &pos, NULL);
    progress_bar(percent);
}

static void loading_screen(void)
{
    if (native_loading()) {
        canvas_register(g_back_buffer, 320, 200);
    }
    draw_loading(0);
    present_back_buffer();
}

/* 0x419a90 */
static void load_plc(void)
{
    g_level_plc = (s32 *)load_level("PLC", 0, 1);
    g_cars_plc = (s32 *)load("CARS\\CARS.PLC", 0, 1, NULL);
}

/* 0x419bd0 */
static void load_msh(void)
{
    g_level_msh = (s32 *)load_level("MSH", 0, 1);
    g_cars_msh = (s32 *)load("CARS\\CARS.MSH", 0, 1, NULL);
}

/* 0x419d10: every texture into one block, each at a 64 KB page. The
 * original sizes the block from the files first, then reads them. The
 * slots (0x5530f0..0x553110) are the track's, CARS, DIVSPR, LIGHT,
 * SMOKE, DARKSMOK, BOOM, EXSMOKE and a blank page: DIVSPR is read last
 * but goes to slot 2, and the effects name their textures by slot. */
static void load_textures(void)
{
    static const char *const general[6] = {
        "DIVSPR", "LIGHT", "SMOKE", "DARKSMOK", "BOOM", "EXSMOKE"
    };
    char names[RACE_TEX_SLOTS - 1][64];
    long sizes[RACE_TEX_SLOTS - 1];
    long total = 0;
    u8 *block;
    int i;

    snprintf(names[0], sizeof(names[0]), "LEVELS\\%s%s.TEX", g_track_dir, g_track_name);
    snprintf(names[1], sizeof(names[1]), "CARS\\CARS.TEX");
    for (i = 0; i < 6; i++) {
        snprintf(names[2 + i], sizeof(names[2 + i]), "GENERAL\\%s.TEX", general[i]);
    }
    for (i = 0; i < RACE_TEX_SLOTS - 1; i++) {
        sizes[i] = file_size(names[i]);
        if (sizes[i] < 0) {
            log_printf("Error while trying to read %s\n", names[i]);
            exit(1);
        }
        g_race_tex_pages[i] = (int)((sizes[i] + 0xffff) >> 16);
        total += (long)g_race_tex_pages[i] << 16;
    }
    g_race_tex_pages[RACE_TEX_SLOTS - 1] = 1;
    total += 0x10000;
    block = mem_calloc(g_race_pool, (u32)total + 0xffff);
    g_race_bytes += (s32)(total + 0xffff);
    if (block == NULL) {
        mem_out_of_memory();
    }
    block = (u8 *)(((uintptr_t)block + 0xffff) & ~(uintptr_t)0xffff);
    for (i = 0; i < RACE_TEX_SLOTS; i++) {
        g_race_tex[i] = block;
        if (i < RACE_TEX_SLOTS - 1) {
            file_read_at(names[i], block, (u32)sizes[i], 0);
        }
        block += (size_t)g_race_tex_pages[i] << 16;
    }
}

/* 0x41ac40: the HUD's fonts. */
static void load_fonts(void)
{
    static const char *const fonts[9] = {
        "yellow_s.lft", "pos_s.lft", "speed_s.lft", "yellow.lft", "pos.lft",
        "speed.lft", "yellow_h.lft", "pos_h.lft", "speed_h.lft"
    };
    /* 0x528624, 0x52863c, 0x528648, 0x528628, 0x528640, 0x52864c,
     * 0x52862c, 0x528644, 0x528650 */
    char name[64];
    int i;

    g_race_fnt = load("FONTS\\\\IGNITION.FNT", 0, 1, NULL);
    for (i = 0; i < 9; i++) {
        snprintf(name, sizeof(name), "FONTS\\\\%s", fonts[i]);
        g_race_fonts[i] = font_load(name, 0);
        if (g_race_fonts[i] == -1) {
            log_printf("Cannot get a font handle\n");
            exit(-1);
        }
    }
}

/* 0x41af70: the HUD's pictures. */
static void load_pictures(void)
{
    static const char *const pics[8] = {
        "s_pangfx.pic", "n_pangfx.pic", "h_pan1.pic", "h_pan2.pic",
        "s_signs.pic", "n_signs.pic", "h_signs.pic", "pokal.pic"
    };
    /* 0x563c08, 0x563c00, 0x601660, 0x601670, 0x54f990, 0x54f910,
     * 0x54f950, 0x60166c */
    int i;

    for (i = 0; i < 8; i++) {
        g_race_pics[i] = load(pics[i], 0, 1, NULL);
    }
}

/* 0x418dd0: the level's files, with the loading screen once the
 * palette and picture are in. */
static void load_level_files(void)
{
    g_level_col = load_level("COL", 0, 1);
    g_level_pan = load_level("PAN", 0x10000, 0x10000);
    {
        char name[64];

        snprintf(name, sizeof(name), "LEVELS\\%s%s.PIC", g_track_dir, g_track_name);
        g_level_pic = file_load(name);
        if (g_level_pic == NULL) {
            log_printf("Error while loading %s\n", name);
            exit(1);
        }
    }
    race_target();
    g_video_mode.width = 320;
    g_video_mode.height = 200;
    g_video_mode.bpp = 8;
    g_video_mode.flag = 1;
    if (!video_set_mode()) {
        g_fatal_error = 1;
        return;
    }
    loading_screen();
    g_level_shd = load_level("SHD", 0x10000, 0x10000);
    g_level_tab = load_level("TAB", 0x100, 0x100);
    race_progress(5);
    load_plc();
    race_progress(12);
    load_msh();
    race_progress(18);
    load_textures();
    race_progress(25);
    g_level_pos = (s32 *)load_level("POS", 0, 1);
    load_fonts();
    load_pictures();
    race_progress(30);
}

/* 0x418b70 */
void race_load_1(void)
{
    g_gfx_color_table = g_identity_table;
    g_race_pool = mem_pool_create(NULL);
    g_race_buffer = mem_calloc(g_race_pool, 0x77240);
    g_drivers = mem_calloc(g_race_pool, RACE_DRIVERS * sizeof(struct race_driver));
    g_race_bytes += RACE_DRIVERS * sizeof(struct race_driver);
    if (g_drivers == NULL) {
        mem_out_of_memory();
    }
    g_two_players = 0;
    read_settings();
    load_level_files();
}

/* 0x418be0 */
void race_load_2(void)
{
    race_unpack_types();
    race_track_order();
    race_progress(0x22);
    race_make_tables();
    race_progress(0x28);
    race_load_cars();
    race_progress(0x2e);
    race_init_cameras();
    race_progress(0x34);
    race_load_sounds();
    race_progress(0x3c);
    race_make_world();
    race_progress(0x46);
    log_printf("track: %d pieces, %d in order\n", g_road_count, g_track_link_count);
}
