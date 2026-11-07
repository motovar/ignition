/*
 * soft3d/faces.c - the face handlers of the original's pipeline
 * (0x44b560..0x44e900, called through the table at 0x49c8e0): each turns
 * a face of the current object, from its projected vertices, into a
 * screen space command in a depth bucket, or leaves it out (off the
 * screen, turned away, or too near).
 *
 * A face is a type byte and words: vertex indices, then colours,
 * texture coordinates (8.8 texels) and the texture's page offset. A
 * handler reads g_sw_face and moves it past what it took; the
 * triangle handlers take a run of faces of their type.
 *
 * The depth bucket is the sum of the vertices' depths over 16; objects
 * of class 100 and over (the cars, the effects) are pulled 0x5c nearer
 * (0x54 for class 210), so they draw over the road they stand on.
 */

#include "soft3d/sw.h"

#define FACE_WORD(i) (((const s32 *)g_sw_face)[i])

static const struct sw_vert *vert(int i)
{
    return &g_sw_verts[FACE_WORD(i)];
}

/* An object of class 100 or more is pulled 0x5c nearer, and not past
 * the eye. */
static s32 nearer(s32 d)
{
    if (g_sw_obj->klass >= 100) {
        d -= 0x5c;
        if (d < 0) {
            d = 0;
        }
    }
    return d;
}

/* The triangle handlers' rule: class 210 is pulled 0x54 nearer. */
static s32 nearer_17(s32 d)
{
    if (g_sw_obj->klass >= 100) {
        d -= g_sw_obj->klass == 210 ? 0x54 : 0x5c;
        if (d < 0) {
            d = 0;
        }
    }
    return d;
}

/* Whether the triangle's box misses the clip (in 8.8, the ends kept). */
static int box_out(const struct sw_vert *a, const struct sw_vert *b, const struct sw_vert *c)
{
    s32 hi, lo;

    hi = b->x > a->x ? b->x : a->x;
    hi = hi > c->x ? hi : c->x;
    if (hi < g_sw_clip[0]) {
        return 1;
    }
    lo = b->x < a->x ? b->x : a->x;
    lo = lo < c->x ? lo : c->x;
    if (g_sw_clip[2] < lo) {
        return 1;
    }
    hi = b->y > a->y ? b->y : a->y;
    hi = hi > c->y ? hi : c->y;
    if (hi < g_sw_clip[1]) {
        return 1;
    }
    lo = b->y < a->y ? b->y : a->y;
    lo = lo < c->y ? lo : c->y;
    return g_sw_clip[3] < lo;
}

/* Twice the triangle's area, positive when it faces the eye, from the
 * positions in 8.8 (the flat faces) or in 12.4 (the textured ones). */
static s32 facing(const struct sw_vert *a, const struct sw_vert *b, const struct sw_vert *c,
                  int shift)
{
    s32 xa = a->x >> shift, ya = a->y >> shift, xb = b->x >> shift, yb = b->y >> shift;
    s32 xc = c->x >> shift, yc = c->y >> shift;

    return sw_add(sw_mul(yb - ya, xc - xb), sw_mul(yc - yb, xa - xb)) ^ g_sw_mirror;
}

/* The texture of the current object at a mip level: -5 the texture
 * itself, -4 to -1 its blurred levels (slot[level] in the original). */
static const u8 *tex_level(s32 level)
{
    if (g_sw_handle == NULL || level < -5 || level > -1) {
        return g_sw_tex;
    }
    return g_sw_handle->level[level + 5];
}

/* Whether the current object's texture has magnified tiles: its slot[0]
 * is not the texture. */
static int magnified(void)
{
    return g_sw_handle != NULL && g_sw_handle->slot[0] != g_sw_handle->level[0];
}

/* The magnified tile the face's first texel is in: tile column
 * u0 >> 14's buffer, the face's page and tile row v0 >> 14 within it,
 * 4 bytes for each of the texture's. The port keeps to the four
 * columns. */
static const u8 *magnified_tex(void)
{
    s32 col = FACE_WORD(4) >> 14, row = FACE_WORD(5) >> 14;

    if (col < 0 || col > 3) {
        col = col < 0 ? 0 : 3;
    }
    return g_sw_handle->slot[4 * col] + (size_t)(u32)(FACE_WORD(10) + (row << 14)) * 4;
}

/* 0x44b560: type 0 ends the mesh. */
static void face_end(void)
{
    g_sw_faces_left = 1;
}

/* 0x44b5e0: a point of the face's colour, past 200 from the eye. */
static void face_point(void)
{
    const struct sw_vert *v = vert(1);
    struct sw_cmd_point *c;

    if (v->z > 200 && (c = sw_cmd_alloc(sizeof(*c))) != NULL) {
        c->type = FACE_WORD(0) & 0xffff;
        c->x = v->x;
        c->y = v->y;
        c->colour = FACE_WORD(2);
        c->f14 = 0;
        sw_cmd_bucket(nearer((v->z * 3) >> 4));
    }
    g_sw_face += 12;
}

/* 0x44dbb0: a point through the .SHD. */
static void face_point_table(void)
{
    const struct sw_vert *v = vert(1);
    struct sw_cmd_point_table *c;

    if (v->z > 200 && (c = sw_cmd_alloc(sizeof(*c))) != NULL) {
        c->type = SW_POINT_TABLE;
        c->x = v->x;
        c->y = v->y;
        c->colour = FACE_WORD(2);
        c->table = g_sw_shd;
        sw_cmd_bucket(nearer((v->z * 3) >> 4));
    }
    g_sw_face += 12;
}

/* A sprite's source: the box u0, v0 .. u1, v1 (8.8) of the page at
 * word 6, held at its middle. */
static void sprite_src(struct sw_cmd_sprite *c, const u8 *table)
{
    c->src = &c->src_;
    c->scale = &c->scale_;
    c->src_.hot_x = (FACE_WORD(2) + FACE_WORD(4)) / 2 - FACE_WORD(2);
    c->src_.hot_y = (FACE_WORD(3) + FACE_WORD(5)) / 2 - FACE_WORD(3);
    c->src_.u0 = FACE_WORD(2);
    c->src_.v0 = FACE_WORD(3);
    c->src_.u1 = FACE_WORD(4);
    c->src_.v1 = FACE_WORD(5);
    c->src_.table = table;
    c->src_.tex = tex_level(-5) + FACE_WORD(6);
    c->scale_.f38 = 0;
    c->scale_.f3c = 0;
}

/* 0x44d0f0: a sprite through the .SHD, its size words 7 and 8 times
 * 4 / depth. */
static void face_sprite(void)
{
    const struct sw_vert *v = vert(1);
    struct sw_cmd_sprite *c;

    if (v->z > 200 && g_sw_tex != NULL && (c = sw_cmd_alloc(sizeof(*c))) != NULL) {
        double k = 4.0 / v->z;

        c->type = SW_SPRITE;
        c->x = v->x;
        c->y = v->y;
        sprite_src(c, g_sw_shd);
        c->scale_.sx = sw_ftol(FACE_WORD(7) * k);
        c->scale_.sy = sw_ftol(FACE_WORD(8) * k);
        sw_cmd_bucket(nearer(sw_ftol(v->z * 2.8) >> 4));
    }
    g_sw_face += 0x24;
}

/* 0x44d230: a sprite through the .PAN, on a whole pixel, its size
 * scaled by the focal length only (a lens flare). */
static void face_sprite_fixed(void)
{
    const struct sw_vert *v = vert(1);
    struct sw_cmd_sprite *c;

    if (v->z > 200 && g_sw_tex != NULL && (c = sw_cmd_alloc(sizeof(*c))) != NULL) {
        double fy = g_scene->focal_y;

        c->type = SW_SPRITE;
        c->x = v->x & ~0xff;
        c->y = v->y & ~0xff;
        sprite_src(c, g_sw_pan);
        c->scale_.sx = sw_ftol((double)0.003952569328248501f * fy * FACE_WORD(7));
        c->scale_.sy = sw_ftol(fy * 0.004820206304829848 * FACE_WORD(8));
        sw_cmd_bucket(nearer(sw_ftol(v->z * 2.8) >> 4));
    }
    g_sw_face += 0x24;
}

/* Types 11 and 12: a line of the face's colour,
 * its ends past 400 from the eye together. */
static void face_line(int size)
{
    const struct sw_vert *a = vert(1), *b = vert(2);
    struct sw_cmd_line *c;

    if (a->z + b->z > 400 && (c = sw_cmd_alloc((size_t)size)) != NULL) {
        c->type = FACE_WORD(0) & 0xffff;
        c->x0 = a->x;
        c->y0 = a->y;
        c->x1 = b->x;
        c->y1 = b->y;
        c->colour = FACE_WORD(3);
        c->f20 = 0;
        if (size > 0x24) {
            c->f24 = 0;
        }
        sw_cmd_bucket(nearer((a->z + b->z * 2) >> 4));
    }
    g_sw_face += 0x10;
}

/* 0x44d3a0 */
static void face_line_11(void)
{
    face_line(0x24);
}

/* 0x44b690 */
static void face_line_12(void)
{
    face_line(0x28);
}

/* 0x44d470: a line whose colour runs from word 3 to word 4 (8.8),
 * through the table at word 5. The original keeps the table's address
 * in the mesh; the port keeps 0 or 1 there and takes the .SHD for 1. */
static void face_line_shade(void)
{
    const struct sw_vert *a = vert(1), *b = vert(2);
    struct sw_cmd_line_shade *c;

    if (a->z + b->z > 400 && FACE_WORD(5) != 0 && g_sw_shd != NULL
        && (c = sw_cmd_alloc(sizeof(*c))) != NULL) {
        c->type = FACE_WORD(0) & 0xffff;
        c->x0 = a->x;
        c->y0 = a->y;
        c->x1 = b->x;
        c->y1 = b->y;
        c->table = g_sw_shd;
        c->c0 = FACE_WORD(3);
        c->c1 = FACE_WORD(4);
        sw_cmd_bucket(nearer((a->z + b->z * 2) >> 4));
    }
    g_sw_face += 0x18;
}

/* 0x44b770: a flat triangle of the face's colour. */
static void face_flat(void)
{
    const struct sw_vert *a = vert(1), *b = vert(2), *v = vert(3);
    struct sw_cmd_flat *c;
    s32 sum;

    if (!box_out(a, b, v)) {
        sum = v->z + b->z + a->z;
        if (sum > 600 && facing(a, b, v, 0) > 0 && (c = sw_cmd_alloc(sizeof(*c))) != NULL) {
            c->type = FACE_WORD(0) & 0xffff;
            c->v[0].x = a->x;
            c->v[0].y = a->y;
            c->v[1].x = b->x;
            c->v[1].y = b->y;
            c->v[2].x = v->x;
            c->v[2].y = v->y;
            c->f2c = 0;
            c->colour = FACE_WORD(4);
            sw_cmd_bucket(nearer(sum >> 4));
        }
    }
    g_sw_face += 0x14;
}

/* 0x44b980: the same with a fourth word to each vertex, 0. */
static void face_flat16(void)
{
    const struct sw_vert *a = vert(1), *b = vert(2), *v = vert(3);
    struct sw_cmd_flat16 *c;
    s32 sum;

    if (!box_out(a, b, v)) {
        sum = v->z + b->z + a->z;
        if (sum > 600 && facing(a, b, v, 0) > 0 && (c = sw_cmd_alloc(sizeof(*c))) != NULL) {
            c->type = FACE_WORD(0) & 0xffff;
            c->v[0].x = a->x;
            c->v[0].y = a->y;
            c->v[1].x = b->x;
            c->v[1].y = b->y;
            c->v[2].x = v->x;
            c->v[2].y = v->y;
            c->v[0].i = c->v[1].i = c->v[2].i = 0;
            c->colour = FACE_WORD(4);
            sw_cmd_bucket(nearer(sum >> 4));
        }
    }
    g_sw_face += 0x14;
}

/* An affine textured command (type 17) for the face. */
static void put_texture(const struct sw_vert *a, const struct sw_vert *b, const struct sw_vert *v,
                        s32 level, s32 d)
{
    struct sw_cmd_texture *c = sw_cmd_alloc(sizeof(*c));

    if (c == NULL) {
        return;
    }
    c->type = SW_TEXTURE;
    c->v[0].x = a->x;
    c->v[0].y = a->y;
    c->v[1].x = b->x;
    c->v[1].y = b->y;
    c->v[2].x = v->x;
    c->v[2].y = v->y;
    c->uv = &FACE_WORD(4);
    c->tex = tex_level(level) + FACE_WORD(10);
    sw_cmd_bucket(d);
}

/* Takes the next face of a run of `type` faces: the original goes on
 * while one is left after this one. */
static int next_of_run(int type)
{
    g_sw_face += 0x2c;
    g_sw_faces_left--;
    return (s8)*g_sw_face == type && g_sw_faces_left > 0;
}

/* 0x44c2e0: a run of affine textured triangles (type 17, the
 * perspective switch off). */
static void face_texture(void)
{
    do {
        const struct sw_vert *a = vert(1), *b = vert(2), *v = vert(3);
        s32 sum;

        if (box_out(a, b, v) || g_sw_tex == NULL) {
            continue;
        }
        sum = b->z + v->z + a->z;
        if (sum > 600 && facing(a, b, v, 4) > 0) {
            put_texture(a, b, v, -5, nearer_17(sum >> 4));
        }
    } while (next_of_run(17));
    g_sw_faces_left++;
}

/* The mip level a face takes (0x44bb90's and 0x44d550's test): its
 * screen area against its texture's, with mip levels made. */
/* Twice the face's screen area times 1.5 (s) and its texture area
 * (t), for the mip tests. */
static void mip_areas(const struct sw_vert *a, const struct sw_vert *b, const struct sw_vert *v,
                      s32 *s_out, s32 *t_out)
{
    s32 xa, ya, xb, yb, xc, yc, s, t;
    s32 u0, v0, u1, v1, u2, v2;

    xa = a->x >> 8, ya = a->y >> 8, xb = b->x >> 8, yb = b->y >> 8;
    xc = v->x >> 8, yc = v->y >> 8;
    s = sw_mul(yc + yb, xc - xb) + sw_mul(ya + yb, xb - xa) + sw_mul(ya + yc, xa - xc);
    s = (s * 3) >> 1;
    s = s < 0 ? -s : s;
    u0 = FACE_WORD(4) >> 8, v0 = FACE_WORD(5) >> 8, u1 = FACE_WORD(6) >> 8;
    v1 = FACE_WORD(7) >> 8, u2 = FACE_WORD(8) >> 8, v2 = FACE_WORD(9) >> 8;
    t = sw_mul(v2 + v0, u0 - u2) + sw_mul(v2 + v1, u2 - u1) + sw_mul(v1 + v0, u1 - u0);
    t = t < 0 ? -t : t;
    *s_out = s;
    *t_out = t;
}

static s32 mip_level(const struct sw_vert *a, const struct sw_vert *b, const struct sw_vert *v)
{
    s32 s, t;

    if (g_sw_mip != 1) {
        return -5;
    }
    mip_areas(a, b, v, &s, &t);
    if (s > t) {
        return -5;
    }
    return (sw_mul(s, 4) <= t) - 4;
}

/* The mip handlers' test: one more level, -2, for a face whose texture
 * is 16 times its screen area. */
static s32 mip_level4(const struct sw_vert *a, const struct sw_vert *b, const struct sw_vert *v)
{
    s32 s, t;

    mip_areas(a, b, v, &s, &t);
    if (s > t) {
        return -5;
    }
    if (sw_mul(s, 4) > t) {
        return -4;
    }
    return (sw_mul(s, 16) <= t) - 3;
}

/* A perspective command (type 20, or 21 through `table`): near faces
 * keep their depths; from 480 to 720 they are drawn closer to affine,
 * the depths pulled toward their mean. */
static void put_persp(const struct sw_vert *a, const struct sw_vert *b, const struct sw_vert *v,
                      s32 sum, s32 d, s32 level, int type, const u8 *table)
{
    /* 0x44cb20 keeps the factor in a register; the others store it to a
     * float first. */
    struct sw_cmd_persp *c = sw_cmd_alloc(type == SW_PERSP_TABLE ? sizeof(*c)
                                          : offsetof(struct sw_cmd_persp, table));
    int k;

    if (c == NULL) {
        return;
    }
    if (d <= 0x1e0) {
        c->v[0].z = a->z;
        c->v[1].z = b->z;
        c->v[2].z = v->z;
    } else {
        s32 mean = sum / 3;
        double f = (720 - d) * (double)0.004166666883975267f;

        if (type != SW_PERSP_TABLE) {
            f = (float)f;
        }

        c->v[0].z = sw_ftol((double)(a->z - mean) * f) + mean;
        c->v[1].z = sw_ftol((double)(b->z - mean) * f) + mean;
        c->v[2].z = sw_ftol((double)(v->z - mean) * f) + mean;
    }
    c->type = type;
    c->v[0].x = a->x;
    c->v[0].y = a->y;
    c->v[1].x = b->x;
    c->v[1].y = b->y;
    c->v[2].x = v->x;
    c->v[2].y = v->y;
    if (type == SW_PERSP && level == -5 && magnified()) {
        /* The mip handlers draw a near face from its magnified tile. */
        for (k = 0; k < 6; k++) {
            c->uv[k] = (s32)(((u32)FACE_WORD(4 + k) & 0x3fff) << 2);
        }
        c->tex = magnified_tex();
        return;
    }
    for (k = 0; k < 6; k++) {
        c->uv[k] = FACE_WORD(4 + k);
    }
    c->tex = tex_level(level) + FACE_WORD(10);
    if (type == SW_PERSP_TABLE) {
        c->table = table;
    }
}

/* Whether a face of an object of class `klass` at bucket d is drawn in
 * perspective: within 720, and the class one of the track's (below
 * 100, 200, 300 to 302). */
static int persp_class(s32 d, s32 klass)
{
    if (d > 0x2d0) {
        return 0;
    }
    return klass < 100 || klass == 200 || (klass >= 300 && klass <= 302);
}

/* 0x44bb90: a run of textured triangles (type 17) with the perspective
 * switch on. */
static void face_texture_persp(void)
{
    do {
        const struct sw_vert *a = vert(1), *b = vert(2), *v = vert(3);
        s32 sum, d, level;

        if (box_out(a, b, v) || g_sw_tex == NULL) {
            continue;
        }
        sum = v->z + b->z + a->z;
        if (sum <= 600 || facing(a, b, v, 4) <= 0) {
            continue;
        }
        d = sum >> 4;
        level = mip_level(a, b, v);
        if (persp_class(d, g_sw_obj->klass)) {
            put_persp(a, b, v, sum, d, level, SW_PERSP, NULL);
            sw_cmd_bucket(nearer_17(d));
        } else {
            put_texture(a, b, v, level, nearer_17(d));
        }
    } while (next_of_run(17));
    g_sw_faces_left++;
}

/* 0x44d550: a run of type 21 faces: textured, in perspective when the
 * switch is on, a little nearer than the rest (0x50). The run goes on
 * only while three faces are left. */
static void face_texture_21(void)
{
    for (;;) {
        const struct sw_vert *a = vert(1), *b = vert(2), *v = vert(3);
        s32 sum, d, level;

        if (!box_out(a, b, v) && g_sw_tex != NULL) {
            sum = b->z + v->z + a->z;
            if (sum > 600 && facing(a, b, v, 4) > 0) {
                d = sum >> 4;
                level = mip_level(a, b, v);
                if (g_scene->perspective && persp_class(d, g_sw_obj->klass)) {
                    put_persp(a, b, v, sum, d, level, SW_PERSP, NULL);
                    sw_cmd_bucket(nearer_17(d - 0x50));
                } else {
                    put_texture(a, b, v, level, nearer_17(d - 0x50));
                }
            }
        }
        g_sw_face += 0x2c;
        if ((s8)*g_sw_face != 21 || g_sw_faces_left < 3) {
            return;
        }
        g_sw_faces_left--;
    }
}

/* 0x44cb20: a textured triangle through `table` ([texel << 8 | dst]),
 * `bias` buckets nearer (types 22 and 23); near enough and with the
 * perspective switch on, in perspective, but for faces whose word 0's
 * high half is 0x4d. */
static void face_texture_table(const u8 *table, s32 bias)
{
    const struct sw_vert *a = vert(1), *b = vert(2), *v = vert(3);
    s32 sum, d, klass = g_sw_obj->klass;

    if (!box_out(a, b, v) && g_sw_tex != NULL && table != NULL) {
        sum = a->z + v->z + b->z;
        if (sum > 600 && facing(a, b, v, 4) > 0) {
            d = sum >> 4;
            if (d <= 0x2d0 && (klass < 100 || klass == 200 || (klass >= 300 && klass <= 302))
                && g_scene->perspective && ((u32)FACE_WORD(0) & 0xffff0000u) != 0x4d0000) {
                put_persp(a, b, v, sum, d, -5, SW_PERSP_TABLE, table);
            } else {
                struct sw_cmd_texture_table *c = sw_cmd_alloc(sizeof(*c));

                if (c == NULL) {
                    g_sw_face += 0x2c;
                    return;
                }
                c->type = SW_TEXTURE_TABLE;
                c->v[0].x = a->x;
                c->v[0].y = a->y;
                c->v[1].x = b->x;
                c->v[1].y = b->y;
                c->v[2].x = v->x;
                c->v[2].y = v->y;
                c->uv = &FACE_WORD(4);
                c->table = table;
                c->tex = tex_level(-5) + FACE_WORD(10);
            }
            d -= bias;
            if (klass >= 100) {
                d -= 0x5c;
            }
            sw_cmd_bucket(d < 0 ? 0 : d);
        }
    }
    g_sw_face += 0x2c;
}

/* 0x44caa0: type 18, keyed (colour 0 shows through). */
static void face_texture_18(void)
{
    face_texture_table(g_sw_key, 0);
}

/* 0x44cac0: type 19, through the .SHD. */
static void face_texture_19(void)
{
    face_texture_table(g_sw_shd, 0);
}

/* 0x44cae0: type 22, keyed and 0x50 nearer. */
static void face_texture_22(void)
{
    face_texture_table(g_sw_key, 0x50);
}

/* 0x44cb00: type 23, through the .SHD and 0x50 nearer. */
static void face_texture_23(void)
{
    face_texture_table(g_sw_shd, 0x50);
}

/* 0x44cf00: type 20, a flat triangle through the .SHD (a shadow), 0x4c
 * nearer for class 100 and over. */
static void face_flat_table(void)
{
    const struct sw_vert *a = vert(1), *b = vert(2), *v = vert(3);
    struct sw_cmd_flat_table *c;
    s32 sum, d;

    if (!box_out(a, b, v)) {
        sum = v->z + a->z + b->z;
        if (sum > 600 && facing(a, b, v, 0) > 0 && g_sw_shd != NULL
            && (c = sw_cmd_alloc(sizeof(*c))) != NULL) {
            c->type = SW_FLAT_TABLE;
            c->v[0].x = a->x;
            c->v[0].y = a->y;
            c->v[1].x = b->x;
            c->v[1].y = b->y;
            c->v[2].x = v->x;
            c->v[2].y = v->y;
            c->table = g_sw_shd;
            c->colour = FACE_WORD(4);
            d = sum >> 4;
            if (g_sw_obj->klass >= 100) {
                d -= 0x4c;
                if (d < 0) {
                    d = 0;
                }
            }
            sw_cmd_bucket(d);
        }
    }
    g_sw_face += 0x14;
}

/* An affine command for a mip handler: from the magnified tile
 * (type 22) for a near face that has one, else type 17 at its level. */
static void put_texture_mip(const struct sw_vert *a, const struct sw_vert *b,
                            const struct sw_vert *v, s32 level, s32 d)
{
    struct sw_cmd_texture *c;

    if (level != -5 || !magnified()) {
        put_texture(a, b, v, level, d);
        return;
    }
    c = sw_cmd_alloc(sizeof(*c));
    if (c == NULL) {
        return;
    }
    c->type = SW_TEXTURE_MIP;
    c->v[0].x = a->x;
    c->v[0].y = a->y;
    c->v[1].x = b->x;
    c->v[1].y = b->y;
    c->v[2].x = v->x;
    c->v[2].y = v->y;
    c->uv = &FACE_WORD(4);
    c->tex = magnified_tex();
    sw_cmd_bucket(d);
}

/* 0x44dc60: a run of affine textured triangles (type 17) with mip
 * levels. */
static void face_texture_mip(void)
{
    do {
        const struct sw_vert *a = vert(1), *b = vert(2), *v = vert(3);
        s32 sum;

        if (box_out(a, b, v) || g_sw_tex == NULL) {
            continue;
        }
        sum = a->z + b->z + v->z;
        if (sum > 600 && facing(a, b, v, 4) > 0) {
            put_texture_mip(a, b, v, mip_level4(a, b, v), nearer_17(sum >> 4));
        }
    } while (next_of_run(17));
    g_sw_faces_left++;
}

/* A face of the perspective mip handlers, `bias` buckets nearer: in
 * perspective near the eye, from the magnified tile at level -5. Their
 * level is -4 when no levels were made. */
static void texture_persp_mip(s32 bias)
{
    const struct sw_vert *a = vert(1), *b = vert(2), *v = vert(3);
    s32 sum, d, level;

    if (box_out(a, b, v) || g_sw_tex == NULL) {
        return;
    }
    sum = v->z + b->z + a->z;
    if (sum <= 600 || facing(a, b, v, 4) <= 0) {
        return;
    }
    d = sum >> 4;
    level = g_sw_mip == 1 ? mip_level4(a, b, v) : -4;
    if (persp_class(d, g_sw_obj->klass)) {
        put_persp(a, b, v, sum, d, level, SW_PERSP, NULL);
        sw_cmd_bucket(nearer_17(d - bias));
    } else {
        put_texture_mip(a, b, v, level, nearer_17(d - bias));
    }
}

/* 0x44e1b0: a run of textured triangles (type 17) with the perspective
 * switch on and mip levels. */
static void face_texture_persp_mip(void)
{
    do {
        texture_persp_mip(0);
    } while (next_of_run(17));
    g_sw_faces_left++;
}

/* 0x44e900: type 21 with mip levels: as 0x44e1b0, 0x50 nearer, in
 * perspective whatever the switch says. The run goes on over the type
 * 17 faces that follow, as the original's copy of 0x44e1b0 does. */
static void face_texture_21_mip(void)
{
    do {
        texture_persp_mip(0x50);
    } while (next_of_run(17));
    g_sw_faces_left++;
}

/* 0x44c610: a run of affine textured triangles (type 17) at their mip
 * level, for the switch 0x49c9b4, which nothing sets. */
static void face_texture_mip_b(void)
{
    do {
        const struct sw_vert *a = vert(1), *b = vert(2), *v = vert(3);
        s32 sum, s, t, level;

        if (box_out(a, b, v) || g_sw_tex == NULL) {
            continue;
        }
        sum = b->z + v->z + a->z;
        if (sum > 600 && facing(a, b, v, 4) > 0) {
            mip_areas(a, b, v, &s, &t);
            level = s > t ? -5 : (sw_mul(s, 4) <= t) - 4;
            put_texture(a, b, v, level, nearer_17(sum >> 4));
        }
    } while (next_of_run(17));
    g_sw_faces_left++;
}

/* The handlers by type; 0x44b560 in the slots of types that end the
 * mesh. */
sw_face_fn g_sw_face_fns[24] = {        /* 0x49c8e0 */
    face_end, face_point, face_point_table, face_end, face_end, face_end, face_end,
    face_sprite, face_sprite_fixed, face_end, face_end, face_line_11, face_line_12,
    face_line_shade, face_end, face_flat, face_flat16, face_texture, face_texture_18,
    face_texture_19, face_flat_table, face_texture_21, face_texture_22, face_texture_23
};

/* 0x44b480 */
void sw_faces_select(void)
{
    struct scene *sc = g_scene;

    if (g_sw_mip_b == 0) {
        if (sc->perspective == 0) {
            g_sw_face_fns[17] = g_sw_mip ? face_texture_mip : face_texture;
        } else {
            g_sw_face_fns[17] = g_sw_mip ? face_texture_persp_mip : face_texture_persp;
        }
        g_sw_face_fns[21] = g_sw_mip ? face_texture_21_mip : face_texture_21;
    } else {
        if (sc->perspective == 0) {
            g_sw_face_fns[17] = g_sw_mip ? face_texture_mip_b : face_texture;
        } else {
            g_sw_face_fns[17] = face_texture_persp;
        }
        g_sw_face_fns[21] = face_texture_21;
    }
    sc->poly_count = 0;
    sc->f6c = 0;
}
