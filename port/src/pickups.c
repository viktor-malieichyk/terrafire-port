#include "pickups.h"

#include "anims.h"
#include "callbacks.h"
#include "effects.h"
#include "game.h"
#include "hud.h"
#include "missions.h"
#include "objects.h"
#include "render.h"
#include "spawn.h"
#include "tables.h"
#include "sound.h"
#include "unported.h"
#include "weapons.h"

static int32_t sar(int32_t v, int n) { return v >= 0 ? v >> n : ~(~v >> n); }

/* A pod bursts only while it is on screen. */
void pod_hit(int slot)
{
    if (!render_obj_on_screen(slot)) return;
    pickup_spawn(g_state.obj[slot].o_8, obj_x(slot), obj_y(slot) - 0x1000, 0);
    effect_spawn(0x10a, false, obj_x(slot), obj_y(slot));
    obj_kill(slot);
}

void pickup_spawn(int contents, int32_t x, int32_t y, int dir)
{
    int slot = obj_find_free(0xce, 0xd7, false);
    if (slot == -1) return;
    obj_spawn(slot, 0x807, x, y, 9);
    g_state.obj[slot].o_8 = contents;
    int32_t d = g_state.difficulty;
    if (g.mission_type == MISSION_NORMAL) {
        /* Appears after 20 frames; expires sooner on harder settings
         * ((5 - difficulty) * 256 frames). */
        obj_set_callback(slot, 0, CB_20eb0, (5 - d) * 0x100);
        obj_freeze(slot);
        obj_set_callback(slot, 1, CB_2aaa0, 0x14);
        switch (contents) {
        case 0x7da: obj_set_anim(slot, ANIM_50448, 0); break;
        case 0x7e9: obj_set_anim(slot, ANIM_50810, 0); break;
        case 0x7f8: obj_set_anim(slot, ANIM_50bd8, 0); break;
        case 0x807: obj_set_anim(slot, ANIM_50fa0, 0); break;
        case 0x816: obj_set_anim(slot, ANIM_51550, 0); break;
        case 0x825: obj_set_anim(slot, ANIM_51b00, 0); break;
        case 0x834:
            obj_set_anim(slot, ANIM_520b0, 0);
            obj_set_callback(slot, 0, CB_20e50, (7 - d) * 0x40);
            break;
        default: break;
        }
        return;
    }
    /* Bonus level: launched in direction `dir`, random expiry and start frame. */
    obj_set_velocity(slot, sar(g_trig_a[dir], 4), sar(g_trig_b[dir] * 0xd, 8));
    g_state.f_015++;
    obj_set_callback(slot, 0, CB_20eb0, (5 - d) * 0x100 + g_random_table[g_state.f_015 & 0xff] % 0x96);
    obj_set_callback(slot, 1, CB_1038c, 1);
    int32_t r = g_random_table[(g_state.f_015 + 1) & 0xff];
    int anim = contents == 0x807 ? ANIM_51188 : contents == 0x816 ? ANIM_51738 :
               contents == 0x825 ? ANIM_51ce8 : -1;
    if (anim < 0) return;
    g_state.f_015++;
    obj_set_anim(slot, anim, (int16_t)(r % g_anims[anim].count));
}

/* The player touches a pickup (type 0xce). 0x834 pickups are left alone. */
void pickup_collect(int slot)
{
    int32_t contents = g_state.obj[slot].o_8;
    if (contents == 0x834) return;
    obj_kill(slot);
    sound_play(0xf);
    switch (contents) {
    case 0x7da:                                   /* weapon upgrade */
        g_state.f_01d_flag = 1;
        weapon_set(g_state.weapon + 1);
        hud_message(0x8c, MSG_WEAPON_UPGRADE);
        break;
    case 0x7e9:                                   /* save point */
        g.save_requested = true;
        break;
    case 0x7f8:                                   /* extra life */
        hud_message(0x8c, MSG_EXTRA_LIFE);
        g_state.f_01e_flag = 1;
        life_indicator_spawn(g_state.f_00c + 0x113);
        g_state.f_00c++;
        break;
    case 0x807:                                   /* shields +50% */
        hud_message(0x8c, MSG_SHIELDS_RESTORED);
        SHIELDS += g_state.shields_max >> 1;
        if (SHIELDS > g_state.shields_max) SHIELDS = g_state.shields_max;
        break;
    case 0x816: if (!g_state.f_014) g_state.score += 0x32; break;
    case 0x825: if (!g_state.f_014) g_state.score += 0x19; break;
    default: break;
    }
}

/* A pickup nobody collected disappears in a small explosion. */
void cb_pickup_expire(int slot)
{
    effect_spawn(g.mission_type == MISSION_NORMAL ? 0x129 : 0x148, false, obj_x(slot), obj_y(slot));
    obj_kill(slot);
}

/* The 0x834 pickup goes off by itself: screen shake and a fan of 21 player
 * bullets from -100 to +100 degrees, then it expires. */
void cb_pickup_burst(int slot)
{
    g_state.f_0af = 0x28;
    for (int32_t a = -100; a < 0x65; a += 10)
        bullet_spawn(0x550, 0x11, obj_x(slot), obj_y(slot), 0, 0, 7, a, CB_194e4, 200);
    cb_pickup_expire(slot);
}
