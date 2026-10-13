#include "objects.h"

#include "anims.h"
#include "callbacks.h"
#include "fatal.h"
#include "unported.h"

#include <SDL3/SDL.h>

Object g_objects[OBJ_COUNT] = {0};

static int s_head = -1;         /* 0x841ec, first (lowest z) active object */
static int s_cur = -1;          /* 0x59e24, object being updated */
static int s_next = -1;         /* 0x59e28, next object to update */
static bool s_updating;         /* 0x59e20 */

static void (*s_cb_override)(int slot);
static void (*s_step_hook)(void);

void objects_set_step_hook(void (*fn)(void)) { s_step_hook = fn; }

int objects_first(void) { return s_head; }
void objects_set_callback_override(void (*fn)(int slot)) { s_cb_override = fn; }

static void run_callback(int cb, int slot)
{
    if (s_cb_override) s_cb_override(slot);
    else if (cb >= 0 && cb < CALLBACK_COUNT) g_callbacks[cb](slot);
    else SDL_Log("object %d: invalid callback index %d", slot, cb);
}

void objects_init(void)
{
    for (int i = 0; i < OBJ_COUNT; i++) {
        g_objects[i].id = (int16_t)i;
        g_objects[i].alive = 0;
    }
    s_head = s_cur = s_next = -1;
}

static void obj_insert(int slot);
static void obj_remove(int slot);

void obj_link(int slot) { obj_insert(slot); }

void objects_park(bool parked[OBJ_COUNT])
{
    for (int i = 0; i < OBJ_COUNT; i++) {
        parked[i] = obj_alive(i);
        if (parked[i]) obj_remove(i);
    }
}

void objects_unpark(bool parked[OBJ_COUNT])
{
    for (int i = 0; i < OBJ_COUNT; i++)
        if (parked[i]) {
            obj_insert(i);
            parked[i] = false;
        }
}

void objects_clear_active(void)
{
    for (int i = s_head; i != -1; i = g_objects[i].next) g_objects[i].alive = 0;
    s_head = s_cur = s_next = -1;
}

int obj_find_free(int lo, int hi, bool must)
{
    for (int i = lo; i <= hi; i++)
        if (!g_objects[i].alive) return i;
    if (must) tf_fatal("no free object slot in %d..%d", lo, hi);
    return -1;
}

bool obj_alive(int slot) { return g_objects[slot].alive != 0; }
int32_t obj_x(int slot) { return g_objects[slot].x; }
int32_t obj_y(int slot) { return g_objects[slot].y; }
void obj_unfreeze(int slot) { g_objects[slot].frozen = 0; }
void obj_freeze(int slot) { g_objects[slot].frozen = 1; }

void obj_clear_callbacks(int slot)
{
    for (int k = 0; k < 3; k++) g_objects[slot].cb_timer[k] = -1;
}

/* 0x2bc60: mark alive and insert by z; equal z goes after existing objects. */
static void obj_insert(int slot)
{
    Object *o = &g_objects[slot];
    o->alive = 1;
    if (s_head == -1) {
        o->next = o->prev = -1;
        s_head = slot;
        return;
    }
    if (o->z < g_objects[s_head].z) {
        o->prev = -1;
        o->next = (int16_t)s_head;
        g_objects[s_head].prev = (int16_t)slot;
        s_head = slot;
        return;
    }
    int at = s_head;
    while (g_objects[at].next != -1 && g_objects[g_objects[at].next].z <= o->z) at = g_objects[at].next;
    o->prev = (int16_t)at;
    o->next = g_objects[at].next;
    if (o->next != -1) g_objects[o->next].prev = (int16_t)slot;
    g_objects[at].next = (int16_t)slot;
}

/* 0x2cac0: mark dead and unlink, keeping objects_update()'s cursor valid. */
static void obj_remove(int slot)
{
    Object *o = &g_objects[slot];
    o->alive = 0;
    if (slot == s_cur) s_cur = -1;
    if (slot == s_next) s_next = g_objects[s_next].next;
    if (o->prev == -1) {
        s_head = o->next;
        if (s_head != -1) g_objects[s_head].prev = -1;
    } else if (o->next == -1) {
        g_objects[o->prev].next = -1;
    } else {
        g_objects[o->prev].next = o->next;
        g_objects[o->next].prev = o->prev;
    }
}

void obj_spawn(int slot, int bank, int32_t x, int32_t y, int32_t z)
{
    Object *o = &g_objects[slot];
    if (o->alive) {
        /* The original would link the object in twice; flag it rather than
         * silently corrupting the list. */
        SDL_Log("obj_spawn: slot %d is already alive", slot);
        obj_remove(slot);
    }
    o->slot = -1;
    o->orient = 0;
    o->vx = o->vy = o->ax = o->ay = 0;
    o->f_0f = 0;
    o->frozen = 0;
    o->anim = -1;
    o->box[0] = o->box[1] = -32000;
    o->box[2] = o->box[3] = 32000;
    o->no_drag = -1;
    o->f_49 = 0;
    o->parent = o->child = -1;
    o->bank = (int16_t)bank;
    o->x = x;
    o->y = y;
    o->z = z;
    for (int k = 0; k < 3; k++) o->cb_timer[k] = -1;
    /* Not reset by the original: anim_frame, cb[], f_4a. */
    obj_insert(slot);
}

void obj_set_pos(int slot, int32_t x, int32_t y)
{
    Object *o = &g_objects[slot];
    if (o->child != -1 && o->no_drag == -1) obj_move(o->child, x - o->x, y - o->y);
    o->x = x;
    o->y = y;
}

static int32_t step_x(int orient, int32_t dx, int32_t dy);
static int32_t step_y(int orient, int32_t dx, int32_t dy);

int32_t obj_motion_x(int slot)
{
    const Object *o = &g_objects[slot];
    int32_t v = o->vx + o->ax;
    if (o->anim >= 0) {
        const AnimFrame *f = &g_anims[o->anim].frames[o->anim_frame];
        v += step_x(o->orient, f->dx, f->dy);
    }
    return v;
}

int32_t obj_motion_y(int slot)
{
    const Object *o = &g_objects[slot];
    int32_t v = o->vy + o->ay;
    if (o->anim >= 0) {
        const AnimFrame *f = &g_anims[o->anim].frames[o->anim_frame];
        v += step_y(o->orient, f->dx, f->dy);
    }
    return v;
}

void obj_set_velocity(int slot, int32_t vx, int32_t vy)
{
    g_objects[slot].vx = vx;
    g_objects[slot].vy = vy;
}

void obj_add_velocity(int slot, int32_t dvx, int32_t dvy)
{
    g_objects[slot].vx += dvx;
    g_objects[slot].vy += dvy;
}

void obj_clear_anim(int slot) { g_objects[slot].anim = -1; }

int obj_sprite_bank(int slot)
{
    return g_objects[slot].slot == -1 ? g_objects[slot].bank : -1;
}

void obj_attach(bool copy_orient, int child, int child_point, int parent, int parent_point)
{
    if (copy_orient) {
        int16_t orient = g_objects[parent].orient;
        if (g_objects[child].child != -1) g_objects[g_objects[child].child].orient = orient;
        g_objects[child].orient = orient;
    }
    if (child_point == -1 || parent_point == -1) {
        child_point = parent_point = -1;
    } else {
        UNPORTED("0x2c4a8", "position attached object at its attach point");
    }
    g_objects[parent].child = (int16_t)child;
    g_objects[child].parent = (int16_t)parent;
    g_objects[child].f_4a = (int16_t)child_point;
    g_objects[parent].no_drag = (int16_t)parent_point;
    g_objects[child].f_49 = copy_orient;
}

void obj_set_slot(int slot, int frame_slot)
{
    if (frame_slot != -1) g_objects[slot].slot = (int16_t)frame_slot;
}

void obj_set_orient(int slot, int orient)
{
    Object *o = &g_objects[slot];
    if (o->child != -1) g_objects[o->child].orient = (int16_t)orient;
    o->orient = (int16_t)orient;
}

void obj_set_box(int slot, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    Object *o = &g_objects[slot];
    o->box[0] = x0 >= 0 ? x0 >> 8 : ~(~x0 >> 8);
    o->box[1] = y0 >= 0 ? y0 >> 8 : ~(~y0 >> 8);
    o->box[2] = x1 >= 0 ? x1 >> 8 : ~(~x1 >> 8);
    o->box[3] = y1 >= 0 ? y1 >> 8 : ~(~y1 >> 8);
}

void obj_kill(int slot)
{
    Object *o = &g_objects[slot];
    if (!o->alive) return;
    if (o->child != -1) obj_kill(o->child);
    if (o->parent != -1) {
        g_objects[o->parent].child = -1;
        o->parent = -1;
    }
    obj_remove(slot);
}

void obj_set_callback(int slot, int k, int cb, int frames)
{
    Object *o = &g_objects[slot];
    if (frames == -1) {
        o->cb_timer[k] = -1;
    } else if (frames == 0) {
        o->cb_timer[k] = -1;
        run_callback(cb, slot);
    } else {
        o->cb[k] = (int16_t)cb;
        o->cb_timer[k] = (int16_t)(frames - 1);
    }
}

void obj_move(int slot, int32_t dx, int32_t dy)
{
    Object *o = &g_objects[slot];
    if (o->child != -1 && o->no_drag == -1) obj_move(o->child, dx, dy);
    o->x += dx;
    o->y += dy;
}

/* 0x2c9a8 / 0x2ca7c: an animation step through the object's orientation
 * (like orient_x/orient_y, but -1 for an invalid orientation). */
static int32_t step_x(int orient, int32_t dx, int32_t dy)
{
    switch (orient) {
    case 0: case 6: return dx;
    case 1: case 5: return -dy;
    case 2: case 4: return -dx;
    case 3: case 7: return dy;
    default: return -1;
    }
}

static int32_t step_y(int orient, int32_t dx, int32_t dy)
{
    switch (orient) {
    case 0: case 4: return dy;
    case 1: case 7: return dx;
    case 2: case 6: return -dy;
    case 3: case 5: return -dx;
    default: return -1;
    }
}

static void set_orient(Object *o, uint16_t orient)
{
    if (o->child != -1) g_objects[o->child].orient = (int16_t)orient;
    o->orient = (int16_t)orient;
}

void obj_anim_step(int slot)
{
    Object *o = &g_objects[slot];
    if (o->anim < 0) return;
    const AnimDef *a = &g_anims[o->anim];
    const AnimFrame *f = &a->frames[o->anim_frame];

    if (o->slot == -1) {
        if (f->image != -1) {
            o->bank = f->image;
            o->slot = -1;
        }
    } else if (f->image != -1) {
        o->slot = f->image;
    }

    uint16_t r = (uint16_t)o->orient;
    switch (f->op) {
    case 1:                                    /* mirror */
        set_orient(o, r ^ 4);
        break;
    case 2:                                    /* rotate one way */
        set_orient(o, r < 4 ? (uint16_t)((r + 1) & 3) : (uint16_t)(((r - 1) & 3) | 4));
        break;
    case 3:                                    /* rotate the other way */
        set_orient(o, r < 4 ? (uint16_t)((r - 1) & 3) : (uint16_t)(((r + 1) & 3) | 4));
        break;
    default:
        break;
    }

    int32_t dx = step_x(o->orient, f->dx, f->dy);
    int32_t dy = step_y(o->orient, f->dx, f->dy);
    if (o->child != -1 && o->no_drag == -1) obj_move(o->child, dx, dy);
    o->x += dx;
    o->y += dy;

    if (++o->anim_frame >= a->count) {
        if (a->loop == 0) o->anim = -1;
        else o->anim_frame = 0;
    }
}

void obj_set_anim(int slot, int anim, int frame)
{
    g_objects[slot].anim_frame = (int16_t)frame;
    g_objects[slot].anim = (int16_t)anim;
    obj_anim_step(slot);
}

void objects_update(void)
{
    if (s_step_hook) s_step_hook();
    s_updating = true;
    int node = s_head;
    while (node != -1) {
        s_cur = node;
        s_next = g_objects[node].next;
        Object *o = &g_objects[node];
        if (!o->frozen) {
            obj_anim_step(node);
            o->vx += o->ax;
            o->vy += o->ay;
            if (o->child != -1 && o->no_drag == -1) obj_move(o->child, o->vx, o->vy);
            o->x += o->vx;
            o->y += o->vy;
        }
        for (int k = 0; k < 3; k++) {
            if (o->cb_timer[k] == -1) continue;
            if (o->cb_timer[k]-- == 0) {
                run_callback(o->cb[k], s_cur);
                if (s_cur == -1) break;        /* the callback killed this object */
            }
        }
        node = s_next;
    }
    s_cur = s_next = -1;
    s_updating = false;
}
