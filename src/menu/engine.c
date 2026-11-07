/*
 * menu/engine.c - the engine that runs the front end's menu pages
 * (0x410930..0x411b40).
 *
 * The engine keeps a stack of pages and of the item selected on each.
 * Every frame it takes the keys pressed, moves the selection or changes
 * the selected item's value by the page's key map, follows the
 * navigation tree when an item is activated, and runs the page's
 * handlers. Handlers read the engine's state through the snapshot that
 * menu_snapshot publishes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "canvas.h"
#include "input.h"
#include "lisa/font.h"
#include "lisa/gfx.h"
#include "lisa/sprite.h"
#include "menu/menu.h"
#include "menu/engine.h"
#include "menu/pages.h"
#include "platform/platform.h"
#include "video.h"
#include "dik.h"
#include "startup.h"
#include "stubs.h"

enum {
    STACK = 16,
    ITEM_SPRITES = 50,
    TEXT_SIZE = 0x80
};

u8 g_settings[SETTINGS_SIZE + 0x10];   /* 0x4bf580 */
struct menu_context g_menu;             /* 0x4c50d0 */
struct menu_snapshot g_menu_view;       /* 0x4c52a0 */

/* An item's value as an index into its visuals, held to min..max: a
 * value from the settings file outside it would read past them. */
static int value_index(const struct menu_item *it)
{
    int v = *it->value;

    if (v < it->min) {
        v = it->min;
    } else if (v > it->max) {
        v = it->max;
    }
    return v - it->min;
}

static struct menu_page *s_page;        /* 0x4c5298 */
static struct menu_page *s_prev_page;   /* 0x4c4e40 */
static int s_depth;                     /* 0x4c529c */
static int s_selected;                  /* 0x4c50cc */
static int s_prev_selected;             /* 0x4c4e44 */
static struct menu_page *s_page_stack[STACK];   /* 0x4c5140 */
static s32 s_select_stack[STACK];       /* 0x4c4e88 */
static s16 s_edit;                      /* 0x4c4e58, see enum edit */
static const s8 *s_keymap;              /* 0x4c4e54 */
static char s_text[TEXT_SIZE];          /* 0x4c4fc8 */
static int s_text_len;                  /* 0x4c513c */
static int s_tree_node;                 /* 0x4c510c */
static int s_quit;                      /* 0x4c5134 */
static int s_first_frame;               /* 0x4c4e80 */
static s32 s_delay;                     /* 0x4c4e5c */
static int s_step;                      /* 0x4c5138 */
static double s_ticks;                  /* 0x4c5180 */
static double s_step_part;              /* 0x4c4e48 */
static double s_last_ticks;             /* 0x4c5290 */
static u8 *s_work;                      /* 0x4c5108, 0x300 bytes */

/* Two sprites an item: its label and its value or binding. */
static struct sprite *s_sprites[ITEM_SPRITES][2];      /* 0x4c4cb0 */

enum edit {
    EDIT_NONE = 0,
    EDIT_TEXT = 1,
    EDIT_TEXT_NOW = 2,
    EDIT_KEY = 3
};

/* 0x411520 */
static void menu_snapshot(void)
{
    memcpy(g_menu_view.page_stack, s_page_stack, sizeof(g_menu_view.page_stack));
    memcpy(g_menu_view.select_stack, s_select_stack, sizeof(g_menu_view.select_stack));
    g_menu_view.page = s_page;
    g_menu_view.prev_page = s_prev_page;
    g_menu_view.depth = s_depth;
    g_menu_view.selected = s_selected;
    g_menu_view.prev_selected = s_prev_selected;
    g_menu_view.edit = s_edit;
}

static int abs_action(int a)
{
    return a < 0 ? -a : a;
}

/* 0x411590: whether `key` was pressed since last asked.
 *
 * The original indexes the pressed table with the scan code as a signed
 * char, so an extended key (0x80 and up: the arrows, Home, Delete) reads
 * 0x100 bytes earlier, the same key in the trigger table, which sits
 * just before. Both tables are set on a press and cleared when read
 * here, so the keys behave alike; the tables are named as the original
 * uses them. */
static int menu_key_take(int key)
{
    u8 *pressed = key < 0x80 ? &g_menu.key_pressed[key] : &g_key_trigger[key];

    if (*pressed == 0) {
        return 0;
    }
    *pressed = 0;
    return 1;
}

/* 0x4115b0
 * Ticks of 25 ms since the last call: 0.01 when no time passed, at
 * most 12. */
static double menu_ticks(void)
{
    double now = (double)(s32)g_frame_time * 0.04;
    double ticks = now - s_last_ticks;

    s_last_ticks = now;
    if (ticks == 0.0) {
        return 0.01;
    }
    return ticks > 12.0 ? 12.0 : ticks;
}

/* 0x411630 */
static void menu_sound_tick(void)
{
    if (g_menu.sound) {
        sound_tick();
    }
}

/* 0x411b40
 * Makes item `index`'s label sprite (`which` 0) or value sprite (1)
 * from the visual for its state. */
static int menu_item_sprite(int index, int which, int selected)
{
    struct menu_item *it = &s_page->items[index];
    struct menu_visual *visuals = it->visuals;
    const struct menu_visual *v;
    struct tex_desc desc;
    int look;

    if (visuals == NULL) {
        return 0;
    }
    if (index < 0 || index >= s_page->count) {
        return 0;
    }
    look = selected ^ 1;
    if (it->kind == 0) {
        look += 2;
        if (it->value != NULL) {
            look += (it->max - it->min) * 2 + 2;
        }
    }
    memset(&desc, 0, sizeof(desc));
    if (which == 0) {
        v = &visuals[look];
        if (v->pixels == NULL) {
            return 1;
        }
        desc.width = v->width < 0 ? g_menu.screen_w : v->width;
        desc.height = v->height < 0 ? g_menu.screen_h : v->height;
    } else {
        if (it->value != NULL) {
            v = &visuals[look + value_index(it) * 2 + 2];
        } else if (it->choice != NULL && it->choice->visual != NULL) {
            v = it->choice->visual;
        } else {
            return 1;
        }
        if (v->pixels == NULL) {
            return 1;
        }
        desc.width = v->width > 0 ? v->width : g_menu.screen_w;
        desc.height = v->height > 0 ? v->height : g_menu.screen_h;
    }
    desc.stride = v->stride;
    desc.pixels = v->pixels;
    s_sprites[index][which] = sprite_create(&desc, s_sprites[index][which]);
    return 1;
}

static void make_item_sprites(void)
{
    int i;

    for (i = 0; i < s_page->count; i++) {
        int selected = i == s_selected;

        menu_item_sprite(i, 0, selected);
        menu_item_sprite(i, 1, selected);
    }
}

static int copy_choice_text(const struct menu_choice *choice)
{
    size_t n = strlen((const char *)choice->data);

    if (n >= sizeof(s_text)) {
        n = sizeof(s_text) - 1;
    }
    memcpy(s_text, choice->data, n);
    s_text[n] = '\0';
    return (int)n;
}

/* 0x411640
 * Goes into the page the selected item leads to (`dir` 1), back to the
 * page before (-1), or stays (0), then remakes the page's sprites. A
 * path that ends in a callback runs it instead. Returns 0 when nothing
 * changed. */
int menu_change_page(int dir)
{
    int t;

    if (dir < 0 && s_depth == 0) {
        s_quit = 1;
        return 0;
    }
    if (dir > 0 && s_page->items[s_selected].kind == 0) {
        return 0;
    }
    s_page->selected = s_selected;
    s_select_stack[s_depth] = s_selected;

    for (t = 0; t < g_menu.tree_count; t++) {
        const s16 *path = g_menu.tree[t];
        int d;
        s16 code;

        for (d = 0; d <= s_depth; d++) {
            s16 want = path[2 * d];

            if (want != 0 && want - s_select_stack[d] != 1) {
                break;
            }
        }
        if (d <= s_depth) {
            continue;
        }
        s_tree_node = t;
        code = path[2 * s_depth + 1];
        if (code < 0) {
            menu_callback cb = g_menu.callbacks[-code - 1];

            if (cb != NULL) {
                menu_snapshot();
                cb(0, dir < 0 ? MENU_KEY_BACK : MENU_KEY_ACTIVATE);
            }
            break;
        }
        if (dir > 0) {
            s_prev_page = s_page;
            s_page = g_menu.pages[code];
            s_page_stack[s_depth + 1] = s_page;
            if (!g_menu.keep_selection) {
                s_page->selected = 0;
            }
            s_selected = s_page->selected;
            s_prev_selected = s_selected;
            goto changed;
        }
    }
    if (dir > 0) {
        return 0;
    }
    if (dir < 0) {
        s_prev_page = s_page;
        s_page = s_page_stack[s_depth - 1];
        s_page->selected = g_menu.keep_selection ? s_select_stack[s_depth - 1] : 0;
        s_selected = s_page->selected;
        s_prev_selected = s_selected;
    }
changed:
    g_menu_view.page_changed = 1;
    s_depth += dir;
    s_keymap = s_page->keymap != NULL ? s_page->keymap : g_menu_keymap_default;
    {
        const struct menu_choice *ch = s_page->items[s_selected].choice;

        if (ch != NULL && ch->words[6] == MENU_CHOICE_TEXT_NOW) {
            s_edit = EDIT_TEXT_NOW;
            s_text_len = copy_choice_text(ch);
        }
    }
    make_item_sprites();
    return 1;
}

/* Moves the selection by `step` over items that can be selected:
 * `wrap` goes round, otherwise it stops at the end, and with `restore`
 * it goes back when no item could be selected. */
static void move_selection(int step, int wrap, int restore)
{
    int saved = s_selected, tries;

    if (s_page->count <= 0) {
        if (restore) {
            s_selected = saved;
        }
        return;
    }
    for (tries = 0; tries < s_page->count; tries++) {
        s_selected += step;
        if (s_selected < 0) {
            s_selected = wrap ? s_page->count - 1 : 0;
        } else if (s_selected > s_page->count - 1) {
            s_selected = wrap ? 0 : s_page->count - 1;
        }
        if (s_page->items[s_selected].kind != 0) {
            return;
        }
    }
    if (restore) {
        s_selected = saved;
    }
}

/* What Return does on an item with a text field or key binding. */
static void activate_choice(struct menu_item *it)
{
    struct menu_choice *ch = it->choice;

    if (s_edit == EDIT_NONE) {
        if (it->kind == 0) {
            return;
        }
        if (ch->words[6] == MENU_CHOICE_TEXT) {
            s_edit = EDIT_TEXT;
            s_text_len = copy_choice_text(ch);
        } else if (ch->words[6] == MENU_CHOICE_KEY) {
            s_text[0] = '\0';
            s_text_len = 0;
            s_edit = EDIT_KEY;
        }
    } else if (s_edit == EDIT_TEXT) {
        s_edit = EDIT_NONE;
        s_text[s_text_len] = '\0';
        strcpy((char *)ch->data, s_text);
    } else if (s_edit == EDIT_TEXT_NOW) {
        s_edit = EDIT_NONE;
        s_text[s_text_len] = '\0';
        strcpy((char *)ch->data, s_text);
        menu_change_page(it->action == -1 ? -1 : 1);
    }
}

/* 0x410d80
 * Handles every key pressed since the last step.
 *
 * The original passes a callback the dword at item + 0x12, the action's
 * argument with the next field above it; and compares the value after
 * the action with one read before it, uninitialised when the item had
 * no value. Both are kept as written. */
static void menu_navigate(void)
{
    int key;

    for (key = 0; key < 256; key++) {
        struct menu_item *it;
        struct menu_choice *ch;
        s32 old_value = 0;
        s8 action;

        if (!menu_key_take(key)) {
            continue;
        }
        if (s_page->handler_b != NULL) {
            menu_snapshot();
            if (s_page->handler_b(key, s_keymap[key]) == MENU_KEY_TAKEN) {
                continue;
            }
        }
        it = &s_page->items[s_selected];
        ch = it->choice;
        if (ch != NULL && (s_edit == EDIT_TEXT || s_edit == EDIT_TEXT_NOW)) {
            if (key < 0x54) {
                u8 c = g_menu_text_chars[key];

                if (c != 0xff) {
                    int size = (u8)ch->words[4];

                    if (size - 1 <= s_text_len) {
                        s_text_len = size - 2;
                    }
                    s_text_len++;
                    s_text[s_text_len - 1] = (char)c;
                    s_text[s_text_len] = '\0';
                } else if (key == 0x0e && s_text_len > 0) {
                    s_text[s_text_len - 1] = '\0';
                    s_text_len--;
                }
            }
        } else if (ch != NULL && s_edit == EDIT_KEY) {
            if (g_menu.bindable == NULL || g_menu.bindable[key] != 0) {
                *ch->data = (u8)key;
                s_edit = EDIT_NONE;
            }
            return;
        }
        if (s_keymap[key] == 0) {
            continue;
        }
        s_prev_selected = s_selected;
        if (it->value != NULL) {
            old_value = *it->value;
        }
        if (it->action > 0 && g_menu.callbacks[it->action - 1] != NULL) {
            s32 arg = (s32)((u16)it->action_arg | (u32)(u16)it->f14 << 16);

            menu_snapshot();
            g_menu.callbacks[it->action - 1](arg, s_keymap[key]);
        }
        it = &s_page->items[s_selected];
        action = s_keymap[key];
        if (it->value == NULL && abs_action(action) >= 3 && abs_action(action) <= 5) {
            action = 0;
        }
        if (s_edit != EDIT_NONE && abs_action(action) != 6) {
            action = 0;
        }
        switch (action) {
        case MENU_KEY_BACK:
            if (s_edit == EDIT_NONE) {
                menu_change_page(-1);
            } else if (s_edit == EDIT_TEXT) {
                s_edit = EDIT_NONE;
            } else if (s_edit == EDIT_TEXT_NOW) {
                s_edit = EDIT_NONE;
                menu_change_page(-1);
            }
            break;
        case MENU_KEY_MIN:
            *it->value = it->min;
            break;
        case MENU_KEY_DEC:
            if (it->kind != 0 && --*it->value < it->min) {
                *it->value = it->min;
            }
            break;
        case MENU_KEY_DEC_WRAP:
            if (it->kind != 0 && --*it->value < it->min) {
                *it->value = it->max;
            }
            break;
        case MENU_KEY_UP:
            move_selection(-1, 0, 1);
            break;
        case MENU_KEY_UP_WRAP:
            move_selection(-1, 1, 0);
            break;
        case MENU_KEY_DOWN_WRAP:
            move_selection(1, 1, 0);
            break;
        case MENU_KEY_DOWN:
            move_selection(1, 0, 1);
            break;
        case MENU_KEY_INC_WRAP:
            if (it->kind != 0 && ++*it->value > it->max) {
                *it->value = it->min;
            }
            break;
        case MENU_KEY_INC:
            if (it->kind != 0 && ++*it->value > it->max) {
                *it->value = it->max;
            }
            break;
        case MENU_KEY_MAX:
            *it->value = it->max;
            break;
        case MENU_KEY_ACTIVATE:
            if (it->choice != NULL) {
                activate_choice(it);
            } else {
                menu_change_page(it->action == -1 ? -1 : 1);
            }
            break;
        default:
            break;
        }
        if (s_selected != s_prev_selected) {
            s_page->selected = s_selected;
            menu_item_sprite(s_prev_selected, 0, 0);
            menu_item_sprite(s_prev_selected, 1, 0);
            menu_item_sprite(s_selected, 0, 1);
            menu_item_sprite(s_selected, 1, 1);
        }
        it = &s_page->items[s_selected];
        if (it->value != NULL && *it->value != old_value) {
            menu_item_sprite(s_selected, 1, 1);
        }
    }
}

/* 0x410930 */
int menu_engine_init(void)
{
    s_work = calloc(0x300, 1);
    s_edit = EDIT_NONE;
    s_quit = 0;
    s_page_stack[0] = g_menu.root;
    s_text_len = 0;
    s_depth = 0;
    s_select_stack[0] = 0;
    s_page = g_menu.root;
    s_prev_page = g_menu.root;
    s_selected = s_page->selected;
    memset(s_sprites, 0, sizeof(s_sprites));
    s_delay = 0;
    menu_ticks();
    menu_change_page(0);
    menu_snapshot();
    s_first_frame = 1;
    return 1;
}

/* 0x4109d0
 * Runs one navigation step per 25 ms tick. Returns -1 when the root
 * page was left, 1 otherwise. */
int menu_engine_frame(void)
{
    s_selected = s_page->selected;
    s_ticks = menu_ticks();
    if (!s_first_frame) {
        g_menu_view.page_changed = 0;
    }
    for (s_step = 0; (s32)s_ticks >= s_step; s_step++) {
        s_step_part = (s32)s_ticks == s_step ? s_ticks - s_step : 1.0;
        if (s_delay > 0) {
            s_delay -= 25;
        } else {
            menu_navigate();
        }
        if (s_delay < 0) {
            s_delay = 0;
        }
    }
    if (s_quit) {
        s_quit = 0;
        return -1;
    }
    menu_sound_tick();
    if (s_page->handler_a != NULL) {
        menu_snapshot();
        s_page->handler_a();
    }
    menu_snapshot();
    s_first_frame = 0;
    return 1;
}

/* 0x410d70: ignores keys for `ms` milliseconds. */
int menu_set_delay(s32 ms)
{
    s_delay = ms;
    return 1;
}

/* 0x410d20 */
int menu_engine_free(void)
{
    int i;

    free(s_work);
    s_work = NULL;
    for (i = 0; i < ITEM_SPRITES; i++) {
        if (s_sprites[i][0] != NULL) {
            sprite_create(NULL, s_sprites[i][0]);
        }
        if (s_sprites[i][1] != NULL) {
            sprite_create(NULL, s_sprites[i][1]);
        }
    }
    return 1;
}

struct sprite *menu_item_sprite_of(int index, int which)
{
    return s_sprites[index][which];
}

double menu_step_part(void)
{
    return s_step_part;
}

const char *menu_edit_text(void)
{
    return s_text;
}

/* 0x411b10 */
static void menu_draw_sprite(const struct sprite *sprite, int x, int y)
{
    struct sprite_pos pos;

    pos.x = x << 8;
    pos.y = y << 8;
    sprite_draw(sprite, &pos, NULL);
}

/* The screen position of a point given from the screen's centre, or in
 * the 3200 x 2400 space when the context says so. */
static int screen_x(s16 x)
{
    int half = g_menu.screen_w * g_menu.centre / 2;

    return g_menu.scaled ? x * g_menu.screen_w / 0xc80 + half : x + half;
}

/* NATIVE's menus have height to spare: the items' rows, given from the
 * screen's centre, are spread up to 1.3 times as far apart (their sizes
 * kept), as far as the page's items stay on its layer: the lowest item's
 * text goes no lower than 74 below the centre, the highest no higher than
 * 98 above it. A page whose items already reach further (Options' RESET
 * GHOST CAR, at 78) keeps the original's spacing. */
extern u8 *g_menu_screen;

/* A list of text items, the pages the spread and lift apply to; pages
 * with pictures among their items (the cars, the flags) keep the
 * original's places, as other sprites are drawn at fixed ones. */
static int list_page(const struct menu_page *p)
{
    int i;

    if (p == NULL || p->items == NULL || p->count == 0) {
        return 0;
    }
    for (i = 0; i < p->count; i++) {
        const struct menu_visual *v = p->items[i].visuals;

        if (v == NULL || v->text == NULL || v->pixels != NULL) {
            return 0;
        }
    }
    return 1;
}

static int page_spread(void)
{
    const struct menu_page *p = g_menu_view.page;
    int i, lo = 0, hi = 0, k = 13;

    if (!list_page(p)) {
        return 10;
    }
    for (i = 0; i < p->count; i++) {
        const struct menu_visual *v = p->items[i].visuals;

        lo = v->text_y < lo ? v->text_y : lo;
        hi = v->text_y > hi ? v->text_y : hi;
    }
    if (hi > 0 && hi * k > 740) {
        k = 740 / hi;
    }
    if (lo < 0 && -lo * k > 980) {
        k = 980 / -lo;
    }
    return k < 10 ? 10 : k;
}

int menu_spread_y(int y)
{
    float mx, my;
    int k;

    canvas_extent(g_menu_screen, &mx, &my);
    if (my <= 0) {
        return y;
    }
    k = page_spread();
    return (y * k + (y < 0 ? -5 : 5)) / 10;
}

/* And the list is lifted toward the logo, which NATIVE puts at the
 * window's top: its first item's text to 28 rows from the layer's top,
 * a gap under the logo, by no more than the margin. */
int menu_lift_y(void)
{
    const struct menu_page *p = g_menu_view.page;
    float mx, my;
    int i, lo = 0, lift;

    canvas_extent(g_menu_screen, &mx, &my);
    if (my <= 0 || !list_page(p)) {
        return 0;
    }
    for (i = 0; i < p->count; i++) {
        lo = p->items[i].visuals->text_y < lo ? p->items[i].visuals->text_y : lo;
    }
    lift = menu_spread_y(lo) + 100 - 28;
    if (lift > (int)my) {
        lift = (int)my;
    }
    return lift > 0 ? lift : 0;
}

/* A position given from the screen's centre, spread and lifted. */
int menu_place_y(int y)
{
    return menu_spread_y(y) - menu_lift_y();
}

static int screen_y(s16 y)
{
    int half = g_menu.screen_h * g_menu.centre / 2;

    return menu_place_y(g_menu.scaled ? y * g_menu.screen_h / 0x960 : y) + half;
}

/* 0x411960: draws a visual's sprite and its text, the text through the
 * identity colour table. A value's visual goes with the value sprite (1),
 * the one remade when the value changes, not the label's (0). */
void menu_draw_visual(const struct menu_visual *v, const struct sprite *sprite)
{
    u8 *table;

    if (v == NULL) {
        return;
    }
    /* No sprite when it could not be made: nothing to draw, not a crash. */
    if (v->pixels != NULL && sprite != NULL) {
        menu_draw_sprite(sprite, screen_x(v->x), screen_y(v->y));
    }
    if (v->text == NULL) {
        return;
    }
    table = g_gfx_color_table;
    g_gfx_color_table = g_identity_table;
    text_draw(v->text, g_menu.fonts[v->font], screen_x(v->text_x), screen_y(v->text_y));
    g_gfx_color_table = table;
}

/* 0x410af0
 * Draws the page's items into the 2D target: each label in its look,
 * the value's text, and a key binding's name or a text field with its
 * cursor. The cursor goes into the edit buffer, blinking at the
 * choice's rate, even when the item is not being edited. */
int menu_draw_items(void)
{
    int i;

    for (i = 0; i < s_page->count; i++) {
        struct menu_item *it = &s_page->items[i];
        int selected = i == s_selected;
        int look = selected ^ 1;
        struct menu_choice *ch;

        if (it->kind == 0) {
            look += 2;
            if (it->value != NULL) {
                look += (it->max - it->min) * 2 + 2;
            }
        }
        if (it->visuals != NULL) {
            menu_draw_visual(&it->visuals[look], s_sprites[i][0]);
        }
        if (it->value != NULL && it->visuals != NULL) {
            menu_draw_visual(&it->visuals[look + value_index(it) * 2 + 2], s_sprites[i][1]);
        }
        ch = it->choice;
        if (ch != NULL) {
            struct menu_visual v;

            memset(&v, 0, sizeof(v));
            if (ch->words[6] == MENU_CHOICE_KEY && (s_edit == EDIT_NONE || !selected)) {
                v.text = g_key_names[ch->data[0]];
            } else {
                v.text = (const char *)ch->data;
            }
            if (selected) {
                s16 blink = ch->words[5];
                char cursor = (char)((u16)ch->words[4] >> 8);

                if (s_edit != EDIT_NONE) {
                    menu_draw_visual(ch->visual, s_sprites[i][1]);
                    v.text = s_text;
                }
                if (blink == 0 || (s32)g_frame_time % blink < (s16)(blink / 2)) {
                    s_text[s_text_len] = cursor;
                    s_text[s_text_len + 1] = '\0';
                } else {
                    s_text[s_text_len] = '\0';
                }
            }
            v.text_x = ch->words[0];
            v.text_y = ch->words[1];
            v.font = selected ? ch->words[2] : ch->words[3];
            menu_draw_visual(&v, NULL);
        }
    }
    return 1;
}

/* The port's, for touch screens: what a tap at (fx, fy), a share of the
 * drawable's width and height, asks of the current page. A row other
 * than the selected one: `count` presses of Up or Down to reach it, as
 * move_selection steps. The selected row: Return on its label, Right on
 * its values. 0 when the tap is on no row. */
int menu_touch(float fx, float fy, int *count)
{
    float mx, my, lx, ly;
    int k, best = -1, best_d = 10, target, steps, sel;

    *count = 0;
    if (s_page == NULL || s_page->count <= 0) {
        return 0;
    }
    canvas_extent(g_menu_screen, &mx, &my);
    if (mx > 0 || my > 0) {
        lx = fx * (320.0f + 2 * mx) - mx;
        ly = fy * (200.0f + 2 * my) - my;
    } else {
        int w = 0, h = 0;
        float cw;

        video_drawable_size(&w, &h);
        cw = w > 0 && h > 0 ? (h * 4.0f / 3.0f) / w : 1.0f;
        lx = (fx - (1.0f - cw) / 2) / cw * g_menu.screen_w;
        ly = fy * g_menu.screen_h;
    }
    k = menu_pages_touch(lx, ly, count);
    if (k >= 0) {
        return k;
    }
    for (k = 0; k < s_page->count; k++) {
        const struct menu_item *it = &s_page->items[k];
        int d;

        if (it->kind == 0 || it->visuals == NULL) {
            continue;
        }
        d = (int)ly - (screen_y(it->visuals[0].text_y) + 5);
        d = d < 0 ? -d : d;
        if (d < best_d) {
            best_d = d;
            best = k;
        }
    }
    if (best < 0) {
        return 0;
    }
    sel = s_page->selected;
    if (best == sel) {
        const struct menu_item *it = &s_page->items[best];

        *count = 1;
        if (it->value != NULL && it->max > it->min
            && lx >= screen_x(it->visuals[2].x) - 4) {
            return DIK_RIGHT;
        }
        return DIK_RETURN;
    }
    /* The presses move_selection would take, without wrapping. */
    target = best;
    for (steps = 0, k = sel; k != target && steps < s_page->count; ) {
        k += target > sel ? 1 : -1;
        if (s_page->items[k].kind != 0) {
            steps++;
        }
    }
    *count = steps;
    return target > sel ? DIK_DOWN : DIK_UP;
}

void menu_tap(float fx, float fy)
{
    u32 at = plat_time_ms();
    int count, key, i;

    key = menu_touch(fx, fy, &count);
    fprintf(stderr, "menu: tap at %.3f, %.3f: key %02x, %d times\n", fx, fy, key, count);
    for (i = 0; key != 0 && i < count; i++) {
        plat_schedule_key(at + (u32)i * 70, key, 50);
    }
}
