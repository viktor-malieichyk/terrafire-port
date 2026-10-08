#include "effects.h"

#include "anims.h"
#include "callbacks.h"
#include "collision.h"
#include "game.h"
#include "missions.h"
#include "objects.h"
#include "render.h"
#include "tables.h"
#include "sound.h"
#include "unported.h"

static int32_t sar(int32_t v, int n) { return v >= 0 ? v >> n : ~(~v >> n); }

static int32_t next_rand(void)
{
    g_state.f_019++;
    return g_random_table[g_state.f_019 & 0xff];
}

static bool point_on_screen(int32_t x, int32_t y)
{
    int sx = sar(x, 8) - g_view.cam_x, sy = sar(y, 8) - g_view.cam_y;
    return g_view.clip_l <= sx && sx <= g_view.clip_r && g_view.clip_t <= sy && sy <= g_view.clip_b;
}

/* 0x229ec: a falling debris piece (spawned by the 0x10a explosion). It lives
 * for its state o_0 frames; landing on the level (or running out) leaves an
 * impact effect, and falling onto object 11 from above destroys it too. */
void cb_debris(int slot)
{
    int32_t life = g_state.obj[slot].o_0--;
    if (life != 0 && collide_point(10, 10, obj_x(slot), obj_y(slot)) == -1) {
        if (obj_alive(11) && obj_y(11) < obj_y(slot) &&
            collide_point(11, 11, obj_x(slot), obj_y(slot)) != -1) {
            obj_kill(slot);
            int32_t x = obj_x(slot), y = obj_y(slot);
            effect_spawn(0x158, false, x, y);
            bool seen = point_on_screen(x, y);
            sound_stop(seen ? 7 : 6);
            sound_play(seen ? 0x1e : 0x1d);
            return;
        }
        obj_set_callback(slot, 0, CB_229ec, 1);
        return;
    }
    effect_spawn(0x158, false, obj_x(slot), obj_y(slot));
    obj_kill(slot);
}

/* 0x144b8: an exhaust particle. Removed when its animation ends; on touching
 * the level it bounces off as a small spark that lasts 30 more frames. */
void cb_exhaust(int slot)
{
    if (g_objects[slot].anim < 0) {
        obj_kill(slot);
        return;
    }
    if (collide_point(10, 10, obj_x(slot), obj_y(slot)) == -1) {
        obj_set_callback(slot, 0, CB_144b8, 1);
        return;
    }
    int32_t r = g_random_table[(g_state.f_019 + 1) & 0xff];
    int32_t vy = obj_motion_y(slot) < 0 ? r % 0x100 : -(r % 0x100);
    g_state.f_019 += 2;
    obj_set_velocity(slot, g_random_table[g_state.f_019 & 0xff] % 0x200 - 0x100, vy);
    g_objects[slot].ax = 0;
    g_objects[slot].ay = 8;
    obj_clear_anim(slot);
    g_state.f_019++;
    g_objects[slot].bank = (int16_t)(g_random_table[g_state.f_019 & 0xff] % 4 + 0x2cb);   /* 0x2c3e4 */
    g_objects[slot].slot = -1;
    obj_set_callback(slot, 0, CB_144b8, 0x1e);
}

typedef struct {
    int main_anim, frag_a, frag_b;   /* animations */
    int sparks;                      /* sparks near the player */
    int z, ring_step;                /* z of the main piece; degrees between fragments */
    int32_t speed_base, speed_range;
} Preset;

void effect_spawn(int kind, bool ring, int32_t x, int32_t y)
{
    bool normal = g.mission_type == MISSION_NORMAL;
    Preset p;
    switch (kind) {
    case 0x10a:
        p = (Preset){ ANIM_45848, ANIM_45b48, normal ? ANIM_45a40 : ANIM_45b48, 0x28, 0x2a,
                      normal ? 0x1e : 0x28, 0xe00, normal ? 0xf00 : 0x800 };
        sound_play(4);
        break;
    case 0x129:
        p = (Preset){ ANIM_45a40, ANIM_45c50, ANIM_45b48, 10, 0x2b, 0x28, 0x800, 0xa00 };
        sound_play(5);
        break;
    case 0x148:
        p = (Preset){ ANIM_45b48, ANIM_45d08, ANIM_45c50, 5, 0x2c, 0x3c, 0x600, 0x800 };
        sound_play(5);
        break;
    case 0x158:
        p = (Preset){ ANIM_45c50, ANIM_45d08, ANIM_45d08, 3, 0x2d, 0x3c, 0x300, 0x800 };
        sound_play(6);   /* the original picks 6 or 7 by whether (x, y) is on screen (0x2a9e0) */
        break;
    case 0x168:
        p = (Preset){ ANIM_45d08, ANIM_45d08, ANIM_45d08, 3, 0x2e, 0x3c, 0x300, 0x700 };
        break;
    default:
        UNPORTED("0x110d0", "effect kind without a preset");
        return;
    }
    if (!g.debris) p.sparks = 3;

    int main = obj_find_free(0x118, 0x167, false);
    if (main == -1) return;
    obj_spawn(main, kind, x, y, p.z);
    obj_set_anim(main, p.main_anim, 0);
    obj_set_callback(main, 0, CB_2c118, g_anims[p.main_anim].count);
    obj_set_orient(main, (main & 1) << 2);

    if (ring) {
        for (int a = 0; a < 360; a += p.ring_step) {
            int f = obj_find_free(0x118, 0x167, false);
            if (f == -1) break;
            int32_t d = next_rand() % p.speed_range + p.speed_base, ox, oy;
            if (normal) {
                oy = sar(g_trig_b[a] * d, 10);
                ox = sar(g_trig_a[a] * d * 3, 11);
            } else {
                oy = sar(d * 12 * g_trig_b[a], 13);
                ox = sar(d * g_trig_a[a], 9);
            }
            obj_spawn(f, 0, x + ox, y + oy, p.z + 1);
            int anim = next_rand() % 2 != 0 ? p.frag_a : p.frag_b;
            obj_set_anim(f, anim, 0);
            obj_freeze(f);                                  /* appears after a delay */
            int32_t delay = next_rand() % 0x19 + 5;
            obj_set_callback(f, 0, CB_2aaa0, delay);
            obj_set_callback(f, 1, CB_2c118, g_anims[g_objects[f].anim].count + delay);
        }
    }

    int slot = 0x1f5;
    if (normal && kind == 0x10a && g.debris) {
        /* Falling debris: a fan upwards, or sideways/down if there is no ground below. */
        bool ground = collide_point(10, 10, x, y - 0x1000) != -1;
        int a = ground ? 0x5a : -45, last = ground ? 0x10e : 0x2d, step = ground ? 0x14 : 10;
        for (; a <= last; a += step) {
            slot = obj_find_free(slot + 1, 0x259, false);
            if (slot == -1) return;
            obj_spawn(slot, 0x8e1, x, y, 0x2f);
            obj_set_anim(slot, ANIM_536d8, (int16_t)(next_rand() % g_anims[ANIM_536d8].count));
            obj_set_orient(slot, next_rand() % 8);
            int32_t speed = next_rand() % 0x226 + 0x96;
            int ang = a < 0 ? 360 - (-a) % 360 : a % 360;
            obj_set_velocity(slot, sar(g_trig_a[ang] * speed, 10), sar(g_trig_b[ang] * speed, 10));
            g_objects[slot].ax = 0;
            g_objects[slot].ay = 10;
            g_state.obj[slot].o_0 = 300;
            obj_set_callback(slot, 0, CB_229ec, 1);
        }
    }

    /* Sparks, only close to the player. */
    int32_t dx = obj_x(main) - obj_x(SLOT_PLAYER), dy = obj_y(main) - obj_y(SLOT_PLAYER);
    if ((dx < 0 ? -dx : dx) >= 0xc800 || (dy < 0 ? -dy : dy) >= 0xc800) return;
    for (int i = 1; i <= p.sparks; i++) {
        slot = obj_find_free(slot + 1, 0x259, false);
        if (slot == -1) return;
        obj_spawn(slot, 0x2c6, x, y, 7);
        int32_t r = next_rand();
        obj_set_anim(slot, ANIM_48180, (int16_t)(r % 0x28));
        obj_set_callback(slot, 0, CB_2c118, g_anims[ANIM_48180].count - r % 0x28);
        if (normal) {
            int32_t ry = g_random_table[(g_state.f_019 + 1) & 0xff];
            g_state.f_019 += 2;
            int32_t rx = g_random_table[g_state.f_019 & 0xff];
            obj_set_velocity(slot, rx % 600 - 300, ry % 600 - 400);
            g_objects[slot].ax = 0;
            g_objects[slot].ay = 8;
        } else {
            int32_t r1 = g_random_table[(g_state.f_019 + 1) & 0xff];
            int32_t r2 = g_random_table[(g_state.f_019 + 2) & 0xff];
            g_state.f_019 += 3;
            int32_t r0 = g_random_table[g_state.f_019 & 0xff];
            obj_set_velocity(slot, ((r0 % 5 + 1) * g_trig_a[r1 % 360]) / 0x28,
                             ((r2 % 5 + 1) * g_trig_b[r1 % 360]) / 0x28);
        }
    }
}
