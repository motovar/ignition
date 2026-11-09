/*
 * race/debug.c - the race's debug lines (0x43e9c0): the frame rate,
 * and in a replay the camera's roll, else the player's road piece, the
 * second car's squash, the two drivers' kinds, and the player's skill
 * and speed, in IGNITION.FNT (0x4133d0).
 *
 * The original has the code but never calls it, and nothing sets its
 * switch (0x4949b4). The port shows the frame rate with --fps or Alt+F,
 * and the other lines too at a second Alt+F (r_4949b4 2).
 */

#include <math.h>
#include <stdio.h>

#include "race/cars.h"
#include "race/debug.h"
#include "race/init.h"
#include "race/race.h"
#include "startup.h"

s32 r_4949b4;                       /* the frame rate on */
static s32 s_recording;             /* 0x4949dc: "RECORDING!", never set */

extern s32 r_494984;                /* race.c: this frame's rate */
extern s32 r_563bec;                /* view.c: the replay camera's roll */
extern s32 r_4949bc;                /* view.c: the replay camera's numbers on */

/* 0x4133d0
 * `text` at x, y of dst (rows `pitch` apart) in a raw font: its height
 * at +2, the gap after each glyph at +4, the glyphs' widths at +6, each
 * character's glyph at +0xc6, each glyph's offset at +0x1c6 into the
 * pixels at +0x546, a glyph's rows `width` apart. A pixel that is not 0
 * is written as `colour` plus it. */
static void debug_text(int x, int y, const char *text, u8 *dst, int pitch, const u8 *font,
                       int colour)
{
    s16 height, gap;
    int top = y * pitch, bottom;

    if (font == NULL) {
        return;
    }
    height = (s16)(font[2] | font[3] << 8);
    gap = (s16)(font[4] | font[5] << 8);
    bottom = top + height * pitch;
    for (; *text != '\0'; text++) {
        s8 glyph = (s8)font[0xc6 + (s8)*text];
        s8 width = (s8)font[6 + glyph];
        const u8 *o = font + 0x1c6 + 4 * glyph;
        const u8 *src = font + 0x546 + (o[0] | o[1] << 8 | o[2] << 16 | (u32)o[3] << 24);
        int row, col, k = 0;

        for (row = top; row < bottom; row += pitch) {
            for (col = 0; col < width; col++) {
                u8 p = src[k + col];

                if (p != 0) {
                    dst[x + col + row] = (u8)(colour + p);
                }
            }
            k += width;
        }
        x += width + gap;
    }
}

static void line(int x, int y, const char *text)
{
    debug_text(x, y, text, g_back_buffer, g_screen_width, g_race_fnt, 0x14);
}

/* 0x43ec70
 * The replay camera's numbers, one to a line down the left (r_4949bc on):
 * its position, X, Y and Z, and its angles, X VIN and Y VIN. */
void race_replay_numbers(void)
{
    char buf[32];

    if (r_4949bc == 0) {
        return;
    }
    snprintf(buf, sizeof(buf), "X %.0f", r_5532c0);
    line(0, 0, buf);
    snprintf(buf, sizeof(buf), "Y %.0f", r_5532b8);
    line(0, 10, buf);
    snprintf(buf, sizeof(buf), "Z %.0f", r_553288);
    line(0, 20, buf);
    snprintf(buf, sizeof(buf), "X VIN %.2f", r_6192f8);
    line(0, 30, buf);
    snprintf(buf, sizeof(buf), "Y VIN %.2f", r_619318);
    line(0, 40, buf);
}

/* 0x43e9c0 */
void race_debug_lines(void)
{
    char buf[36];

    if (r_4949b4) {
        snprintf(buf, sizeof(buf), "FPS %d", r_494984);
        line(0x10e, 0, buf);
    }
    /* The port's: the other lines only when asked for. */
    if (r_4949b4 < 2) {
        return;
    }
    if (s_recording == 1) {
        line(0x96, 0x0a, "RECORDING!");
    }
    if (r_5532b0 == 1) {
        snprintf(buf, sizeof(buf), "ROLL %.1f", (double)(r_563bec / 10));
        line(5, 0x1e, buf);
        return;
    }
    snprintf(buf, sizeof(buf), "OLDROADNR %d", g_cars[0].f364);
    line(5, 0x0a, buf);
    snprintf(buf, sizeof(buf), "SQUASHED2 %d", g_cars[1].f560);
    line(5, 0x14, buf);
    snprintf(buf, sizeof(buf), "TYPE1 %d", g_drivers[0].control);
    line(0, 0x28, buf);
    snprintf(buf, sizeof(buf), "TYPE2 %d", g_drivers[1].control);
    line(0, 0x32, buf);
    snprintf(buf, sizeof(buf), "SKILLNAD %.3f", fabs(g_cars[0].f128 - g_cars[0].f120));
    line(0, 0x3c, buf);
    snprintf(buf, sizeof(buf), "SPEED %.3f", fabs(g_cars[0].f118));
    line(0, 0x46, buf);
}
