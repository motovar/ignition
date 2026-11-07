/*
 * startup.h - the game's video and data set up.
 */

#ifndef IGN_STARTUP_H
#define IGN_STARTUP_H

#include "lisa/sprite.h"
#include "types.h"

extern u8 *g_identity_table;
extern int g_game_pool;
extern int g_fatal_error;
extern char g_error_text[256];
extern s32 g_screen_width;
extern s32 g_screen_height;
/* The largest 2D screen: the original's 800 x 600, or the window's size
 * over its scale in NATIVE. */
enum { SCREEN_MAX_W = 2560, SCREEN_MAX_H = 1440 };

/* The back buffer has guard bands on both sides: a write a little outside
 * it (a row above, a few pixels left of the first) lands in them and not in
 * the globals the linker puts beside it (the screen's size, the fonts).
 * back_guard_check names the stage a stray write was found after. */
enum { BACK_GUARD = 0x10000 };
struct back_block {
    u8 pre[BACK_GUARD];
    u8 buf[SCREEN_MAX_W * SCREEN_MAX_H];
    u8 post[BACK_GUARD];
};
extern struct back_block g_back_block;
#define g_back_buffer (g_back_block.buf)
void back_guard_check(const char *where);
extern u8 *g_sysgfx;
extern u8 *g_sysgfx2;
extern u8 *g_sys_palette;
extern int g_fonts_menu[8];
extern struct sprite *g_position_icons[4];
extern struct sprite *g_bolt_icon;

/* x, y, width, height of the position icons in N_SYSGFX.PIC (0x497bb8). */
extern const s32 g_position_icon_rect[4];
extern s32 g_screen_size;
extern u8 *g_menu_background;
extern u8 g_menu_remap[256];
extern u8 *g_cone_table;
extern s32 g_cheats[10];

int game_video_init(void);

/* The black palette game_video_init sets (0x553470). */
const u8 *startup_black_palette(void);

/* Sets the menu's video mode from g_screen_size. */
int game_set_menu_mode(void);

#endif /* IGN_STARTUP_H */
