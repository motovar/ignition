/*
 * soft3d/sw.h - the original's software 3D pipeline and rasteriser,
 * inside: the projected vertices, the screen space commands the face
 * handlers make (0x44b5e0..0x44e900) and the rasteriser draws
 * (0x44f0e9), and the state they share.
 *
 * The original keeps a command as a run of 32 bit words with pointers
 * in some of them. Here each type is a struct in the same order; the
 * comments give each field's offset in the original. Positions are
 * screen pixels in 8.8 fixed point, the view's centre added; depths are
 * the view space Z plus the eye's distance, in world units; texture
 * coordinates are 8.8 texels of a 256 x 256 page, tex[v << 8 | u].
 */

#ifndef IGN_SOFT3D_SW_H
#define IGN_SOFT3D_SW_H

#include <stddef.h>

#include "scene.h"
#include "types.h"

/* A projected vertex (0x63c5ec, 12 bytes). */
struct sw_vert {
    s32 x, y;                   /* +0x00, 8.8 */
    s32 z;                      /* +0x08, at least 4 * near_ofs */
};

_Static_assert(sizeof(struct sw_vert) == 12, "sw_vert");

/* Command types: the rasteriser's dispatch (the jump table at
 * 0x4abe10) and the size of each in the original. */
enum {
    SW_POINT = 0,               /* 0x454548 */
    SW_POINT_FLAT = 1,          /* 0x450630, 0x18: a pixel of table[c << 8] */
    SW_POINT_TABLE = 2,         /* 0x453790, 0x14 */
    SW_SPRITE = 7,              /* 0x455648, 0x44 */
    SW_LINE = 11,               /* 0x451747, 0x24 */
    SW_LINE_12 = 12,            /* 0x4549a3, 0x28 */
    SW_LINE_SHADE = 13,         /* 0x44f1fb, 0x28: a line through a table */
    SW_FLAT = 15,               /* 0x450693, 0x30 */
    SW_FLAT_16 = 16,            /* 0x452ecb, 0x38 */
    SW_TEXTURE = 17,            /* 0x452800, 0x24 */
    SW_TEXTURE_TABLE = 18,      /* 0x44ff60, 0x28 */
    SW_FLAT_TABLE = 19,         /* 0x454604, 0x24 */
    SW_PERSP = 20,              /* 0x4516f0, 0x44 */
    SW_PERSP_TABLE = 21,        /* 0x4516f0, 0x48 */
    SW_TEXTURE_MIP = 22         /* 0x452050, 0x24: 17 from a magnified tile */
};

/* 1: a point of one colour (0x44b5e0). */
struct sw_cmd_point {
    s32 type;                   /* +0x00, the face's low 16 bits */
    s32 x, y;                   /* +0x04 */
    s32 f0c;                    /* +0x0c, not written */
    s32 colour;                 /* +0x10, the face's word 2 */
    s32 f14;                    /* +0x14, 0 */
};

/* 2: a point through a table (0x44dbb0). */
struct sw_cmd_point_table {
    s32 type;                   /* +0x00 */
    s32 x, y;                   /* +0x04 */
    s32 colour;                 /* +0x0c */
    const u8 *table;            /* +0x10, the .SHD */
};

/* 7: a sprite (0x44d0f0, 0x44d230), with its source and scale. */
struct sw_sprite_src {
    s32 hot_x, hot_y;           /* +0x14: texels from (u0, v0) */
    s32 u0, v0, u1, v1;         /* +0x1c */
    const u8 *tex;              /* +0x2c, the page */
    const u8 *table;            /* +0x30 */
};

/* A 2 x 2 matrix: x = sx u + f3c v, y = f38 u + sy v, in 8.8 pixels
 * a texel. */
struct sw_sprite_scale {
    s32 sx, f38, f3c, sy;       /* +0x34 */
};

struct sw_cmd_sprite {
    s32 type;                   /* +0x00 */
    const struct sw_sprite_src *src;        /* +0x04 */
    const struct sw_sprite_scale *scale;    /* +0x08 */
    s32 x, y;                   /* +0x0c */
    struct sw_sprite_src src_;  /* +0x14 */
    struct sw_sprite_scale scale_;  /* +0x34 */
};

/* 11, 12, 13: lines (0x44d3a0, 0x44b690, 0x44d470). */
struct sw_cmd_line {
    s32 type;                   /* +0x00 */
    s32 x0, y0, f0c;            /* +0x04 */
    s32 x1, y1, f18;            /* +0x10 */
    s32 colour;                 /* +0x1c */
    s32 f20, f24;               /* +0x20, 0 (f24 for type 12) */
};

struct sw_cmd_line_shade {
    s32 type;                   /* +0x00 */
    s32 x0, y0, f0c;            /* +0x04 */
    s32 x1, y1, f18;            /* +0x10 */
    const u8 *table;            /* +0x1c */
    s32 c0, c1;                 /* +0x20, 8.8 colours at the ends */
};

/* 15: a flat triangle (0x44b770). */
struct sw_cmd_flat {
    s32 type;                   /* +0x00 */
    struct { s32 x, y, z; } v[3];   /* +0x04, z not written */
    s32 colour;                 /* +0x28 */
    s32 f2c;                    /* +0x2c, 0 */
};

/* 16: a flat triangle with a fourth word a vertex (0x44b980). */
struct sw_cmd_flat16 {
    s32 type;                   /* +0x00 */
    struct { s32 x, y, z, i; } v[3];    /* +0x04, z not written; i the shade, 8.8 */
    s32 colour;                 /* +0x34 */
};

/* 17, 22: an affine textured triangle (0x44c2e0). */
struct sw_cmd_texture {
    s32 type;                   /* +0x00 */
    struct { s32 x, y; } v[3];  /* +0x04 */
    const s32 *uv;              /* +0x1c, u0 v0 u1 v1 u2 v2 in the face (8.8) */
    const u8 *tex;              /* +0x20, the page */
};

/* 18: an affine textured triangle through a table (0x44cb20). */
struct sw_cmd_texture_table {
    s32 type;                   /* +0x00 */
    struct { s32 x, y; } v[3];  /* +0x04 */
    const s32 *uv;              /* +0x1c */
    const u8 *tex;              /* +0x20 */
    const u8 *table;            /* +0x24, [texel << 8 | dst] */
};

/* 19: a flat triangle through a table (0x44cf00). */
struct sw_cmd_flat_table {
    s32 type;                   /* +0x00 */
    struct { s32 x, y; } v[3];  /* +0x04 */
    s32 colour;                 /* +0x1c */
    const u8 *table;            /* +0x20 */
};

/* 20, 21: a perspective textured triangle (0x44bb90, 0x44cb20). */
struct sw_cmd_persp {
    s32 type;                   /* +0x00 */
    const u8 *tex;              /* +0x04 */
    struct { s32 x, y, z; } v[3];   /* +0x08 */
    s32 uv[6];                  /* +0x2c */
    const u8 *table;            /* +0x44, type 21 */
};

/* The raster context (0x498730). */
struct sw_raster {
    void *const *cmds;          /* +0x00, ended by NULL */
    u8 *target;                 /* +0x04, g_render_target */
    u8 *target2;                /* +0x08 */
    const u8 *table;            /* +0x0c, for type 1 points */
    s32 x0, y0, x1, y1;         /* +0x10, the clip, the ends included */
};

extern struct sw_raster g_sw_raster;    /* 0x498730 */

/* The pipeline's state, shared by the face handlers. */
extern s32 g_sw_clip[4];        /* 0x49ca2c: x0, y0, x1 + 1, y1 + 1 in 8.8 */
extern s32 g_sw_mirror;         /* 0x49c9a8: -1 when the view is mirrored */
extern const struct scene_obj *g_sw_obj;   /* 0x50dd28 */
extern const struct sw_vert *g_sw_verts;   /* 0x50dd8c, the object's */
extern const u8 *g_sw_face;     /* 0x50dddc */
extern s32 g_sw_faces_left;     /* 0x50dd6c */
extern const u8 *g_sw_tex;      /* 0x4cdc28, the object's texture slot */
extern const u8 *g_sw_shd;      /* 0x63b5f0, the level's .SHD */
extern const u8 *g_sw_key;      /* 0x63c5b4, the colour key */
extern const u8 *g_sw_pan;      /* 0x63c5f4, the level's .PAN */
extern s32 g_sw_mip;            /* 0x49c9ac: mip levels were made */
extern s32 g_sw_mip_b;          /* 0x49c9b4, never set */
extern s32 r_49c9bc;            /* mip levels were made: 0x4466d0 mixes rand() for 100 frames */

/* What 0x447280 makes of a texture slot (a calloc of 0x15 dwords, the
 * slot pointing 5 in): level[0] the texture (slot[-5]), level[1..4] its
 * blurred levels (slot[-4..-1]); slot[0] the texture, or with the
 * magnified tiles 0x4475c0 makes, slot[4 k] tile column k's. */
struct sw_tex_handle {
    const u8 *level[5];
    const u8 *slot[16];
};

/* Each scene texture slot's handle (0x63c5c8), NULL where 0x447280 was
 * not run; and the current object's (with g_sw_tex). */
extern const struct sw_tex_handle *g_sw_tex_handles[SCENE_TEXTURES];
extern const struct sw_tex_handle *g_sw_handle;

/* 0x447280: a slot's handle, its `levels` - 1 blurred levels of w x h
 * tiles over `rows` rows of 256, and its magnified tiles; `pal` the
 * level's palette, 3 bytes a colour. */
void sw_tex_make(int slot, s32 count, s32 a8, s32 levels, s32 w, s32 h, s32 rows, const u8 *pal);

/* The polygon store (0x63b600): a node names a command and the next in
 * its depth bucket. */
struct sw_node {
    void *cmd;
    struct sw_node *next;
};

enum { SW_BUCKETS = 6000 };

/* Takes the command memory for a new command of `size` bytes (the
 * original's next free node). NULL when the store is full. */
void *sw_cmd_alloc(size_t size);
/* Puts the command just taken in depth bucket d, clamped to 0..5999. */
void sw_cmd_bucket(s32 d);

/* 0x44b480: picks the face handlers for the perspective and mip
 * switches. */
void sw_faces_select(void);
/* The face handlers by type (0x49c8e0). */
typedef void (*sw_face_fn)(void);
extern sw_face_fn g_sw_face_fns[24];

/* The rasteriser's types (0x44f0e9's cases). */
void sw_raster_point(const struct sw_cmd_point *c);         /* 0x454548 */
void sw_raster_point_flat(const struct sw_cmd_point *c);    /* 0x450630 */
void sw_raster_point_table(const struct sw_cmd_point_table *c);    /* 0x453790 */
void sw_raster_sprite(const struct sw_cmd_sprite *c);       /* 0x455648 */
void sw_raster_line(const struct sw_cmd_line *c);           /* 0x451747 */
void sw_raster_line_12(const struct sw_cmd_line *c);        /* 0x4549a3 */
void sw_raster_flat(const struct sw_cmd_flat *c);           /* 0x450693 */
void sw_raster_flat16(const struct sw_cmd_flat16 *c);       /* 0x452ecb */
void sw_raster_texture(const struct sw_cmd_texture *c);     /* 0x452800 */
void sw_raster_texture_table(const struct sw_cmd_texture_table *c);  /* 0x44ff60 */
void sw_raster_flat_table(const struct sw_cmd_flat_table *c);    /* 0x454604 */
void sw_raster_persp(const struct sw_cmd_persp *c);         /* 0x4516f0 */
void sw_raster_texture_mip(const struct sw_cmd_texture *c); /* 0x452050 */

/* The rasteriser's screen (0x44f070 and the hooks it runs). */
extern u8 *g_sw_dst;            /* 0x49c9f0, g_sw_raster.target */
extern const u8 *g_sw_point_table;  /* 0x49c9f8 */
extern s32 g_sw_pitch;          /* 0x49ca3c */
extern s32 g_sw_height;         /* 0x49ca40 */
extern s32 g_sw_rows[600];      /* 0x49ca44: y * pitch */
extern u32 g_sw_recip[15002];   /* 0x49d3a8: 0x10000 / n - 1 for n = 1.. */
/* The clip as 0x44f0e9 sets it from the context, in pixels and in 8.8;
 * g_sw_clip holds it in 8.8 too. */
extern s32 r_49c9fc, r_49ca00;  /* x0, y0 */
extern s32 r_49ca04, r_49ca08;  /* x1, y1 */
extern s32 r_49ca0c, r_49ca10;  /* x0, y0 again */
extern s32 r_49ca14, r_49ca18;  /* x1 + 1, y1 + 1 */
extern s32 r_49ca1c, r_49ca20;  /* x0 << 8, y0 << 8 */
extern s32 r_49ca24, r_49ca28;  /* ((x1 + 1) << 8) - 1, ((y1 + 1) << 8) - 1 */

/* The high word of a 32 x 32 bit signed multiply (imul's edx). */
static inline s32 sw_mulhi(s32 a, s32 b)
{
    return (s32)(((int64_t)a * b) >> 32);
}

/* The rasteriser's hooks, run by sw_raster_set_screen with the screen's
 * width and height (the list at 0x4abe6c). */
void sw_hook_451730(int w, int h);
void sw_hook_45498c(int w, int h);
void sw_hook_450684(int w, int h);
void sw_hook_452ebc(int w, int h);
void sw_hook_44ff20(int w, int h);
void sw_hook_452770(int w, int h);
void sw_hook_452010(int w, int h);
void sw_hook_453c33(int w, int h);

/* 32 bit wrapping arithmetic, as the original's registers do it. */
static inline s32 sw_mul(s32 a, s32 b)
{
    return (s32)((u32)a * (u32)b);
}

static inline s32 sw_add(s32 a, s32 b)
{
    return (s32)((u32)a + (u32)b);
}

static inline s32 sw_sub(s32 a, s32 b)
{
    return (s32)((u32)a - (u32)b);
}

/* 0x46950c, the original's _ftol: the value truncated to a 64 bit integer
 * (fistp with the rounding set to chop) and its low 32 bits kept, so a
 * value past 32 bits wraps. One past 64 bits, or not a number, is the
 * integer indefinite, 0x8000000000000000, whose low 32 bits are 0. A C
 * cast of such a value is undefined: x86 gives INT_MIN, ARM saturates. */
static inline s32 sw_ftol(double v)
{
    if (!(v >= -9223372036854775808.0 && v < 9223372036854775808.0)) {
        return 0;
    }
    return (s32)(u32)(u64)(int64_t)v;
}

/* idiv: the quotient toward zero; a zero or overflowing divide, which
 * would fault in the original, gives 0. */
static inline s32 sw_div(s32 a, s32 b)
{
    if (b == 0 || (a == INT32_MIN && b == -1)) {
        return 0;
    }
    return a / b;
}

#endif /* IGN_SOFT3D_SW_H */
