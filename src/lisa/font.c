/*
 * font.c - Lisa 2 bitmap fonts.
 *
 * The original indexes the glyph tables with the character as a signed
 * char minus 32, so characters below 32 and above 127 read whatever lies
 * before the tables. The shipped fonts end at 'j' (106) and the game's
 * strings are plain ASCII; here such characters have no glyph.
 */

#include <string.h>

#include "lisa/exit.h"
#include "lisa/file.h"
#include "lisa/font.h"
#include "lisa/mem.h"

struct font g_fonts[FONT_MAX];      /* 0x63f2e0 */

static int s_ready;                 /* 0x4ba6c4 */
static int s_exit_id;               /* 0x50e680 */

static int font_exit(s32 user);

static u16 rd16(const u8 *p)
{
    return (u16)(p[0] | p[1] << 8);
}

static s32 rd32(const u8 *p)
{
    return (s32)((u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24);
}

/* The glyph index of character `c`, or -1. */
static int glyph_index(unsigned char c)
{
    return c >= 32 && c < 128 ? c - 32 : -1;
}

static int has_glyph(const struct font *f, int g)
{
    return g >= 0 && f->present[g] == 1 && f->glyphs[g] != NULL;
}

/* A space in a proportional font: 0.35 of the cell width, from
 * (cell << 8) * 0.35 * (1 / 256.0) truncated. */
static s32 proportional_space(const struct font *f)
{
    return (s32)((double)((s32)(s16)f->cell_width << 8) * 0.35 * 0.00390625);
}

/* 0x456180 */
int font_init(void)
{
    int i;

    if (s_ready == 1) {
        return FONT_ERR_NOT_READY;
    }
    s_ready = 1;
    s_exit_id = exit_alloc_id();
    exit_register(s_exit_id, "fontExit()", font_exit, 0);
    for (i = 0; i < FONT_MAX; i++) {
        g_fonts[i].used = 0;
        g_fonts[i].align = 0;
        g_fonts[i].disabled = 0;
        g_fonts[i].proportional = 1;
        g_fonts[i].spacing = 1;
    }
    return 1;
}

/* 0x456470 */
static int font_free(int font)
{
    struct font *f = &g_fonts[font];
    int g;

    f->used = 0;
    for (g = 0; g < FONT_GLYPHS; g++) {
        if (f->present[g] == 1) {
            sprite_create(NULL, f->glyphs[g]);
        }
    }
    return 1;
}

/* Frees a loaded font's slot and glyph sprites (0x456470). */
int font_unload(int font)
{
    if (font < 0 || font >= FONT_MAX || g_fonts[font].used != 1) {
        return 0;
    }
    return font_free(font);
}

/* 0x456210 */
static int font_exit(s32 user)
{
    int i;

    (void)user;
    if (!s_ready) {
        return FONT_ERR_NOT_INIT;
    }
    exit_unregister(s_exit_id);
    s_ready = 0;
    for (i = 0; i < FONT_MAX; i++) {
        if (g_fonts[i].used == 1) {
            font_free(i);
        }
    }
    return 1;
}

/* 0x456270 */
int font_parse(const u8 *data, int unused)
{
    struct font *f;
    int slot, g;

    (void)unused;
    if (!s_ready) {
        font_init();
    }
    if (memcmp(data, "LFT", 4) != 0) {
        g_file_error = FONT_ERR_MAGIC;
        return -1;
    }
    if (rd16(data + 4) != 100) {
        g_file_error = FONT_ERR_VERSION;
        return -1;
    }
    for (slot = 0; slot < FONT_MAX && g_fonts[slot].used != 0; slot++) {
    }
    if (slot == FONT_MAX) {
        g_file_error = FONT_ERR_NO_SLOT;
        return -1;
    }
    f = &g_fonts[slot];
    f->glyph_count = rd16(data + 6);
    f->cell_width = rd16(data + 8);
    f->height = rd16(data + 0xa);
    for (g = 0; g < FONT_GLYPHS; g++) {
        f->widths[g] = (s16)rd16(data + 0x68c + 2 * g);
    }
    for (g = 0; g < FONT_GLYPHS; g++) {
        s32 offset = rd32(data + 0xc + 4 * g);
        struct tex_desc desc;

        if (offset == -1) {
            f->present[g] = 0;
            f->glyphs[g] = NULL;
            continue;
        }
        f->present[g] = 1;
        memset(&desc, 0, sizeof(desc));
        desc.width = f->widths[g];
        desc.height = (s16)f->height;
        desc.pixels = (u8 *)data + 0x84c + offset;
        desc.stride = f->widths[g];
        f->glyphs[g] = sprite_create(&desc, NULL);
    }
    f->used = 1;
    return slot;
}

/* 0x456420 */
int font_load(const char *name, int unused)
{
    u8 *data = file_load(name);
    int font;

    if (data == NULL) {
        g_file_error = FONT_ERR_LOAD;
        return -1;
    }
    font = font_parse(data, unused);
    mem_free(0, data);
    return font;
}

/* The pixels character `c` moves the pen. */
static s32 advance(const struct font *f, unsigned char c)
{
    int g = glyph_index(c);

    if (!f->proportional) {
        if (has_glyph(f, g) || c == ' ') {
            return (s16)f->cell_width + f->spacing;
        }
        return 0;
    }
    if (has_glyph(f, g)) {
        return f->widths[g] + f->spacing;
    }
    if (c == ' ') {
        return proportional_space(f);
    }
    return 0;
}

/* 0x4564d0
 * The original sets the error for an unknown font and goes on reading
 * it; this one returns 0. */
int text_width(const char *text, int font)
{
    const struct font *f;
    s32 width = 0;
    size_t i;

    if (font < 0 || font >= FONT_MAX || !g_fonts[font].used) {
        g_file_error = FONT_ERR_BAD_FONT;
        return 0;
    }
    f = &g_fonts[font];
    if (f->disabled) {
        return 2;
    }
    for (i = 0; text[i] != '\0'; i++) {
        width += advance(f, (unsigned char)text[i]);
    }
    return width;
}

/* 0x456660 */
int text_draw(const char *text, int font, int x, int y)
{
    return text_draw_scaled(text, font, x, y, 1);
}

/* The port's: text_draw with each glyph and its advance `scale` times
 * as big, for NATIVE's larger 2D. */
int text_draw_scaled(const char *text, int font, int x, int y, int scale)
{
    const struct font *f;
    struct sprite_xform xf;
    s32 pen;
    size_t i;

    if (font < 0 || font >= FONT_MAX || !g_fonts[font].used) {
        return FONT_ERR_BAD_FONT;
    }
    f = &g_fonts[font];
    if (f->disabled) {
        return 2;
    }
    pen = x;
    if (f->align != FONT_ALIGN_LEFT) {
        s32 end = x;

        for (i = 0; text[i] != '\0'; i++) {
            end += advance(f, (unsigned char)text[i]) * scale;
        }
        if (f->align == FONT_ALIGN_CENTER) {
            pen = x + (x - end) / 2;
        } else if (f->align == FONT_ALIGN_RIGHT) {
            pen = 2 * x - end;
        }
    }
    for (i = 0; text[i] != '\0'; i++) {
        unsigned char c = (unsigned char)text[i];
        int g = glyph_index(c);

        if (has_glyph(f, g)) {
            struct sprite_pos pos;
            s32 left = pen;

            /* A fixed font centres a narrow glyph in its cell. */
            if (!f->proportional && f->widths[g] != (s16)f->cell_width) {
                left += ((s16)f->cell_width - f->widths[g]) / 2 * scale;
            }
            pos.x = left << 8;
            pos.y = y << 8;
            xf.scale = (float)scale;
            xf.angle = 0.0f;
            sprite_draw(f->glyphs[g], &pos, scale == 1 ? NULL : &xf);
        }
        pen += advance(f, c) * scale;
    }
    return 1;
}
