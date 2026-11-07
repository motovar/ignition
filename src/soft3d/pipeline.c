/*
 * soft3d/pipeline.c - the original's scene pipeline (0x4466d0): find
 * the objects in view (0x448e70), project their vertices (0x449e70,
 * 0x44a3d0), turn their faces into screen space commands in depth
 * buckets (0x44c1f0), and list the commands far to near.
 */

#include <math.h>
#include <string.h>

#include "race/ai.h"
#include "race/data.h"
#include "soft3d/soft3d.h"
#include "soft3d/sw.h"

int g_soft3d;

/* What the original allocates in scene_create (0x4468d0). */
enum {
    VISIBLE = 1500,             /* 0x63c5cc */
    VERTS = 20000,              /* 0x63c5ec */
    NODES = 14000,              /* 0x63b600 */
    ARENA = 0x249f0 * 4         /* 0x63c600, bytes */
};

s32 g_sw_clip[4];                       /* 0x49ca2c */
s32 g_sw_mirror;                        /* 0x49c9a8 */
const struct scene_obj *g_sw_obj;       /* 0x50dd28 */
const struct sw_vert *g_sw_verts;       /* 0x50dd8c */
const u8 *g_sw_face;                    /* 0x50dddc */
s32 g_sw_faces_left;                    /* 0x50dd6c */
const u8 *g_sw_tex;                     /* 0x4cdc28 */
const u8 *g_sw_shd;                     /* 0x63b5f0 */
const u8 *g_sw_key;                     /* 0x63c5b4 */
const u8 *g_sw_pan;                     /* 0x63c5f4 */
s32 g_sw_mip;                           /* 0x49c9ac */
s32 g_sw_mip_b;                         /* 0x49c9b4 */
s32 r_49c9bc;                           /* counts frames when mip levels were made */
const struct sw_tex_handle *g_sw_handle;    /* the slot behind 0x4cdc28 */

/* Each visible object's mesh and its projected vertices (0x63c5fc). */
static struct {
    const s32 *mesh;
    struct sw_vert *verts;
} s_xobj[VISIBLE];

static struct sw_vert s_verts[VERTS];   /* 0x63c5ec */
static struct sw_node s_nodes[NODES + 1];   /* 0x63b600 */
static struct sw_node *s_at;            /* 0x4cdc84 */
static struct sw_node *s_buckets[SW_BUCKETS];   /* 0x63b5e8 */
static u64 s_arena[ARENA / 8];          /* 0x63c600 */
static size_t s_cmd_size;
/* The commands far to near (0x63c5bc; the original reuses the vertex
 * memory). */
static void *s_list[NODES + 1];

/* The view's rows in fixed point (0x4cdc60..0x4cdc80), as floats
 * (0x4cdce8..0x4cdd08), the camera's fraction through them (0x50ddc8,
 * 0x50dde0, 0x50de04), four times the eye's distance (0x50ddfc) and the
 * centre in 8.8 (0x4cdbbc, 0x4cdbe8). */
static s32 s_m[9];
static float s_mf[9];
static s32 s_cam_frac[3];
static s32 s_near4;
static s32 s_cx8, s_cy8;

void soft3d_set_tables(const u8 *shd, const u8 *key, const u8 *pan)
{
    g_sw_shd = shd;
    g_sw_key = key;
    g_sw_pan = pan;
}

void *sw_cmd_alloc(size_t size)
{
    u8 *cmd;

    size = (size + 7) & ~(size_t)7;
    if (s_at >= &s_nodes[NODES]
        || (u8 *)s_at->cmd + size > (u8 *)s_arena + sizeof(s_arena)) {
        return NULL;
    }
    cmd = s_at->cmd;
    s_cmd_size = size;
    return cmd;
}

void sw_cmd_bucket(s32 d)
{
    if (d >= SW_BUCKETS) {
        d = SW_BUCKETS - 1;
    }
    if (d < 0) {
        d = 0;
    }
    s_at[1].cmd = (u8 *)s_at->cmd + s_cmd_size;
    s_at->next = s_buckets[d];
    s_buckets[d] = s_at;
    s_at++;
}

/* 0x44b340: the camera's rows in 1.14, for the cull. */
static void cull_rows(s32 r[9])
{
    const struct scene *sc = g_scene;
    double t1 = sc->pitch * -0.0017453292519943333;
    double t2 = sc->yaw * -0.0017453292519943333;
    double t3 = sc->roll * -0.0017453292519943333;
    double c3 = cos(t3), s1 = sin(t1), s1s2 = s1 * sin(t2), c1 = cos(t1), s1c2 = s1 * cos(t2);

    r[0] = sw_ftol((c3 * cos(t2) - s1s2 * sin(t3)) * 16384.0);
    r[1] = sw_ftol(c1 * sin(t3) * -16384.0);
    r[2] = sw_ftol((c3 * sin(t2) + s1c2 * sin(t3)) * 16384.0);
    r[3] = sw_ftol((cos(t2) * sin(t3) + c3 * s1s2) * 16384.0);
    r[4] = sw_ftol(c3 * c1 * 16384.0);
    r[5] = sw_ftol((sin(t3) * sin(t2) - c3 * s1c2) * 16384.0);
    r[6] = sw_ftol(c1 * sin(t2) * -16384.0);
    r[7] = sw_ftol(s1 * 16384.0);
    r[8] = sw_ftol(c1 * cos(t2) * 16384.0);
}

/* A cell's objects into the visible list, as both of 0x448e70's walks
 * do: a cell whose first object fails the check is passed over. */
static void take_cell(struct scene_obj *o, struct scene *sc)
{
    if (o == NULL || o->self != o) {
        return;
    }
    for (; o != NULL; o = o->next) {
        if (o->self != o) {
            continue;
        }
        if (g_scene_group != 0 && o->group != 0 && o->group != g_scene_group) {
            continue;
        }
        /* The original has no end to the list; the port stops at it. */
        if (sc->obj_count < VISIBLE) {
            g_scene_visible[sc->obj_count++] = o;
        }
    }
}

/* A row of 0x44b340's times (dx, dy, dz), wrapping. */
static s32 dot3(const s32 *row, s32 dx, s32 dy, s32 dz)
{
    return sw_add(sw_add(sw_mul(dz, row[2]), sw_mul(dy, row[1])), sw_mul(dx, row[0]));
}

static int trunc256(s32 v)
{
    return (v + (v < 0 ? 0xff : 0)) >> 8;
}

/* 0x448e70
 * Gathers the objects of the cells ahead of the camera, a square of
 * gather_radius cells or the disc of g_cells_disc, then keeps those
 * whose bounding sphere may be on the screen. The port skips cells
 * outside the grid, which the original reads past. */
static void gather_cull(void)
{
    struct scene *sc = g_scene;
    struct scene_obj **grid;
    int gw, gh, cell, row, col, n, i;
    s32 r[9];

    sc->obj_count = 0;
    grid = scene_grid(&gw, &gh);
    if (grid == NULL) {
        return;
    }
    if (sc->gather_radius != 0) {
        double a = sc->yaw * 0.0017453292519943333;
        s32 third = sc->gather_radius / 3, half = sc->gather_radius / 2;

        cell = (sw_ftol(cos(a) * third) + trunc256(sw_ftol(sc->z)) - half) * gw;
        cell += sw_ftol(third * sin(a)) + trunc256(sw_ftol(sc->x)) - half;
        for (row = sc->gather_radius; row > 0; row--) {
            for (col = sc->gather_radius; col > 0; col--, cell++) {
                if (cell >= 0 && cell < gw * gh) {
                    take_cell(grid[cell], sc);
                }
            }
            cell += gw - sc->gather_radius;
        }
    } else {
        int sector = sw_ftol(sc->pitch * 0.01) * 36 + sw_ftol(sc->yaw * 0.01), k;
        const s32 *ahead;

        if (sector < 0 || sector >= 36 * 36) {
            sector = 0;
        }
        ahead = &g_cells_ahead[2 * sector];
        cell = (ahead[1] + trunc256(sw_ftol(sc->z)) + g_cells_disc[1]) * gw;
        cell += trunc256(sw_ftol(sc->x)) + ahead[0] + g_cells_disc[2];
        /* Rows of (cells, step to the next row); the last row's step is
         * -5000, and so is the count the original reads after it, past
         * the table's end. */
        for (k = 3;; k += 2) {
            for (i = g_cells_disc[k]; i > 0; i--) {
                cell++;
                if (cell >= 0 && cell < gw * gh) {
                    take_cell(grid[cell], sc);
                }
            }
            if (g_cells_disc[k + 1] == -5000) {
                break;
            }
            cell += g_cells_disc[k + 1] + gw;
        }
    }

    cull_rows(r);
    n = sc->obj_count;
    sc->obj_count_all = n;
    sc->obj_count = 0;
    for (i = 0; i < n; i++) {
        struct scene_obj *o = g_scene_visible[i];
        s32 dx = sw_ftol(o->x - sc->x), dy = sw_ftol(o->y - sc->y), dz = sw_ftol(o->z - sc->z);
        s32 rad = o->radius, z, s, e;

        z = (dot3(r + 6, dx, dy, dz) >> 15) + sc->near_ofs;
        if (z + rad < 0) {
            continue;
        }
        if (z < sc->near_ofs) {
            z = sc->near_ofs;
        }
        /* Across: the test only drops a sphere with a negative radius
         * past the right edge, as the original's. */
        s = sc->center_x + sw_div(sw_mul(dot3(r, dx, dy, dz) >> 15, -sc->focal_x), z);
        e = 2 - sw_div(-sc->focal_x * rad, z);
        if (s + e < g_sw_clip[0] >> 8 && s - e > g_sw_clip[2] >> 8) {
            continue;
        }
        s = sc->center_y + sw_div(sw_mul(dot3(r + 3, dx, dy, dz) >> 15, -sc->focal_y), z);
        e = 2 - sw_div(-sc->focal_y * rad, z);
        if (s + e <= g_sw_clip[1] >> 8 || s - e >= g_sw_clip[3] >> 8) {
            continue;
        }
        g_scene_visible[sc->obj_count++] = o;
    }
}

/* The view's rows (0x449e70 with k 1024, 0x44a3d0 with 256): the
 * focal lengths are folded in, X and Y to 10 or 8 bits more; Z is in
 * 16.16 with 2 bits more. Floats where the original stores them. */
static void view_rows(double k, int late_round)
{
    const struct scene *sc = g_scene;
    double t1 = sc->pitch * -0.0017453292519943333;
    double t2 = sc->yaw * -0.0017453292519943333;
    double t3 = sc->roll * -0.0017453292519943333;
    float c3 = (float)cos(t3);
    float a = (float)((double)-sc->focal_x * sc->zoom * k);
    float b = (float)((double)-sc->focal_y * k);
    double s1 = sin(t1), c1 = cos(t1), s2 = sin(t2), c2 = cos(t2), s3 = sin(t3);
    float s1s2 = (float)(s1 * s2);
    double s1c2 = s1 * c2;
    float s1c2f = (float)s1c2;
    int i;

    s_mf[0] = (float)((c3 * c2 - s1s2 * s3) * a);
    s_mf[1] = (float)(-(a * c1 * s3));
    /* 0x44a3d0 takes S1 C2 back from its float here. */
    s_mf[2] = (float)((c3 * s2 + (late_round ? (double)s1c2f : s1c2) * s3) * a);
    s_mf[3] = (float)((c2 * s3 + c3 * s1s2) * b);
    s_mf[4] = (float)(b * c1 * c3);
    s_mf[5] = (float)((s3 * s2 - c3 * s1c2f) * b);
    s_mf[6] = (float)(-(s2 * c1) * 262144.0);
    s_mf[7] = (float)(s1 * 262144.0);
    s_mf[8] = (float)(c2 * c1 * 262144.0);
    for (i = 0; i < 9; i++) {
        s_m[i] = sw_ftol(s_mf[i]);
    }
}

/* The camera's place: whole units, and the fraction left through the
 * rows (camera.x - (int)camera.x in 1/4096). */
static void camera_frac(s32 cam[3])
{
    const struct scene *sc = g_scene;
    s32 f[3];
    int i;

    cam[0] = sw_ftol(sc->x);
    cam[1] = sw_ftol(sc->y);
    cam[2] = sw_ftol(sc->z);
    f[0] = sw_ftol((sc->x - cam[0]) * 4096.0);
    f[1] = sw_ftol((sc->y - cam[1]) * 4096.0);
    f[2] = sw_ftol((sc->z - cam[2]) * 4096.0);
    for (i = 0; i < 3; i++) {
        s32 v = sw_add(sw_add(sw_mul(f[0], s_m[3 * i]), sw_mul(s_m[3 * i + 2], f[2])),
                       sw_mul(s_m[3 * i + 1], f[1]));

        s_cam_frac[i] = v / 4096;
    }
}

/* One vertex: rows . v - the fraction, divided by the depth. */
static void project(struct sw_vert *out, s32 x, s32 y, s32 z, int shift)
{
    s32 sx = sw_sub(sw_add(sw_add(sw_mul(s_m[2], z), sw_mul(s_m[1], y)), sw_mul(s_m[0], x)),
                    s_cam_frac[0]);
    s32 sy = sw_sub(sw_add(sw_add(sw_mul(s_m[5], z), sw_mul(s_m[4], y)), sw_mul(s_m[3], x)),
                    s_cam_frac[1]);
    s32 w = sw_sub(sw_add(sw_add(sw_mul(s_m[8], z), sw_mul(s_m[7], y)), sw_mul(s_m[6], x)),
                   s_cam_frac[2]);

    w = (w >> 16) + s_near4;
    if (w < s_near4) {
        w = s_near4;
    }
    out->x = sw_add(s_cx8, sw_mul(sw_div(sx, w), 1 << shift));
    out->y = sw_add(s_cy8, sw_mul(sw_div(sy, w), 1 << shift));
    out->z = w;
}

/* Wraps an object's angle into 0..3600 as 16 bit arithmetic does. */
static s16 wrap_angle(s16 a)
{
    if (a < 0) {
        u16 n = (u16)((u16)(0xe0f - a) / 0xe10);

        a = (s16)(n * 0xe10 + a);
    }
    if (a > 0xe10) {
        u16 n = (u16)((u16)(a - 1) / 0xe10);

        a = (s16)(a + n * -0xe10);
    }
    return a;
}

/* A turned object's vertices (0x44a900, and 0x44ae20 with shift 2): its rotation, from the sine tables,
 * times the view's rows, then each vertex as `project` does it. The
 * object's angles are wrapped in place. */
static void project_turned(s32 dx, s32 dy, s32 dz, struct scene_obj *o, const s32 *mesh,
                           int shift)
{
    const float *sn = scene_sin_table(), *cs = sn + 900;
    s32 tx = sw_sub(sw_add(sw_add(sw_mul(s_m[2], dz), sw_mul(s_m[1], dy)), sw_mul(s_m[0], dx)),
                    s_cam_frac[0]);
    s32 ty = sw_sub(sw_add(sw_add(sw_mul(s_m[5], dz), sw_mul(s_m[4], dy)), sw_mul(s_m[3], dx)),
                    s_cam_frac[1]);
    s32 tz = sw_sub(sw_add(sw_add(sw_mul(s_m[8], dz), sw_mul(s_m[7], dy)), sw_mul(s_m[6], dx)),
                    s_cam_frac[2]);
    double cx, cy, cz, nsx, nsy;
    float sz, v30, v54, v50, v4c, v48, v44, v3c, v38, v34;
    s32 q[9], n, k;
    struct sw_vert *out;

    if ((u32)(s32)(s16)(o->ax | o->ay | o->az) > 0xe10) {
        o->ax = wrap_angle(o->ax);
        o->ay = wrap_angle(o->ay);
        o->az = wrap_angle(o->az);
    }
    cx = cs[5400 - o->ax];
    cy = cs[5400 - o->ay];
    cz = cs[1800 + o->az];
    nsx = sn[5400 - o->ax];
    sz = sn[1800 + o->az];
    nsy = sn[5400 - o->ay];
    v30 = (float)(nsx * nsy);
    v54 = (float)(cz * cy + sz * v30);
    v50 = (float)(v30 * cz - sz * cy);
    v4c = (float)(cx * nsy);
    v48 = (float)-(sz * cx);
    v44 = (float)-(cx * cz);
    {
        double nsxcy = nsx * cy;

        v30 = (float)nsxcy;
        v3c = (float)(nsxcy * sz - nsy * cz);
        v38 = (float)(nsy * sz + (double)v30 * cz);
    }
    v34 = (float)(cx * cy);
    q[0] = sw_ftol(((double)v3c * s_mf[2] + (double)v48 * s_mf[1]) + (double)v54 * s_mf[0]);
    q[1] = sw_ftol(((double)v38 * s_mf[2] + (double)v44 * s_mf[1]) + (double)v50 * s_mf[0]);
    q[2] = sw_ftol(((double)v34 * s_mf[2] + (double)v4c * s_mf[0]) + s_mf[1] * nsx);
    q[3] = sw_ftol(((double)v3c * s_mf[5] + (double)v48 * s_mf[4]) + (double)v54 * s_mf[3]);
    q[4] = sw_ftol(((double)v38 * s_mf[5] + (double)v50 * s_mf[3]) + (double)v44 * s_mf[4]);
    q[5] = sw_ftol(((double)v34 * s_mf[5] + (double)v4c * s_mf[3]) + s_mf[4] * nsx);
    q[6] = sw_ftol(((double)v48 * s_mf[7] + (double)v3c * s_mf[8]) + (double)v54 * s_mf[6]);
    if (shift == 0) {
        q[7] = sw_ftol(((double)v50 * s_mf[6] + (double)v44 * s_mf[7]) + (double)v38 * s_mf[8]);
    } else {
        q[7] = sw_ftol(((double)v50 * s_mf[6] + (double)v38 * s_mf[8]) + (double)v44 * s_mf[7]);
    }
    q[8] = sw_ftol(((double)v4c * s_mf[6] + (double)v34 * s_mf[8]) + s_mf[7] * nsx);

    n = mesh[0];
    if (n <= 0) {
        return;
    }
    out = &s_verts[g_scene->vert_count];
    g_scene->vert_count += n;
    for (k = 0; k < n; k++) {
        const s32 *v = mesh + 2 + 3 * k;
        s32 x = sw_add(sw_add(sw_add(sw_mul(v[2], q[2]), sw_mul(v[1], q[1])), sw_mul(v[0], q[0])),
                       tx);
        s32 y = sw_add(sw_add(sw_add(sw_mul(v[2], q[5]), sw_mul(v[1], q[4])), sw_mul(v[0], q[3])),
                       ty);
        s32 w = sw_add(sw_add(sw_add(sw_mul(v[2], q[8]), sw_mul(v[1], q[7])), sw_mul(v[0], q[6])),
                       tz);

        w = (w >> 16) + s_near4;
        if (w < s_near4) {
            w = s_near4;
        }
        out[k].x = sw_add(s_cx8, sw_mul(sw_div(x, w), 1 << shift));
        out[k].y = sw_add(s_cy8, sw_mul(sw_div(y, w), 1 << shift));
        out[k].z = w;
    }
}

/* 0x44a900 */
static void project_turned_44a900(s32 dx, s32 dy, s32 dz, struct scene_obj *o,
                                  const s32 *mesh)
{
    project_turned(dx, dy, dz, o, mesh, 0);
}

/* 0x44ae20 */
static void project_turned_44ae20(s32 dx, s32 dy, s32 dz, struct scene_obj *o,
                                  const s32 *mesh)
{
    project_turned(dx, dy, dz, o, mesh, 2);
}

/* Projects each visible object's vertices into s_verts: the rows keep
 * 2 bits less (shift 2) for a focal length over 300. Vertices past the
 * store are not drawn; the original has no end to it. */
static void transform_objects(int shift)
{
    struct scene *sc = g_scene;
    int i;
    s32 cam[3];

    g_sw_mirror = 0.0 < sc->zoom ? 0 : -1;
    sc->vert_count = 0;
    s_near4 = sc->near_ofs << 2;
    s_cx8 = sc->center_x << 8;
    s_cy8 = sc->center_y << 8;
    view_rows(shift ? 256.0 : 1024.0, shift);
    camera_frac(cam);
    for (i = 0; i < sc->obj_count; i++) {
        struct scene_obj *o = g_scene_visible[i];
        const s32 *mesh = o->mesh;
        s32 dx = o->x - cam[0], dy = o->y - cam[1], dz = o->z - cam[2];

        s_xobj[i].mesh = mesh;
        s_xobj[i].verts = &s_verts[sc->vert_count];
        if (sc->vert_count + mesh[0] > VERTS) {
            s_xobj[i].mesh = NULL;
            continue;
        }
        if ((o->ax | o->ay | o->az) != 0) {
            if (shift) {
                project_turned_44ae20(dx, dy, dz, o, mesh);
            } else {
                project_turned_44a900(dx, dy, dz, o, mesh);
            }
        } else {
            int k, n = mesh[0];

            if (n <= 0) {
                continue;
            }
            for (k = 0; k < n; k++) {
                const s32 *v = mesh + 2 + 3 * k;

                project(&s_verts[sc->vert_count + k], v[0] + dx, dy - v[1], v[2] + dz, shift);
            }
            sc->vert_count += n;
        }
    }
}

/* 0x44a3d0 */
static void transform_44a3d0(void)
{
    transform_objects(2);
}

/* 0x449e70 */
static void transform(void)
{
    if (g_scene->focal_x > 300) {
        transform_44a3d0();
        return;
    }
    transform_objects(0);
}

/* 0x44c1f0
 * Each visible object's faces through the handler of their type, into
 * the depth buckets. The handlers take g_sw_face and move it on; a
 * handler may take a run of faces of its type. */
static void emit(void)
{
    struct scene *sc = g_scene;
    int i;

    sw_faces_select();
    s_nodes[0].cmd = s_arena;
    s_at = s_nodes;
    for (i = 0; i < sc->obj_count; i++) {
        const s32 *mesh = s_xobj[i].mesh;
        const struct scene_obj *o = g_scene_visible[i];

        if (mesh == NULL) {
            continue;
        }
        g_sw_obj = o;
        g_sw_tex = o->tex >= 0 && o->tex < SCENE_TEXTURES ? g_scene_textures[o->tex] : NULL;
        g_sw_handle = g_sw_tex != NULL ? g_sw_tex_handles[o->tex] : NULL;
        g_sw_face = (const u8 *)(mesh + 2 + 3 * mesh[0]);
        g_sw_verts = s_xobj[i].verts;
        for (g_sw_faces_left = mesh[1]; g_sw_faces_left > 0; g_sw_faces_left--) {
            int type = (s8)*g_sw_face;

            /* A type the table does not have would crash the original. */
            if (type < 0 || type >= 24 || g_sw_face_fns[type] == NULL) {
                break;
            }
            g_sw_face_fns[type]();
        }
    }
    sc->poly_count = (s32)(s_at - s_nodes);
}

/* 0x4466d0 */
void *const *soft3d_scene_render(void)
{
    struct scene *sc = g_scene;
    int n = 0, d;

    g_sw_clip[0] = g_sw_raster.x0 << 8;
    g_sw_clip[1] = g_sw_raster.y0 << 8;
    g_sw_clip[2] = (g_sw_raster.x1 + 1) << 8;
    g_sw_clip[3] = (g_sw_raster.y1 + 1) << 8;
    if (sc->stage_lookat) {
        scene_look_at(sc);
    }
    if (sc->stage_gather) {
        gather_cull();
    }
    if (sc->stage_transform) {
        transform();
    }
    if (sc->stage_emit) {
        emit();
    }
    if (sc->stage_collect) {
        for (d = SW_BUCKETS - 1; d >= 0; d--) {
            struct sw_node *nd;

            for (nd = s_buckets[d]; nd != NULL; nd = nd->next) {
                s_list[n++] = nd->cmd;
            }
            s_buckets[d] = NULL;
        }
        s_list[n] = NULL;
    }
    /* With mip levels made, the original sums bytes of its memory blocks
     * into a check word (0x50dd4c) for 100 frames, drawing on rand();
     * the port draws the same numbers and keeps no sum. */
    if (r_49c9bc != 0) {
        int k, blocks = 0, times = r_49c9bc < 10 ? 40 : 2;

        (void)blocks;
        for (k = 0; k < times; k++) {
            race_msrand();
            race_msrand();
            race_msrand();
            race_msrand();
        }
        r_49c9bc++;
        if (r_49c9bc == 100) {
            r_49c9bc = 0;
        }
    }
    return s_list;
}

void soft3d_draw(u8 *target, int pitch, int height, int x0, int y0, int x1, int y1,
                 const u8 *table)
{
    (void)pitch;
    (void)height;
    g_sw_raster.target = target;
    g_sw_raster.target2 = target;
    g_sw_raster.table = table;
    g_sw_raster.x0 = x0;
    g_sw_raster.y0 = y0;
    g_sw_raster.x1 = x1;
    g_sw_raster.y1 = y1;
    g_sw_raster.cmds = soft3d_scene_render();
    sw_raster_draw(&g_sw_raster);
}
