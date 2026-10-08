#include "bonus.h"

#include <math.h>
#include <stdbool.h>

#include "anims.h"
#include "callbacks.h"
#include "collision.h"
#include "effects.h"
#include "pickups.h"
#include "render.h"
#include "game.h"
#include "objects.h"
#include "res.h"
#include "spawn.h"
#include "tables.h"
#include "unported.h"

#define ARENA_X0 50000
#define ARENA_Y0 50000
#define ARENA_X1 0x20250
#define ARENA_Y1 0x18a50

static int32_t next_random(void)
{
    g_state.f_015++;
    return g_random_table[g_state.f_015 & 0xff];
}

static void load_banks(int first, int entry, int last)
{
    if (!res_bank_loaded(first)) res_load_banks(entry, first, last, false);
}

void cb_wrap(int slot)
{
    obj_set_callback(slot, 1, CB_1038c, 1);
    if (obj_right(slot) < ARENA_X0 && obj_motion_x(slot) < 0)
        obj_set_pos(slot, obj_x(slot) + ARENA_X1 - obj_left(slot), obj_y(slot));
    else if (obj_left(slot) > ARENA_X1 && obj_motion_x(slot) > 0)
        obj_set_pos(slot, ARENA_X0 - (obj_right(slot) - obj_x(slot)), obj_y(slot));
    if (obj_bottom(slot) < ARENA_Y0 && obj_motion_y(slot) < 0)
        obj_set_pos(slot, obj_x(slot), obj_y(slot) + ARENA_Y1 - obj_top(slot));
    else if (obj_top(slot) > ARENA_Y1 && obj_motion_y(slot) > 0)
        obj_set_pos(slot, obj_x(slot), ARENA_Y0 - (obj_bottom(slot) - obj_y(slot)));
}

/* 0x1ffb4: the player in the middle of the arena, plus the life indicators. */
static void player_setup(void)
{
    if (!res_bank_loaded(0x730)) {
        static const int sets[8][3] = {
            { 0x5e, 0x730, 0x7d7 }, { 0x40, 0x10a, 0x172 }, { 0x46, 0x2c6, 0x2cf },
            { 0x55, 0x514, 0x5ae }, { 0x59, 0x5c1, 0x5d9 }, { 0x5b, 0x67e, 0x691 },
            { 0x5f, 0x7d8, 0x842 }, { 0x61, 0x8e1, 0x8f2 },
        };
        for (int i = 0; i < 8; i++) res_load_banks(sets[i][0], sets[i][1], sets[i][2], false);
    }
    obj_spawn(SLOT_PLAYER, 0x730, 0x16350, 0x12750, 0x1c);
    obj_set_anim(SLOT_PLAYER, ANIM_4fcb8, 0);
    obj_set_callback(SLOT_PLAYER, 0, CB_20318, 5);
    obj_set_callback(SLOT_PLAYER, 1, CB_1038c, 5);
    g_state.last_shot = 0;
    g_state.last_side_shot = 0;
    g_state.obj[SLOT_BONUS_TIMER].o_0 = 1;
    for (int s = 0x114; s < g_state.f_00c + 0x113; s++) life_indicator_spawn(s);
}

/* ------------------------------------------------------------ chain creature */

/* o_8 low byte: 0 = head, 1 = follows the segment before it. */
static bool follower(int slot) { return (g_state.obj[slot].o_8 & 0xff) != 0; }

/* A follower repeats the turn the segment ahead is making, with that
 * segment's next turn. */
static void follow(int slot)
{
    const Object *p = &g_objects[slot - 1];
    obj_set_anim(slot, p->anim, 0);
    int frames = g_objects[slot].anim >= 0 ? g_anims[g_objects[slot].anim].count : 0;
    if (p->cb_timer[0] != -1) obj_set_callback(slot, 0, p->cb[0], frames);   /* 0x2aad8 */
    else obj_set_callback(slot, 0, 0, -1);
}

/* The head turns towards the player (`towards`), except one time in three or
 * when it runs into a later segment. */
static void head_turn(int slot, bool towards, int anim_a, int cb_a, int anim_b, int cb_b)
{
    if (next_random() % 3 == 0 || collide_sprites(slot, slot + 1, 0xb9) != -1) towards = !towards;
    int anim = towards ? anim_a : anim_b;
    obj_set_anim(slot, anim, 0);
    obj_set_callback(slot, 0, towards ? cb_a : cb_b, g_anims[anim].count);
}

void cb_chain_down(int slot)
{
    if (follower(slot)) { follow(slot); return; }
    head_turn(slot, obj_y(slot) < obj_y(SLOT_PLAYER), ANIM_5608f, CB_25234, ANIM_560ab, CB_254f8);
}

void cb_chain_left(int slot)
{
    if (follower(slot)) { follow(slot); return; }
    head_turn(slot, obj_x(SLOT_PLAYER) < obj_x(slot), ANIM_56096, CB_25320, ANIM_560b2, CB_2540c);
}

void cb_chain_right(int slot)
{
    if (follower(slot)) { follow(slot); return; }
    head_turn(slot, obj_x(slot) < obj_x(SLOT_PLAYER), ANIM_56088, CB_2540c, ANIM_560a4, CB_25320);
}

void cb_chain_up(int slot)
{
    if (follower(slot)) { follow(slot); return; }
    head_turn(slot, obj_y(SLOT_PLAYER) < obj_y(slot), ANIM_5609d, CB_254f8, ANIM_560b9, CB_25234);
}

/* Appears once its delay is up; wraps around a slightly smaller arena. */
void cb_chain_wrap(int slot)
{
    if (g_objects[slot].frozen) obj_unfreeze(slot);
    if (obj_x(slot) < 0xb450) obj_move(slot, 0x15d00, 0);
    else if (obj_x(slot) > 0x21150) obj_move(slot, -0x15d00, 0);
    if (obj_y(slot) < 0xb450) obj_move(slot, 0, 0xe500);
    else if (obj_y(slot) > 0x19950) obj_move(slot, 0, -0xe500);
    obj_set_callback(slot, 1, CB_256f0, 1);
}

/* 0x255e4: n segments at (x, y), appearing 0x20 frames apart from `delay`. */
static void chain_spawn(int n, int32_t x, int32_t y, int delay)
{
    load_banks(0x944, 0x68, 0x957);
    bool first = true;
    for (int t = delay + 1, end = t + n * 0x20; t < end; t += 0x20, delay += 0x20) {
        int slot = obj_find_free(0xb3, 0xb9, true);
        obj_spawn(slot, 0x944, x, y, 0x1a);
        obj_set_velocity(slot, 0x10, 0x10);
        g_state.obj[slot].o_0 = 200;
        obj_freeze(slot);
        g_state.obj[slot].o_8 = (g_state.obj[slot].o_8 & ~0xff) | (first ? 0 : 1);
        first = false;
        obj_set_callback(slot, 0, CB_2540c, t);
        obj_set_callback(slot, 1, CB_256f0, delay);
    }
}

/* 0x216c4: a drifting, wrapping object (0x94-0x96) appearing after `delay`. */
static void drifter_spawn(int32_t x, int32_t y, int32_t vx, int32_t vy, int delay)
{
    load_banks(0x843, 0x60, 0x8e0);
    int slot = obj_find_free(0x94, 0x96, true);
    obj_spawn(slot, 0x875, x, y, 0x19);
    obj_set_anim(slot, ANIM_52b80, (int16_t)(next_random() % g_anims[ANIM_52b80].count));
    obj_set_velocity(slot, vx, vy);
    obj_freeze(slot);
    g_state.obj[slot].o_0 = 0xfa;
    obj_set_callback(slot, 1, CB_1038c, 1);
    obj_set_callback(slot, 0, CB_2aaa0, delay);
}

#define FATAL 10000
#define LAST_HIT(slot) (g_state.obj[(slot)].o_4)

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }
static int wrap360(int32_t a) { return a < 0 ? 360 - iabs(a) % 360 : a % 360; }

static void add_score(int32_t per_level)
{
    if (!g_state.f_014) g_state.score += (g_state.difficulty + 1) * per_level;
}

/* Damage applies once per frame pair, like the player's (unless fatal). */
static bool hit_counts(int slot, int damage)
{
    int32_t mf = g_state.mission_frames;
    return damage == FATAL || LAST_HIT(slot) < mf - 1 || LAST_HIT(SLOT_PLAYER) < mf - 1;
}

/* 0x21344: scatter n pickups (2 or 3 at a random starting angle; 5 = all
 * shields). Each is shields (1 in 4*difficulty+20), else 0x825 / 0x816. */
static void drop_pickups(int n, int32_t x, int32_t y)
{
    int32_t dir;
    int32_t r = g_random_table[(g_state.f_015 + 1) & 0xff];
    if (n == 2) { dir = (r % 6) * 0x1e + 0xf; g_state.f_015++; }
    else if (n == 3) { dir = (r % 4) * 0x1e + 0xf; g_state.f_015++; }
    else if (n == 5) dir = 9;
    else return;
    for (int i = 0; i < n; i++) {
        int contents;
        if (n == 5 || next_random() % (g_state.difficulty * 4 + 0x14) == 0) contents = 0x807;
        else contents = (i & 1) == 0 ? 0x825 : 0x816;
        pickup_spawn(contents, x, y, dir);
        dir += 0x168 / n;
    }
}

void chain_hit(int slot, int damage)
{
    if (!hit_counts(slot, damage)) return;
    int32_t hp = g_state.obj[slot].o_0 - damage;
    g_state.obj[slot].o_0 = hp < 0 ? 0 : hp;
    if (g_state.obj[slot].o_0 != 0) return;
    add_score(0x15e);
    effect_spawn(0x148, true, obj_x(slot), obj_y(slot));
    drop_pickups(2, obj_x(slot), obj_y(slot));
    obj_kill(slot);
    if (slot < 0xb9) g_state.obj[slot + 1].o_8 &= ~0xff;   /* the next segment leads */
}

void rock_bounce(int slot)
{
    int32_t a = iabs(obj_motion_y(slot)), b = iabs(obj_motion_x(slot));
    if (obj_x(slot) < obj_x(SLOT_PLAYER)) a = -a;
    if (obj_y(slot) < obj_y(SLOT_PLAYER)) b = -b;
    obj_set_velocity(slot, a, b);
}

static bool rock_damage(int slot, int damage)
{
    if (!hit_counts(slot, damage)) return false;
    g_state.obj[slot].o_0 -= damage;
    return g_state.obj[slot].o_0 < 1;
}

/* 0x21cfc / 0x21e5c: medium and small rocks. */
static void rock_spawn(int lo, int hi, int bank, int anim, int hp, int32_t x, int32_t y, int32_t vx, int32_t vy)
{
    int slot = obj_find_free(lo, hi, true);
    obj_spawn(slot, bank, x, y, 0x19);
    obj_set_anim(slot, anim, (int16_t)(next_random() % g_anims[anim].count));
    obj_set_velocity(slot, vx, vy);
    obj_set_callback(slot, 1, CB_1038c, 1);
    g_state.obj[slot].o_0 = hp;
    if (slot & 1) obj_set_orient(slot, 2);
}

void rock_large_hit(int slot, int damage)
{
    if (!rock_damage(slot, damage)) return;
    add_score(0x96);
    drop_pickups(3, obj_x(slot), obj_y(slot));
    int32_t a = next_random() % 0x32 + 0x14, div = (slot & 3) + 5;
    for (int i = 0; i < 3; i++) {
        int k = wrap360(a);
        rock_spawn(0x97, 0x9f, 0x899, ANIM_53008, 0xb4, obj_x(slot), obj_y(slot),
                   g_trig_a[k] / div, g_trig_b[k] / div);
        int32_t next = a + 0x78;
        if ((next > 0xa9 && next < 0xbf) || (next > 0x103 && next < 0x119)) next = a + 0x8c;
        a = next;
    }
    effect_spawn(0x129, true, obj_x(slot), obj_y(slot));
    obj_kill(slot);
}

void rock_medium_hit(int slot, int damage)
{
    if (!rock_damage(slot, damage)) return;
    add_score(0x4b);
    drop_pickups(2, obj_x(slot), obj_y(slot));
    int32_t a = next_random() % 0x32 + 0x14 + (slot & 1) * 0x5a, div = (slot & 1) + 8;
    for (int i = 0; i < 2; i++) {
        int k = wrap360(a);
        a += 0xb4;
        rock_spawn(0xa0, 0xb1, 0x8bd, ANIM_53490, 0x78, obj_x(slot), obj_y(slot),
                   g_trig_a[k] / div, g_trig_b[k] / div);
    }
    effect_spawn(0x148, true, obj_x(slot), obj_y(slot));
    obj_kill(slot);
}

void rock_small_hit(int slot, int damage)
{
    if (!rock_damage(slot, damage)) return;
    effect_spawn(0x158, true, obj_x(slot), obj_y(slot));
    obj_kill(slot);
    add_score(0x32);
}

void giant_rock_hit(int slot, int damage)
{
    if (!rock_damage(slot, damage)) return;
    add_score(0xfa);
    drop_pickups(5, obj_x(slot), obj_y(slot));
    int32_t a = next_random() % 0x32 + 0x14, div = (slot & 3) + 5;
    for (int i = 0; i < 3; i++) {
        int k = wrap360(a);
        drifter_spawn(obj_x(slot), obj_y(slot), g_trig_a[k] / div, g_trig_b[k] / div, 0);
        int32_t next = a + 0x78;
        if ((next > 0xa9 && next < 0xbf) || (next > 0x103 && next < 0x119)) next = a + 0x8c;
        a = next;
    }
    int32_t x = obj_x(slot), y = obj_y(slot);
    effect_spawn(0x10a, true, x, y);
    effect_spawn(0x158, true, x + 0x1700, y - 0x1200);
    effect_spawn(0x158, true, x + 0x800, y + 0x1900);
    effect_spawn(0x158, true, x - 0x1f00, y - 0x600);
    obj_kill(slot);
}

/* 0x21a64: mission 3's single giant rock. */
static void giant_rock_spawn(void)
{
    load_banks(0x843, 0x60, 0x8e0);
    int slot = obj_find_free(0x93, 0x93, true);
    obj_spawn(slot, 0x843, 0x11d50, 0x7350, 0x19);
    obj_set_anim(slot, ANIM_526f8, (int16_t)(next_random() % g_anims[ANIM_526f8].count));
    obj_set_velocity(slot, 0x80, 0x80);
    obj_set_callback(slot, 1, CB_1038c, 1);
    g_state.obj[slot].o_0 = 800;
}

/* ------------------------------------------------------------ edge walkers */

void walker_hit(int slot, int damage)
{
    if (!hit_counts(slot, damage)) return;
    int32_t hp = g_state.obj[slot].o_0 - damage;
    g_state.obj[slot].o_0 = hp < 0 ? 0 : hp;
    if (g_state.obj[slot].o_0 != 0) return;
    add_score(500);
    effect_spawn(0x148, true, obj_x(slot), obj_y(slot));
    drop_pickups(3, obj_x(slot), obj_y(slot));
    obj_kill(slot);
}

/* Walk straight on for o_8 more steps while inside the arena and clear of
 * the walkers after it, else turn one of two ways at random and walk 0-2
 * steps. */
static void walker_step(int slot, int orient, bool along_y, int self, int straight,
                        int anim_a, int cb_a, int anim_b, int cb_b)
{
    obj_set_orient(slot, orient);
    int32_t left = --g_state.obj[slot].o_8;
    int32_t p = along_y ? obj_y(slot) : obj_x(slot);
    bool turn = !(left >= 0 && p >= ARENA_X0 && p <= (along_y ? ARENA_Y1 : ARENA_X1));
    if (!turn && slot < 0x86) turn = collide_sprites(slot, slot + 1, 0x86) != -1;
    if (turn) {
        bool a = next_random() % 2 == 0;
        int anim = a ? anim_a : anim_b;
        obj_set_anim(slot, anim, 0);
        obj_set_callback(slot, 0, a ? cb_a : cb_b, g_anims[anim].count);
        g_state.obj[slot].o_8 = next_random() % 3;
    } else {
        obj_set_anim(slot, straight, 0);
        obj_set_callback(slot, 0, self, g_anims[straight].count);
    }
}

void cb_walker_up(int slot)
{
    walker_step(slot, 0, true, CB_1384c, ANIM_46c38, ANIM_464a0, CB_135cc, ANIM_46728, CB_13988);
}

void cb_walker_right(int slot)
{
    walker_step(slot, 2, false, CB_135cc, ANIM_469b0, ANIM_46218, CB_1384c, ANIM_45f90, CB_1370c);
}

void cb_walker_down(int slot)
{
    walker_step(slot, 2, true, CB_1370c, ANIM_46c38, ANIM_464a0, CB_13988, ANIM_46728, CB_135cc);
}

void cb_walker_left(int slot)
{
    walker_step(slot, 0, false, CB_13988, ANIM_469b0, ANIM_46218, CB_1370c, ANIM_45f90, CB_1384c);
}

/* Every 20 frames: a shot backwards along its motion. */
void cb_walker_shoot(int slot)
{
    obj_set_callback(slot, 2, CB_132e8, 0x14);
    int b = obj_find_free(0xd8, 0x113, false);
    if (b == -1) return;
    obj_spawn(b, 0x578, obj_x(slot), obj_y(slot), 8);
    obj_set_anim(b, ANIM_4ab20, 0);
    g_state.obj[b].o_0 = 0x96;
    g_state.obj[b].o_4 = 0;
    obj_set_callback(b, 0, CB_18dc0, 1);
    obj_set_velocity(b, -obj_motion_x(slot), -obj_motion_y(slot));
}

/* 0x133a8: n walkers entering from the four edges in turn, 0x31 frames apart. */
static void walkers_spawn(int n, int delay)
{
    load_banks(0x1f2, 0x42, 0x264);
    for (int i = 0; i < n; i++) {
        int slot = obj_find_free(0x82, 0x86, true);
        g_state.obj[slot].o_0 = 0x1c2;
        g_state.obj[slot].o_8 = next_random() % 3;
        delay += 0x31;
        int cb;
        switch (i % 4) {
        case 0: obj_spawn(slot, 0x1f2, 0x5f50, next_random() % 0xc800 + 50000, 0xf); cb = CB_1384c; break;
        case 1: obj_spawn(slot, 0x1f2, next_random() % 0x14000 + 50000, 0x5f50, 0xf); cb = CB_135cc; break;
        case 2: obj_spawn(slot, 0x1f2, 0x26650, next_random() % 0xc800 + 50000, 0xf); cb = CB_1370c; break;
        default: obj_spawn(slot, 0x1f2, next_random() % 0x14000 + 50000, 0x1ee50, 0xf); cb = CB_13988; break;
        }
        obj_set_callback(slot, 0, cb, delay);
        obj_set_callback(slot, 1, CB_1038c, delay);
        obj_set_callback(slot, 2, CB_132e8, delay);
    }
}

/* ------------------------------------------------------ mission 15 seekers */

static int32_t sar(int32_t v, int n) { return v >= 0 ? v >> n : ~(~v >> n); }

/* Heading from the object to the player in degrees, 0 = up (x87 code:
 * trunc(atan2(dx, -dy) * 57.2957795), as in aimed_shot). */
static int aim_at_player(int slot)
{
    double a = (double)(int32_t)-(obj_y(SLOT_PLAYER) - obj_y(slot));
    double c = (double)(int32_t)(obj_x(SLOT_PLAYER) - obj_x(slot));
    return wrap360((int32_t)(atan2(c, a) * 57.2957795));
}

static int32_t total_vx(int slot) { return g_objects[slot].vx + g_objects[slot].ax; }   /* 0x2c0e0 */
static int32_t total_vy(int slot) { return g_objects[slot].vy + g_objects[slot].ay; }   /* 0x2c0fc */

void cb_boss15(int slot)
{
    int k = aim_at_player(slot);
    obj_set_velocity(slot, ((g_trig_a[k] / 0x5f + total_vx(slot)) * 0x3c0) / 1000,
                     ((g_trig_b[k] / 0x5f + total_vy(slot)) * 0x3c0) / 1000);
    obj_set_callback(slot, 0, CB_11ab4, 1);
}

/* Home in on the player, or push away from a fellow seeker it touches. */
void cb_seeker(int slot)
{
    int o = collide_sprites(slot, 0x72, 0x75);
    int32_t vx, vy;
    if (o == -1) {
        int k = aim_at_player(slot);
        vy = ((g_trig_b[k] / 0xfa + total_vy(slot)) * 0x3e3) / 1000;
        vx = ((g_trig_a[k] / 0xfa + total_vx(slot)) * 0x3e3) / 1000;
    } else {
        vy = sar(obj_y(slot) - obj_y(o), 6);
        vx = sar(obj_x(slot) - obj_x(o), 6);
    }
    obj_set_velocity(slot, vx, vy);
    obj_set_callback(slot, 0, CB_11fb4, 1);
}

/* 0x121dc / 0x123f4 */
static void seeker_spawn(int32_t x, int32_t y, int32_t vx, int32_t vy)
{
    load_banks(0x173, 0x3e, 0x1d1);
    int slot = obj_find_free(0x72, 0x75, true);
    obj_spawn(slot, 0x18c, x, y, 0x18);
    obj_set_anim(slot, ANIM_44e00, (int16_t)(next_random() % g_anims[ANIM_44e00].count));
    obj_set_velocity(slot, vx, vy);
    obj_set_callback(slot, 0, CB_11fb4, 0x46);
    obj_set_callback(slot, 1, CB_1038c, 1);
    g_state.obj[slot].o_0 = 500;
}

static void seeker_small_spawn(int32_t x, int32_t y, int32_t vx, int32_t vy)
{
    int slot = obj_find_free(0x76, 0x81, true);
    obj_spawn(slot, 0x1a0, x, y, 0x18);
    obj_set_anim(slot, ANIM_45448, (int16_t)(next_random() % g_anims[ANIM_45448].count));
    obj_set_velocity(slot, vx, vy);
    obj_set_callback(slot, 1, CB_1038c, 1);
    g_state.obj[slot].o_0 = 200;
}

void boss15_hit(int slot, int damage)
{
    if (!rock_damage(slot, damage)) return;
    add_score(0x15e);
    drop_pickups(5, obj_x(slot), obj_y(slot));
    int32_t k = next_random() % 0x32 + 0x14;
    for (int i = 0; i < 4; i++, k += 90)
        seeker_spawn(obj_x(slot), obj_y(slot), sar(g_trig_a[k], 2), sar(g_trig_b[k], 2));
    int32_t x = obj_x(slot), y = obj_y(slot);
    effect_spawn(0x10a, true, x, y);
    effect_spawn(0x158, true, x + 0x1700, y - 0x1200);
    effect_spawn(0x158, true, x + 0x800, y + 0x1900);
    effect_spawn(0x158, true, x - 0x1f00, y - 0x600);
    obj_kill(slot);
}

void seeker_hit(int slot, int damage)
{
    if (!rock_damage(slot, damage)) return;
    add_score(0x96);
    drop_pickups(3, obj_x(slot), obj_y(slot));
    int32_t a = next_random() % 0x32 + 0x14;
    for (int i = 0; i < 3; i++) {
        int32_t div = next_random() % 2 + 5;
        int k = wrap360(a);
        seeker_small_spawn(obj_x(slot), obj_y(slot), g_trig_a[k] / div, g_trig_b[k] / div);
        int32_t next = a + 0x78;
        if ((next > 0xa9 && next < 0xbf) || (next > 0x103 && next < 0x119)) next = a + 0x8c;
        a = next;
    }
    effect_spawn(0x129, true, obj_x(slot), obj_y(slot));
    obj_kill(slot);
}

void seeker_small_hit(int slot, int damage)
{
    if (!rock_damage(slot, damage)) return;
    effect_spawn(0x148, true, obj_x(slot), obj_y(slot));
    obj_kill(slot);
    add_score(0x32);
}

/* 0x11ca0: mission 15's boss. */
static void boss15_spawn(void)
{
    load_banks(0x173, 0x3e, 0x1d1);
    int slot = obj_find_free(0x71, 0x71, true);
    obj_spawn(slot, 0x173, 0x8750, 0x7350, 0x18);
    obj_set_anim(slot, ANIM_44b78, (int16_t)(next_random() % g_anims[ANIM_44b78].count));
    obj_set_callback(slot, 0, CB_11ab4, 1);
    obj_set_callback(slot, 1, CB_1038c, 1);
    g_state.obj[slot].o_0 = 0x4b0;
}

/* ---------------------------------------------------- mission 23 orbiters */

/* Twelve objects 30 degrees apart around a centre (o_8, o_c), turning half
 * a degree per frame. */
static unsigned orbit_angle(int slot)
{
    return ((unsigned)((slot - 0x87) * 0x168) / 0xc + (unsigned)(g_state.mission_frames >> 1)) % 0x168;
}

/* The ring closes in until frame 0x21c. */
void cb_orbit_close(int slot)
{
    unsigned a = orbit_angle(slot);
    int32_t mf = g_state.mission_frames, r = (0x21c - mf) * 0x23 + 10000;
    obj_set_pos(slot, g_state.obj[slot].o_8 + (g_trig_a[a] * r) / 400,
                g_state.obj[slot].o_c + (g_trig_b[a] * r) / 500);
    obj_set_callback(slot, 0, mf < 0x21c ? CB_1b360 : CB_1b5a8, 1);
}

/* Then it pulses; after frame 0x32a the centre drifts, wrapping. */
void cb_orbit_pulse(int slot)
{
    unsigned a = orbit_angle(slot);
    int32_t mf = g_state.mission_frames, r = g_trig_a[mf % 0x168] * 2 + 10000;
    ObjState *st = &g_state.obj[slot];
    int32_t x = st->o_8 + (g_trig_a[a] * r) / 400, y = st->o_c + (g_trig_b[a] * r) / 500;
    if (x < 0xb450) x += 0x15d00;
    else if (x > 0x21150) x -= 0x15d00;
    if (y < 0xb450) y += 0xe500;
    else if (y > 0x19950) y -= 0xe500;
    if (mf > 0x32a) {
        st->o_8 += 0x80;
        st->o_c += 0x40;
        if (st->o_8 > 0x21150) st->o_8 -= 0x15d00;
        if (st->o_c > 0x19950) st->o_c -= 0xe500;
    }
    obj_set_pos(slot, x, y);
    obj_set_callback(slot, 0, CB_1b5a8, 1);
}

void orbiter_hit(int slot, int damage)
{
    if (!hit_counts(slot, damage)) return;
    int32_t hp = g_state.obj[slot].o_0 - damage;
    g_state.obj[slot].o_0 = hp < 0 ? 0 : hp;
    if (g_state.obj[slot].o_0 != 0) return;
    add_score(0x4b);
    effect_spawn(0x148, true, obj_x(slot), obj_y(slot));
    obj_kill(slot);
}

/* 0x1b4f0 */
static void orbiters_spawn(void)
{
    load_banks(0x5af, 0x58, 0x5c0);
    for (int i = 0, slot = 0x87; slot < 0x93; i++, slot++) {
        obj_spawn(slot, 0x5af, 0, 0, 0x10);
        obj_set_anim(slot, ANIM_4afb0, (int16_t)((unsigned)(g_anims[ANIM_4afb0].count * i) / 0xc));
        g_state.obj[slot].o_c = 0x126d0;
        g_state.obj[slot].o_0 = 300;
        g_state.obj[slot].o_8 = 0x162d0;
        obj_set_callback(slot, 0, CB_1b360, 1);
    }
}

/* ------------------------------------------------------- mission 19 boss */

#define BOSS19 0xb2

static int32_t next_random_b(void)
{
    g_state.f_019++;
    return g_random_table[g_state.f_019 & 0xff];
}

/* Hovers firing (count*6 frames), then plays its "rise" animation. */
void cb_boss19_hover(int slot)
{
    (void)slot;
    obj_set_anim(BOSS19, ANIM_54ac8, 0);
    obj_set_velocity(BOSS19, 0, 0);
    obj_set_callback(BOSS19, 0, CB_24288, g_anims[ANIM_54ac8].count * 6);
    obj_set_callback(BOSS19, 1, CB_23f58, 1);
}

void cb_boss19_rise(int slot)
{
    (void)slot;
    obj_set_anim(BOSS19, ANIM_548e0, 0);
    obj_set_callback(BOSS19, 0, CB_23ab0, g_anims[ANIM_548e0].count);
    obj_set_callback(BOSS19, 1, 0, -1);
}

/* Chases the player for count*9 frames ... */
void cb_boss19_hunt(int slot)
{
    (void)slot;
    obj_set_anim(BOSS19, ANIM_54ac8, 0);
    obj_set_callback(BOSS19, 0, CB_23b04, g_anims[ANIM_54ac8].count * 9);
    obj_set_callback(BOSS19, 1, CB_238c0, 1);
}

/* ... and on until it is entirely on screen, then hovers again. */
void cb_boss19_settle(int slot)
{
    (void)slot;
    if (render_obj_fully_on_screen(BOSS19)) {
        obj_set_anim(BOSS19, ANIM_548e0, 0);
        obj_set_velocity(BOSS19, 0, 0);
        obj_set_callback(BOSS19, 0, CB_24224, g_anims[ANIM_548e0].count);
        obj_set_callback(BOSS19, 1, 0, -1);
    } else {
        obj_set_callback(BOSS19, 0, CB_23b04, g_anims[ANIM_54ac8].count);
    }
}

void cb_boss19_chase(int slot)
{
    obj_set_callback(BOSS19, 1, CB_238c0, 1);
    int k = aim_at_player(BOSS19);
    obj_set_velocity(BOSS19, sar((g_trig_a[k] / 0x4b + total_vx(slot)) * 0x3c0, 10),
                     sar((g_trig_b[k] / 0x4b + total_vy(slot)) * 0x3c0, 10));
}

/* Every 10-29 frames: a shot at the player, up to 10 degrees off. */
void cb_boss19_fire(int slot)
{
    obj_set_callback(BOSS19, 1, CB_23f58, next_random() % 0x14 + 10);
    int b = obj_find_free(0xd8, 0x113, false);
    if (b == -1) return;
    obj_spawn(b, 0x578, obj_x(slot), obj_y(slot), 0x16);
    obj_set_anim(b, ANIM_4ab20, 0);
    int deg = 0;
    {
        double a = (double)(int32_t)-(obj_y(SLOT_PLAYER) - obj_y(b));
        double c = (double)(int32_t)(obj_x(SLOT_PLAYER) - obj_x(b));
        deg = (int32_t)(atan2(c, a) * 57.2957795);
    }
    /* The original's wrap macro draws one number for the sign test and
     * another for the value. */
    int32_t test = next_random() % 0x14 + deg - 10;
    int32_t v = next_random() % 0x14 + deg - 10;
    int k = test < 0 ? 360 - iabs(v) % 360 : v % 360;
    obj_set_velocity(b, sar(g_trig_a[k] * 3, 3), sar(g_trig_b[k] * 3, 3));
    g_state.obj[b].o_0 = 0xaa;
    g_state.obj[b].o_4 = 0;
    obj_set_callback(b, 0, CB_18dc0, 1);
}

void boss19_hit(int damage)
{
    int32_t mf = g_state.mission_frames;
    if (!(damage == FATAL || LAST_HIT(BOSS19) < mf - 1 || LAST_HIT(SLOT_PLAYER) < mf - 1)) return;
    g_state.obj[BOSS19].o_0 -= damage;
    if (g_state.obj[BOSS19].o_0 >= 1) return;
    if (g_objects[BOSS19].cb_timer[2] == -1) {      /* 0x2aad8: not dying yet */
        g_state.obj[BOSS19].o_8 = 0x32;
        obj_set_callback(BOSS19, 2, CB_23bf4, 1);
    }
}

/* 50 steps of explosions around it, then the big one. */
void cb_boss19_dying(int slot)
{
    (void)slot;
    int32_t n = --g_state.obj[BOSS19].o_8;
    if (n > 0) {
        int32_t ry = next_random_b();
        int32_t y = obj_y(BOSS19);
        int32_t rx = next_random_b();
        int32_t x = obj_x(BOSS19);
        int32_t rk = next_random_b();
        if (n > 0x1e) {
            effect_spawn(rk % 2 == 0 ? 0x168 : 0x158, false, (rx % 0x3c) * 0x100 - 0x1e00 + x,
                         y + (ry % 0x32) * 0x100 - 0x1900);
            obj_set_callback(BOSS19, 2, CB_23bf4, next_random() % 6 + 6);
        } else if (n > 0xf) {
            effect_spawn(rk % 4 == 0 ? 0x168 : 0x158, false, x + (rx % 0x50) * 0x100 - 0x2800,
                         y + (ry % 0x42) * 0x100 - 0x2100);
            obj_set_callback(BOSS19, 2, CB_23bf4, 6);
        } else {
            effect_spawn(rk % 3 == 0 ? 0x129 : 0x148, true, x + (rx % 0x50) * 0x100 - 0x2800,
                         (ry % 0x42) * 0x100 - 0x2100 + y);
            obj_set_callback(BOSS19, 2, CB_23bf4, 6);
        }
        return;
    }
    add_score(0x9c4);
    drop_pickups(5, obj_x(BOSS19), obj_y(BOSS19));
    effect_spawn(0x10a, true, obj_x(BOSS19), obj_y(BOSS19));
    effect_spawn(0x158, true, obj_x(BOSS19), obj_y(BOSS19));
    obj_kill(BOSS19);
}

/* 0x242d0 */
static void boss19_spawn(void)
{
    load_banks(0x900, 100, 0x935);
    obj_spawn(BOSS19, 0x900, 0x162d0, 0x1e450, 0x17);
    obj_set_anim(BOSS19, ANIM_548e0, 0);
    g_state.obj[BOSS19].o_0 = 5000;
    obj_set_velocity(BOSS19, 0, -0x80);
    obj_set_callback(BOSS19, 0, CB_24224, g_anims[ANIM_548e0].count * 3);
}

void level_init_bonus(void)
{
    objects_clear_active();                         /* 0x2c190 */
    obj_spawn(5, -1, ARENA_X0, ARENA_Y0, 1);
    obj_set_slot(5, 0x7e);
    obj_set_anim(5, ANIM_563a8, 0);
    player_setup();
    switch (g_state.mission) {
    case 3:
        giant_rock_spawn();
        break;
    case 7:
        chain_spawn(7, 0xb450, 0xf550, 1);
        drifter_spawn(0x23450, 0xf550, -0x80, 0x40, 200);
        drifter_spawn(0xf550, 0x9150, 0x40, -0x80, 400);
        break;
    case 11:
        drifter_spawn(0x23450, 0xf550, -0x80, 0x40, 1);
        drifter_spawn(0xf550, 0x9150, 0x40, -0x80, 0x32);
        walkers_spawn(2, 200);
        break;
    case 15:
        boss15_spawn();
        break;
    case 19:
        boss19_spawn();
        break;
    case 23:
        orbiters_spawn();
        walkers_spawn(2, 500);
        break;
    default:
        break;
    }
}
