#include "hits.h"

#include "bonus.h"
#include "callbacks.h"
#include "collision.h"
#include "effects.h"
#include "enemies.h"
#include "pickups.h"
#include "game.h"
#include "hud.h"
#include "tow.h"
#include "objects.h"
#include "res.h"
#include "tables.h"
#include "sound.h"
#include "unported.h"
#include "weapons.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#define OBJ_LEVEL 10
#define OBJ_TOW 15
#define FATAL 10000
#define LAST_HIT(slot) (g_state.obj[(slot)].o_4)   /* mission_frames of the last hit */

static int32_t sar(int32_t v, int n) { return v >= 0 ? v >> n : ~(~v >> n); }

static int32_t next_random(void)
{
    g_state.f_015++;
    return g_random_table[g_state.f_015 & 0xff];
}

void player_damage(int by_obj, int damage)
{
    int32_t mf = g_state.mission_frames;
    /* 30-frame grace period unless the hit is fatal. */
    if (damage != FATAL && mf - 30 <= LAST_HIT(SLOT_PLAYER) && mf - 30 <= LAST_HIT(by_obj)) return;
    /* "SHIELDS CRITICAL" when shields fall into the low band. */
    int32_t sixth = g_state.shields_max / 6;
    int32_t lo = sixth > 1 ? 1 : sixth, hi = sixth < 1 ? 1 : sixth;
    bool was_outside = !(lo <= SHIELDS && SHIELDS <= hi);
    int32_t after = SHIELDS - damage;
    if (was_outside && lo <= after && after <= hi) hud_message(0xd2, MSG_SHIELDS_CRITICAL);
    SHIELDS = after < 0 ? 0 : after;
    LAST_HIT(SLOT_PLAYER) = mf;
    LAST_HIT(by_obj) = mf;
}

static int32_t clamp_bounce(int32_t v)
{
    int32_t a = v < 0 ? -v : v;
    if (a <= 0x15e) return v;
    int32_t r = next_random() % 100 + 300;
    return v < 0 ? -r : r;
}

void player_hit(int by_obj, int damage, int32_t cx, int32_t cy, int32_t bx, int32_t by)
{
    bx = clamp_bounce(bx);
    by = clamp_bounce(by);
    obj_set_velocity(SLOT_PLAYER, bx, by);
    int32_t mf = g_state.mission_frames;
    if (LAST_HIT(SLOT_PLAYER) < mf - 1 || LAST_HIT(by_obj) < mf - 1) {
        if (g_state.stage == 2) tow_pull(bx, by);
        g_state.angle = (g_state.angle + 340 + (next_random() % 2) * 40) % 360;
        effect_spawn(0x158, false, cx, cy);                    /* 0x110d0 */
    }
    player_damage(by_obj, damage);
}

/* 0x257bc: the placement map marks bounceable surfaces at the contact point
 * (0x54-0x5b give a direction); anywhere else a crash is fatal. */
void bounce_direction(int32_t x, int32_t y, int32_t *bx, int32_t *by)
{
    const FrameSlot *map = &g_slots[3];
    int col = sar(x, 12), row = sar(y, 12) - 0x50;
    int code = (map->cells && col >= 0 && row >= 0 && col < map->w && row < map->h)
               ? map->cells[row * map->w + col].chunk : -1;
    switch (code) {
    case 0x54: *bx = next_random() % 0xc9 - 100; *by = -0x100; break;   /* floor */
    case 0x55: *bx = 0; *by = 0x100; break;                             /* ceiling */
    case 0x56: *bx = -0x100; *by = 0; break;
    case 0x57: *bx = 0x100; *by = 0; break;
    case 0x58: *bx = -0x100; *by = -0x100; break;
    case 0x59: *bx = 0x100; *by = -0x100; break;
    case 0x5a: *bx = -0x100; *by = 0x100; break;
    case 0x5b: *bx = 0x100; *by = 0x100; break;
    default:
        player_damage(OBJ_LEVEL, FATAL);
        /* The original left the bounce uninitialised here; use none. */
        *bx = *by = 0;
        break;
    }
}

/* Type of an object: the start of the slot range it falls in (0x563b0). */
int obj_type(int slot)
{
    static const int16_t bases[] = {
        0x0, 0x3, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0x10, 0x17, 0x1c,
        0x1e, 0x23, 0x27, 0x2b, 0x35, 0x37, 0x3c, 0x3d, 0x40, 0x48, 0x4a, 0x4f, 0x55, 0x59,
        0x67, 0x6e, 0x71, 0x72, 0x76, 0x82, 0x87, 0x93, 0x94, 0x97, 0xa0, 0xb2, 0xb3, 0xba,
        0xbe, 0xc5, 0xcb, 0xce, 0xd8, 0x114, 0x118, 0x168, 0x19a, 0x1ba, 0x1f6, 0x25a, 0x28c,
    };
    int t = 0;
    for (size_t i = 0; i < sizeof bases / sizeof bases[0] && bases[i] <= slot; i++) t = bases[i];
    return t;
}

/* The object part of 0x1e888: one collision per frame with objects
 * 0x10-0xd7, by type. (x, y, my) are the player's position and vertical
 * motion as they were at the start of 0x1e888. */
static void player_hits_objects(int32_t x, int32_t y, int32_t my)
{
    int32_t mf = g_state.mission_frames;
    for (int o = 0xf;;) {
        o = collide_sprites(SLOT_PLAYER, o + 1, 0xd7);
        if (o == -1) return;
        int32_t ox = obj_x(o), oy = obj_y(o);
        (void)obj_motion_x(o);
        int32_t omy = obj_motion_y(o);
        int32_t dx = x - ox, dy = y - oy;
        int32_t mx2 = x - sar(dx, 1), my2 = y - sar(dy, 1);   /* halfway between */
        bool recent = mf - 30 <= LAST_HIT(o);
        switch (obj_type(o)) {
        case 0x10: case 0x17: case 0x1c: case 0x1e:          /* destroyed on contact */
            effect_spawn(0x10a, false, obj_x(o), obj_y(o));
            obj_kill(o);
            score_add(true, 0x19);
            player_hit(o, 0x3c, mx2, my2, sar(dx, 4), sar(dy, 4));
            return;
        case 0x23: hanger_hit(o, 0x1e); player_hit(o, 0x32, mx2, my2, sar(dx, 4), sar(dy, 4)); return;
        case 0x27: crawler_hit(o, 0x28); player_hit(o, 0x32, mx2, my2, sar(dx, 4), sar(dy, 4)); return;
        case 0x2b:
            obj_kill(o);
            player_hit(o, 0x1e, ox, oy, sar(x - ox, 4), sar(omy, 1));
            return;
        case 0x37:
            if (recent) return;
            player_hit(o, 0x28, mx2, y, ox < x ? 0xfa : -0xfa, my - 0xfa);
            return;
        case 0x3c: core_hit(0x1e); player_hit(o, 0x1e, mx2, my2, sar(dx, 4), sar(dy, 4)); return;
        case 0x3d: case 0x40: case 0x48: case 0x4a: case 0x4f:
            turret_hit(o, 0x1e);
            player_hit(o, 0x28, mx2, my2, sar(dx, 4), sar(dy, 4));
            return;
        case 0x55: strider_hit(o, 10000); player_hit(o, 0x32, mx2, my2, sar(dx, 4), sar(dy, 4)); return;
        case 0x59: pod_hit(o); player_hit(o, 0x28, mx2, my2, sar(dx, 4), sar(dy, 4)); return;
        case 0x67:
            if (recent) return;
            worm_hit(o, 0x1e);
            player_hit(o, 0x1e, mx2, my2, sar(dx, 2), sar(dy, 2));
            return;
        case 0x6e:
            obj_set_velocity(o, sar(ox - x, 6), sar(oy - y, 6));
            drone_hit(o, 0x1e);
            player_hit(o, 0x32, mx2, my2, sar(dx, 4), sar(dy, 4));
            return;
        case 0x71:
            boss15_hit(o, 0x1e);
            player_hit(o, 0x28, x - sar(dx, 2), y - sar(dy, 2), sar(dx, 2), sar(dy, 2));
            return;
        case 0x72: rock_bounce(o); seeker_hit(o, 0x1e); player_hit(o, 0x1e, mx2, my2, sar(dx, 4), sar(dy, 4)); return;
        case 0x76: rock_bounce(o); seeker_small_hit(o, 0x1e); player_hit(o, 0x14, mx2, my2, sar(dx, 4), sar(dy, 4)); return;
        case 0x82:
            if (recent) return;
            walker_hit(o, 0x1e);
            player_hit(o, 0x1e, mx2, my2, sar(dx, 3), sar(dy, 3));
            return;
        case 0x87:
            if (recent) return;
            orbiter_hit(o, 10000);
            player_hit(o, 0x1e, mx2, my2, sar(dx, 2), sar(dy, 2));
            return;
        case 0x93:
            giant_rock_hit(o, 0x1e);
            player_hit(o, 0x32, x - dx / 6, y - dy / 6, sar(dx, 2), sar(dy, 2));
            return;
        case 0x94: rock_bounce(o); rock_large_hit(o, 0x1e); player_hit(o, 0x32, mx2, my2, sar(dx, 5), sar(dy, 5)); return;
        case 0x97: rock_bounce(o); rock_medium_hit(o, 0x1e); player_hit(o, 0x28, mx2, my2, sar(dx, 4), sar(dy, 4)); return;
        case 0xa0: rock_bounce(o); rock_small_hit(o, 0x1e); player_hit(o, 0x23, mx2, my2, sar(dx, 4), sar(dy, 4)); return;
        case 0xb2:
            boss19_hit(0x1e);
            player_hit(o, 0x4b, x - sar(dx, 2), y - sar(dy, 2), sar(dx, 2), sar(dy, 2));
            return;
        case 0xb3:
            if (recent) return;
            chain_hit(o, 0x1e);
            player_hit(o, 0x28, mx2, my2, sar(dx, 3), sar(dy, 3));
            return;
        case 0xba: case 0xc5: case 0xcb:                     /* bounce off */
            if (obj_left(o) <= x && x <= obj_right(o)) {
                g_state.f_015++;
                player_hit(o, 0x1e, x, my2, g_random_table[g_state.f_015 & 0xff] % 200 - 100,
                           dy < 0 ? -0x200 : 0x200);
            } else {
                player_hit(o, 0x1e, mx2, my2, dx < 0 ? -0x80 : 0x80, dy < 0 ? -0x100 : 0x100);
            }
            return;
        case 0xbe:                                            /* the fixed objects 0xbe-0xc4 */
            player_hit(o, 0x1e, mx2, my2, sar(dx, 4), sar(dy, 4));
            switch (o) {
            case 0xbe: group37_switch_hit(); break;
            case 0xbf: case 0xc0: gate_switch_hit(); break;
            case 0xc1: switch_c1_hit(); break;
            case 0xc2: switch_c2_hit(); break;
            case 0xc3: switch_c3_hit(); break;
            case 0xc4: switch_c4_hit(); break;
            default: break;
            }
            return;
        case 0xce: pickup_collect(o); return;                 /* pickup */
        default: break;                                       /* not solid: check the next one */
        }
    }
}

static int32_t sq_dist_px(int a, int b)
{
    int32_t dx = sar(obj_x(a) - obj_x(b), 8), dy = sar(obj_y(a) - obj_y(b), 8);
    return dx * dx + dy * dy;
}

void player_hits(void)
{
    int32_t x = obj_x(SLOT_PLAYER), y = obj_y(SLOT_PLAYER);
    int32_t mx = obj_motion_x(SLOT_PLAYER), my = obj_motion_y(SLOT_PLAYER);

    /* Tow stages: 0 free, 1 beam attached (within 30 px), 2 towing (line taut). */
    if (g_state.stage == 0) {
        if (obj_alive(OBJ_TOW) && sq_dist_px(SLOT_PLAYER, OBJ_TOW) < 900) g_state.stage = 1;
    } else if (g_state.stage == 1) {
        if (sq_dist_px(SLOT_PLAYER, OBJ_TOW) > 0x63f) {
            g_state.stage = 2;
            double a = (double)(int32_t)(y - obj_y(OBJ_TOW));
            double b = (double)(int32_t)(obj_x(OBJ_TOW) - x);
            g_state.tow_angle = atan2(b, a);   /* fpatan: angle of the tow line, 0 = up */
            if (g_state.obj[OBJ_TOW].o_0 != -1) {
                hud_message(0x8c, MSG_SELF_DESTRUCT);
                sound_play(0);
                obj_set_callback(OBJ_TOW, 0, CB_10db8, 0x46);
            }
        }
    } else if (g_state.stage == 2) {
        tow_update();
    }

    if (g.invulnerable) return;

    int32_t cx, cy;
    if (collide_sprite_tiles(SLOT_PLAYER, OBJ_LEVEL, &cx, &cy)) {
        int32_t bx, by;
        bounce_direction(cx, cy, &bx, &by);
        player_hit(OBJ_LEVEL, my * my + mx * mx < 0x240001 ? 20 : FATAL, cx, cy, bx, by);
    }
    if (obj_alive(12) && collide_sprite_tiles(SLOT_PLAYER, 12, &cx, &cy)) {
        /* Object 12 moves: harmless-ish while rising, fatal while descending. */
        if (obj_motion_y(12) < 1)
            player_hit(12, 0x1e, cx, cy, obj_x(12) < x ? 400 : -400, -my);
        else
            player_damage(12, FATAL);
        return;
    }
    if (obj_alive(11) && collide_point(11, 11, obj_x(SLOT_PLAYER), obj_y(SLOT_PLAYER)) != -1) {
        player_damage(11, FATAL);
        return;
    }
    player_hits_objects(x, y, my);
}
