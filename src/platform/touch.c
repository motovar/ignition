/*
 * platform/touch.c - the on-screen pad. See touch.h.
 *
 * The buttons are rings with an icon, drawn as RGBA textures with
 * rnd.h's LAYER program and blending, so they can be see-through. A held
 * button is filled. The icons are made here, from a few shapes.
 *
 * The move icon at the bottom of the screen starts the layout editor (it
 * is one of the items the editor moves and sizes, but it cannot be
 * hidden): drag a button to move it and tap it to pick it. Three sliders set the
 * size (of the picked button, of every button when none is picked), how
 * see-through the buttons are and how thick their rings are. The bin
 * hides the picked button (a hidden button shows faintly in the editor;
 * tapping it brings it back), the circular arrow puts everything back
 * and the check mark keeps the layout, in touchpad.cfg in the working
 * folder. The boost gauge toggle (a bar) switches whether the action
 * button's bolt, in a race, fills from its foot with the boost as the
 * HUD's bar does, and the button glows when it is full.
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
    I_MOVE, I_TRASH, I_RESET, I_DONE, I_GAUGE,
    I_SIZE, I_OPACITY, I_BORDER, I_KNOB,   /* the sliders': no ring */
    I_BOLT, I_FILL,             /* the action button's boost: its bolt, and a glow */
    ICONS
};

/* A button's look: V_LIT is held (or picked); the editor's are amber. */
enum { V_NORMAL, V_LIT, V_EDIT, V_TOOL, V_GHOST, V_FILL, V_FILL_HI, V_FILL_LOW, VARIANTS };

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
    { { 6.0f, 0.5f, 0.38f }, 0, I_MOVE },       /* the editor's, last */
};

enum {
    ITEMS = sizeof(s_buttons) / sizeof(s_buttons[0]),
    BUTTONS = ITEMS - 1,        /* the ones that are the game's */
    EDIT_ITEM = BUTTONS,
    FINGERS = 10,
    SIZE = 128
};

/* The limits the sliders keep to. */
#define R_MIN 0.25f
#define R_MAX 1.3f
#define SCALE_MIN 0.5f
#define SCALE_MAX 1.6f
#define ALPHA_MIN 0.1f
#define ALPHA_MAX 1.0f
#define BORDER_MIN 0.03f
#define BORDER_MAX 0.30f
#define BORDER_DEFAULT 0.117f
#define CONFIG_FILE "touchpad.cfg"

enum { T_TRASH, T_RESET, T_GAUGE, T_DONE, TOOLS };
static const int s_tool_icon[TOOLS] = { I_TRASH, I_RESET, I_GAUGE, I_DONE };

enum { S_SIZE, S_OPACITY, S_BORDER, SLIDERS };
static const int s_slider_icon[SLIDERS] = { I_SIZE, I_OPACITY, I_BORDER };

/* What a finger holds: a button it moves, a slider it sets, or nothing. */
enum { DRAG_NONE = -1, DRAG_SLIDER = -2 };      /* a slider k is DRAG_SLIDER - k */

static struct finger {
    long id;
    float x, y;
    int down;
    int drag;
    float grab_x, grab_y;       /* where it holds the button, from its centre */
} s_fingers[FINGERS];

static struct place s_layout[ITEMS];
static u8 s_hidden[ITEMS];
static float s_scale = 1.0f, s_alpha = 1.0f, s_border = BORDER_DEFAULT;
static int s_seen, s_loaded, s_edit, s_pick = -1;
static int s_boost_gauge = 1;   /* the action button shows the boost in a race */
static int s_frame;             /* touch_draw's frames, for the blinking */

static int s_w, s_h;            /* the drawable's size, as last given */

/* ------------------------------------------------------------ layout */

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void default_layout(void)
{
    int i;

    for (i = 0; i < ITEMS; i++) {
        s_layout[i] = s_buttons[i].at;
        s_hidden[i] = 0;
    }
    s_scale = 1.0f;
    s_alpha = 1.0f;
    s_border = BORDER_DEFAULT;
    s_boost_gauge = 1;
}

static int ok_place(const struct place *p)
{
    return fabsf(p->x) < 20.0f && p->y > -1.0f && p->y < 8.0f && p->r >= R_MIN && p->r <= R_MAX;
}

/* Version 1 had the three numbers of each button; version 2 adds the
 * scale, opacity and ring thickness, and a hidden flag a button; version
 * 3 the editor's icon after the buttons. */
static void load_layout(void)
{
    FILE *fp = fopen(CONFIG_FILE, "r");
    struct place read[ITEMS];
    int hidden[ITEMS];
    float scale = 1.0f, alpha = 1.0f, border = BORDER_DEFAULT;
    int i, n, version = 0, gauge = 1;

    if (fp == NULL) {
        return;
    }
    if (fscanf(fp, "ignition-touchpad %d", &version) == 1 && (version >= 1 && version <= 4)) {
        n = version >= 3 ? ITEMS : BUTTONS;
        if (version >= 2 && fscanf(fp, "%f %f %f", &scale, &alpha, &border) != 3) {
            version = 0;
        }
        for (i = 0; version != 0 && i < n; i++) {
            hidden[i] = 0;
            if (fscanf(fp, "%f %f %f", &read[i].x, &read[i].y, &read[i].r) != 3
                || (version >= 2 && fscanf(fp, "%d", &hidden[i]) != 1) || !ok_place(&read[i])) {
                version = 0;
            }
        }
        if (version == 4 && fscanf(fp, "%d", &gauge) != 1) {
            gauge = 1;
        }
        if (version != 0) {
            s_boost_gauge = gauge != 0;
            for (i = 0; i < n; i++) {
                s_layout[i] = read[i];
                s_hidden[i] = hidden[i] != 0 && i != EDIT_ITEM;
            }
            s_scale = clampf(scale, SCALE_MIN, SCALE_MAX);
            s_alpha = clampf(alpha, ALPHA_MIN, ALPHA_MAX);
            s_border = clampf(border, BORDER_MIN, BORDER_MAX);
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
    fprintf(fp, "ignition-touchpad 4\n%.3f %.3f %.3f\n", s_scale, s_alpha, s_border);
    for (i = 0; i < ITEMS; i++) {
        fprintf(fp, "%.3f %.3f %.3f %d\n", s_layout[i].x, s_layout[i].y, s_layout[i].r,
                s_hidden[i]);
    }
    fprintf(fp, "%d\n", s_boost_gauge);
    fclose(fp);
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
    *r = s_layout[i].r * s_scale * unit;
}

/* Puts button `i` with its centre at (cx, cy) pixels, kept on the screen,
 * as the edge it is nearer to. */
static void place_at(int i, float cx, float cy, int w, int h)
{
    float unit = (float)h / 6.0f, r = s_layout[i].r * s_scale * unit;

    cx = clampf(cx, r, (float)w - r);
    cy = clampf(cy, r, (float)h - r);
    if (cx < (float)w * 0.5f) {
        s_layout[i].x = cx / unit < 0.01f ? 0.01f : cx / unit;
    } else {
        s_layout[i].x = -((float)w - cx) / unit > -0.01f ? -0.01f : -((float)w - cx) / unit;
    }
    s_layout[i].y = ((float)h - cy) / unit;
}

/* Keeps every button on the screen after a size change. */
static void keep_on_screen(void)
{
    int i;

    for (i = 0; i < ITEMS; i++) {
        float cx, cy, r;

        place(i, s_w, s_h, &cx, &cy, &r);
        place_at(i, cx, cy, s_w, s_h);
    }
}

/* The editor's own controls, not part of the layout. */
static void tool_place(int k, int w, int h, float *cx, float *cy, float *r)
{
    float unit = (float)h / 6.0f;

    *cx = (float)w * 0.5f + ((float)k - (float)(TOOLS - 1) * 0.5f) * 1.1f * unit;
    *cy = (float)h - 1.6f * unit;
    *r = 0.42f * unit;
}

/* A slider: the track from x0 to x1 at height y, its label's centre and
 * radius. */
static void slider_place(int k, int w, int h, float *x0, float *x1, float *y, float *lx, float *lr)
{
    float unit = (float)h / 6.0f, half = 2.2f * unit;

    if (half > (float)w * 0.25f) {
        half = (float)w * 0.25f;
    }
    *x0 = (float)w * 0.5f - half;
    *x1 = (float)w * 0.5f + half;
    *y = (float)h * 0.4f + ((float)k - 1.0f) * 0.9f * unit;
    *lr = 0.33f * unit;
    *lx = *x0 - 0.65f * unit;
}

/* A slider's range and its value now. */
static void slider_range(int k, float *lo, float *hi, float *value)
{
    switch (k) {
    case S_SIZE:
        if (s_pick >= 0) {
            *lo = R_MIN;
            *hi = R_MAX;
            *value = s_layout[s_pick].r;
        } else {
            *lo = SCALE_MIN;
            *hi = SCALE_MAX;
            *value = s_scale;
        }
        break;
    case S_OPACITY:
        *lo = ALPHA_MIN;
        *hi = ALPHA_MAX;
        *value = s_alpha;
        break;
    default:
        *lo = BORDER_MIN;
        *hi = BORDER_MAX;
        *value = s_border;
        break;
    }
}

static int s_gen = 1;           /* the icons are made again when it changes */

static void slider_set(int k, float x)
{
    float x0, x1, y, lx, lr, lo, hi, value, t;

    slider_place(k, s_w, s_h, &x0, &x1, &y, &lx, &lr);
    slider_range(k, &lo, &hi, &value);
    t = clampf((x - x0) / (x1 - x0), 0.0f, 1.0f);
    value = lo + t * (hi - lo);
    switch (k) {
    case S_SIZE:
        if (s_pick >= 0) {
            s_layout[s_pick].r = value;
        } else {
            s_scale = value;
        }
        keep_on_screen();
        break;
    case S_OPACITY:
        s_alpha = value;
        s_gen++;
        break;
    default:
        s_border = value;
        s_gen++;
        break;
    }
}

static int inside(float x, float y, float cx, float cy, float r)
{
    float dx = x - cx, dy = y - cy;

    /* A little slack around each button. */
    return dx * dx + dy * dy < r * r * 1.4f;
}

/* The button under (x, y), the one it is nearest to the centre of; the
 * hidden ones too when `ghosts`. */
static int pick(float x, float y, int ghosts)
{
    int i, best = -1;
    float best_d = 0.0f;

    for (i = 0; i < ITEMS; i++) {
        float cx, cy, r, d;

        if (s_hidden[i] && !ghosts) {
            continue;
        }
        place(i, s_w, s_h, &cx, &cy, &r);
        d = ((x - cx) * (x - cx) + (y - cy) * (y - cy)) / (r * r);
        if (d < 1.4f && (best < 0 || d < best_d)) {
            best = i;
            best_d = d;
        }
    }
    return best;
}

static void tool_action(int tool)
{
    switch (tool) {
    case T_TRASH:
        if (s_pick >= 0 && s_pick != EDIT_ITEM) {
            s_hidden[s_pick] = 1;
        }
        break;
    case T_RESET:
        default_layout();
        s_pick = -1;
        s_gen++;
        break;
    case T_GAUGE:
        s_boost_gauge = !s_boost_gauge;
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
    float cx, cy, r, x0, x1, y, lx, lr;

    if (s_w <= 0 || s_h <= 0) {
        return;
    }
    if (!s_edit) {
        place(EDIT_ITEM, s_w, s_h, &cx, &cy, &r);
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
    for (i = 0; i < SLIDERS; i++) {
        slider_place(i, s_w, s_h, &x0, &x1, &y, &lx, &lr);
        if (g->x > x0 - 0.35f * (float)s_h / 6.0f && g->x < x1 + 0.35f * (float)s_h / 6.0f
            && fabsf(g->y - y) < 0.45f * (float)s_h / 6.0f) {
            g->drag = DRAG_SLIDER - i;
            slider_set(i, g->x);
            return;
        }
    }
    s_pick = pick(g->x, g->y, 1);
    if (s_pick >= 0) {
        s_hidden[s_pick] = 0;   /* a tap on a hidden button brings it back */
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
        s_fingers[f].drag = DRAG_NONE;
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
        s_fingers[f].drag = DRAG_NONE;
    } else if (s_edit && s_fingers[f].drag >= 0) {
        place_at(s_fingers[f].drag, x - s_fingers[f].grab_x, y - s_fingers[f].grab_y, s_w, s_h);
    } else if (s_edit && s_fingers[f].drag <= DRAG_SLIDER) {
        slider_set(DRAG_SLIDER - s_fingers[f].drag, x);
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

            if (s_hidden[i]) {
                continue;
            }
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
 * the outer part of the radius: the shapes stay within half of it, the
 * slider labels' within 0.9. */
static int glyph(int icon, float u, float v)
{
    float r = sqrtf(u * u + v * v);

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
    case I_BOLT:
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
    case I_TRASH:       /* a bin: the handle, the lid and the body with slits */
        return in_box(u, v, -0.13f, -0.46f, 0.13f, -0.34f) || in_box(u, v, -0.38f, -0.32f, 0.38f, -0.2f)
            || (in_box(u, v, -0.28f, -0.14f, 0.28f, 0.44f)
                && !(in_box(u, v, -0.025f, -0.04f, 0.025f, 0.34f)
                     || in_box(u, v, -0.145f, -0.04f, -0.095f, 0.34f)
                     || in_box(u, v, 0.095f, -0.04f, 0.145f, 0.34f)));
    case I_RESET: {     /* a circular arrow, open at the top right */
        float a = atan2f(-v, u) * 57.29578f;

        if (r * r > 0.09f && r * r < 0.2025f && !(a > 25.0f && a < 80.0f)) {
            return 1;
        }
        return in_triangle(u, v, 0.17f, -0.12f, 0.51f, -0.12f, 0.21f, -0.43f);
    }
    case I_DONE:        /* a check mark */
        return segment(u, v, -0.4f, 0.0f, -0.12f, 0.3f) < 0.09f
            || segment(u, v, -0.12f, 0.3f, 0.42f, -0.3f) < 0.09f;
    case I_SIZE:        /* a small and a big ring */
        return fabsf(r - 0.3f) < 0.07f || fabsf(r - 0.72f) < 0.08f;
    case I_OPACITY:     /* a ring, its left half full */
        return (r > 0.7f && r < 0.84f) || (r <= 0.7f && u < 0.0f);
    case I_BORDER:      /* a thick ring */
        return r > 0.42f && r < 0.86f;
    case I_KNOB:
        return r < 0.85f;
    case I_FILL:        /* inside the ring */
        return r < 0.84f;
    case I_GAUGE: {     /* the bar: its frame, half full */
        int frame = in_box(u, v, -0.24f, -0.46f, 0.24f, 0.46f)
            && !in_box(u, v, -0.15f, -0.37f, 0.15f, 0.37f);

        return frame || in_box(u, v, -0.09f, 0.0f, 0.09f, 0.31f);
    }
    default:
        return 0;
    }
}

/* The icons that are only a shape, without the ring around them. */
static int no_ring(int icon)
{
    return icon >= I_SIZE;
}

/* The icons' shapes, 0 to 255 of a 2 x 2 sample a pixel; made once. */
static u8 s_glyph[ICONS][SIZE * SIZE];
static int s_glyphs_made;

static void make_glyphs(void)
{
    int icon, x, y, sx, sy;

    for (icon = 0; icon < ICONS; icon++) {
        for (y = 0; y < SIZE; y++) {
            for (x = 0; x < SIZE; x++) {
                int hits = 0;

                for (sy = 0; sy < 2; sy++) {
                    for (sx = 0; sx < 2; sx++) {
                        float u = ((float)x + ((float)sx + 0.5f) * 0.5f) * (2.0f / SIZE) - 1.0f;
                        float v = ((float)y + ((float)sy + 0.5f) * 0.5f) * (2.0f / SIZE) - 1.0f;

                        hits += glyph(icon, u, v);
                    }
                }
                s_glyph[icon][y * SIZE + x] = (u8)(hits * 255 / 4);
            }
        }
    }
    s_glyphs_made = 1;
}

/* Colours a look is made of, 0 to 1. */
struct rgb {
    float r, g, b;
};

static const struct rgb C_WHITE = { 0.92f, 0.92f, 0.92f };
static const struct rgb C_AMBER = { 1.0f, 0.745f, 0.157f };
static const struct rgb C_GREY = { 0.59f, 0.59f, 0.59f };
static const struct rgb C_DARK = { 0.137f, 0.137f, 0.137f };
static const struct rgb C_YELLOW = { 1.0f, 0.84f, 0.12f };    /* the HUD bar's */
static const struct rgb C_RED = { 1.0f, 0.3f, 0.16f };

/* `src` of coverage `a` over the premultiplied pixel `px`. */
static void over(float *px, struct rgb c, float a)
{
    float keep = 1.0f - a;

    px[0] = c.r * a + px[0] * keep;
    px[1] = c.g * a + px[1] * keep;
    px[2] = c.b * a + px[2] * keep;
    px[3] = a + px[3] * keep;
}

/* An icon in a look, as premultiplied RGBA with the ring's smooth
 * edges; the buttons keep a little opacity when held or in the editor,
 * whatever the setting. */
static void make_icon(int icon, int variant, u8 *out)
{
    struct rgb line = variant == V_NORMAL || variant == V_LIT ? C_WHITE : C_AMBER;
    int lit = variant == V_LIT, ring = !no_ring(icon);
    float alpha = s_alpha;
    int x, y;

    switch (variant) {
    case V_LIT:
        alpha = alpha < 0.55f ? 0.55f : alpha;
        break;
    case V_EDIT:
        alpha = alpha < 0.5f ? 0.5f : alpha;
        break;
    case V_TOOL:
        alpha = 1.0f;
        break;
    case V_GHOST:
        alpha = 0.22f;
        break;
    case V_FILL:        /* the bolt filled, as the HUD's bar */
        line = C_YELLOW;
        alpha = 1.0f;
        break;
    case V_FILL_HI:     /* the glow of a full boost */
        line = C_YELLOW;
        alpha = 0.4f;
        break;
    case V_FILL_LOW:    /* too little to use */
        line = C_RED;
        alpha = 1.0f;
        break;
    default:
        break;
    }
    for (y = 0; y < SIZE; y++) {
        for (x = 0; x < SIZE; x++) {
            float u = ((float)x + 0.5f) * (2.0f / SIZE) - 1.0f;
            float v = ((float)y + 0.5f) * (2.0f / SIZE) - 1.0f;
            float r = sqrtf(u * u + v * v);
            float px[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float g = (float)s_glyph[icon][y * SIZE + x] / 255.0f;
            int k;

            if (ring) {
                float outer = clampf((1.0f - r) * (SIZE * 0.5f), 0.0f, 1.0f);
                float edge = clampf((r - (1.0f - s_border)) * (SIZE * 0.5f), 0.0f, 1.0f);

                if (lit) {
                    over(px, C_GREY, outer * (1.0f - edge) * 0.65f);
                }
                over(px, lit ? C_DARK : line, g);
                over(px, line, outer * edge);
            } else {
                over(px, line, g);
            }
            for (k = 0; k < 4; k++) {
                out[(y * SIZE + x) * 4 + k] = (u8)(clampf(px[k] * alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
        }
    }
}

static unsigned s_tex[ICONS][VARIANTS];
static int s_tex_gen[ICONS][VARIANTS];
static unsigned s_solid;

/* The texture of an icon in a look, made or made again when the opacity
 * or the ring's thickness changed. */
static unsigned icon_texture(int icon, int variant)
{
    static u8 px[SIZE * SIZE * 4];

    if (!s_glyphs_made) {
        make_glyphs();
    }
    if (s_tex[icon][variant] != 0 && s_tex_gen[icon][variant] == s_gen) {
        return s_tex[icon][variant];
    }
    make_icon(icon, variant, px);
    if (s_tex[icon][variant] == 0) {
        s_tex[icon][variant] = rnd_texture(RND_RGBA8, SIZE, SIZE, px, 0);
    } else {
        rnd_texture_set(s_tex[icon][variant], SIZE, SIZE, px, 0);
    }
    s_tex_gen[icon][variant] = s_gen;
    return s_tex[icon][variant];
}

static void draw_quad(unsigned tex, float x0, float y0, float x1, float y1, int w, int h)
{
    float rect[4];

    rect[0] = x0 / (float)w * 2.0f - 1.0f;
    rect[1] = 1.0f - y1 / (float)h * 2.0f;
    rect[2] = x1 / (float)w * 2.0f - 1.0f;
    rect[3] = 1.0f - y0 / (float)h * 2.0f;
    rnd_uniform(RND_U_RECT, rect);
    rnd_texture_unit(0, tex);
    rnd_draw(RND_STRIP, 0, 4);
}

static void draw_icon(int icon, int variant, float cx, float cy, float r, int w, int h)
{
    draw_quad(icon_texture(icon, variant), cx - r, cy - r, cx + r, cy + r, w, h);
}

/* The boost in the action button, in its bolt: the bolt fills yellow from
 * its foot as high as the HUD's bar, and the button glows behind when it
 * is full, blinking slowly as the HUD's lamp does. Burning, or too little
 * to use (under a fifth, then red), the bolt blinks fast. `front` is the
 * bolt, drawn over the button; not it, the glow, drawn under it. */
static void draw_boost(float cx, float cy, float r, int w, int h, int front)
{
    float level = g_touch_boost > 1.0f ? 1.0f : g_touch_boost;
    int full = level >= 1.0f, burning = g_touch_boost_on, low = level < 0.2f && !burning;
    int fast = (s_frame / 4) % 2, slow = (s_frame / 15) % 2;
    float top, foot;
    int y0, y1;

    if (!front) {
        if (full && !burning && slow) {
            draw_icon(I_FILL, V_FILL_HI, cx, cy, r, w, h);
        }
        return;
    }
    if (level <= 0.0f || ((burning || low) && fast)) {
        return;
    }
    top = cy + 0.5f * r - level * r;
    foot = cy + 0.5f * r;
    y0 = (int)floorf(top);
    y1 = (int)ceilf(foot) + 1;
    rnd_scissor(1, (int)floorf(cx - r), h - y1, (int)ceilf(2.0f * r) + 1, y1 - y0);
    draw_icon(I_BOLT, low ? V_FILL_LOW : V_FILL, cx, cy, r, w, h);
    rnd_scissor(0, 0, 0, 0, 0);
}

void touch_draw(int w, int h)
{
    u32 on;
    int i, gauge;
    float cx, cy, r;
    static const u8 amber[4] = { 255, 190, 40, 255 };

    s_w = w;
    s_h = h;
    s_frame++;
    ensure_layout();
    if (!s_seen || w <= 0 || h <= 0) {
        return;
    }
    if (s_solid == 0) {
        s_solid = rnd_texture(RND_RGBA8, 1, 1, amber, 0);
    }
    on = s_edit ? 0 : held(w, h);
    rnd_bind_screen(w, h);
    /* Depth test off, but depth writes stay on, as everywhere else:
     * glClear honours the depth mask, so leaving it off here would stop
     * the next frame's depth clears and black out the 3D picture. */
    rnd_depth(RND_DEPTH_OFF, 1);
    rnd_scissor(0, 0, 0, 0, 0);
    rnd_cull(RND_CULL_OFF);
    rnd_blend(RND_BLEND_PREMUL);
    rnd_use(RND_LAYER);
    for (i = 0; i < ITEMS; i++) {
        int variant;

        if (s_hidden[i] && !s_edit) {
            continue;
        }
        variant = s_edit ? (s_hidden[i] ? V_GHOST : i == s_pick ? V_LIT : V_EDIT)
                         : (on & s_buttons[i].pad) ? V_LIT : V_NORMAL;
        place(i, w, h, &cx, &cy, &r);
        gauge = s_boost_gauge && !s_edit && s_buttons[i].icon == I_ACTION && g_touch_boost >= 0.0f;
        if (gauge) {
            draw_boost(cx, cy, r, w, h, 0);
        }
        draw_icon(s_buttons[i].icon, variant, cx, cy, r, w, h);
        if (gauge) {
            draw_boost(cx, cy, r, w, h, 1);
        }
    }
    if (s_edit) {
        for (i = 0; i < TOOLS; i++) {
            tool_place(i, w, h, &cx, &cy, &r);
            draw_icon(s_tool_icon[i], i == T_GAUGE && !s_boost_gauge ? V_GHOST : V_TOOL, cx, cy, r, w,
                      h);
        }
        for (i = 0; i < SLIDERS; i++) {
            float x0, x1, y, lx, lr, lo, hi, value, kx, thin = (float)h / 6.0f * 0.04f;

            slider_place(i, w, h, &x0, &x1, &y, &lx, &lr);
            slider_range(i, &lo, &hi, &value);
            kx = x0 + (x1 - x0) * (value - lo) / (hi - lo);
            draw_quad(s_solid, x0, y - thin, x1, y + thin, w, h);
            draw_icon(s_slider_icon[i], V_TOOL, lx, y, lr, w, h);
            draw_icon(I_KNOB, V_TOOL, kx, y, lr * 0.8f, w, h);
        }
    }
    rnd_blend(RND_BLEND_OFF);
}
