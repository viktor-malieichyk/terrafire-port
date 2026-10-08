#include "render.h"

#include "objects.h"
#include "palette.h"
#include "pak.h"
#include "platform.h"
#include "res.h"
#include "timing.h"
#include "unported.h"

#include <stdlib.h>
#include <string.h>

uint8_t g_buf[BUF_W * BUF_H] = {0};
View g_view = {0};

#define ORIGIN (BUF_MARGIN * BUF_W + BUF_MARGIN)   /* 0x841f8: screen (0,0) in g_buf */
#define GRID_W 21
#define GRID_H 14

static uint8_t s_blend[65536];          /* 0x73dc8: s_blend[src << 8 | dst] */
static int32_t s_occl[GRID_W * GRID_H]; /* 0x73930 */
static int32_t s_obj_clip[4];           /* 0x84224, 0x8421c, 0x841d8, 0x841d0: world pixels */

static int32_t sar(int32_t v, int n) { return v >= 0 ? v >> n : ~(~v >> n); }

bool render_init(void)
{
    size_t size;
    uint8_t *e = pak_read(60, &size);
    bool ok = e && size == sizeof s_blend;
    if (ok) memcpy(s_blend, e, sizeof s_blend);
    free(e);
    g_view.clip_l = g_view.clip_t = 0;
    g_view.clip_r = SCREEN_W - 1;
    g_view.clip_b = SCREEN_H - 1;
    g_view.view_w = SCREEN_W;
    g_view.view_h = SCREEN_H;
    return ok;
}

/* ------------------------------------------------------------------ camera */

void render_camera_center(int32_t x, int32_t y)
{
    g_view.cam_x = sar(x, 8) - 0xa0;
    g_view.cam_y = sar(y, 8) - 100;
}

void render_camera(int32_t x, int32_t y)
{
    g_view.cam_x = sar(x, 8);
    g_view.cam_y = sar(y, 8);
}

int32_t render_cam_x(void) { return (g_view.cam_x + g_view.clip_l) * 0x100; }
int32_t render_cam_y(void) { return (g_view.cam_y + g_view.clip_t) * 0x100; }

void render_set_view_size(int32_t width)
{
    int prev_w = g_view.view_w;
    int w = (sar(width, 8) / 32) * 32;
    if (w < 32) w = 32;
    if (w > 320) w = 320;
    int h = w * 200 / 320;
    int left = (320 - w) / 2, top = (200 - h) / 2;
    int right = left + w - 1, bottom = top + h - 1;

    /* Full view while the border around the new view is blacked out. */
    g_view.clip_l = g_view.clip_t = 0;
    g_view.clip_r = SCREEN_W - 1;
    g_view.clip_b = SCREEN_H - 1;
    g_view.view_w = SCREEN_W;
    g_view.view_h = SCREEN_H;
    if (w < 320) {
        int32_t cx = g_view.cam_x, cy = g_view.cam_y;
        render_fill(0, cx << 8, cy << 8, (cx + 319) << 8, ((cy + top) << 8) - 0x100);
        render_fill(0, cx << 8, ((cy + bottom) << 8) + 0x100, (cx + 319) << 8, (cy + 199) << 8);
        render_fill(0, cx << 8, (cy + top) << 8, ((cx + left) << 8) - 0x100, (cy + bottom) << 8);
        render_fill(0, ((cx + right) << 8) + 0x100, (cy + top) << 8, (cx + 319) << 8, (cy + bottom) << 8);
    }
    if (w < prev_w) UNPORTED("0x2d88b", "copy after view shrink");
    g_view.view_w = w;
    g_view.view_h = h;
    g_view.clip_l = left;
    g_view.clip_t = top;
    g_view.clip_r = right;
    g_view.clip_b = bottom;
}

/* -------------------------------------------------------------- occlusion */

/* Clip rectangle for one object: its box (world pixels) inside the view. */
static void object_clip(const Object *o, int *l, int *t, int *r, int *b)
{
    *l = o->box[0] - g_view.cam_x;
    if (*l < g_view.clip_l) *l = g_view.clip_l;
    *t = o->box[1] - g_view.cam_y;
    if (*t < g_view.clip_t) *t = g_view.clip_t;
    *r = o->box[2] - g_view.cam_x;
    if (*r > g_view.clip_r) *r = g_view.clip_r;
    *b = o->box[3] - g_view.cam_y;
    if (*b > g_view.clip_b) *b = g_view.clip_b;
}

static void sync_attached(void)                    /* 0x2cff8 */
{
    for (int i = objects_first(); i != -1; i = g_objects[i].next)
        if (g_objects[i].parent != -1 && g_objects[i].f_4a != -1) {
            UNPORTED("0x2c4a8", "position attached object at its attach point");
            return;
        }
}

void render_build_occlusion(void)
{
    sync_attached();
    memset(s_occl, 0, sizeof s_occl);
    /* In list (z) order: a later, higher-z occluder overwrites the cell. */
    for (int n = objects_first(); n != -1; n = g_objects[n].next) {
        const Object *o = &g_objects[n];
        if (o->slot == -1 || o->frozen) continue;
        const FrameSlot *fs = &g_slots[o->slot];
        if (!fs->cells) continue;
        int l, t, r, b;
        object_clip(o, &l, &t, &r, &b);
        int sx = sar(o->x, 8) - g_view.cam_x, sy = sar(o->y, 8) - g_view.cam_y;
        if (!(sx <= r - 16 && sy <= b - 16 && l <= fs->w_fx4 + sx && t <= fs->h_fx4 + sy)) continue;
        int tx0 = sar(l - sx, 4) - 1, ty0 = sar(t - sy, 4) - 1;
        if (tx0 < 0) tx0 = 0;
        if (ty0 < 0) ty0 = 0;
        int tx1 = sar(r - sx, 4) - 1, ty1 = sar(b - sy, 4) - 1;
        if (tx1 > fs->w - 1) tx1 = fs->w - 1;
        if (ty1 > fs->h - 1) ty1 = fs->h - 1;
        int g = sar(sx + 0x20 + tx0 * 16, 4) + sar(sy + 0x20 + ty0 * 16, 4) * GRID_W;
        for (int ty = ty0; ty <= ty1; ty++) {
            for (int tx = tx0; tx <= tx1; tx++, g++)
                if (fs->cells[ty * fs->w + tx].flag && g >= 0 && g < GRID_W * GRID_H)
                    s_occl[g] = o->z;
            g += GRID_W - (tx1 - tx0 + 1);
        }
    }
    /* Each cell becomes the minimum over itself and its right/lower neighbours. */
    int p = 0;
    for (int row = 0; row < GRID_H - 1; row++) {
        for (int col = 0; col < GRID_W - 1; col++, p++) {
            int32_t a = s_occl[p] < s_occl[p + 1] ? s_occl[p] : s_occl[p + 1];
            int32_t c = s_occl[p + GRID_W] < s_occl[p + GRID_W + 1] ? s_occl[p + GRID_W] : s_occl[p + GRID_W + 1];
            s_occl[p] = a < c ? a : c;
        }
        if (s_occl[p + GRID_W] < s_occl[p]) s_occl[p] = s_occl[p + GRID_W];
        p++;
    }
    for (int col = 0; col < GRID_W - 1; col++, p++)
        if (s_occl[p + 1] < s_occl[p]) s_occl[p] = s_occl[p + 1];
}

/* ------------------------------------------------------------------ drawing */

static void draw_tile(const Chunk *c, int at)
{
    const TileImage *im = c->image;
    if (!im) return;
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int i = y * 16 + x;
            if (c->kind != 0 || im->mask[i]) g_buf[at + y * BUF_W + x] = im->pixels[i];
        }
}

static void draw_tileframe_object(const Object *o)      /* 0x27f00 */
{
    const FrameSlot *fs = &g_slots[o->slot];
    if (!fs->cells) return;
    int sx = sar(o->x, 8) - g_view.cam_x, sy = sar(o->y, 8) - g_view.cam_y;
    int l, t, r, b;
    object_clip(o, &l, &t, &r, &b);
    if (!(sy <= b && t < fs->h_fx4 + sy && sx <= r && l < fs->w_fx4 + sx)) return;
    int tx0 = sar(l - sx, 4), ty0 = sar(t - sy, 4);
    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    int tx1 = sar(r - sx, 4), ty1 = sar(b - sy, 4);
    if (tx1 > fs->w - 1) tx1 = fs->w - 1;
    if (ty1 > fs->h - 1) ty1 = fs->h - 1;
    int g = sar(tx0 * 16 + sx + 16, 4) + sar(ty0 * 16 + sy + 16, 4) * GRID_W;
    int px = sx < l ? l - (l - sx) % 16 : sx;
    int py = sy < t ? t - (t - sy) % 16 : sy;
    for (int ty = ty0; ty <= ty1; ty++) {
        int at = ORIGIN + (py + (ty - ty0) * 16) * BUF_W + px;
        for (int tx = tx0; tx <= tx1; tx++, g++, at += 16) {
            if (g >= 0 && g < GRID_W * GRID_H && s_occl[g] > o->z) continue;   /* covered later */
            draw_tile(&g_chunks[fs->cells[ty * fs->w + tx].chunk], at);
        }
        g += GRID_W - (tx1 - tx0 + 1);
    }
}

/* 0x28f30 with the four blitters: column by column, top to bottom, walking
 * the bank's pixels with the view's steps; colour 0 is transparent; blend
 * mode writes s_blend[src << 8 | dst]. Clipping is done per pixel here. */
static void draw_sprite_view(const Bank *bk, const BankView *v, uint8_t blend, int32_t wx, int32_t wy)
{
    if (!bk->pixels) return;
    int w = sar(v->w, 8), h = sar(v->h, 8);
    int l = s_obj_clip[0] - g_view.cam_x, t = s_obj_clip[1] - g_view.cam_y;
    int r = s_obj_clip[2] - g_view.cam_x, b = s_obj_clip[3] - g_view.cam_y;
    if (l < g_view.clip_l) l = g_view.clip_l;
    if (t < g_view.clip_t) t = g_view.clip_t;
    if (r > g_view.clip_r) r = g_view.clip_r;
    if (b > g_view.clip_b) b = g_view.clip_b;
    int sx = wx - g_view.cam_x, sy = wy - g_view.cam_y;
    if (!(sx <= r && l < w + sx && sy <= b && t < sy + h)) return;
    int p = v->start;
    for (int c = 0; c < w; c++) {
        int x = sx + c;
        for (int row = 0; row < h; row++, p += v->px_step) {
            int y = sy + row;
            uint8_t s = bk->pixels[p];
            if (!s || x < l || x > r || y < t || y > b) continue;
            uint8_t *d = &g_buf[ORIGIN + y * BUF_W + x];
            *d = blend ? s_blend[s << 8 | *d] : s;
        }
        p += v->col_step;
    }
}

void render_bank(int bank, uint8_t blend, int view, int32_t x, int32_t y,
                 int32_t clip_x0, int32_t clip_y0, int32_t clip_x1, int32_t clip_y1)
{
    if (bank < 0 || bank >= RES_BANKS || view < 0 || view >= 8) return;
    const Bank *bk = &g_banks[bank];
    const BankView *v = &bk->view[view];
    s_obj_clip[0] = sar(clip_x0, 8);
    s_obj_clip[1] = sar(clip_y0, 8);
    s_obj_clip[2] = sar(clip_x1, 8);
    s_obj_clip[3] = sar(clip_y1, 8);
    draw_sprite_view(bk, v, blend, sar(x + v->hot_x, 8), sar(y + v->hot_y, 8));
}

void render_copy_view(uint8_t *out)
{
    for (int y = 0; y < SCREEN_H; y++) memcpy(&out[y * SCREEN_W], &g_buf[ORIGIN + y * BUF_W], SCREEN_W);
}

void render_capture(uint8_t *out, int w, int h)
{
    int x0 = (SCREEN_W - (w >> 2) * 4) >> 1, y0 = (SCREEN_H - h) >> 1;
    for (int y = 0; y < h; y++) memcpy(&out[y * w], &g_buf[ORIGIN + (y0 + y) * BUF_W + x0], (size_t)w);
}

void render_load_view(const uint8_t *in)
{
    for (int y = 0; y < SCREEN_H; y++) memcpy(&g_buf[ORIGIN + y * BUF_W], &in[y * SCREEN_W], SCREEN_W);
}

void render_reset_view(void)
{
    g_view.cam_x = g_view.cam_y = 0;
    render_set_view_size(0x14000);
}

void render_frame_slot(int slot, int32_t x, int32_t y)
{
    if (slot < 0 || slot >= RES_SLOTS) return;
    const FrameSlot *fs = &g_slots[slot];
    if (!fs->cells) return;
    int sx = sar(x, 8) - g_view.cam_x, sy = sar(y, 8) - g_view.cam_y;
    if (!(sy <= g_view.clip_b && g_view.clip_t < fs->h_fx4 + sy &&
          sx <= g_view.clip_r && g_view.clip_l < fs->w_fx4 + sx)) return;
    int tx0 = sar(g_view.clip_l - sx, 4), ty0 = sar(g_view.clip_t - sy, 4);
    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    int tx1 = sar(g_view.clip_r - sx, 4), ty1 = sar(g_view.clip_b - sy, 4);
    if (tx1 > fs->w - 1) tx1 = fs->w - 1;
    if (ty1 > fs->h - 1) ty1 = fs->h - 1;
    int px = sx < g_view.clip_l ? g_view.clip_l - (g_view.clip_l - sx) % 16 : sx;
    int py = sy < g_view.clip_t ? g_view.clip_t - (g_view.clip_t - sy) % 16 : sy;
    for (int ty = ty0; ty <= ty1; ty++) {
        int at = ORIGIN + (py + (ty - ty0) * 16) * BUF_W + px;
        for (int tx = tx0; tx <= tx1; tx++, at += 16)
            draw_tile(&g_chunks[fs->cells[ty * fs->w + tx].chunk], at);
    }
}

void render_mark_covered(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int l = sar(x0, 8) - g_view.cam_x, t = sar(y0, 8) - g_view.cam_y;
    int r = sar(x1, 8) - g_view.cam_x, b = sar(y1, 8) - g_view.cam_y;
    if (l < g_view.clip_l) l = g_view.clip_l;
    if (t < g_view.clip_t) t = g_view.clip_t;
    if (r > g_view.clip_r) r = g_view.clip_r;
    if (b > g_view.clip_b) b = g_view.clip_b;
    int gx0 = (l + 0x1f) >> 4, gy0 = (t + 0x1f) >> 4, gx1 = (r + 1) >> 4, gy1 = (b + 1) >> 4;
    if (gx0 >= GRID_W || gy0 >= GRID_H) return;
    for (int gy = gy0; gy < gy1 && gy < GRID_H; gy++)
        for (int gx = gx0; gx < gx1 && gx < GRID_W; gx++) s_occl[gy * GRID_W + gx] = 999999;
}

int render_objects(int start, int32_t max_z)
{
    /* Like the present routine, skip drawing frames that won't be shown. */
    bool shown = tf_frame_rate() == TF_RATE_FULL || (tf_frame_counter() & 1) == 0 || palette_fading();
    if (!shown) return -1;
    int n = start == -1 ? objects_first() : start;
    while (n != -1 && g_objects[n].z <= max_z) {
        const Object *o = &g_objects[n];
        if (!o->frozen) {
            for (int k = 0; k < 4; k++) s_obj_clip[k] = o->box[k];
            if (o->slot != -1) {
                draw_tileframe_object(o);
            } else if (o->bank >= 0 && o->bank < RES_BANKS && o->orient >= 0 && o->orient < 8) {
                const Bank *bk = &g_banks[o->bank];
                const BankView *v = &bk->view[o->orient];
                if (v->w != 0x100 || v->h != 0x100)
                    draw_sprite_view(bk, v, o->f_0f, sar(o->x + v->hot_x, 8), sar(o->y + v->hot_y, 8));
                else if (bk->pixels)
                    render_plot(bk->pixels[v->start], o->x + v->hot_x, o->y + v->hot_y);
            }
        }
        n = o->next;
    }
    return n;
}

/* --------------------------------------------------------------- primitives */

void render_plot(uint8_t colour, int32_t x, int32_t y)
{
    int sx = sar(x, 8) - g_view.cam_x, sy = sar(y, 8) - g_view.cam_y;
    if (sx <= g_view.clip_r && sx >= g_view.clip_l && sy <= g_view.clip_b && sy >= g_view.clip_t)
        g_buf[ORIGIN + sy * BUF_W + sx] = colour;
}

void render_fill(uint8_t colour, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int l = sar(x0, 8) - g_view.cam_x, t = sar(y0, 8) - g_view.cam_y;
    int r = sar(x1, 8) - g_view.cam_x, b = sar(y1, 8) - g_view.cam_y;
    if (!(l <= g_view.clip_r && g_view.clip_l <= r && t <= g_view.clip_b && g_view.clip_t <= b)) return;
    if (l < g_view.clip_l) l = g_view.clip_l;
    if (t < g_view.clip_t) t = g_view.clip_t;
    if (r > g_view.clip_r) r = g_view.clip_r;
    if (b > g_view.clip_b) b = g_view.clip_b;
    for (int y = t; y <= b; y++) memset(&g_buf[ORIGIN + y * BUF_W + l], colour, (size_t)(r - l + 1));
}

/* The original clips with 0x29e20 and draws with an assembly line routine
 * (0x2d8b8). This uses Bresenham with per-pixel clipping; check exact pixels
 * against the original before relying on them. */
void render_line(uint8_t colour, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int ax = sar(x0, 8) - g_view.cam_x, ay = sar(y0, 8) - g_view.cam_y;
    int bx = sar(x1, 8) - g_view.cam_x, by = sar(y1, 8) - g_view.cam_y;
    int dx = abs(bx - ax), dy = -abs(by - ay), stepx = ax < bx ? 1 : -1, stepy = ay < by ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 4096; guard++) {
        if (ax >= g_view.clip_l && ax <= g_view.clip_r && ay >= g_view.clip_t && ay <= g_view.clip_b)
            g_buf[ORIGIN + ay * BUF_W + ax] = colour;
        if (ax == bx && ay == by) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; ax += stepx; }
        if (e2 <= dx) { err += dx; ay += stepy; }
    }
}

bool render_obj_on_screen(int slot)
{
    const Object *o = &g_objects[slot];
    int l, t, r, b;
    object_clip(o, &l, &t, &r, &b);
    int sx, sy, w, h;
    if (o->slot == -1) {
        const BankView *v = &g_banks[o->bank].view[o->orient];
        sx = sar(o->x + v->hot_x, 8) - g_view.cam_x;
        sy = sar(v->hot_y + o->y, 8) - g_view.cam_y;
        w = sar(v->w, 8);
        h = sar(v->h, 8);
    } else {
        sx = sar(o->x, 8) - g_view.cam_x;
        sy = sar(o->y, 8) - g_view.cam_y;
        w = g_slots[o->slot].w_fx4;
        h = g_slots[o->slot].h_fx4;
    }
    return !(r < sx || sx + w - 1 < l || b < sy || sy + h - 1 < t);
}

bool render_obj_fully_on_screen(int slot)
{
    const Object *o = &g_objects[slot];
    int l, t, r, b;
    object_clip(o, &l, &t, &r, &b);
    int sx, sy, w, h;
    if (o->slot == -1) {
        const BankView *v = &g_banks[o->bank].view[o->orient];
        sx = sar(o->x + v->hot_x, 8) - g_view.cam_x;
        sy = sar(v->hot_y + o->y, 8) - g_view.cam_y;
        w = sar(v->w, 8);
        h = sar(v->h, 8);
    } else {
        sx = sar(o->x, 8) - g_view.cam_x;
        sy = sar(o->y, 8) - g_view.cam_y;
        w = g_slots[o->slot].w_fx4;
        h = g_slots[o->slot].h_fx4;
    }
    int ex = sx + w - 1, ey = sy + h - 1;
    return !(sx < l || r < sx || ex < l || r < ex || sy < t || b < sy || ey < t || b < ey);
}

void render_beam(int slot, int32_t rx, int32_t ry)
{
    const Object *o = &g_objects[slot];
    if (o->bank < 0 || o->bank >= RES_BANKS || o->orient < 0 || o->orient > 7) return;
    const Bank *bk = &g_banks[o->bank];
    const BankView *v = &bk->view[o->orient];
    if (!bk->pixels) return;
    int cols = 0, rows = 0;
    for (int32_t c = v->hot_x; c < v->hot_x + v->w; c += 0x100) cols++;
    for (int32_t r = v->hot_y; r < v->hot_y + v->h; r += 0x100) rows++;
    if (cols > 256 || rows > 256) return;
    int32_t xa[256], xb[256], ya[256], yb[256];
    int32_t acc = v->hot_x * rx;
    for (int i = 0; i < cols; i++, acc += rx * 0x100) {
        int32_t d = sar(acc, 10);
        xa[i] = d + o->x;
        xb[i] = sar(d * 0xd, 4) + o->x;
    }
    acc = v->hot_y * ry;
    for (int i = 0; i < rows; i++, acc += ry * 0x100) {
        int32_t d = sar(acc, 10);
        ya[i] = d + o->y;
        yb[i] = sar(d * 0xd, 4) + o->y;
    }
    int p = v->start;
    for (int c = 0; c < cols; c++) {
        for (int r = 0; r < rows; r++, p += v->px_step) {
            uint8_t px = bk->pixels[p];
            bool centre = v->hot_x + c * 0x100 == 0 && v->hot_y + r * 0x100 == 0;
            if (px && !centre) render_line(px, xa[c], ya[r], xb[c], yb[r]);
        }
        p += v->col_step;
    }
}

int32_t render_present(void)
{
    for (int y = g_view.clip_t; y <= g_view.clip_b; y++)
        memcpy(&g_screen[y * SCREEN_W + g_view.clip_l], &g_buf[ORIGIN + y * BUF_W + g_view.clip_l],
               (size_t)(g_view.clip_r - g_view.clip_l + 1));
    return tf_present();
}
