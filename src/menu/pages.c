/*
 * menu/pages.c - what the front end draws for its pages: handler A
 * (0x409fb0), which every page runs once a frame.
 *
 * Pages are named by their index in the current language's table
 * (g_frontend_pages, 0x4be768): 4 and 5 choose player 1's and player
 * 2's car, 6 the track, 8 is the network game, 16 the best times, 21
 * the language.
 */

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "canvas.h"
#include "dik.h"
#include "frontend/frontend.h"
#include "frontend/state.h"
#include "game.h"
#include "input.h"
#include "lisa/file.h"
#include "lisa/font.h"
#include "lisa/gfx.h"
#include "lisa/sprite.h"
#include "menu/menu.h"
#include "lisa/texture.h"
#include "menu/engine.h"
#include "menu/pages.h"
#include "menu/panels.h"
#include "settings.h"
#include "sound.h"
#include "startup.h"
#include "stubs.h"
#include "video.h"

enum {
    PAGE_CAR_P1 = 4,
    PAGE_CAR_P2 = 5,
    PAGE_TRACK = 6,
    PAGE_NETWORK = 8,
    PAGE_CD = 10,
    PAGE_DIFFICULTY = 15,
    PAGE_BEST_TIMES = 16,
    PAGE_LANGUAGE = 21
};

s32 g_car_order[11];                    /* 0x4be6e0 */
s32 g_car_count;                        /* 0x4be858 */
struct sprite *g_sprite_track;          /* 0x4bf55c */
struct sprite *g_sprite_flags[5];       /* 0x4be618 */

double g_menu_background_t;           /* 0x4bf570 */

static struct menu_page *page(int k)
{
    return g_frontend_pages[k];
}

/* 0x40b7a0 */
void draw_sprite_at(const struct sprite *s, int x, int y)
{
    struct sprite_pos pos;

    pos.x = x << 8;
    pos.y = y << 8;
    sprite_draw(s, &pos, NULL);
}

/* 0x40b7d0 */
void draw_sprite_scaled(const struct sprite *s, int x, int y, float scale, float angle)
{
    struct sprite_pos pos;
    struct sprite_xform xf;

    pos.x = x << 8;
    pos.y = y << 8;
    xf.scale = scale;
    xf.angle = angle;
    sprite_draw(s, &pos, &xf);
}

/* 0x446240: dst = table[src << 8 | dst] over the rectangle
 * (x0, y0)..(x1, y1) of src, put at (dx, dy) of dst. */
void blend_rect(const u8 *src, int x0, int y0, int x1, int y1, u8 *dst, int dx, int dy,
                const u8 *table, int src_pitch, int dst_pitch)
{
    int r, x, shade;

    if (canvas_blend_rect(src, x0, y0, x1, y1, dst, dx, dy, table, src_pitch)) {
        return;
    }
    /* Over a layered 3D's clear buffer the pixel under is the 3D's, which
     * the table's row is put through on the GPU (lisa/gfx.h). */
    shade = g_gfx_shade != NULL && dst == g_gfx_shade_base && table == g_gfx_shade_table;
    for (r = 0; r < y1 - y0; r++) {
        const u8 *s = src + (y0 + r) * src_pitch;
        u8 *d = dst + (dy + r) * dst_pitch + dx - x0;

        for (x = x0; x < x1; x++) {
            if (shade && d[x] == 0 && s[x] != 0) {
                gfx_shade_set(dx + (x - x0), dy + r, s[x], 0);
            } else {
                d[x] = table[s[x] << 8 | d[x]];
            }
        }
    }
}

/* blend_rect's rectangle stretched to dw x dh at (dx, dy), each pixel
 * the nearest, clipped to dst's dst_w x dst_h. */
void blend_rect_scaled(const u8 *src, int x0, int y0, int x1, int y1, u8 *dst, int dx, int dy,
                       int dw, int dh, const u8 *table, int src_pitch, int dst_w, int dst_h)
{
    int r, c, w = x1 - x0, h = y1 - y0, shade;

    if (canvas_blend_scaled(src, x0, y0, x1, y1, dst, dx, dy, dw, dh, table, src_pitch)) {
        return;
    }
    shade = g_gfx_shade != NULL && dst == g_gfx_shade_base && table == g_gfx_shade_table;
    for (r = 0; r < dh; r++) {
        const u8 *s = src + (y0 + r * h / dh) * src_pitch + x0;
        u8 *d = dst + (dy + r) * dst_w;

        if (dy + r < 0 || dy + r >= dst_h) {
            continue;
        }
        for (c = 0; c < dw; c++) {
            if (dx + c >= 0 && dx + c < dst_w) {
                u8 sp = s[c * w / dw];

                if (shade && d[dx + c] == 0 && sp != 0) {
                    gfx_shade_set(dx + c, dy + r, sp, 0);
                } else {
                    d[dx + c] = table[sp << 8 | d[dx + c]];
                }
            }
        }
    }
}

static const char *mini_text(u32 addr)
{
    return ui_text(addr + 200 * g_menu_language);
}

/* Centred text. The hints at the screen's foot (PRESS ANY KEY, SELECT
 * YOUR LANGUAGE, the players' car prompts) go down into NATIVE's bottom
 * margin, most of the way to the window's foot. */
static void draw_centred(const char *text, int font, int y)
{
    float mx, my;

    canvas_extent(g_menu_screen, &mx, &my);
    if (y >= 0xb0) {
        y += (int)(my * 0.75f);
    }
    text_draw(text, font, 160 - text_width(text, font) / 2, y);
}

/* A page's band of rows blended in, its ends spread with the items in
 * NATIVE (menu_spread_y) so the items stay inside it. */
static void band_rect(int w, int x0, int y0, int x1, int y1, const u8 *tab)
{
    int half = g_menu_modes[g_menu_mode_index][1] / 2;
    int a = menu_place_y(y0 - half) + half, b = menu_place_y(y1 - half) + half;

    a = a < 0 ? 0 : a;
    b = b > half * 2 ? half * 2 : b;

    blend_rect(g_menu_buffer2, x0, a, x1, b, g_menu_screen, x0, a, tab, w, w);
}

/* The blended panel layer under the current page (0x40a5e6). */
static void blend_panels(void)
{
    const s32 *mode = g_menu_modes[g_menu_mode_index];
    int w = mode[0];
    struct menu_page *cur = g_menu_view.page;
    const u8 *tab = g_menu_table;
    const struct menu_visual *v;

    if (cur == page(PAGE_BEST_TIMES)) {
        blend_rect(g_menu_buffer2, 8, 0, 0x13f, 0xb4, g_menu_screen, 8, 0, tab, w, w);
    } else if (cur == page(PAGE_NETWORK)) {
        blend_rect(g_menu_buffer2, 0x0a, 0, 0x13f, 0xc7, g_menu_screen, 0x0a, 0, tab, w, w);
    } else if (cur == page(22) || cur == page(23) || cur == page(24)) {
        band_rect(w, 0, 0x40, 0x13f, 0x9f, tab);
    } else if (cur == page(17) || cur == page(18)) {
        band_rect(w, 0, 0x41, 0x140, 0xa8, tab);
    } else if (cur == page(19) || cur == page(20)) {
        band_rect(w, 0, 0x41, 0x140, 0xbc, tab);
    } else if ((v = cur->items[0].visuals) != NULL
               && (fe_4be850 == 0.0 || fe_4be850 >= 1306.8)) {
        int y = menu_place_y(v->y) + mode[1] / 2 - 4;
        int y1 = y + menu_spread_y(cur->count * 19) + 5;
        float mx, my;

        /* A last item low on the page has its panel's foot past the
         * screen's 200 rows: NATIVE's margin shows it, and the layer has
         * the rows; otherwise it is cut at the foot. */
        canvas_extent(g_menu_screen, &mx, &my);
        if (y1 > mode[1] + (my > 0 ? 40 : 0)) {
            y1 = mode[1] + (my > 0 ? 40 : 0);
        }
        blend_rect(g_menu_buffer2, 0, y, 0x13f, y1,
                   g_menu_screen,
                   0, y, tab, w, w);
    }
}

/* A panel around `text` placed as a visual places its text: from the
 * screen centre, in 19 line rows (0x40b8d6). */
static void panel_for_text(const char *text, int font, s16 tx, s16 ty, int highlight)
{
    const s32 *mode = g_menu_modes[g_menu_mode_index];
    int y = menu_place_y(ty) + mode[1] / 2;

    panel_add(tx + mode[0] / 2 - 13, y / 19, text_width(text, g_fonts_menu[font]),
              y % 19 - 12, highlight);
}

/* A one line question in a box, for pages 22 to 24 (0x40bd9e). */
static void dialog(const char *text)
{
    int font = g_fonts_menu[3];
    int w = text_width(text, font);

    panels_clear();
    panel_add(0x93 - w / 2, 3, w, 1, 1);
    panels_draw(g_menu_buffer2, g_menu_modes[g_menu_mode_index][0]);
    text_draw(text, font, 0xa0 - w / 2, 0x46);
}

/* Lines of centred text in boxes, from y 0x46 down (0x40bf89). */
static void text_lines(u32 base, int lines)
{
    int font = g_fonts_menu[3];
    int i;

    panels_clear();
    for (i = 0; i < lines; i++) {
        const char *t = ui_text(base + 4 * (50 * g_menu_language + i));
        int w = text_width(t, font);
        int y = 0x46 + 19 * i;

        panel_add(0x93 - w / 2, y / 19, w, y % 19 - 12, 1);
    }
    panels_draw(g_menu_buffer2, g_menu_modes[g_menu_mode_index][0]);
    for (i = 0; i < lines; i++) {
        const char *t = ui_text(base + 4 * (50 * g_menu_language + i));

        text_draw(t, font, 0xa0 - text_width(t, font) / 2, 0x46 + 19 * i);
    }
}

/* 0x40b820
 * Redraws the page's layer (g_menu_buffer2, blended over the picture by
 * handler A): a panel round every item's text, then the texts. While
 * idle it shows the credits instead, a title and up to three names a
 * step. */
int menu_redraw_page(void)
{
    const s32 *mode = g_menu_modes[g_menu_mode_index];
    struct menu_page *cur = g_menu_view.page;
    const char *title = NULL;
    const char *names[3] = { NULL, NULL, NULL };
    int step = 0, i;

    if (g_menu_sound) {
        sound_tick();
    }
    memset(g_menu_buffer2, 0, 64000);
    if (fe_4be850 != 0.0) {
        return 0;
    }
    set_target(g_menu_buffer2, mode[0], mode[0], mode[1], 8);
    g_gfx_color_table = g_identity_table;
    if (fe_4be598 < 1080.0) {
        panels_clear();
        for (i = 0; i < cur->count; i++) {
            struct menu_item *it = &cur->items[i];
            const struct menu_visual *v = it->visuals;

            if (v == NULL) {
                continue;
            }
            if (v->text != NULL) {
                panel_for_text(v->text, v->font, v->text_x, v->text_y, 0);
            }
            if (it->value != NULL) {
                const struct menu_visual *vv = &v[(*it->value - it->min) * 2 + 2];

                panel_for_text(vv->text, vv->font, vv->text_x, vv->text_y, 1);
            } else if (it->choice != NULL) {
                const struct menu_choice *ch = it->choice;
                const char *text = ch->words[6] == MENU_CHOICE_KEY
                                 ? g_key_names[ch->data[0]] : (const char *)ch->data;

                panel_for_text(g_menu_view.edit == 1 ? "WWW|" : text, ch->words[2],
                               ch->words[0], ch->words[1], 1);
            }
        }
    } else {
        step = (int)(fe_4be598 - 1080.0) / 0xa8;
        panels_clear();
        if (idle_seq(step) >= 0) {
            title = ui_text(0x47dfe8 + 4 * (idle_seq(step) + 50 * g_menu_language));
            panel_add(0x0b, 3, text_width(title, g_fonts_menu[1]), 5, 0);
        }
        for (i = 0; i < 3; i++) {
            s16 who = g_idle_seq_raw[(0x47e4e6 - 0x47e4a0) / 2 + 4 * step - i];
            int y = 0x7b + 19 * i;

            if (who >= 0) {
                int w;

                names[i] = ui_text(0x47e498 + 4 * who);
                w = text_width(names[i], g_fonts_menu[3]);
                panel_add(0x11b - w, y / 19, w, y % 19 - 12, 1);
            }
        }
        panels_draw(g_menu_buffer2, mode[0]);
    }
    if (fe_4bf564 == 0 && fe_4be598 < 1080.0) {
        panels_apart(menu_spread_y(19) != 19);
        panels_draw(g_menu_buffer2, mode[0]);
        panels_apart(0);
        menu_draw_items();
    } else if (fe_4be598 >= 1080.0) {
        if (title != NULL) {
            text_draw(title, g_fonts_menu[1], 0x18, 0x4a);
        }
        for (i = 0; i < 3; i++) {
            if (names[i] != NULL) {
                text_draw(names[i], g_fonts_menu[3],
                          0x128 - text_width(names[i], g_fonts_menu[3]), 0x7b + 19 * i);
            }
        }
        draw_centred(mini_text(0x47dfec), g_fonts_menu[6], 0xbf);
    }
    if (fe_4be598 < 1080.0) {
        if (fe_4bf564) {
            const char *t = mini_text(0x47dfe8);
            int w = text_width(t, g_fonts_menu[1]);

            panels_clear();
            panel_add(0x93 - w / 2, 4, w, 2, 0);
            panels_draw(g_menu_buffer2, mode[0]);
            text_draw(t, g_fonts_menu[1], 0xa0 - w / 2, 0x5a);
        }
        if (cur == page(22)) {
            dialog(mini_text(0x47e0a4));
        } else if (cur == page(23)) {
            dialog(mini_text(0x47e0a8));
        } else if (cur == page(24)) {
            dialog(mini_text(0x47e0ac));
        }
        if (cur == page(17) || cur == page(18)) {
            text_lines(0x47e060, 4);
        }
        if (cur == page(19) || cur == page(20)) {
            text_lines(0x47e088, 5);
        }
    }
    g_gfx_color_table = g_menu_table;
    set_target(g_menu_screen, mode[0], mode[0], mode[1], 8);
    return 0;
}

/* 0x40c520: seconds as mm:ss:hh, minutes capped at 99. The text is in
 * a shared buffer. */
const char *format_time(double t)
{
    static char text[40];
    int minutes = (int)(t * 0.016666666666666666);
    int whole;

    if (minutes > 99) {
        minutes = 99;
    }
    whole = (int)t;
    sprintf(text, "%.2d:%.2d:%.2d", minutes, whole - 60 * minutes,
            (int)((t - whole) * 100.0));
    return text;
}

/* 0x40c1e0
 * The best times page: five rows of place, initials and time for the
 * chosen track, the race times or, with TIME set to TOTAL, the laps.
 * The records are in the settings, 120 bytes a track from 0x1bf:
 * five initials of 4 bytes, five doubles, then the same for laps. */
int menu_best_times(void)
{
    static const char *const place[5] = { "1", "2", "3", "4", "5" };
    const s32 *mode = g_menu_modes[g_menu_mode_index];
    int font = g_fonts_menu[2];
    int base = 0x1bf + 120 * settings_get(0x57f) + (settings_get(0x583) ? 60 : 0);
    int i;

    set_target(g_menu_buffer2, mode[0], mode[0], mode[1], 8);
    g_gfx_color_table = g_identity_table;
    panels_clear();
    for (i = 0; i < 5; i++) {
        int y = 0x4d + 19 * i;

        panel_add(0x27, y / 19, 0x0f, y % 19 - 12, 1);
        panel_add(0x51, y / 19, 0x2d, y % 19 - 12, 1);
        panel_add(0x99, y / 19, 0x66, y % 19 - 12, 1);
    }
    panels_draw(g_menu_buffer2, mode[0]);
    for (i = 0; i < 5; i++) {
        int y = 0x4d + 19 * i;
        double t;

        memcpy(&t, g_settings + base + 20 + 8 * i, sizeof(t));
        g_fonts[font].align = FONT_ALIGN_CENTER;
        text_draw(place[i], font, 0x3b, y);
        text_draw((const char *)g_settings + base + 4 * i, font, 0x74, y);
        text_draw(format_time(t), font, 0xd9, y);
        g_fonts[font].align = FONT_ALIGN_LEFT;
    }
    g_gfx_color_table = g_menu_table;
    set_target(g_menu_screen, mode[0], mode[0], mode[1], 8);
    return 0;
}

/* Player keys move the car selection on page 5; elsewhere the key map
 * is restored (0x40a003). */
static void player_keys(void)
{
    static const s8 actions[4] = { MENU_KEY_DEC_WRAP, MENU_KEY_INC_WRAP,
                                   MENU_KEY_UP_WRAP, MENU_KEY_DOWN_WRAP };
    int i;

    if (g_menu_view.page == page(PAGE_CAR_P2)) {
        for (i = 0; i < 4; i++) {
            u8 key = g_settings[0x187 + i];

            if (g_menu_keymap[key] == 0) {
                g_menu_keymap[key] = actions[i];
            }
        }
    } else {
        for (i = 1; i <= 4; i++) {
            u8 key = g_settings[0x186 + i];

            g_menu_keymap[key] = g_menu_keymap_orig[key];
        }
    }
}

/* The cars of the selection, from `first` on, wrapped (0x40ab5a). */
static void car_ring(int player, int first, int n, s32 *cars)
{
    int k;

    for (k = 0; k < n; k++) {
        s32 idx = settings_get(0x40 + 4 * player) + k + first;

        if (idx < 0) {
            idx += g_car_count;
        }
        if (g_car_count <= idx) {
            idx -= g_car_count;
        }
        cars[k] = g_car_order[idx];
    }
}

/* The car carousel under the selected car (0x40ab3e): three cars at
 * rest, four while it turns. The cars are 3D (car3d_draw). */
static void car_carousel(int player)
{
    double spin = menu_dt() * 27.777777777777775;
    s32 cars[4];

    if (fe_4be87c > 0) {
        double s = sin(fe_4be87c * 0.00392699081698725);

        if (fe_4be5fc < 0) {
            car_ring(player, -1, 4, cars);
            car3d_draw(cars[0], (int)(spin * 1.0588235294117647), s + 0.01, 0x4e, 0x9e);
            car3d_draw(cars[3], (int)(spin * 1.0588235294117647), 1.01 - s, 0xf0, 0x9e);
            car3d_draw(cars[1], (int)(spin * 1.0588235294117647), s * 0.6000000000000001 + 1.01,
                       (int)(s * 81.0 + 78.0), 0x9e);
            car3d_draw(cars[2], (int)(spin * 1.894736842105263), s * -0.6000000000000001 + 1.61,
                       (int)(s * 81.0 + 159.0), 0x9e);
        } else {
            car_ring(player, -2, 4, cars);
            car3d_draw(cars[3], (int)(spin * 1.0588235294117647), s + 0.01, 0xf0, 0x9e);
            car3d_draw(cars[2], (int)(spin * 1.0588235294117647), s * 0.6000000000000001 + 1.01,
                       (int)(s * -81.0 + 240.0), 0x9e);
            car3d_draw(cars[0], (int)(spin * 1.0588235294117647), 1.01 - s, 0x4e, 0x9e);
            car3d_draw(cars[1], (int)(spin * 1.894736842105263), s * -0.6000000000000001 + 1.61,
                       (int)(s * -81.0 + 159.0), 0x9e);
        }
    } else {
        car_ring(player, -1, 3, cars);
        car3d_draw(cars[0], (int)(spin * 1.0588235294117647), 1.01, 0x4e, 0x9e);
        car3d_draw(cars[2], (int)(spin * 1.0588235294117647), 1.01, 0xf0, 0x9e);
        car3d_draw(cars[1], (int)(spin * 1.894736842105263), 1.61, 0x9f, 0x9e);
    }
}

/* How far a picture has grown back after a change: 1 - t * rate,
 * folded and capped at 1 (0x40a90e). */
static float grow(s32 t, double rate)
{
    double v = t * rate + 1.0;

    if (v < 0.0) {
        v = -v;
    }
    return (float)(1.0 < v ? 1.0 : v);
}

/* Remakes the sprites that show the current choice when its page opens
 * or the choice changes. */
static void make_choice_sprites(void)
{
    struct menu_page *cur = g_menu_view.page;
    int changed = g_menu_view.page_changed;
    int i;

    if (changed || fe_4bf554) {
        if (cur == page(PAGE_CAR_P1) || cur == page(PAGE_CAR_P2)) {
            s32 car;

            fe_4bf554 = 0;
            car = *cur->items[cur->selected].value;
            /* A value from the settings file outside 0..10 would read
             * past the table and the pictures. */
            car = g_car_order[car < 0 || car > 10 ? 0 : car];
            if (car < 0 || car > 10) {
                car = 0;
            }
            g_menu_desc.width = g_menu_desc.stride = 0xba;
            g_menu_desc.height = 0x3d;
            g_menu_desc.hot_x = 0x5d00;
            g_menu_desc.hot_y = 0x1e80;
            g_menu_desc.pixels = g_pic_cars + car * (0xba * 0x3d);
            g_sprite_car = sprite_create(&g_menu_desc, g_sprite_car);
        }
    }
    if ((changed && cur == page(PAGE_TRACK)) || fe_4be728) {
        struct menu_page *tracks = page(PAGE_TRACK);
        int locked = 2 - page(PAGE_DIFFICULTY)->items[0].max;

        /* The original sets the kind of items 6 and 7, but the page has 7:
         * its 8th record is whatever follows the table, and here a
         * neighbouring page's action or a visual's pixel pointer took the
         * write (a pixel pointer of 1 crashed the next sprite made). */
        for (i = 6; i < 8; i++) {
            if (i < tracks->count) {
                tracks->items[i].kind = 1;
            }
        }
        for (i = 0; i < locked && i < 2; i++) {
            if (7 - i < tracks->count) {
                tracks->items[7 - i].kind = 0;
            }
        }
        fe_4be728 = 0;
        g_menu_desc.width = g_menu_desc.stride = 0x73;
        g_menu_desc.height = 0x63;
        g_menu_desc.hot_x = 0x3980;
        g_menu_desc.hot_y = 0x3980;
        g_menu_desc.pixels = g_pic_tracks
            + g_track_pic_offsets[tracks->selected < 0 || tracks->selected > 7 ? 0 : tracks->selected];
        g_sprite_track = sprite_create(&g_menu_desc, g_sprite_track);
    }
    if ((changed && cur == page(PAGE_LANGUAGE)) || fe_4be6d8) {
        fe_4be6d8 = 0;
        g_menu_desc.width = g_menu_desc.stride = 0x3d;
        g_menu_desc.height = 0x35;
        g_menu_desc.hot_x = 0;
        g_menu_desc.hot_y = 0;
        for (i = 0; i < 5; i++) {
            /* The chosen language's flag is the lit one, six flags on. */
            int offset = i * 0xca1 + (i == settings_get(SET_LANGUAGE) ? 0x4bc6 : 0);

            g_menu_desc.pixels = g_pic_flags + offset;
            g_sprite_flags[i] = sprite_create(&g_menu_desc, g_sprite_flags[i]);
        }
    }
}

/* The tracks' short names (0x47dfa8). */
static const char *const s_track_short[8] = {
    "MOOSEJAW F.", "GOLD RUSH", "SNAKE ISLAND", "LOST RUINS",
    "YODEL PEAKS", "CAPE THOR", "TOKYO BULLET", "MOOSEJAW FALLS"
};

/* Where the players' badges go about (0xa0, 0x69), by how many players
 * there are, 1 to 6 (built on the stack at 0x40b12a). */
static const s8 s_badge_xy[6][6][2] = {
    { { 0, 0 }, { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 } },
    { { -30, 0 }, { 30, 0 }, { -1, -1 }, { -1, -1 }, { -1, -1 }, { -1, -1 } },
    { { -60, 0 }, { 0, 0 }, { 60, 0 }, { -1, -1 }, { -1, -1 }, { -1, -1 } },
    { { -30, -30 }, { 30, -30 }, { -30, 30 }, { 30, 30 }, { -1, -1 }, { -1, -1 } },
    { { -60, -30 }, { 60, -30 }, { 0, 0 }, { -60, 30 }, { 60, 30 }, { -1, -1 } },
    { { -60, -30 }, { 0, -30 }, { 60, -30 }, { -60, 30 }, { 0, 30 }, { 60, 30 } }
};

/* 0x40af54, in handler A: the network page. The track, a badge and the
 * name of each human player, and what the session waits for; the
 * messages go out (net_menu, then net_send_settings once joined), and
 * the race starts when the host says so. */
static void menu_network_page(void)
{
    const s32 *mode = g_menu_modes[g_menu_mode_index];
    int w = mode[0];
    int title_w, track_w, half, humans, k, i;
    const char *track = s_track_short[(u32)settings_get(0x0f3) & 7];
    const char *text = NULL;

    fe_4be568++;
    fe_net.mode = g_net_state;
    if (fe_4be568 == 1) {
        menu_apply_settings();
    }
    memset(g_menu_buffer2, 0, 64000);
    set_target(g_menu_buffer2, mode[0], mode[0], mode[1], 8);
    g_gfx_color_table = g_identity_table;
    if (fe_net.mode != 2 && settings_get(0x008) == 2) {
        settings_set(0x153, g_net_players + 1);
    }
    panels_clear();
    title_w = text_width(mini_text(0x47e004), g_fonts_menu[1]);
    track_w = text_width(mini_text(0x47e008), g_fonts_menu[1]);
    half = title_w / 2;
    panel_add(0x93 - half, 0, title_w, -2, 0);
    panel_add(0x0b, 1, track_w, -2, 0);
    panel_add(track_w + 0x26, 1, text_width(track, g_fonts_menu[1]), -2, 1);
    panels_draw(g_menu_buffer2, w);
    text_draw(mini_text(0x47e004), g_fonts_menu[1], 0xa0 - half, 0x0a);
    text_draw(mini_text(0x47e008), g_fonts_menu[0], 0x18, 0x1d);
    text_draw(track, g_fonts_menu[2], track_w + 0x33, 0x1d);

    /* The badges: player-driven cars (control 0, or 2 over the network). */
    panels_clear();
    for (i = 0, humans = 0; i < settings_get(0x153); i++) {
        s32 control = settings_get(0x15f + 4 * i);

        if (control == 0 || control == 2) {
            humans++;
        }
    }
    g_fonts[g_fonts_menu[7]].align = FONT_ALIGN_CENTER;
    for (i = 0, k = 0; i < settings_get(0x153); i++) {
        s32 control = settings_get(0x15f + 4 * i);
        char name[16];
        int x, y;

        if (control != 0 && control != 2) {
            continue;
        }
        /* The original reads past its table with more than 6. */
        if (humans > 6 || k >= 6) {
            break;
        }
        x = s_badge_xy[humans - 1][k][0] + 0xa0;
        y = s_badge_xy[humans - 1][k][1];
        k++;
        draw_sprite_at(g_sprite_badges[(u32)settings_get(0x01c + 4 * i) % 12], x, y + 0x69);
        snprintf(name, sizeof(name), "%s", (const char *)g_settings + 0x06c + 15 * i);
        text_draw(name, g_fonts_menu[7], x, y + 0x7f);
    }
    g_fonts[g_fonts_menu[7]].align = FONT_ALIGN_LEFT;
    panels_draw(g_menu_buffer2, w);

    if (fe_net.stage == 0) {
        int text_w = title_w;

        panels_clear();
        if (fe_net.mode == 3) {
            text = mini_text(g_net_players > 0 ? 0x47e010 : 0x47e018);
        } else if (fe_net.mode == 4) {
            text = mini_text(0x47e014);
        }
        if (text != NULL) {
            text_w = text_width(text, g_fonts_menu[1]);
            panel_add(0x93 - text_w / 2, 9, text_w, -9, 0);
        }
        panels_draw(g_menu_buffer2, w);
        if (text != NULL) {
            text_draw(text, g_fonts_menu[1], 0xa0 - text_w / 2, 0xae);
        }
        net_menu(&fe_net, g_settings);
    }
    if (fe_net.stage == 1) {
        panels_clear();
        if (fe_net.mode == 4) {
            text = mini_text(0x47e018);
            half = text_width(text, g_fonts_menu[1]);
            panel_add(0x93 - half / 2, 9, half, -9, 0);
            panels_draw(g_menu_buffer2, w);
            text_draw(text, g_fonts_menu[1], 0xa0 - half / 2, 0xae);
        }
        net_send_settings(&fe_net, g_settings);
    }
    if (fe_net.stage == 2 && fe_4be8a0 == 0.0) {
        menu_cb_4045e0(0, MENU_KEY_ACTIVATE);
    }
    g_gfx_color_table = g_menu_table;
    set_target(g_menu_screen, mode[0], mode[0], mode[1], 8);
}

/* 0x409fb0 */
int menu_page_handler_a(void)
{
    const s32 *mode = g_menu_modes[g_menu_mode_index];
    struct menu_page *cur;
    int intro_done = fe_4be850 == 0.0 || fe_4be850 >= 1306.8;
    s16 panel;
    int i;

    /* Backspace goes back, except while typing. */
    g_menu_keymap[0x0e] = g_menu_view.edit == 0 ? MENU_KEY_BACK : 0;
    if (g_settings[0x6c] == 0) {
        g_settings[0x6c] = ' ';
        g_settings[0x6d] = 0;
    }
    if (g_settings[0x7b] == 0) {
        g_settings[0x7b] = ' ';
        g_settings[0x7c] = 0;
    }
    player_keys();
    cur = g_menu_view.page;
    if (cur != page(22) && cur != page(23) && cur != page(24)) {
        page(22)->selected = 1;
        page(23)->selected = 1;
        page(24)->selected = 1;
    }
    if (g_cd_track_count > 0) {
        struct menu_item *track = &page(PAGE_CD)->items[2];

        track->max = g_cd_track_count > 25 ? 25 : g_cd_track_count;
        /* A saved track past the disc's last: the original presses
         * RIGHT (0x40a108) to wrap it round, on whatever page is up, which
         * on the language page walks the flags to the last one. The port
         * wraps the setting itself. */
        if (track->max < settings_get(SET_CD_TRACK)) {
            settings_set(SET_CD_TRACK, track->min);
        }
    }
    g_joysticks[0].enabled = settings_get(0x123) > 0;
    g_joysticks[1].enabled = settings_get(0x127) > 0;
    make_choice_sprites();

    if (fe_4be58c) {
        fe_4be58c = 0;
        menu_redraw_page();
        if (cur == page(PAGE_BEST_TIMES) && fe_4be598 < 1080.0) {
            menu_best_times();
        }
    }
    if (fe_4be850 == 0.0 || fe_4be850 >= 1278.0) {
        if (cur == page(PAGE_LANGUAGE)) {
            canvas_clear(g_menu_screen, 0, 64000);
        } else {
            g_menu_background_t += menu_dt();
            if (g_menu_background_t >= 2.1176470588235294) {
                while (g_menu_background_t >= 2.1176470588235294) {
                    g_menu_background_t -= 2.1176470588235294;
                }
                menu_background_render();
            }
            canvas_copy(g_menu_screen, g_menu_buffer1, 64000);
        }
    }
    if (cur != page(PAGE_LANGUAGE) && cur != page(13) && cur != page(14)
        && cur != page(PAGE_BEST_TIMES) && cur != page(PAGE_NETWORK) && intro_done) {
        float mx, my;

        /* In NATIVE the logo goes up into the margin, to the window's
         * top, and the page below it has the height to itself. */
        canvas_extent(g_menu_screen, &mx, &my);
        draw_sprite_at(g_sprite_logo, 0x4b, 2 - (int)my);
    }
    panel = idle_seq((int)(fe_4be598 - 1080.0) / 0xa8);
    if (panel == -2) {
        draw_sprite_at(g_sprite_panel, 0xa0, 0x7d);
    } else if (panel == -3) {
        draw_sprite_at(g_sprite_panel2, 0xa0, 0x7d);
    }
    if (cur == page(PAGE_BEST_TIMES) && g_menu_view.page_changed) {
        menu_best_times();
    }
    if (g_menu_sound) {
        sound_tick();
    }
    if (fe_4be598 > 1080.0) {
        int w = mode[0];

        blend_rect(g_menu_buffer2, 0x0a, 0x44, 0x13f, (s32)g_frame_time % 0x329 < 500 ? 0xc7 : 0xbf,
                   g_menu_screen, 0x0a, 0x44, g_menu_table, w, w);
    } else {
        blend_panels();
    }

    if (fe_4be598 < 1080.0) {
        if (cur == page(PAGE_LANGUAGE)) {
            /* Four flags: the second language has none here. */
            for (i = 0; i < 5; i++) {
                if (i != 1) {
                    draw_sprite_at(g_sprite_flags[i], (i > 1 ? i - 1 : i) * 69 + 0x1a, 0x4a);
                }
            }
            draw_centred("SELECT YOUR LANGUAGE", g_fonts_menu[6], 0xbf);
        }
        if (cur == page(PAGE_CAR_P1) || cur == page(PAGE_CAR_P2)) {
            float scale = grow(fe_4be87c, -0.00625);

            if (fe_4be87c == 0) {
                draw_sprite_at(g_sprite_car, 0xa0, 0x5a);
            } else {
                draw_sprite_scaled(g_sprite_car, 0xa0, 0x5a, scale, 0.0f);
            }
            draw_sprite_at(g_sprite_car_select, 0x20, 0x7a);
            if (page(0)->selected == 1 && page(2)->selected == 0) {
                draw_centred(mini_text(cur == page(PAGE_CAR_P1) ? 0x47dffc : 0x47e000),
                             g_fonts_menu[6], 0xc0);
            }
        }
        if (cur == page(PAGE_TRACK)) {
            float scale = grow(fe_4be724, -0.005);

            if (fe_4be724 == 0) {
                draw_sprite_at(g_sprite_track, 0xfb, 0x88);
            } else {
                draw_sprite_scaled(g_sprite_track, 0xfb, 0x88, scale, 0.0f);
            }
            if (g_menu_view.page_changed) {
                if (page(0)->selected == 1 && page(2)->selected == 0) {
                    fe_4be588 = 1;
                } else {
                    fe_4be594 = 1;
                }
            }
        }
        if (cur == page(PAGE_CAR_P1) || cur == page(PAGE_CAR_P2)) {
            int player = cur == page(PAGE_CAR_P1) ? 0 : 1;

            if (player == 1 && g_menu_view.page_changed) {
                fe_4be588 = 1;
            }
            car_carousel(player);
        }
    }
    if (cur == page(PAGE_NETWORK)) {
        menu_network_page();
    } else {
        fe_net.mode = 2;
        fe_4be568 = 0;
    }
    if (intro_done) {
        const s32 *r = g_position_icon_rect;
        float fx, fy;
        int mx, my;

        /* The corners of the screen, or of NATIVE's margins, which need
         * not be whole menu pixels: rounded out, so that the corners
         * reach the screen's edges. */
        canvas_extent(g_menu_screen, &fx, &fy);
        mx = (int)ceilf(fx);
        my = (int)ceilf(fy);
        draw_sprite_at(g_position_icons[0], -mx, -my);
        draw_sprite_at(g_position_icons[1], mode[0] - r[2] + mx, -my);
        draw_sprite_at(g_position_icons[2], -mx, mode[1] - r[3] + my);
        draw_sprite_at(g_position_icons[3], mode[0] - r[2] + mx, mode[1] - r[3] + my);
    }
    return 0;
}

/* The cheat codes, typed anywhere in the menus (0x47df88). */
static const char *const s_cheat_codes[7] = {
    "BANARNE", "SURMULE", "SLASKTRATT", "FILMJOLK", "SKUNK", "SVINPOLE", "STRINGS"
};

/* The intro's animations (0x47dc50). */
static const char *const s_intro_files[6] = {
    "baltazar\\data\\ign1.cdp", "baltazar\\data\\ign2.cdp",
    "baltazar\\data\\ign3_0.cdp", "baltazar\\data\\ign3_1.cdp",
    "baltazar\\data\\ign3_2.cdp", "baltazar\\data\\ign3_3.cdp"
};

/* 0x456120
 * Whether the last typed characters hold `code`; a match clears them.
 * The original upper cases the code in place first. */
static int typed_code(const char *code)
{
    char upper[16];
    size_t i;

    for (i = 0; code[i] != '\0' && i < sizeof(upper) - 1; i++) {
        upper[i] = (char)toupper((unsigned char)code[i]);
    }
    upper[i] = '\0';
    if (strstr(g_typed, upper) == NULL) {
        return 0;
    }
    memset(g_typed, 0xff, 64);
    return 1;
}

/* 0x409ef0 */
static void cheats_check(void)
{
    int i;

    for (i = 0; i < 7; i++) {
        if (typed_code(s_cheat_codes[i])) {
            g_cheats[i] ^= 1;
            sound_load(0, 0, 0xfde8, 0x55f0);
        }
    }
    /* SURMULE: all difficulty levels. */
    if (g_cheats[1]) {
        page(PAGE_DIFFICULTY)->items[0].max = 3;
        fe_4be58c = 1;
        fe_4be728 = 1;
    }
    /* SLASKTRATT: all eleven cars. */
    if (g_cheats[2]) {
        for (i = 0; i < 11; i++) {
            g_car_order[i] = i;
        }
        g_car_count = 11;
        page(PAGE_CAR_P1)->items[0].max = 10;
        page(PAGE_CAR_P2)->items[0].max = g_car_count - 1;
        fe_4be58c = 1;
    }
}

/* The cars a player may choose: all eleven less the ones not yet won,
 * by the difficulty reached (0x4099c2). */
static void build_car_order(void)
{
    static const s8 locked[4] = { 10, 9, 6, 1 };
    int n = 4 - settings_get(0x587);
    int car, count = 0;

    if (settings_get(0x58b)) {
        n--;
    }
    for (car = 0; car < 11; car++) {
        int j = 0;

        if (n > 0) {
            while (j < n && locked[j] != car) {
                j++;
            }
        }
        if (j == n) {
            g_car_order[count++] = car;
        }
    }
    g_car_count = count;
    page(PAGE_CAR_P1)->items[0].max = count - 1;
    page(PAGE_CAR_P2)->items[0].max = count - 1;
    if (page(PAGE_CAR_P1)->items[0].max < settings_get(0x40)) {
        settings_set(0x40, page(PAGE_CAR_P1)->items[0].max);
    }
    if (page(PAGE_CAR_P2)->items[0].max < settings_get(0x44)) {
        settings_set(0x44, page(PAGE_CAR_P2)->items[0].max);
    }
}

/* Return on the language page: the language's tables, the cars, and
 * the intro the first time (0x40998d). */
static void choose_language(void)
{
    s32 lang = settings_get(SET_LANGUAGE);
    int i;

    g_menu_language = lang;
    g_menu.pages = g_frontend_lists[lang];
    memcpy(g_frontend_pages, g_frontend_page_rows[lang], sizeof(g_frontend_pages));
    build_car_order();
    if (frontend_load_language()) {
        const char *loading;

        fe_4be560 = 0.0;
        fe_4be56c = 0;
        fe_4be850 = 0.01;
        canvas_clear(g_menu_screen, 0, 64000);
        loading = ui_text(0x47e0a0 + 200 * g_menu_language);
        text_draw(loading, g_fonts_menu[7], 160 - text_width(loading, g_fonts_menu[7]) / 2, 0x5e);
        menu_present();
        for (i = 0; i < 6; i++) {
            long size = file_size(s_intro_files[i]);

            fe_4be880[i] = malloc((size_t)size);
            g_intro_sizes[i] = (u32)size;
            file_read_at(s_intro_files[i], fe_4be880[i], (u32)size, 0);
        }
        canvas_clear(g_menu_screen, 0, 64000);
        menu_present();
        frontend_intro_start(fe_4be880[0], g_menu_screen);
    } else {
        fe_4be5a8 = 0.01;
        sound_unload(0, 0, 3);
        menucar_load();
    }
}

/* A key bound twice: KEY ALREADY ASSIGNED for a while (0x409cfa). */
static int key_taken(void)
{
    fe_4bf564 = 1;
    menu_set_delay(0x44c);
    sound_load(0, 2, 0xfde8, 0x55f0);
    return MENU_KEY_TAKEN;
}

/* Whether `key` is bound on page `a`, item `i`, other than the item
 * being set. */
static int bound(int a, int i, int key)
{
    return page(a)->items[i].choice->data[0] == (u8)key;
}

/* 0x409610 */
int menu_page_handler_b(int key, int action)
{
    struct menu_page *cur = g_menu_view.page;
    int a = abs(action);
    int i;

    cheats_check();
    if (fe_4be598 != 0.0) {
        if (fe_4be598 >= 1080.0) {
            fe_4be598 = 0.0;
            fe_4be58c = 1;
            return MENU_KEY_TAKEN;
        }
        fe_4be598 = 0.0;
    }
    /* A key skips the intro. */
    if (0.0 < fe_4be850 && fe_4be850 < 1278.0) {
        fe_4be850 = 1278.0;
        video_set_palette(g_menu_white);
        fe_4be584 = sound_voice_of(fe_4be8b0);
        if (fe_4be584 != NULL) {
            fe_4be584->rate = 0;
        }
        fe_4be584 = sound_voice_of(fe_4be8b4);
        if (fe_4be584 != NULL) {
            fe_4be584->rate = 0;
        }
        sound_stop_all();
        return MENU_KEY_TAKEN;
    }
    if (cur == page(PAGE_NETWORK)) {
        if (action == MENU_KEY_BACK && (fe_net.mode == 4 || fe_net.mode == 3)) {
            return MENU_KEY_TAKEN;
        }
        if (!(action == MENU_KEY_ACTIVATE && fe_net.mode == 3 && g_net_players > 0)) {
            return MENU_KEY_TAKEN;
        }
    }
    if (a != 0 || g_menu_view.edit != 0) {
        fe_4be58c = 1;
    }
    if (cur == page(PAGE_DIFFICULTY) && g_menu_view.selected == 0
        && page(PAGE_DIFFICULTY)->items[0].max == 0 && a == 3) {
        sound_load(0, 2, 0xfde8, 0x55f0);
        return MENU_KEY_TAKEN;
    }
    if (cur != page(PAGE_LANGUAGE) && a >= 1 && a <= 6) {
        sound_load(0, a == 6 ? 0 : 1, 0xc350, 0x55f0);
    }
    /* The two players cannot both take the one joystick. */
    if (cur == page(11) && g_menu_view.selected == 2
        && (settings_get(0x127) == 1 || settings_get(0x127) == 2)) {
        if (settings_get(0x123) == 0 && action == 3) {
            settings_set(0x123, 2);
        } else if (settings_get(0x123) == 3 && action == -3) {
            settings_set(0x123, 1);
        }
    }
    if (cur == page(12) && g_menu_view.selected == 2
        && (settings_get(0x123) == 1 || settings_get(0x123) == 2)) {
        if (settings_get(0x127) == 0 && action == 3) {
            settings_set(0x127, 2);
        } else if (settings_get(0x127) == 3 && action == -3) {
            settings_set(0x127, 1);
        }
    }
    if (cur == page(PAGE_LANGUAGE)) {
        if (action != 0) {
            fe_4be6d8 = 1;
        }
        /* The second language is left out: steps go round it. */
        if ((settings_get(SET_LANGUAGE) == 0 && action == 3)
            || (settings_get(SET_LANGUAGE) == 2 && action == -3)) {
            settings_set(SET_LANGUAGE, 1);
        }
        if (action == MENU_KEY_ACTIVATE && fe_4be850 == 0.0) {
            choose_language();
        }
    }
    if (cur == page(0) && action == MENU_KEY_BACK) {
        return MENU_KEY_TAKEN;
    }
    if (cur == page(0) && action == MENU_KEY_ACTIVATE) {
        page(3)->items[0].kind = 1;
    }
    if (cur == page(2) && action == MENU_KEY_ACTIVATE) {
        page(3)->items[0].kind = (s16)(page(2)->selected ^ 1);
        if (page(3)->items[0].kind == 0 && page(3)->selected == 0) {
            page(3)->selected = 1;
        }
    }
    if (cur == page(PAGE_TRACK) && (a == 1 || a == 2)) {
        if (fe_4be724 == 0) {
            fe_4be724 = 1;
        } else if (fe_4be724 > 200) {
            fe_4be728 = 1;
        }
    }
    if (cur == page(13) && g_menu_view.edit == 3) {
        for (i = 0; i < page(13)->count; i++) {
            if ((cur->selected != i && bound(13, i, key)) || bound(14, i, key)) {
                return key_taken();
            }
        }
    }
    if (cur == page(14) && g_menu_view.edit == 3) {
        for (i = 0; i < page(14)->count; i++) {
            if ((cur->selected != i && bound(14, i, key)) || bound(13, i, key)) {
                return key_taken();
            }
        }
    }
    if ((cur == page(PAGE_CAR_P1) || cur == page(PAGE_CAR_P2)) && (a == 3 || a == 4)) {
        fe_4be87c = 1;
        menu_set_delay(400);
        if (action < 0) {
            fe_4be5fc = -1;
            return 0;
        }
        fe_4be5fc = 1;
    }
    return 0;
}

/* The language page's flags, as the page draws them: four in a row, the
 * second language left out, each 0x3d x 0x35. A tap on one walks the
 * choice to it with Left or Right, a tap on the chosen one is Return. */
int menu_pages_touch(float lx, float ly, int *count)
{
    int slot, cur;
    s32 lang;

    *count = 0;
    if (g_menu_view.page != page(PAGE_LANGUAGE)) {
        return -1;
    }
    if (ly < 0x4a || ly >= 0x4a + 0x35 || lx < 0x1a) {
        return 0;
    }
    slot = (int)(lx - 0x1a) / 69;
    if (slot > 3 || lx - 0x1a - slot * 69 >= 0x3d) {
        return 0;
    }
    lang = settings_get(SET_LANGUAGE);
    cur = lang > 1 ? (int)lang - 1 : (int)lang;
    *count = slot == cur ? 1 : abs(slot - cur);
    return slot == cur ? DIK_RETURN : slot > cur ? DIK_RIGHT : DIK_LEFT;
}
