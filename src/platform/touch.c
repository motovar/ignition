/*
 * platform/touch.c - the on-screen pad. See touch.h.
 *
 * The buttons are rings with an icon, drawn with rnd.h's OVER program:
 * small index textures (0 is left alone by OVER), a palette of their own
 * and an empty snapshot. A held button is filled. The icons are drawn
 * here, pixel by pixel, from a few shapes.
 *
 * The small move icon at the bottom of the screen starts the layout
 * editor: drag a button to move it, tap it to pick it and then - or +
 * resizes it (with none picked they resize every button), the circular
 * arrow puts the buttons back and the check mark keeps the layout, in
 * touchpad.cfg in the working folder.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "platform/common.h"
#include "platform/touch.h"
#include "rnd.h"

enum icon {
    I_LEFT, I_RIGHT, I_UP, I_DOWN,
    I_GAS,                      /* A: accelerate, select */
    I_BRAKE,                    /* X */
    I_ACTION,                   /* B: action, back */
    I_LOOK,                     /* Y: look back */
    I_PAUSE,                    /* Start */
    I_MENU,                     /* Select */
    I_MOVE, I_PLUS, I_MINUS, I_RESET, I_DONE,
    ICONS
};

/* Palette indices: 1 white, 2 grey (a held button's fill), 3 amber (the
 * editor), 4 dark (an icon on a held button's fill). */
enum { V_NORMAL, V_LIT, V_EDIT, VARIANTS };

/* In units of a sixth of the screen's height from the bottom left
 * (x < 0: from the right edge). */
struct place {
    float x, y, r;
};

static const struct button {
    struct place at;
    u32 pad;
    int icon;
} s_buttons[] = {
    { { 1.0f, 1.9f, 0.55f }, PAD_LEFT, I_LEFT },
    { { 3.0f, 1.9f, 0.55f }, PAD_RIGHT, I_RIGHT },
    { { 2.0f, 2.9f, 0.55f }, PAD_UP, I_UP },
    { { 2.0f, 0.9f, 0.55f }, PAD_DOWN, I_DOWN },
    { { -1.1f, 1.4f, 0.7f }, PAD_A, I_GAS },
    { { -2.6f, 1.0f, 0.55f }, PAD_X, I_BRAKE },
    { { -1.1f, 2.9f, 0.55f }, PAD_B, I_ACTION },
    { { -2.6f, 2.4f, 0.55f }, PAD_Y, I_LOOK },
    { { -0.6f, 5.5f, 0.35f }, PAD_START, I_PAUSE },
    { { 0.6f, 5.5f, 0.35f }, PAD_SELECT, I_MENU },
};

enum {
    BUTTONS = sizeof(s_buttons) / sizeof(s_buttons[0]),
    FINGERS = 10,
    SIZE = 128
};

/* A button's radius stays within these, in the same units. */
#define R_MIN 0.25f
#define R_MAX 1.3f
#define CONFIG_FILE "touchpad.cfg"

enum { T_MINUS, T_PLUS, T_RESET, T_DONE, TOOLS };
static const int s_tool_icon[TOOLS] = { I_MINUS, I_PLUS, I_RESET, I_DONE };

static struct finger {
    long id;
    float x, y;
    int down;
    int drag;                   /* the button this finger moves, or -1 */
    float grab_x, grab_y;       /* where it holds the button, from its centre */
} s_fingers[FINGERS];

static struct place s_layout[BUTTONS];
static int s_seen, s_loaded, s_edit, s_pick = -1;
static int s_w, s_h;            /* the drawable's size, as last given */
static unsigned s_tex[ICONS][VARIANTS], s_empty, s_palette;

/* ------------------------------------------------------------ layout */

static void load_layout(void)
{
    FILE *fp = fopen(CONFIG_FILE, "r");
    struct place read[BUTTONS];
    int i, version = 0;

    if (fp == NULL) {
        return;
    }
    if (fscanf(fp, "ignition-touchpad %d", &version) == 1 && version == 1) {
        for (i = 0; i < BUTTONS; i++) {
            if (fscanf(fp, "%f %f %f", &read[i].x, &read[i].y, &read[i].r) != 3
                || !(fabsf(read[i].x) < 20.0f) || !(read[i].y > -1.0f && read[i].y < 8.0f)
                || !(read[i].r >= R_MIN && read[i].r <= R_MAX)) {
                break;
            }
        }
        if (i == BUTTONS) {
            memcpy(s_layout, read, sizeof(s_layout));
        }
    }
    fclose(fp);
}

static void save_layout(void)
{
    FILE *fp = fopen(CONFIG_FILE, "w");
    int i;

    if (fp == NULL) {
        return;
    }
    fprintf(fp, "ignition-touchpad 1\n");
    for (i = 0; i < BUTTONS; i++) {
        fprintf(fp, "%.3f %.3f %.3f\n", s_layout[i].x, s_layout[i].y, s_layout[i].r);
    }
    fclose(fp);
}

static void default_layout(void)
{
    int i;

    for (i = 0; i < BUTTONS; i++) {
        s_layout[i] = s_buttons[i].at;
    }
}

static void ensure_layout(void)
{
    if (!s_loaded) {
        s_loaded = 1;
        default_layout();
        load_layout();
    }
}

/* Button `i` on a drawable of `w` x `h`: the centre and radius in pixels. */
static void place(int i, int w, int h, float *cx, float *cy, float *r)
{
    float unit = (float)h / 6.0f;

    *cx = s_layout[i].x >= 0.0f ? s_layout[i].x * unit : (float)w + s_layout[i].x * unit;
    *cy = (float)h - s_layout[i].y * unit;
    *r = s_layout[i].r * unit;
}

/* Puts button `i` with its centre at (cx, cy) pixels, kept on the screen,
 * as the edge it is nearer to. */
static void place_at(int i, float cx, float cy, int w, int h)
{
    float unit = (float)h / 6.0f, r = s_layout[i].r * unit;

    if (cx > (float)w - r) {
        cx = (float)w - r;
    }
    if (cx < r) {
        cx = r;
    }
    if (cy > (float)h - r) {
        cy = (float)h - r;
    }
    if (cy < r) {
        cy = r;
    }
    if (cx < (float)w * 0.5f) {
        s_layout[i].x = cx / unit < 0.01f ? 0.01f : cx / unit;
    } else {
        s_layout[i].x = -((float)w - cx) / unit > -0.01f ? -0.01f : -((float)w - cx) / unit;
    }
    s_layout[i].y = ((float)h - cy) / unit;
}

/* The editor's own controls, not part of the layout. */
static void tool_place(int k, int w, int h, float *cx, float *cy, float *r)
{
    float unit = (float)h / 6.0f;

    *cx = (float)w * 0.5f + ((float)k - 1.5f) * 1.1f * unit;
    *cy = (float)h - 0.6f * unit;
    *r = 0.42f * unit;
}

static void move_place(int w, int h, float *cx, float *cy, float *r)
{
    float unit = (float)h / 6.0f;

    *cx = (float)w * 0.5f;
    *cy = (float)h - 0.5f * unit;
    *r = 0.38f * unit;
}

static int inside(float x, float y, float cx, float cy, float r)
{
    float dx = x - cx, dy = y - cy;

    /* A little slack around each button. */
    return dx * dx + dy * dy < r * r * 1.4f;
}

/* The button under (x, y), the one it is nearest to the centre of. */
static int pick(float x, float y)
{
    int i, best = -1;
    float best_d = 0.0f;

    for (i = 0; i < BUTTONS; i++) {
        float cx, cy, r, d;

        place(i, s_w, s_h, &cx, &cy, &r);
        d = ((x - cx) * (x - cx) + (y - cy) * (y - cy)) / (r * r);
        if (d < 1.4f && (best < 0 || d < best_d)) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

/* The picked button, or every button when none is, by `factor`. */
static void resize(float factor)
{
    int i;

    for (i = 0; i < BUTTONS; i++) {
        float cx, cy, r;

        if (s_pick >= 0 && i != s_pick) {
            continue;
        }
        place(i, s_w, s_h, &cx, &cy, &r);
        s_layout[i].r *= factor;
        if (s_layout[i].r < R_MIN) {
            s_layout[i].r = R_MIN;
        }
        if (s_layout[i].r > R_MAX) {
            s_layout[i].r = R_MAX;
        }
        place_at(i, cx, cy, s_w, s_h);
    }
}

static void tool_action(int tool)
{
    switch (tool) {
    case T_MINUS:
        resize(1.0f / 1.12f);
        break;
    case T_PLUS:
        resize(1.12f);
        break;
    case T_RESET:
        default_layout();
        s_pick = -1;
        break;
    default:
        save_layout();
        s_edit = 0;
        s_pick = -1;
        break;
    }
}

/* ------------------------------------------------------------ fingers */

static void finger_down(struct finger *g)
{
    int i;
    float cx, cy, r;

    if (s_w <= 0 || s_h <= 0) {
        return;
    }
    if (!s_edit) {
        move_place(s_w, s_h, &cx, &cy, &r);
        if (inside(g->x, g->y, cx, cy, r)) {
            s_edit = 1;
            s_pick = -1;
        }
        return;
    }
    for (i = 0; i < TOOLS; i++) {
        tool_place(i, s_w, s_h, &cx, &cy, &r);
        if (inside(g->x, g->y, cx, cy, r)) {
            tool_action(i);
            return;
        }
    }
    s_pick = pick(g->x, g->y);
    if (s_pick >= 0) {
        place(s_pick, s_w, s_h, &cx, &cy, &r);
        g->drag = s_pick;
        g->grab_x = g->x - cx;
        g->grab_y = g->y - cy;
    }
}

void touch_finger(long id, float x, float y, int down)
{
    int f, free_slot = -1;

    s_seen = 1;
    ensure_layout();
    for (f = 0; f < FINGERS; f++) {
        if (s_fingers[f].down && s_fingers[f].id == id) {
            break;
        }
        if (!s_fingers[f].down && free_slot < 0) {
            free_slot = f;
        }
    }
    if (f == FINGERS) {
        if (!down || free_slot < 0) {
            return;
        }
        f = free_slot;
        s_fingers[f].drag = -1;
        s_fingers[f].id = id;
        s_fingers[f].x = x;
        s_fingers[f].y = y;
        s_fingers[f].down = 1;
        finger_down(&s_fingers[f]);
        return;
    }
    s_fingers[f].x = x;
    s_fingers[f].y = y;
    s_fingers[f].down = down;
    if (!down) {
        s_fingers[f].drag = -1;
    } else if (s_edit && s_fingers[f].drag >= 0) {
        place_at(s_fingers[f].drag, x - s_fingers[f].grab_x, y - s_fingers[f].grab_y, s_w, s_h);
    }
}

static u32 held(int w, int h)
{
    u32 b = 0;
    int i, f;

    for (f = 0; f < FINGERS; f++) {
        if (!s_fingers[f].down) {
            continue;
        }
        for (i = 0; i < BUTTONS; i++) {
            float cx, cy, r;

            place(i, w, h, &cx, &cy, &r);
            if (inside(s_fingers[f].x, s_fingers[f].y, cx, cy, r)) {
                b |= s_buttons[i].pad;
            }
        }
    }
    return b;
}

u32 touch_buttons(int w, int h)
{
    s_w = w;
    s_h = h;
    ensure_layout();
    /* The editor takes the fingers: the game gets no buttons. */
    return s_seen && !s_edit ? held(w, h) : 0;
}

/* ------------------------------------------------------------ icons */

static int in_triangle(float u, float v, float ax, float ay, float bx, float by, float cx,
                       float cy)
{
    float d1 = (u - bx) * (ay - by) - (ax - bx) * (v - by);
    float d2 = (u - cx) * (by - cy) - (bx - cx) * (v - cy);
    float d3 = (u - ax) * (cy - ay) - (cx - ax) * (v - ay);

    return !((d1 < 0.0f || d2 < 0.0f || d3 < 0.0f) && (d1 > 0.0f || d2 > 0.0f || d3 > 0.0f));
}

static int in_box(float u, float v, float x0, float y0, float x1, float y1)
{
    return u >= x0 && u <= x1 && v >= y0 && v <= y1;
}

static float segment(float u, float v, float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0, dy = y1 - y0;
    float t = ((u - x0) * dx + (v - y0) * dy) / (dx * dx + dy * dy);
    float px, py;

    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    px = x0 + t * dx - u;
    py = y0 + t * dy - v;
    return sqrtf(px * px + py * py);
}

/* Whether an icon covers (u, v), both -1..1 with v down. The ring takes
 * the outer 0.88 of the radius: the shapes stay within half of it. */
static int glyph(int icon, float u, float v)
{
    switch (icon) {
    case I_LEFT:
        return in_triangle(u, v, -0.45f, 0.0f, 0.3f, -0.46f, 0.3f, 0.46f);
    case I_RIGHT:
        return in_triangle(u, v, 0.45f, 0.0f, -0.3f, -0.46f, -0.3f, 0.46f);
    case I_UP:
        return in_triangle(u, v, 0.0f, -0.45f, -0.46f, 0.3f, 0.46f, 0.3f);
    case I_DOWN:
        return in_triangle(u, v, 0.0f, 0.45f, -0.46f, -0.3f, 0.46f, -0.3f);
    case I_GAS:         /* two chevrons forward */
        return segment(u, v, -0.38f, -0.4f, -0.02f, 0.0f) < 0.1f
            || segment(u, v, -0.02f, 0.0f, -0.38f, 0.4f) < 0.1f
            || segment(u, v, 0.0f, -0.4f, 0.36f, 0.0f) < 0.1f
            || segment(u, v, 0.36f, 0.0f, 0.0f, 0.4f) < 0.1f;
    case I_BRAKE:       /* stop */
        return in_box(u, v, -0.36f, -0.36f, 0.36f, 0.36f);
    case I_ACTION:      /* a bolt */
        return in_triangle(u, v, 0.18f, -0.5f, -0.28f, 0.1f, 0.08f, 0.1f)
            || in_triangle(u, v, -0.18f, 0.5f, 0.28f, -0.1f, -0.08f, -0.1f);
    case I_LOOK: {      /* an eye */
        float e = (u * u) / (0.52f * 0.52f) + (v * v) / (0.32f * 0.32f);

        return (e <= 1.0f && e >= 0.55f) || u * u + v * v < 0.0225f;
    }
    case I_PAUSE:
        return in_box(u, v, -0.3f, -0.35f, -0.1f, 0.35f) || in_box(u, v, 0.1f, -0.35f, 0.3f, 0.35f);
    case I_MENU:        /* a list */
        return in_box(u, v, -0.35f, -0.32f, 0.35f, -0.2f)
            || in_box(u, v, -0.35f, -0.06f, 0.35f, 0.06f)
            || in_box(u, v, -0.35f, 0.2f, 0.35f, 0.32f);
    case I_MOVE:        /* a cross of arrows */
        return in_box(u, v, -0.4f, -0.05f, 0.4f, 0.05f) || in_box(u, v, -0.05f, -0.4f, 0.05f, 0.4f)
            || in_triangle(u, v, -0.56f, 0.0f, -0.3f, -0.2f, -0.3f, 0.2f)
            || in_triangle(u, v, 0.56f, 0.0f, 0.3f, -0.2f, 0.3f, 0.2f)
            || in_triangle(u, v, 0.0f, -0.56f, -0.2f, -0.3f, 0.2f, -0.3f)
            || in_triangle(u, v, 0.0f, 0.56f, -0.2f, 0.3f, 0.2f, 0.3f);
    case I_PLUS:
        return in_box(u, v, -0.4f, -0.07f, 0.4f, 0.07f) || in_box(u, v, -0.07f, -0.4f, 0.07f, 0.4f);
    case I_MINUS:
        return in_box(u, v, -0.4f, -0.07f, 0.4f, 0.07f);
    case I_RESET: {     /* a circular arrow, open at the top right */
        float d2 = u * u + v * v, a = atan2f(-v, u) * 57.29578f;

        if (d2 > 0.09f && d2 < 0.2025f && !(a > 25.0f && a < 80.0f)) {
            return 1;
        }
        return in_triangle(u, v, 0.17f, -0.12f, 0.51f, -0.12f, 0.21f, -0.43f);
    }
    case I_DONE:        /* a check mark */
        return segment(u, v, -0.4f, 0.0f, -0.12f, 0.3f) < 0.09f
            || segment(u, v, -0.12f, 0.3f, 0.42f, -0.3f) < 0.09f;
    default:
        return 0;
    }
}

/* An icon in a ring, as palette indices; 0 outside the ring. */
static void make_icon(int icon, int variant, u8 *px)
{
    u8 line = variant == V_EDIT ? 3 : 1;
    int x, y;

    for (y = 0; y < SIZE; y++) {
        for (x = 0; x < SIZE; x++) {
            float u = ((float)x + 0.5f) * (2.0f / SIZE) - 1.0f;
            float v = ((float)y + 0.5f) * (2.0f / SIZE) - 1.0f;
            float d2 = u * u + v * v;
            u8 c = 0;

            if (d2 <= 1.0f) {
                if (d2 > 0.78f) {
                    c = line;
                } else {
                    if (variant == V_LIT) {
                        c = 2;
                    }
                    if (glyph(icon, u, v)) {
                        c = variant == V_LIT ? 4 : line;
                    }
                }
            }
            px[y * SIZE + x] = c;
        }
    }
}

static int make_textures(void)
{
    static u8 px[SIZE * SIZE], empty[SIZE * SIZE];
    static u8 palette[256 * 3];
    int i, v;

    if (s_tex[0][0] != 0) {
        return 1;
    }
    memset(palette + 3, 235, 3);
    memset(palette + 6, 150, 3);
    palette[9] = 255;
    palette[10] = 190;
    palette[11] = 40;
    memset(palette + 12, 35, 3);
    for (i = 0; i < ICONS; i++) {
        for (v = 0; v < VARIANTS; v++) {
            make_icon(i, v, px);
            s_tex[i][v] = rnd_texture(RND_R8, SIZE, SIZE, px, 0);
        }
    }
    s_empty = rnd_texture(RND_R8, SIZE, SIZE, empty, 0);
    s_palette = rnd_texture(RND_RGB8, 256, 1, palette, 256);
    return s_tex[0][0] != 0;
}

static void draw_icon(int icon, int variant, float cx, float cy, float r, int w, int h)
{
    float rect[4];

    rect[0] = (cx - r) / (float)w * 2.0f - 1.0f;
    rect[1] = 1.0f - (cy + r) / (float)h * 2.0f;
    rect[2] = (cx + r) / (float)w * 2.0f - 1.0f;
    rect[3] = 1.0f - (cy - r) / (float)h * 2.0f;
    rnd_uniform(RND_U_RECT, rect);
    rnd_texture_unit(0, s_tex[icon][variant]);
    rnd_draw(RND_STRIP, 0, 4);
}

void touch_draw(int w, int h)
{
    u32 on;
    int i;
    float cx, cy, r;

    s_w = w;
    s_h = h;
    ensure_layout();
    if (!s_seen || w <= 0 || h <= 0 || !make_textures()) {
        return;
    }
    on = s_edit ? 0 : held(w, h);
    rnd_bind_screen(w, h);
    /* Depth test off, but depth writes stay on, as everywhere else:
     * glClear honours the depth mask, so leaving it off here would stop
     * the next frame's depth clears and black out the 3D picture. */
    rnd_depth(RND_DEPTH_OFF, 1);
    rnd_scissor(0, 0, 0, 0, 0);
    rnd_cull(RND_CULL_OFF);
    rnd_use(RND_OVER);
    rnd_texture_unit(1, s_palette);
    rnd_texture_unit(2, s_empty);
    for (i = 0; i < BUTTONS; i++) {
        int variant = s_edit ? (i == s_pick ? V_LIT : V_EDIT)
                             : (on & s_buttons[i].pad) ? V_LIT : V_NORMAL;

        place(i, w, h, &cx, &cy, &r);
        draw_icon(s_buttons[i].icon, variant, cx, cy, r, w, h);
    }
    if (s_edit) {
        for (i = 0; i < TOOLS; i++) {
            tool_place(i, w, h, &cx, &cy, &r);
            draw_icon(s_tool_icon[i], V_EDIT, cx, cy, r, w, h);
        }
    } else {
        move_place(w, h, &cx, &cy, &r);
        draw_icon(I_MOVE, V_NORMAL, cx, cy, r, w, h);
    }
}
