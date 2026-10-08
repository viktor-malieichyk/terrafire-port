#include "weapons.h"

#include <math.h>
#include <stddef.h>

#include "anims.h"
#include "bonus.h"
#include "callbacks.h"
#include "collision.h"
#include "effects.h"
#include "enemies.h"
#include "game.h"
#include "hits.h"
#include "objects.h"
#include "pickups.h"
#include "render.h"
#include "res.h"
#include "tables.h"
#include "tow.h"
#include "sound.h"
#include "unported.h"

static int32_t sar(int32_t v, int n) { return v >= 0 ? v >> n : ~(~v >> n); }
static int norm_angle(int32_t a) { return a < 0 ? 360 - (-a) % 360 : a % 360; }
#define HP(slot) (g_state.obj[(slot)].o_0)

/* 0x2a9e0 */
static bool point_on_screen(int32_t x, int32_t y)
{
    int sx = sar(x, 8) - g_view.cam_x, sy = sar(y, 8) - g_view.cam_y;
    return g_view.clip_l <= sx && sx <= g_view.clip_r && g_view.clip_t <= sy && sy <= g_view.clip_b;
}

/* 0x2c728 / 0x2c77c with point 0: the object's first collision-quad point. */
int32_t quad_x(int slot, int k)
{
    const Object *o = &g_objects[slot];
    return o->x + g_banks[o->bank].view[o->orient].quad_x[k];
}
int32_t quad_y(int slot, int k)
{
    const Object *o = &g_objects[slot];
    return o->y + g_banks[o->bank].view[o->orient].quad_y[k];
}
int32_t quad_x0(int slot)
{
    const Object *o = &g_objects[slot];
    return o->x + g_banks[o->bank].view[o->orient].quad_x[0];
}
int32_t quad_y0(int slot)
{
    const Object *o = &g_objects[slot];
    return o->y + g_banks[o->bank].view[o->orient].quad_y[0];
}

/* Shared by 0x18fc8 / 0x1923c: one of nine rotation frames per quadrant. */
static void spawn_rotated(int slot, int bank_base, int32_t x, int32_t y, int ang)
{
    int f = ang / 10, frame = f % 9;
    if (f % 18 > 8) frame = 9 - frame;
    obj_spawn(slot, frame + bank_base, x, y, 0xc);
    if (f > 8) obj_set_orient(slot, f < 18 ? 6 : f < 27 ? 2 : 4);
}

/* 0x18fc8 */
void bullet_spawn(int bank_base, int sound, int32_t x, int32_t y, int32_t vx0, int32_t vy0,
                         int32_t speed, int32_t angle, int cb, int32_t life)
{
    int b = obj_find_free(0xd8, 0x113, false);
    if (b == -1) return;
    int ang = norm_angle(angle);
    spawn_rotated(b, bank_base, x, y, ang);
    obj_set_velocity(b, vx0 + sar(speed * g_trig_a[ang], 3), sar(speed * g_trig_b[ang], 3) + vy0);
    HP(b) = life;
    g_state.obj[b].o_4 = 0;
    obj_set_callback(b, 0, cb, 3);
    sound_play(point_on_screen(x, y) ? sound + 1 : sound);
    if (!g.full_rate) return;
    /* Muzzle flash riding on the bullet (70 Hz mode only). */
    int m = obj_find_free(0x1ba, 0x1f5, false);
    if (m == -1) return;
    obj_spawn(m, 0, x, y, 10);
    obj_attach(false, m, -1, b, -1);
    int anim;
    switch (bank_base) {
    case 0x514: anim = ANIM_4abf0; break;
    case 0x51e: anim = ANIM_4ac68; break;
    case 0x528: case 0x550: anim = ANIM_4ac18; break;
    case 0x532: case 0x55a: anim = ANIM_4ac90; break;
    case 0x53c: case 0x564: anim = ANIM_4ac40; break;
    case 0x546: case 0x56e: anim = ANIM_4acb8; break;
    default: return;
    }
    obj_set_anim(m, anim, 0);
}

/* 0x1923c: side projectile; accelerates along its heading plus some of the ship's motion. */
static void side_spawn(int32_t x, int32_t y, int32_t vx, int32_t vy, int32_t angle)
{
    int s = obj_find_free(0xd8, 0x113, false);
    if (s == -1) return;
    int ang = norm_angle(angle);
    spawn_rotated(s, 0x580, x, y, ang);
    obj_set_velocity(s, vx, vy);
    int32_t ay = g_trig_b[ang], my = obj_motion_y(SLOT_PLAYER);
    int32_t ax = g_trig_a[ang], mx = obj_motion_x(SLOT_PLAYER);
    g_objects[s].ax = ax / 0x1e + mx / 0x3c;
    g_objects[s].ay = ay / 0x1e + my / 0x3c;
    HP(s) = 0x50;
    g_state.obj[s].o_4 = 0;
    obj_set_callback(s, 0, CB_194e4, 1);
    obj_set_callback(s, 1, CB_193dc, 1);
}

void player_fire(void)
{
    int32_t mf = g_state.mission_frames;
    if (!(g.controls & 8) || mf < g_state.last_shot + 0xd) return;
    g_state.last_shot = mf;
    int w = g_state.weapon;
    int sound = (w & 1) == 0 ? 0x15 : 0x13;
    g_state.barrel++;
    int bank = g_weapon_banks[w * 3 + (g_state.barrel & 1)];
    int32_t a = g_state.angle;
    bullet_spawn(bank, sound, quad_x0(SLOT_PLAYER), quad_y0(SLOT_PLAYER), obj_motion_x(SLOT_PLAYER),
                 obj_motion_y(SLOT_PLAYER), 8, a, CB_194e4, 0x32);
    if (w > 14) {   /* three-way spread */
        bullet_spawn(bank, sound, quad_x0(SLOT_PLAYER), quad_y0(SLOT_PLAYER), obj_motion_x(SLOT_PLAYER),
                     obj_motion_y(SLOT_PLAYER), 8, (a + 350) % 360, CB_194e4, 0x32);
        bullet_spawn(bank, sound, quad_x0(SLOT_PLAYER), quad_y0(SLOT_PLAYER), obj_motion_x(SLOT_PLAYER),
                     obj_motion_y(SLOT_PLAYER), 8, (a + 10) % 360, CB_194e4, 0x32);
    }
    if (g_state.side_ammo > 0 && g_state.last_side_shot < mf - 0x28) {
        g_state.side_ammo -= 2;
        g_state.last_side_shot = mf;
        for (int k = 0; k < 2; k++) {
            int s = norm_angle(k == 0 ? a + 0xa0 : a - 0xa0);
            side_spawn(obj_x(SLOT_PLAYER) - g_trig_a[a], obj_y(SLOT_PLAYER) - g_trig_b[a],
                       sar(g_trig_a[s], 1) + obj_motion_x(SLOT_PLAYER),
                       sar(g_trig_b[s], 1) + obj_motion_y(SLOT_PLAYER), a);
        }
        sound_play(0xe);
    }
}

void weapon_set(int level)
{
    if (level > 0x10) level = 0x11;
    g_state.weapon = level;
    g_state.side_ammo += g_weapon_banks[level * 3 + 2];
    if (g_state.side_ammo > 100) g_state.side_ammo = 100;
}

/* Aimed with x87 maths: angle = trunc(atan2(dx, -dy) * 57.2957795), the
 * original's own (rounded) degrees-per-radian constant. The original's x87
 * runs at 53-bit precision (control word 0x127f), so double matches it. */
void aimed_shot(int shooter)
{
    int b = obj_find_free(0xd8, 0x113, false);
    if (b == -1) return;
    obj_spawn(b, 0x578, quad_x0(shooter), quad_y0(shooter), g_objects[shooter].z + 1);
    obj_set_anim(b, ANIM_4ab20, 0);
    double a = (double)(int32_t)-(obj_y(SLOT_PLAYER) - obj_y(b));
    double c = (double)(int32_t)(obj_x(SLOT_PLAYER) - obj_x(b));
    int32_t deg = (int32_t)(atan2(c, a) * 57.2957795);
    int ang = deg < 0 ? 360 - (-deg) % 360 : deg % 360;
    obj_set_velocity(b, g_trig_a[ang] / 3, g_trig_b[ang] / 3);
    HP(b) = 300;
    g_state.obj[b].o_4 = 0;
    obj_set_callback(b, 0, CB_194e4, 1);
    sound_play(0x12);
}

int bullet_damage(int bullet)
{
    uint32_t b = (uint32_t)obj_sprite_bank(bullet);
    if (b < 0x51e) return 0x14;
    if (b < 0x528) return 0x18;
    if (b < 0x532) return 0x20;
    if (b <= 0x53b) return 0x28;
    if (b < 0x546) return 0x38;
    if (b < 0x550) return 0x34;
    if (b < 0x55a) return 0x1c;
    if (b < 0x564) return 0x24;
    if (b < 0x56e) return 0x2c;
    if (b < 0x578) return 0x30;
    if (b > 0x57f && b < 0x58a) return 0x1e;
    return 0x28;
}

void enemy_destroyed(int slot)
{
    effect_spawn(0x10a, false, obj_x(slot), obj_y(slot));
    obj_kill(slot);
    if (!g_state.f_014) g_state.score += (g_state.difficulty + 1) * 0x19;
}

void cb_side_trail(int slot)
{
    int32_t my = obj_motion_y(slot), mx = obj_motion_x(slot);
    obj_set_velocity(slot, (mx * 0x3ca) / 1000, (my * 0x3ca) / 1000);
    obj_set_callback(slot, 1, CB_193dc, 1);
    if (!g.debris) return;
    int t = obj_find_free(0x19a, 0x1b9, false);
    if (t == -1) return;
    obj_spawn(t, 0x598, quad_x0(slot), quad_y0(slot), 0xb);
    obj_set_anim(t, ANIM_4ad00, 0);
    obj_set_velocity(t, sar(obj_motion_x(slot), 2), sar(obj_motion_y(slot), 2));
    obj_set_callback(t, 0, CB_2c118, g_anims[ANIM_4ad00].count - 1);
}

/* 0x18dc0: an enemy bullet. Removed (with an impact effect) on touching the
 * level or the player; hitting the player knocks and damages it, and during
 * towing it can push the towed object. */
void cb_enemy_bullet(int b)
{
    if (--HP(b) == 0) {
        if (g_objects[b].anim == ANIM_4ab20) {
            obj_set_anim(b, ANIM_4abc8, 0);
            obj_set_callback(b, 0, CB_2c118, g_anims[ANIM_4abc8].count);
            return;
        }
        obj_kill(b);
        return;
    }
    obj_set_callback(b, 0, CB_18dc0, 1);
    if (!collide_sprite_tiles(b, 10, NULL, NULL)) {
        if (g.invulnerable) return;
        if (collide_sprites(b, SLOT_PLAYER, SLOT_PLAYER) != -1) {
            obj_set_velocity(SLOT_PLAYER, sar(obj_motion_x(b), 2), sar(obj_motion_y(b), 2));
            effect_spawn(0x158, false, obj_x(b), obj_y(b));
            player_damage(b, bullet_damage(b));
            obj_kill(b);
            return;
        }
        if (collide_sprites(b, 15, 15) == -1) return;
        if (g_state.stage == 2) {
            obj_set_velocity(15, sar(obj_motion_x(b), 2), sar(obj_motion_y(b), 2));
            tow_damage(b, bullet_damage(b));
        }
    }
    effect_spawn(0x158, false, obj_x(b), obj_y(b));
    obj_kill(b);
}

/* 0x194e4. The bullet's next position is tested; on a hit it usually makes
 * an impact effect there, damages what it hit and is removed. */
void cb_bullet(int b)
{
    int32_t bx = obj_x(b) + obj_motion_x(b), by = obj_y(b) + obj_motion_y(b);
    if (--HP(b) == 0) {
        /* Out of life: aimed shots fade out, everything else is removed. */
        if (g_objects[b].anim == ANIM_4ab20) {
            obj_set_anim(b, ANIM_4abc8, 0);
            obj_set_callback(b, 0, CB_2c118, g_anims[ANIM_4abc8].count);
            return;
        }
        obj_kill(b);
        return;
    }
    obj_set_callback(b, 0, CB_194e4, 1);
    if (collide_sprite_tiles(b, 10, NULL, NULL) ||
        (obj_alive(12) && collide_point(12, 12, bx, by) != -1)) {
        effect_spawn(0x158, false, bx, by);
        obj_kill(b);
        return;
    }
    if (obj_alive(11) && collide_point(11, 11, bx, by) != -1) {
        obj_kill(b);
        effect_spawn(0x158, false, bx, by);
        bool seen = point_on_screen(bx, by);
        sound_stop(seen ? 7 : 6);
        sound_play(seen ? 0x1e : 0x1d);
        return;
    }
    int o = collide_sprites(b, 0xe, 0xca);
    if (o == -1) return;
    bool lingering = g_objects[b].anim == ANIM_4ab20 && HP(b) > 0x103;
#define HIT(handler) do { effect_spawn(0x158, false, bx, by); handler(o, bullet_damage(b)); obj_kill(b); return; } while (0)
    switch (obj_type(o)) {
    case 0x0e:                                            /* the player */
        if (g.invulnerable) return;
        { int bk = obj_sprite_bank(b); if (bk > 0x57f && bk < 0x58a) return; }   /* own side projectiles */
        obj_set_velocity(SLOT_PLAYER, sar(obj_motion_x(b), 2), sar(obj_motion_y(b), 2));
        effect_spawn(0x158, false, obj_x(b), obj_y(b));
        player_damage(b, bullet_damage(b));
        obj_kill(b);
        return;
    case 0x0f:                                            /* the towed object */
        if (g.invulnerable) return;
        effect_spawn(0x158, false, bx, by);
        if (g_state.stage == 2) {
            obj_set_velocity(15, sar(obj_motion_x(b), 2), sar(obj_motion_y(b), 2));
            tow_damage(b, bullet_damage(b));
        }
        obj_kill(b);
        return;
    case 0x10: case 0x17: case 0x1c:                      /* enemies with hit points */
        effect_spawn(0x158, false, bx, by);
        obj_kill(b);
        if (obj_type(o) == 0x17) cb_mine_chase(o);   /* a hit wakes it */
        if (obj_type(o) == 0x1c) homer_hit(o);
        HP(o) -= bullet_damage(b);
        if (HP(o) <= 0) enemy_destroyed(o);
        return;
    case 0x1e: case 0x2b: case 0xba:                      /* absorbs the bullet */
        effect_spawn(0x158, false, bx, by);
        obj_kill(b);
        return;
    case 0x23: HIT(hanger_hit);
    case 0x27: if (lingering) return; HIT(crawler_hit);
    case 0x3c:
        effect_spawn(0x158, false, bx, by);
        core_hit(bullet_damage(b));
        obj_kill(b);
        return;
    case 0x4a: if (lingering) return; HIT(turret_hit);
    case 0x3d: case 0x40: case 0x48: case 0x4f: HIT(turret_hit);
    case 0x55: HIT(strider_hit);
    case 0x59:
        effect_spawn(0x158, false, bx, by);
        pod_hit(o);
        obj_kill(b);
        return;
    case 0x67: HIT(worm_hit);
    case 0x6e: HIT(drone_hit);
    case 0x71: HIT(boss15_hit);
    case 0x72: HIT(seeker_hit);
    case 0x76: HIT(seeker_small_hit);
    case 0x82: HIT(walker_hit);
    case 0x87: HIT(orbiter_hit);
    case 0x93: HIT(giant_rock_hit);
    case 0x94: HIT(rock_large_hit);
    case 0x97: HIT(rock_medium_hit);
    case 0xa0: HIT(rock_small_hit);
    case 0xb2:
        effect_spawn(0x158, false, bx, by);
        boss19_hit(bullet_damage(b));
        obj_kill(b);
        return;
    case 0xb3: HIT(chain_hit);
    case 0xbe:                                            /* fixed objects 0xbe-0xc4 */
        effect_spawn(0x158, false, bx, by);
        switch (o) {
        case 0xbe: group37_switch_hit(); break;
        case 0xbf: case 0xc0: gate_switch_hit(); break;
        case 0xc1: switch_c1_hit(); break;
        case 0xc2: switch_c2_hit(); break;
        case 0xc3: switch_c3_hit(); break;
        case 0xc4: switch_c4_hit(); break;
        default: break;
        }
        obj_kill(b);
        return;
    case 0xc5: {                                          /* switches its partner on for a while */
        effect_spawn(0x158, false, bx, by);
        obj_kill(b);
        obj_clear_anim(o);
        obj_set_callback(o, 0, CB_143a0, 900);
        int partner = ((o - 0xc5) >> 1) + 0xcb;
        obj_set_callback(partner, 0, CB_14370, 1000);
        if (g_objects[partner].anim != ANIM_47988) obj_set_anim(partner, ANIM_47988, 0);
        return;
    }
    default:
        return;                                           /* passes through */
    }
#undef HIT
}

void score_add(bool scaled, int32_t n)
{
    if (g_state.f_014) return;
    g_state.score += scaled ? (g_state.difficulty + 1) * n : n;
}
