#include "collision.h"

#include "objects.h"
#include "res.h"

static int32_t sar(int32_t v, int n) { return v >= 0 ? v >> n : ~(~v >> n); }

static const BankView *obj_view(const Object *o)
{
    return &g_banks[o->bank].view[o->orient];
}

int32_t obj_left(int slot)
{
    const Object *o = &g_objects[slot];
    return o->slot != -1 ? o->x : o->x + obj_view(o)->hot_x;
}

int32_t obj_top(int slot)
{
    const Object *o = &g_objects[slot];
    return o->slot != -1 ? o->y : o->y + obj_view(o)->hot_y;
}

int32_t obj_right(int slot)
{
    const Object *o = &g_objects[slot];
    if (o->slot != -1) return o->x + g_slots[o->slot].w_fx12 - 0x100;
    return o->x + obj_view(o)->hot_x + obj_view(o)->w - 0x100;
}

int32_t obj_bottom(int slot)
{
    const Object *o = &g_objects[slot];
    if (o->slot != -1) return o->y + g_slots[o->slot].h_fx12 - 0x100;
    return o->y + obj_view(o)->hot_y + obj_view(o)->h - 0x100;
}

/* Is pixel (lx, ly) of a bank view solid? Same addressing as the renderer's
 * column walk. */
static int view_solid(const Bank *b, const BankView *v, int lx, int ly)
{
    int w = sar(v->w, 8), h = sar(v->h, 8);
    if (lx < 0 || ly < 0 || lx >= w || ly >= h || !b->pixels) return 0;
    return b->pixels[ly * v->px_step + lx * (h * v->px_step + v->col_step) + v->start] != 0;
}

int collide_point(int first, int last, int32_t x, int32_t y)
{
    for (int i = first; i <= last; i++) {
        const Object *o = &g_objects[i];
        if (!o->alive) continue;
        if (o->slot == -1) {
            const Bank *b = &g_banks[o->bank];
            const BankView *v = &b->view[o->orient];
            int lx = sar(x, 8) - sar(o->x + v->hot_x, 8);
            int ly = sar(y, 8) - sar(o->y + v->hot_y, 8);
            if (view_solid(b, v, lx, ly)) return i;
        } else {
            const FrameSlot *fs = &g_slots[o->slot];
            int lx = sar(x, 8) - sar(o->x, 8), ly = sar(y, 8) - sar(o->y, 8);
            if (lx < 0 || ly < 0 || lx >= fs->w_fx4 || ly >= fs->h_fx4 || !fs->cells) continue;
            uint16_t bank = (uint16_t)g_chunks[fs->cells[(lx >> 4) + fs->w * (ly >> 4)].chunk].bank;
            if (bank >= RES_BANKS) continue;
            const Bank *shape = &g_banks[bank];
            const BankView *v = &shape->view[0];
            if (view_solid(shape, v, (lx & 0xf) - sar(v->hot_x, 8), (ly & 0xf) - sar(v->hot_y, 8))) return i;
        }
    }
    return -1;
}

/* Any position in the overlap of two rectangles where both are solid? This is
 * what the assembly overlap tests 0x2d7fb / 0x2d84b compute. */
static bool views_touch(const Bank *ba, const BankView *va, int ax, int ay,
                        const Bank *bb, const BankView *vb, int bx, int by,
                        int x0, int y0, int x1, int y1)
{
    for (int x = x0; x <= x1; x++)
        for (int y = y0; y <= y1; y++)
            if (view_solid(ba, va, x - ax, y - ay) && view_solid(bb, vb, x - bx, y - by)) return true;
    return false;
}

bool collide_sprite_tiles(int a, int b, int32_t *cx, int32_t *cy)
{
    const Object *oa = &g_objects[a], *ob = &g_objects[b];
    if (!oa->alive || oa->frozen || !ob->alive || ob->frozen || ob->slot == -1) return false;
    const Bank *bank = &g_banks[oa->bank];
    const BankView *va = &bank->view[oa->orient];
    int aw = sar(va->w, 8), ah = sar(va->h, 8);
    int ax0 = sar(oa->x + va->hot_x, 8), ax1 = aw + ax0 - 1;
    int ay0 = sar(oa->y + va->hot_y, 8), ay1 = ah + ay0 - 1;
    const FrameSlot *fs = &g_slots[ob->slot];
    int bx = sar(ob->x, 8), by = sar(ob->y, 8);
    if (aw == 0 || fs->w_fx4 == 0 || !fs->cells) return false;
    if (ax1 < bx || ay1 < by || bx + fs->w_fx4 - 1 < ax0 || fs->h_fx4 + by - 1 < ay0) return false;
    if (ax1 < ob->box[0] || ay1 < ob->box[1] || ob->box[2] < ax0 || ob->box[3] < ay0) return false;

    int tx0 = sar(ax0 - bx, 4), tx1 = sar(ax1 - bx, 4);
    int ty0 = sar(ay0 - by, 4), ty1 = sar(ay1 - by, 4);
    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    if (tx1 > fs->w - 1) tx1 = fs->w - 1;
    if (ty1 > fs->h - 1) ty1 = fs->h - 1;
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++) {
            uint16_t sb = (uint16_t)g_chunks[fs->cells[ty * fs->w + tx].chunk].bank;
            if (sb >= RES_BANKS) continue;
            const Bank *shape = &g_banks[sb];
            const BankView *vs = &shape->view[0];
            int sw = sar(vs->w, 8);
            if (sw == 0) continue;
            int sx0 = sar(ob->x + vs->hot_x, 8) + tx * 16, sy0 = sar(vs->hot_y + ob->y, 8) + ty * 16;
            int sx1 = sw + sx0 - 1, sy1 = sar(vs->h, 8) + sy0 - 1;
            if (!(sx0 <= ax1 && sy0 <= ay1 && ax0 <= sx1 && ay0 <= sy1)) continue;
            int l = sx0 - ax0 < 0 ? 0 : sx0 - ax0, r = ax1 - sx1 < 0 ? 0 : ax1 - sx1;
            int t = sy0 - ay0 < 0 ? 0 : sy0 - ay0, bm = ay1 - sy1 < 0 ? 0 : ay1 - sy1;
            int px = ax0 + l + sar((aw - l) - r, 1);
            int py = t + ay0 + sar((ah - t) - bm, 1);
            if (views_touch(bank, va, ax0, ay0, shape, vs, sx0, sy0,
                            ax0 + l, ay0 + t, ax1 - r, ay1 - bm)) {
                if (cx) { *cx = px * 0x100; *cy = py * 0x100; }
                return true;
            }
        }
    return false;
}

int collide_sprites(int a, int lo, int hi)
{
    const Object *oa = &g_objects[a];
    if (!oa->alive || oa->frozen || oa->bank < 0 || oa->bank >= RES_BANKS) return -1;
    const Bank *ba = &g_banks[oa->bank];
    const BankView *va = &ba->view[oa->orient];
    int ax0 = sar(oa->x + va->hot_x, 8), ay0 = sar(oa->y + va->hot_y, 8);
    int ax1 = ax0 + sar(va->w, 8) - 1, ay1 = ay0 + sar(va->h, 8) - 1;
    for (int i = lo; i <= hi; i++) {
        const Object *o = &g_objects[i];
        if (!o->alive || o->frozen || i == a || o->bank < 0 || o->bank >= RES_BANKS) continue;
        const Bank *bb = &g_banks[o->bank];
        const BankView *vb = &bb->view[o->orient];
        int bx0 = sar(vb->hot_x + o->x, 8), by0 = sar(vb->hot_y + o->y, 8);
        int bx1 = sar(vb->w, 8) + bx0 - 1, by1 = sar(vb->h, 8) + by0 - 1;
        if (!(bx0 <= ax1 && by0 <= ay1 && ax0 <= bx1 && ay0 <= by1)) continue;
        int x0 = ax0 > bx0 ? ax0 : bx0, y0 = ay0 > by0 ? ay0 : by0;
        int x1 = ax1 < bx1 ? ax1 : bx1, y1 = ay1 < by1 ? ay1 : by1;
        if (views_touch(ba, va, ax0, ay0, bb, vb, bx0, by0, x0, y0, x1, y1)) return i;
    }
    return -1;
}
