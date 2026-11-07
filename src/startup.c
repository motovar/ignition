/*
 * startup.c - the game's video and data set up, run once from game_init.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lisa/file.h"
#include "lisa/font.h"
#include "lisa/gfx.h"
#include "lisa/log.h"
#include "lisa/mem.h"
#include "lisa/sprite.h"
#include "pic.h"
#include "startup.h"
#include "stubs.h"
#include "version.h"
#include "vfs.h"
#include "video.h"

#define IGNITION_VERSION 6.53   /* the double at 0x494978 */

u8 *g_identity_table;           /* 0x525e64 */
int g_game_pool;                /* 0x5530e8 */
int g_fatal_error;              /* 0x54f970 */
char g_error_text[256];         /* 0x6035f0 */
s32 g_screen_width;             /* 0x553000 */
s32 g_screen_height;            /* 0x563c10 */
struct back_block g_back_block;      /* g_back_buffer: 0x563db0, 800 x 600 in the original */

#define GUARD_BYTE 0xa5

/* Looks at the guard bands: the first time it sets them, then it reports
 * (to Default.fil and stderr) the stage after which a byte in one was
 * not as it was left, how far from the buffer, and puts it back. */
void back_guard_check(const char *where)
{
    static int s_armed, s_reports;
    int side;

    if (!s_armed) {
        memset(g_back_block.pre, GUARD_BYTE, sizeof(g_back_block.pre));
        memset(g_back_block.post, GUARD_BYTE, sizeof(g_back_block.post));
        s_armed = 1;
        return;
    }
    for (side = 0; side < 2; side++) {
        u8 *band = side == 0 ? g_back_block.pre : g_back_block.post;
        int i, first = -1, last = -1, count = 0;

        for (i = 0; i < BACK_GUARD; i++) {
            if (band[i] != GUARD_BYTE) {
                if (first < 0) {
                    first = i;
                }
                last = i;
                count++;
            }
        }
        if (count == 0) {
            continue;
        }
        if (s_reports < 40) {
            /* Distances from the buffer: before it counts down to -1. */
            int from = side == 0 ? first - BACK_GUARD : first;
            int to = side == 0 ? last - BACK_GUARD : last;

            log_printf("back buffer %s written after %s: %d bytes, %d to %d, screen %dx%d\
",
                       side == 0 ? "before" : "past", where, count, from, to,
                       (int)g_screen_width, (int)g_screen_height);
            fprintf(stderr, "ignition: back buffer %s written after %s: %d bytes, %d to %d\
",
                    side == 0 ? "before" : "past", where, count, from, to);
            s_reports++;
        }
        memset(band, GUARD_BYTE, BACK_GUARD);
    }
}
u8 *g_sysgfx;                   /* 0x54f998, N_SYSGFX.PIC */
u8 *g_sysgfx2;                  /* 0x553088, N_SYSG_2.PIC */
u8 *g_sys_palette;              /* 0x563bfc, SYS.COL */
int g_fonts_menu[8];            /* 0x552fd0 */
struct sprite *g_position_icons[4];     /* 0x54f960 */
struct sprite *g_bolt_icon;     /* 0x552d84 */
s32 g_screen_size;              /* 0x552fc0, the SCREEN SIZE option */
u8 *g_menu_background;          /* 0x4bcd50 */
u8 g_menu_remap[256];           /* 0x4bcc48 */
u8 *g_cone_table;               /* 0x553080 */
s32 g_cheats[10];               /* 0x54f920, one flag a cheat code */

static u8 s_identity_table[0x10000];   /* in 0x619330, 64 KB aligned */
static u8 s_black_palette[0x300];       /* 0x553470 */

const u8 *startup_black_palette(void)
{
    return s_black_palette;
}

/* Where the four position icons are cut from N_SYSGFX.PIC: x, y, width,
 * height (0x497bb8). Icon i is i heights further down. */
const s32 g_position_icon_rect[4] = { 0xec, 0, 0x14, 0x14 };

/* Sets one of the 8 bit modes, as game_video_init and game_set_menu_mode
 * do. Returns video_set_mode's result. */
static int set_mode(int width, int height)
{
    g_video_mode.width = width;
    g_video_mode.height = height;
    g_video_mode.bpp = 8;
    g_video_mode.flag = 1;
    return video_set_mode();
}

/* 0x418130
 * When a font fails the original passes its error buffer to log_printf
 * as the format; here the message is logged as written. It also sets the
 * failed handle's flags at index -1 before checking. */
static void game_load_system_gfx(void)
{
    static const char *const fonts[8] = {
        "baltazar\\data\\red_dark.lft", "baltazar\\data\\red_lite.lft",
        "baltazar\\data\\bluedark.lft", "baltazar\\data\\bluelite.lft",
        "baltazar\\data\\red_grey.lft", "baltazar\\data\\bluegrey.lft",
        "fonts\\mini.lft", "fonts\\small.lft"
    };
    char name[52];
    int i;

    g_sysgfx = file_load("n_Sysgfx.pic");
    if (g_sysgfx == NULL) {
        log_printf("Error while loading N_SYSGFX.PIC\n");
        exit(1);
    }
    g_sysgfx2 = file_load("n_Sysg_2.pic");
    if (g_sysgfx2 == NULL) {
        log_printf("Error while loading N_SYSG_2.PIC\n");
        exit(1);
    }
    sprintf(name, "SYS.COL");
    g_sys_palette = file_load(name);
    if (g_sys_palette == NULL) {
        log_printf("Error while loading SYS.COL\n");
        exit(1);
    }
    for (i = 0; i < 8; i++) {
        g_fonts_menu[i] = font_load(fonts[i], 0);
    }
    for (i = 0; i < 8; i++) {
        int font = g_fonts_menu[i];

        if (font == -1) {
            log_printf("Could not get a font handle (%d).\n", i);
            g_fatal_error = 1;
            exit(-1);
        }
        g_fonts[font].proportional = 1;
        g_fonts[font].spacing = 1;
    }
}

/* 0x401000
 * Reads the 0x9358 byte menu background and sets up a colour remap:
 * identity, with eight colours moved. */
static void menu_load_background(void)
{
    static const u8 pairs[] = {
        0xc1, 0xf5, 0xc3, 0xbd, 0xc4, 0xbe, 0xd5, 0xb4,
        0xd6, 0xbb, 0xfd, 0xbc, 0xc0, 0xbd, 0xc2, 0xf4, 0
    };
    FILE *fp;
    int i;

    fp = vfs_fopen("MenuBkg.dat", "rb");
    if (fp != NULL) {
        g_menu_background = calloc(0x9358, 1);
        if (fread(g_menu_background, 0x9358, 1, fp) != 1) {
            /* The original ignores a short read. */
        }
        fclose(fp);
    }
    for (i = 0; i < 256; i++) {
        g_menu_remap[i] = (u8)i;
    }
    for (i = 0; pairs[i] != 0; i += 2) {
        g_menu_remap[pairs[i]] = pairs[i + 1];
    }
}

/* 0x4200e0
 * An 800 x 405 table filled with 20, with a V of the values 5 down to 0
 * opening from column 400: row r has value 5 - k in columns 400 + k - r
 * up to 400 - k + r. */
static void cone_table_init(void)
{
    enum { W = 800, H = 405 };
    int r, k, x;

    g_cone_table = mem_calloc(g_game_pool, W * H);
    memset(g_cone_table, 20, W * H);
    for (r = 0; r < H; r++) {
        for (k = 0; k < 6; k++) {
            for (x = 400 + k - r; x < 400 - k + r; x++) {
                if (x >= 0 && x < W) {
                    g_cone_table[r * W + x] = (u8)(5 - k);
                }
            }
        }
    }
}

/* 0x418500
 * Returns 0 on success and on failure alike; a failure sets
 * g_fatal_error. */
int game_set_menu_mode(void)
{
    static const s32 modes[3][2] = { { 320, 200 }, { 640, 480 }, { 800, 600 } };

    if (g_screen_size < 0 || g_screen_size > 2) {
        return 0;
    }
    if (!set_mode(modes[g_screen_size][0], modes[g_screen_size][1])) {
        sprintf(g_error_text, "Cannot use this graphics mode.\n");
        g_fatal_error = 1;
    }
    return 0;
}

static struct sprite *cut_sprite(const u8 *pic, int x, int y, int w, int h)
{
    struct tex_desc desc;

    memset(&desc, 0, sizeof(desc));
    desc.width = w;
    desc.height = h;
    desc.pixels = (u8 *)pic_pixels(pic) + y * 0x100 + x;
    desc.stride = 0x100;
    return sprite_create(&desc, NULL);
}

/* 0x417ea0
 * IDA names the two palette calls operator delete; they go through the
 * video table (0x456c40). */
int game_video_init(void)
{
    int i, row, col;

    /* table[s << 8 | d] = s: drawing copies the source. */
    g_identity_table = s_identity_table;
    for (row = 0; row < 256; row++) {
        for (col = 0; col < 256; col++) {
            g_identity_table[row << 8 | col] = (u8)row;
        }
    }
    g_gfx_color_table = g_identity_table;
    g_game_pool = mem_pool_create(NULL);

    if (!set_mode(640, 480)) {
        sprintf(g_error_text, "Cannot set graphics mode.\n");
        g_fatal_error = 1;
        exit(0);
    }
    if (!set_mode(320, 200)) {
        sprintf(g_error_text, "Cannot set graphics mode.\n");
        g_fatal_error = 1;
        exit(0);
    }
    memset(s_black_palette, 0, sizeof(s_black_palette));
    video_set_palette(s_black_palette);
    g_screen_width = 320;
    g_screen_height = 200;
    set_target(g_back_buffer, 320, 320, 200, 8);

    log_open();
    g_log_to_stdout = 0;
    g_log_to_file = 1;
    log_printf("Ignition version %.2f initiated\n", IGNITION_VERSION);
    log_printf("Reimplementation version %s\n", IGN_VERSION);
    game_load_system_gfx();

    for (i = 0; i < 4; i++) {
        const s32 *r = g_position_icon_rect;

        g_position_icons[i] = cut_sprite(g_sysgfx, r[0], r[1] + r[3] * i, r[2], r[3]);
    }
    /* The pixel offset 0xc1e7 in the file is row 190, column 153. */
    g_bolt_icon = cut_sprite(g_sysgfx, (0xc1e7 - PIC_PIXELS) & 0xff,
                             (0xc1e7 - PIC_PIXELS) >> 8, 0x3d, 0x3a);

    menu_load_background();
    cone_table_init();
    game_set_menu_mode();
    memset(g_cheats, 0, sizeof(g_cheats));
    joystick_init();
    cd_open();
    cd_set_volume(0xff);
    video_set_palette(g_sys_palette + 8);
    return 1;
}
