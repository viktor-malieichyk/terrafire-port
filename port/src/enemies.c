#include "enemies.h"

#include "callbacks.h"
#include "collision.h"
#include "spawn.h"
#include "tables.h"
#include "game.h"
#include "hits.h"
#include "hud.h"
#include "missions.h"
#include "anims.h"
#include "effects.h"
#include "objects.h"
#include "res.h"
#include "render.h"
#include "sound.h"
#include "tow.h"
#include "unported.h"

#include <math.h>
#include <stddef.h>
#include "weapons.h"

static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* The player is within dx/dy (24.8) of the object. */
static bool player_near(int slot, int32_t dx, int32_t dy)
{
    return iabs(obj_x(slot) - obj_x(SLOT_PLAYER)) < dx && iabs(obj_y(slot) - obj_y(SLOT_PLAYER)) < dy;
}

void cb_turret_vertical(int slot)
{
    obj_set_callback(slot, 0, CB_17550, g_missions[g_state.mission].u4c);   /* firing interval */
    if (player_near(slot, 0xaa00, 0x19000) && obj_y(SLOT_PLAYER) > 0x5ffff)
        bullet_spawn(0x550, 0x11, obj_x(slot), obj_y(slot), 0, 0, 6,
                     g_objects[slot].orient == 0 ? 0xb4 : 0, CB_18dc0, 400);
}

void cb_turret_wall(int slot)
{
    obj_set_callback(slot, 0, CB_17384, g_missions[g_state.mission].u4c);
    if (player_near(slot, 0x19000, 0x8c00) && obj_y(SLOT_PLAYER) > 0x5ffff)
        bullet_spawn(0x528, 0x11, obj_x(slot), obj_y(slot), 0, 0, 6,
                     g_objects[slot].orient == 0 ? 0x10e : 0x5a, CB_18dc0, 400);
}

/* Fires along its mounting direction every 25 frames. State o_8 counts a
 * burst (5 or 10 shots, then a 200-frame pause); the spawner sets it to -1,
 * which never counts down (nonstop fire) until switch 0xc4 sets it to 1. */
void cb_gun_mounted(int slot)
{
    static const int angle[4] = { 0xb4, 0x10e, 0, 0x5a }, burst[4] = { 5, 10, 10, 5 };
    int orient = g_objects[slot].orient & 3;
    int32_t *count = &g_state.obj[slot].o_8;
    if (*count == -1 || --*count != 0) {
        obj_set_callback(slot, 0, CB_17cd0, 0x19);
        if (player_near(slot, 0xaa00, 0xc800))
            bullet_spawn(0x55a, 0x11, obj_x(slot), obj_y(slot), 0, 0, 6, angle[orient], CB_18dc0, 200);
    } else {
        *count = burst[orient];
        obj_set_callback(slot, 0, CB_17cd0, 200);
    }
}

#define LAST_HIT(slot) (g_state.obj[(slot)].o_4)

/* Counts a hit unless the same contact already counted last frame; four-way
 * mounted guns (0x4f-0x54) can't be damaged. Destroyed at <= 0 hit points. */
void turret_hit(int slot, int damage)
{
    int32_t mf = g_state.mission_frames;
    bool fresh = damage == 10000 ||
                 ((LAST_HIT(slot) < mf - 1 || LAST_HIT(SLOT_PLAYER) < mf - 1) &&
                  (LAST_HIT(slot) < mf - 1 || LAST_HIT(15) < mf - 1));
    if (!fresh || (slot >= 0x4f && slot <= 0x54)) return;
    g_state.obj[slot].o_0 -= damage;
    if (g_state.obj[slot].o_0 >= 1) return;
    const Object *o = &g_objects[slot];
    const BankView *v = &g_banks[o->bank].view[o->orient];
    effect_spawn(0x10a, false, o->x + v->quad_x[1], o->y + v->quad_y[1]);
    obj_kill(slot);
    if (!g_state.f_014) g_state.score += (g_state.difficulty + 1) * 0x96;
}

/* Fires an aimed shot when the player is in a box in front of it (above a
 * floor turret, below a ceiling one); fires once per animation cycle. */
void cb_turret_floor(int slot)
{
    obj_set_callback(slot, 0, CB_179c8, g_anims[ANIM_4a930].count);
    int32_t px = obj_x(SLOT_PLAYER), py = obj_y(SLOT_PLAYER), x = obj_x(slot), y = obj_y(slot);
    if (!(px - 0xa000 < x && x < px + 0xa000)) return;
    if (g_objects[slot].orient == 0 ? (py - 0x1400 < y && y < py + 0x6400)
                                     : (py - 0x6400 < y && y < py + 0x1400))
        aimed_shot(slot);
}

static int32_t sar7(int32_t v) { return v >= 0 ? v >> 7 : ~(~v >> 7); }

/* Frames for a gate piece to slide between the two wall posts at 0x40/frame. */
static int32_t gate_travel(void) { return sar7(obj_x(0xbd) - obj_x(0xbc)); }

/* The gate's middle pieces 0xba/0xbb slide apart, stay open for the
 * mission's u44 frames in all, then close again. */
void gate_switch_hit(void)
{
    if (obj_motion_x(0xba) != 0) return;            /* already moving */
    hud_message(0xd2, MSG_GATE_OPEN);
    int32_t travel = gate_travel(), open_for = g_missions[g_state.mission].u44;
    if (g_objects[0xbf].anim == ANIM_47ca8) {
        obj_set_velocity(0xba, -0x40, 0);
        obj_set_callback(0xba, 0, CB_14488, travel);
        obj_set_velocity(0xbb, 0x40, 0);
        obj_set_callback(0xbb, 0, CB_14488, travel);
        sound_play(0x17);
        if (obj_y(SLOT_PLAYER) > 0x5ffff) sound_play(2);
    }
    obj_set_callback(0xba, 1, CB_143c0, open_for - travel);
    obj_set_callback(0xbb, 1, CB_143c0, open_for - travel);
    for (int s = 0xbf; s < 0xc1; s++)
        if (obj_alive(s)) {
            obj_set_anim(s, ANIM_47e30, 0);
            obj_set_callback(s, 0, CB_12ce0, open_for);
        }
}

void cb_gate_stop(int slot)
{
    obj_set_velocity(slot, 0, 0);
    if (sound_playing(2)) {
        sound_stop(2);
        sound_play(1);
    }
}

void cb_gate_close(int slot)
{
    if (slot == 0xba) obj_set_velocity(slot, 0x40, 0);
    else if (slot == 0xbb) obj_set_velocity(slot, -0x40, 0);
    obj_set_callback(slot, 0, CB_14488, gate_travel());
    hud_message(0xd2, MSG_GATE_CLOSING);
    if (player_near(slot, 0x28000, 0x19000)) sound_play(2);
}

void cb_switch_reset(int slot) { obj_set_anim(slot, ANIM_47ca8, 0); }

static int32_t sar8(int32_t v);

/* Object 0xbe stops animating and the 0x37-0x3b group restarts staggered,
 * 200 frames apart. Only while 0xbe is still animating. */
void group37_switch_hit(void)
{
    hud_message(0xd2, MSG_FIRE_WALLS);
    if (g_objects[0xbe].anim < 0) return;
    obj_clear_anim(0xbe);
    g_objects[0xbe].bank = 0x2c5;                    /* 0x2c3e4 */
    g_objects[0xbe].slot = -1;
    sound_play(0x17);
    for (int s = 0x37, delay = 200; s < 0x3c; s++, delay += 200)
        if (obj_alive(s)) {
            obj_set_anim(s, ANIM_489c0, 0x74);
            obj_set_callback(s, 0, CB_14d50, delay);
        }
}

/* Restarts its animation every 300 frames unless object 0xbe is idle. */
void cb_group37_cycle(int slot)
{
    obj_set_callback(slot, 0, CB_14d50, 300);
    if (obj_alive(0xbe) && g_objects[0xbe].anim == ANIM_47ca8) return;
    obj_set_anim(slot, ANIM_489c0, 0);
    obj_set_callback(0x37, 1, CB_14eac, 1);
    obj_set_callback(0x37, 2, CB_14eac, g_anims[ANIM_489c0].count);
}

/* Sound level 0-3 from the nearest animating member of the group (state o_8
 * of object 0x37); checks again sooner the closer the player is. */
void cb_group37_sound(int slot)
{
    (void)slot;
    int32_t best = 36000000, px = sar8(obj_x(SLOT_PLAYER)), py = sar8(obj_y(SLOT_PLAYER));
    for (int s = 0x37; s < 0x3c; s++) {
        if (!obj_alive(s) || g_objects[s].anim < 0) continue;
        int32_t dx = sar8(obj_x(s)) - px, dy = sar8(obj_y(s)) - py;
        int32_t d = dy * dy + dx * dx;
        if (d < best) best = d;
    }
    int32_t t = (best >> 0xe) * 4 + 1;
    if (t > 0x45) t = 0x46;
    obj_set_callback(0x37, 1, CB_14eac, t);
    int32_t old = g_state.obj[0x37].o_8;
    int32_t level = 3 - (best >> 0xe) < 0 ? 0 : 3 - (best >> 0xe);
    if (level == old) {
        if (old == 0) return;
        if (sound_playing(8) || sound_playing(9) || sound_playing(10)) return;
    }
    g_state.obj[0x37].o_8 = level;
    sound_stop(8);
    sound_stop(9);
    sound_stop(10);
    if (level > 0) sound_play(level + 7);
}

static int32_t sar8(int32_t v) { return v >= 0 ? v >> 8 : ~(~v >> 8); }

/* Rises slowly for 333 frames, then stops and drops, repeating. Starting to
 * move while the player is just below shakes the screen. Stays still while
 * switch 0xc2 still shows its idle ANIM_47ca8, i.e. until it is hit. */
void cb_crusher_move(int slot)
{
    if (obj_motion_y(slot) < 0) {
        obj_set_velocity(slot, 0, 0);
        g_objects[slot].ax = 0;
        g_objects[slot].ay = 0x40;
        obj_set_callback(slot, 0, CB_1e3bc, 0x24);
        return;
    }
    if (obj_alive(0xc2) && g_objects[0xc2].anim == ANIM_47ca8) {
        g_objects[slot].ax = g_objects[slot].ay = 0;
        obj_set_velocity(slot, 0, 0);
        obj_set_callback(slot, 0, CB_1e3bc, 0x23);
        return;
    }
    int32_t px = obj_x(SLOT_PLAYER), py = obj_y(SLOT_PLAYER), x = obj_x(slot), y = obj_y(slot);
    if (px - 0xda00 < x && x < px + 0x6600 && py - 0xd400 < y && y < py - 0x4800 &&
        obj_motion_y(slot) != 0)
        g_state.f_0af = 0x19;                           /* screen shake */
    if (render_obj_on_screen(slot)) sound_play(4);
    g_objects[slot].ax = g_objects[slot].ay = 0;
    obj_set_velocity(slot, 0, -0x80);
    obj_set_callback(slot, 0, CB_1e3bc, 0x14d);
}

/* Engine sound level 0-3 by the player's distance (state o_8 of object 12);
 * checks again sooner the closer the player is. Sound only. */
void cb_crusher_sound(int slot)
{
    (void)slot;
    int32_t dx = sar8(obj_x(SLOT_PLAYER) - obj_x(12)) - 0x38;
    int32_t dy = sar8(obj_y(SLOT_PLAYER) - obj_y(12)) - 0x5f;
    int32_t d = (dy * dy + dx * dx) >> 0xf;
    int32_t t = d * 4 + 1;
    if (t > 0x45) t = 0x46;
    obj_set_callback(12, 1, CB_1e510, t);
    int32_t level = 3 - d < 0 ? 0 : 3 - d;
    int32_t old = g_state.obj[12].o_8;
    if (level == old) {
        if (old == 0) return;
        if (sound_playing(0xb) || sound_playing(0xc) || sound_playing(0xd)) return;
    }
    g_state.obj[12].o_8 = level;
    sound_stop(0xb);
    sound_stop(0xc);
    sound_stop(0xd);
    sound_play(level < 1 ? 3 : level + 10);
}

/* Switches 0xc1-0xc4: each works once, while it is still animating, and then
 * shows bank 0x2c5. */
static bool switch_flip(int slot, const char *message)
{
    hud_message(0xd2, message);
    if (g_objects[slot].anim < 0) return false;      /* 0x2aa54 */
    obj_clear_anim(slot);
    g_objects[slot].bank = 0x2c5;                     /* 0x2c3e4 */
    g_objects[slot].slot = -1;
    return true;
}

void switch_c1_hit(void)
{
    if (!switch_flip(0xc1, MSG_ROCK_FOUNTAIN)) return;
    sound_play(0x17);
    for (int s = 0x35; s < 0x37; s++)
        if (obj_alive(s)) obj_set_callback(s, 1, CB_14c9c, 1);
}

void switch_c2_hit(void)
{
    if (switch_flip(0xc2, MSG_CRUSHER)) sound_play(0x17);
}

void switch_c3_hit(void)
{
    if (!switch_flip(0xc3, MSG_WATER_PUMPS)) return;
    obj_set_callback(0xb, 0, CB_25d38, 1);
    sound_play(0x17);
}

void switch_c4_hit(void)
{
    if (!switch_flip(0xc4, MSG_GUNS_OFF)) return;
    for (int s = 0x4f; s < 0x55; s++) g_state.obj[s].o_8 = 1;
    sound_play(0x17);
}

/* After switch 0xc1: every 450 frames the fountain's next rock is put off
 * for 250 frames, so it throws rocks only 200 frames in 450. */
void cb_emitter_start(int slot)
{
    obj_set_callback(slot, 0, CB_149a0, 0xfa);
    obj_set_callback(slot, 1, CB_14c9c, 0x1c2);
}

static int32_t rand_at(int k) { return g_random_table[(g_state.f_015 + k) & 0xff]; }

/* Rock fountain: every 5-19 frames, while the player is within 0x12c px,
 * throws a rock (objects 0x2b-0x34) upwards at 330-369 degrees; it falls
 * back under its own gravity (ay = 10). */
void cb_emitter_puff(int slot)
{
    g_state.f_015++;
    obj_set_callback(slot, 0, CB_149a0, rand_at(0) % 0xf + 5);
    if (iabs(obj_x(slot) - obj_x(0xe)) >= 0x12c00 || iabs(obj_y(slot) - obj_y(0xe)) >= 0x12c00) return;
    int b = obj_find_free(0x2b, 0x34, false);
    if (b == -1) return;
    obj_spawn(b, 0x2d0, obj_x(slot), obj_y(slot), 0x21);
    g_state.f_015++;
    obj_set_anim(b, ANIM_48518, (int16_t)(rand_at(0) % g_anims[ANIM_48518].count));
    g_state.f_015++;
    obj_set_orient(b, rand_at(0) % 8);
    /* The original's angle wrap macro draws a number for the sign test and
     * another for the value. */
    int32_t angle;
    if (rand_at(1) % 0x14 + 0x15e < 0) {
        int32_t v = iabs(rand_at(2) % 0x14 + 0x15e);
        angle = 0x168 - v % 0x168;
    } else {
        angle = (rand_at(2) % 0x14 + 0x15e) % 0x168;
    }
    g_state.f_015 += 3;
    int32_t speed = rand_at(0) % 4 + 8;
    obj_set_velocity(b, g_trig_a[angle] * speed / 0xc, g_trig_b[angle] * speed / 0xc);
    g_objects[b].ax = 0;                               /* 0x2c1e0 */
    g_objects[b].ay = 10;
    g_state.obj[b].o_0 = 300;
    g_state.obj[b].o_4 = 0;
    obj_set_callback(b, 0, CB_14c18, 10);
}

/* A rock breaks after 300 frames or on hitting the level (object 10). */
void cb_bubble(int slot)
{
    if (g_state.obj[slot].o_0-- != 0 && collide_point(10, 10, obj_x(slot), obj_y(slot)) == -1) {
        obj_set_callback(slot, 0, CB_14c18, 1);
        return;
    }
    effect_spawn(0x158, false, obj_x(slot), obj_y(slot));
    obj_kill(slot);
}

/* Object 0xb: every 0x33 frames, map codes 0x54 / 0x55 under it send it
 * left / right. */
void cb_follow_markers(int slot)
{
    (void)slot;
    obj_set_callback(0xb, 0, CB_25d38, 0x33);
    int code = map_code_at(obj_x(0xb), obj_y(0xb)) & 0xffff;
    if (code == 0x6a || code < 0x54) return;
    if (code == 0x54) obj_set_velocity(0xb, -0x50, 0);
    else if (code == 0x55) obj_set_velocity(0xb, 0x50, 0);
}

/* Hit while at rest and on screen: accelerates towards the player along
 * trunc(atan2(dx, -dy) * 57.2957795) degrees (x87 in the original, see
 * aimed_shot). */
void homer_hit(int slot)
{
    if (g_objects[slot].ax != 0 || g_objects[slot].ay != 0 || !render_obj_on_screen(slot)) return;
    double a = (double)(int32_t)-(obj_y(SLOT_PLAYER) - obj_y(slot));
    double c = (double)(int32_t)(obj_x(SLOT_PLAYER) - obj_x(slot));
    int32_t deg = (int32_t)(atan2(c, a) * 57.2957795);
    int ang = deg < 0 ? 360 - (-deg) % 360 : deg % 360;
    g_objects[slot].ax = g_trig_a[ang] / 300;          /* 0x2c1e0 */
    g_objects[slot].ay = g_trig_b[ang] / 300;
    obj_set_callback(slot, 0, CB_1e050, 1);
}

static void homer_explode(int slot)
{
    effect_spawn(0x10a, false, obj_x(slot), obj_y(slot));
    obj_kill(slot);
    score_add(true, 0x19);
}

/* Explodes on touching the level, when its o_8 frames run out, or together
 * with an object of 0x10-0x22 it runs into. */
void cb_homer(int slot)
{
    if (!collide_sprite_tiles(slot, 10, NULL, NULL) && --g_state.obj[slot].o_8 > 0) {
        int o = collide_sprites(slot, 0x10, 0x22);
        if (o == -1) {
            obj_set_callback(slot, 0, CB_1e050, 1);
            return;
        }
        homer_explode(o);
    }
    homer_explode(slot);
}

/* Every half animation: while the player is within 0x12c px, a shot up and
 * to the side it faces. */
void cb_floor_gun(int slot)
{
    int16_t c = g_anims[ANIM_49850].count;
    obj_set_callback(slot, 0, CB_171a0, c >= 0 ? c >> 1 : ~(~c >> 1));
    if (!player_near(slot, 0x12c00, 0x12c00)) return;
    int b = obj_find_free(0xd8, 0x113, false);
    if (b == -1) return;
    obj_spawn(b, 0x58a, quad_x0(slot), quad_y0(slot), 0xc);
    g_state.obj[b].o_0 = 200;
    g_state.obj[b].o_4 = 0;
    int32_t vx = 0x200;
    if (obj_sprite_bank(slot) == 0x49d) {
        obj_set_orient(b, 4);
        vx = -0x200;
    }
    obj_set_velocity(b, vx, -0x200);
    obj_set_callback(b, 0, CB_18dc0, 1);
}

/* Every 16 frames, while the player is near: a shot along the barrel, whose
 * angle follows the animation frame (bank 0x4d1 + n: 5n degrees). */
void cb_corner_gun(int slot)
{
    obj_set_callback(slot, 0, CB_17734, 0x10);
    if (!player_near(slot, 0x19000, 0xc800)) return;
    int32_t n = obj_sprite_bank(slot) - 0x4d1, angle = n * 5;
    switch (g_objects[slot].orient) {                 /* 0x2be04 */
    case 0: angle += 0x5a; break;
    case 2: angle += 0x10e; break;
    case 4: angle = 0x10e - n * 5; break;
    case 6: angle = 0x5a - n * 5; break;
    default: break;
    }
    bullet_spawn(0x514, 0x11, quad_x0(slot), quad_y0(slot), 0, 0, 6, angle, CB_18dc0, 200);
}

static int32_t next_random(void)
{
    g_state.f_015++;
    return g_random_table[g_state.f_015 & 0xff];
}

static int32_t sar_n(int32_t v, int n) { return v >= 0 ? v >> n : ~(~v >> n); }

/* Wakes when the player is within ~90 px. */
void cb_mine_idle(int slot)
{
    int32_t dx = sar_n(obj_x(slot) - obj_x(SLOT_PLAYER), 8), dy = sar_n(obj_y(slot) - obj_y(SLOT_PLAYER), 8);
    if (dy * dy + dx * dx < 0x1fa4) {
        obj_set_callback(slot, 0, CB_1db04, 1);
    } else {
        int16_t c = g_anims[ANIM_4c880].count;
        obj_set_callback(slot, 0, CB_1da5c, c >= 0 ? c >> 2 : ~(~c >> 2));
    }
}

static void mine_explode(int slot)
{
    effect_spawn(0x10a, false, obj_x(slot), obj_y(slot));
    obj_kill(slot);
    score_add(true, 0x19);
}

/* First switches to its chase animation; then flies at the player until it
 * touches the level, its o_8 frames run out, or it rams an object of
 * 0x10-0x22 (both explode). */
void cb_mine_chase(int slot)
{
    if (g_objects[slot].anim != ANIM_4cac8) {
        obj_set_anim(slot, ANIM_4cac8, 0);
        obj_set_callback(slot, 0, CB_1db04, g_anims[ANIM_4cac8].count);
        return;
    }
    if (collide_sprite_tiles(slot, 10, NULL, NULL) || --g_state.obj[slot].o_8 < 1) {
        mine_explode(slot);
        return;
    }
    int o = collide_sprites(slot, 0x10, 0x22);
    if (o != -1) {
        mine_explode(o);
        mine_explode(slot);
        return;
    }
    double a = (double)(int32_t)-(obj_y(SLOT_PLAYER) - obj_y(slot));
    double c = (double)(int32_t)(obj_x(SLOT_PLAYER) - obj_x(slot));
    int32_t deg = (int32_t)(atan2(c, a) * 57.2957795);
    int k = deg < 0 ? 360 - (-deg) % 360 : deg % 360;
    obj_set_velocity(slot, g_trig_a[k] / 6, g_trig_b[k] / 6);
    obj_set_callback(slot, 0, CB_1db04, 1);
}

static int32_t next_random_b(void)
{
    g_state.f_019++;
    return g_random_table[g_state.f_019 & 0xff];
}

/* While the player is near: every 1-14 frames a smoke puff (0x1f6-0x259)
 * drifting sideways, gone after 110 frames. */
void cb_vent_puff(int slot)
{
    (void)slot;
    if (!player_near(13, 0x25800, 0x19000)) {
        obj_set_callback(13, 0, CB_145fc, 0x46);
        return;
    }
    obj_set_callback(13, 0, CB_145fc, next_random_b() % 0xe + 1);
    int p = obj_find_free(0x1f6, 0x259, false);
    if (p == -1) return;
    int32_t ry = next_random_b();
    int32_t y = obj_y(13), x = obj_x(13);
    obj_spawn(p, next_random_b() % 4 + 0x2c6, x, y + ry % 0x3f00, 7);
    obj_set_velocity(p, (obj_sprite_bank(p) - 0x2c6) * 0x32 - 0x28a, 0);
    obj_set_callback(p, 0, CB_2c118, 0x6e);
}

/* Hiss level 0-3 by distance (state o_8 of object 13), like cb_crusher_sound. */
void cb_vent_sound(int slot)
{
    (void)slot;
    int32_t a = sar_n(obj_x(SLOT_PLAYER) - obj_x(13), 8) + 0x30;
    int32_t b = sar_n(obj_y(SLOT_PLAYER) - obj_y(13), 8) - 0x20;
    int32_t d = sar_n(sar_n(a * a, 1) + b * b, 0xe);
    int32_t t = d * 4 + 1;
    if (t > 0x45) t = 0x46;
    obj_set_callback(13, 1, CB_14868, t);
    int32_t level = 3 - d < 0 ? 0 : 3 - d;
    int32_t old = g_state.obj[13].o_8;
    if (level == old) {
        if (old == 0) return;
        if (sound_playing(0x1a) || sound_playing(0x1b) || sound_playing(0x1c)) return;
    }
    g_state.obj[13].o_8 = level;
    sound_stop(0x1a);
    sound_stop(0x1b);
    sound_stop(0x1c);
    sound_play(level < 1 ? 3 : level + 0x19);   /* sic: level 0 plays sound 3 */
}

static int cell_at(int32_t x, int32_t y) { return map_cell(sar_n(x, 12), sar_n(y, 12) - 0x50); }

/* Crawls along walls (map code 0x6a = open), shooting at a close player.
 * Each step it may stop for a step when open space lies ahead, then picks
 * one of its four direction animations by probing one side. */
void cb_crawler(int slot)
{
    int32_t x = obj_x(slot), y = obj_y(slot), vx = obj_motion_x(slot), vy = obj_motion_y(slot);
    if (player_near(slot, 0xa000, 0x6400)) aimed_shot(slot);        /* 0x13b78 */
    int16_t steps = g_anims[ANIM_47248].count;
    if (next_random() % 3 == 0 && cell_at(x + vx * 0x20, y + vy * 0x20) == 0x6a) {
        obj_set_callback(slot, 0, CB_13c74, steps);
        return;
    }
    obj_set_callback(slot, 0, CB_13c74, steps - 1);
    int cur = g_objects[slot].anim;
    bool vertical = cur == ANIM_46f40 || cur == ANIM_47248;
    bool turn;
    if (next_random() % 2 == 0) {
        turn = vertical ? cell_at(x - vy * 0x20, y) != 0x6a : cell_at(x, y + vx * 0x20) != 0x6a;
    } else {
        turn = vertical ? cell_at(x + vy * 0x20, y) == 0x6a : cell_at(x, y - vx * 0x20) == 0x6a;
    }
    int next;
    if (turn) {
        if (cur == ANIM_46f40) next = ANIM_47550;
        else if (cur == ANIM_47248) next = ANIM_47858;
        else if (cur == ANIM_47550) next = ANIM_47248;
        else next = ANIM_46f40;
    } else {
        if (cur == ANIM_46f40) next = ANIM_47858;
        else if (cur == ANIM_47248) next = ANIM_47550;
        else if (cur == ANIM_47550) next = ANIM_46f40;
        else next = ANIM_47248;
    }
    obj_set_anim(slot, next, 0);
}

/* ------------------------------------------------- striders 0x55-0x58 */

typedef struct {
    int orient;
    int self, straight;          /* keep going: callback and animation */
    int dx, dy;                  /* probe offset ahead (24.8) */
    int code_a, code_b;          /* map codes forcing / forbidding turn A */
    int anim_a, cb_a, anim_b, cb_b;
} StriderDir;

/* ahead: the player is further along this direction (by 0x40 px);
 * side: the default way to turn. */
static void strider_step(int slot, const StriderDir *d, bool ahead_of_player, bool side)
{
    obj_set_orient(slot, d->orient);
    bool c;
    if (player_near(slot, 0xa000, 0x6400)) {
        bool clear = slot < 0x58 ? collide_sprites(slot, slot + 1, 0x58) == -1 : true;
        bool flip = !(clear && next_random() % 5 != 0);
        if (ahead_of_player &&
            cell_at(obj_x(slot) + d->dx, obj_y(slot) + d->dy) == 0x6a && !flip) {
            obj_set_anim(slot, d->straight, 0);
            obj_set_callback(slot, 0, d->self, g_anims[d->straight].count);
            return;
        }
        c = flip ? !side : side;
    } else {
        c = (next_random() % 2) != 0;
    }
    int code = cell_at(obj_x(slot), obj_y(slot));
    if (code == d->code_a || (c && code != d->code_b)) {
        obj_set_anim(slot, d->anim_a, 0);
        obj_set_callback(slot, 0, d->cb_a, g_anims[d->anim_a].count);
    } else {
        obj_set_anim(slot, d->anim_b, 0);
        obj_set_callback(slot, 0, d->cb_b, g_anims[d->anim_b].count);
    }
}

void cb_strider_down(int slot)
{
    static const StriderDir d = { 2, CB_1c908, ANIM_4bb40, 0, 0x3000, 0x56, 0x57,
                                  ANIM_4b470, CB_1cb08, ANIM_4b608, CB_1cd08 };
    bool ahead = obj_y(slot) < obj_y(SLOT_PLAYER) - 0x4000;
    strider_step(slot, &d, ahead, obj_x(SLOT_PLAYER) < obj_x(slot));
}

void cb_strider_up(int slot)
{
    static const StriderDir d = { 0, CB_1cf04, ANIM_4bb40, 0, -0x3000, 0x57, 0x56,
                                  ANIM_4b470, CB_1cd08, ANIM_4b608, CB_1cb08 };
    bool ahead = obj_y(SLOT_PLAYER) + 0x4000 < obj_y(slot);
    strider_step(slot, &d, ahead, obj_x(slot) < obj_x(SLOT_PLAYER));
}

void cb_strider_left(int slot)
{
    static const StriderDir d = { 2, CB_1cb08, ANIM_4bd48, -0x3000, 0, 0x54, 0x55,
                                  ANIM_4b7a0, CB_1cf04, ANIM_4b938, CB_1c908 };
    bool ahead = obj_x(SLOT_PLAYER) + 0x4000 < obj_x(slot);
    strider_step(slot, &d, ahead, obj_y(SLOT_PLAYER) < obj_y(slot));
}

void cb_strider_right(int slot)
{
    static const StriderDir d = { 0, CB_1cd08, ANIM_4bd48, 0x3000, 0, 0x55, 0x54,
                                  ANIM_4b7a0, CB_1c908, ANIM_4b938, CB_1cf04 };
    bool ahead = obj_x(slot) < obj_x(SLOT_PLAYER) - 0x4000;
    strider_step(slot, &d, ahead, obj_y(slot) < obj_y(SLOT_PLAYER));
}

/* 0x1c75c / 0x13ac4 / 0x1249c: the hit counts unless the object was already
 * hit last frame together with the player or the towed object (the same
 * contact); explode at 0. */
static void tough_hit(int slot, int damage, int32_t score)
{
    int32_t mf = g_state.mission_frames;
    bool fresh = g_state.obj[slot].o_4 < mf - 1;
    if (!(damage == 10000 || ((fresh || g_state.obj[SLOT_PLAYER].o_4 < mf - 1) &&
                              (fresh || g_state.obj[15].o_4 < mf - 1))))
        return;
    g_state.obj[slot].o_0 -= damage;
    if (g_state.obj[slot].o_0 >= 1) return;
    effect_spawn(0x10a, false, obj_x(slot), obj_y(slot));
    obj_kill(slot);
    if (!g_state.f_014) g_state.score += (g_state.difficulty + 1) * score;
}

void strider_hit(int slot, int damage) { tough_hit(slot, damage, 0xfa); }
void drone_hit(int slot, int damage) { tough_hit(slot, damage, 0xfa); }
void crawler_hit(int slot, int damage) { tough_hit(slot, damage, 0xfa); }
void hanger_hit(int slot, int damage) { tough_hit(slot, damage, 0x96); }

/* --------------------------------------------- barrier 0xc5-0xcd (0x14128) */

void cb_barrier_reset(int slot) { obj_set_anim(slot, ANIM_478c0, 0); }

void cb_barrier_flash(int slot)
{
    obj_set_anim(slot, ANIM_47a50, 0);
    obj_set_callback(slot, 0, CB_1435c, g_anims[ANIM_47a50].count);
}

void cb_post_flash(int slot)
{
    obj_set_anim(slot, ANIM_47af8, (int16_t)(obj_sprite_bank(slot) - 0x2af));
}

/* --------------------------------------------- shaft guns 0x23-0x26 */

/* Every 20-69 frames, with the player level and near: a shot towards them,
 * from the barrel on that side. */
void cb_hanger_shoot(int slot)
{
    obj_set_callback(slot, 1, CB_1254c, next_random() % 0x32 + 0x14);
    if (!player_near(slot, 0xc800, 0x3200)) return;
    bool right = obj_x(SLOT_PLAYER) > obj_x(slot);
    bullet_spawn(0x528, 0x11, quad_x(slot, right), quad_y(slot, right), 0, obj_motion_y(slot), 5,
                 right ? 0x5a : 0x10e, CB_18dc0, 200);
}

/* Rides while the shaft is open; markers 0x54 / 0x55 push it up / down. */
void cb_hanger_ride(int slot)
{
    int code = cell_at(obj_x(slot), obj_y(slot)) & 0xffff;
    if (code == 0x6a) {
        obj_set_callback(slot, 0, CB_12784, 0x10);
        return;
    }
    if (code == 0x54) obj_add_velocity(slot, 0, -8);
    else if (code == 0x55) obj_add_velocity(slot, 0, 8);
    obj_set_callback(slot, 0, CB_12784, 1);
}

/* Hum level 0-3 by distance from object 0xb (state o_8). */
void cb_machine_sound(int slot)
{
    (void)slot;
    int32_t px = obj_x(SLOT_PLAYER), ref;
    if (px > obj_x(0xb)) ref = px;
    else if (px < obj_x(0xb) - 0x14000) ref = 0xc000;   /* sic */
    else ref = obj_x(0xb);
    int32_t dy = sar_n(obj_y(SLOT_PLAYER) - obj_y(0xb), 8), dx = sar_n(px - ref, 8);
    int32_t d = (int32_t)(isqrt_approx((uint32_t)(dy * dy + dx * dx)) >> 6);
    int32_t t = d * 4 + 1;
    if (t > 0x45) t = 0x46;
    obj_set_callback(0xb, 1, CB_25bcc, t);
    int32_t level = 3 - d < 0 ? 0 : 3 - d;
    int32_t old = g_state.obj[0xb].o_8;
    if (level == old && (old == 0 || sound_playing(0x1f) || sound_playing(0x20) || sound_playing(0x21)))
        return;
    g_state.obj[0xb].o_8 = level;
    sound_stop(0x1f);
    sound_stop(0x20);
    sound_stop(0x21);
    if (level > 0) sound_play(level + 0x1e);
}

/* The worm's and the core's first hit shut the 0x1425c gate. */
static void gate_force_shut(void)
{
    if (obj_motion_x(0xba) != 0 || g_objects[0xbf].anim == ANIM_47ca8) return;
    for (int s = 0xba; s < 0xbc; s++) {
        obj_clear_callbacks(s);
        cb_gate_close(s);
    }
    for (int s = 0xbf; s < 0xc1; s++) {
        obj_clear_callbacks(s);
        obj_set_anim(s, ANIM_47ca8, 0);
    }
}

/* ------------------------------------------------------ the worm 0x67 */

#define WORM 0x67

/* Turn one way (A) at map code a, never at b, else towards the player (or
 * at random while o_c > 0). Once damaged it may not wind more than three
 * turns either way. */
static void worm_turn(bool towards, int code_a, int code_b, int anim_a, int cb_a, int anim_b, int cb_b)
{
    ObjState *st = &g_state.obj[WORM];
    if (st->o_0 < 3000 && iabs(st->o_8) > 3) {
        st->o_8 = st->o_8 < 0 ? -3 : 3;
        st->o_c = 1;
    }
    bool c;
    if (st->o_c == 0) {
        c = towards;
    } else {
        int32_t r = next_random();
        st->o_c--;
        c = (r % 2) != 0;
    }
    int code = cell_at(obj_x(WORM), obj_y(WORM));
    bool a = code == code_a || (c && code != code_b);
    st->o_8 += a ? 1 : -1;
    obj_set_anim(WORM, a ? anim_a : anim_b, 0);
    obj_set_callback(WORM, 0, a ? cb_a : cb_b, g_anims[a ? anim_a : anim_b].count);
}

void cb_worm_down(int slot)
{
    (void)slot;
    worm_turn(obj_y(WORM) < obj_y(SLOT_PLAYER), 0x55, 0x54, ANIM_542e7, CB_22c98, ANIM_54303, CB_23088);
}

void cb_worm_left(int slot)
{
    (void)slot;
    worm_turn(obj_x(SLOT_PLAYER) < obj_x(WORM), 0x56, 0x57, ANIM_542ee, CB_22de8, ANIM_5430a, CB_22f38);
}

void cb_worm_up(int slot)
{
    (void)slot;
    worm_turn(obj_y(SLOT_PLAYER) < obj_y(WORM), 0x54, 0x55, ANIM_542f5, CB_23088, ANIM_54311, CB_22c98);
}

void cb_worm_right(int slot)
{
    (void)slot;
    worm_turn(obj_x(WORM) < obj_x(SLOT_PLAYER), 0x57, 0x56, ANIM_542e0, CB_22f38, ANIM_542fc, CB_22de8);
}

/* A body segment repeats the move of the one ahead. */
void cb_worm_body(int slot)
{
    obj_set_anim(slot, g_objects[slot - 1].anim, 0);
    int a = g_objects[slot].anim;
    obj_set_callback(slot, 0, CB_232c8, a >= 0 ? g_anims[a].count : 1);
}

void cb_worm_explode(int slot)
{
    effect_spawn(slot == WORM ? 0x10a : 0x129, false, obj_x(slot), obj_y(slot));
    obj_kill(slot);
}

/* The first hit also shuts the 0x1425c gate; at 0 the worm goes up tail
 * first and the 0x37 group switches (as 0x127f4). */
void worm_hit(int slot, int damage)
{
    ObjState *st = &g_state.obj[WORM];
    if (st->o_0 == 3000) {
        st->o_c = 0;
        gate_force_shut();
    }
    int32_t mf = g_state.mission_frames;
    if (!(damage == 10000 || g_state.obj[slot].o_4 < mf - 1 || g_state.obj[SLOT_PLAYER].o_4 < mf - 1)) return;
    if (st->o_0 == 0) return;
    st->o_0 -= damage;
    if (st->o_0 < 0) st->o_0 = 0;
    if (st->o_0 != 0) return;
    if (!g_state.f_014) g_state.score += (g_state.difficulty + 1) * 1000;
    group37_switch_hit();
    for (int s = 0x6d, delay = 1; delay != 0x4e; s--, delay += 0xb)
        obj_set_callback(s, 1, CB_22c4c, delay);
}

/* ------------------------------------------------------- drones 0x6e-0x70 */

static int aim_deg(int slot)
{
    double a = (double)(int32_t)-(obj_y(SLOT_PLAYER) - obj_y(slot));
    double c = (double)(int32_t)(obj_x(SLOT_PLAYER) - obj_x(slot));
    int32_t deg = (int32_t)(atan2(c, a) * 57.2957795);
    return deg < 0 ? 360 - (-deg) % 360 : deg % 360;
}

/* Diagonal markers 0x58-0x5b set its course; otherwise it pushes away from
 * a fellow drone, or (each in turn, every third frame, player within
 * 0xd2 x 0x96 px) steers towards the player. */
void cb_drone(int slot)
{
    obj_set_callback(slot, 0, CB_234c8, 1);
    int32_t vx, vy;
    switch (cell_at(obj_x(slot), obj_y(slot)) & 0xffff) {
    case 0x58: vx = -0x80; vy = -0x80; break;
    case 0x59: vx = 0x80; vy = -0x80; break;
    case 0x5a: vx = -0x80; vy = 0x80; break;
    case 0x5b: vx = 0x80; vy = 0x80; break;
    default: {
        int o = collide_sprites(slot, 0x6e, 0x70);
        if (o != -1) {
            obj_set_velocity(slot, sar_n(obj_x(slot) - obj_x(o), 6), sar_n(obj_y(slot) - obj_y(o), 6));
            return;
        }
        if ((uint32_t)(g_state.mission_frames % 3) != (uint32_t)(slot - 0x6e)) return;
        if (iabs(obj_x(slot) - obj_x(SLOT_PLAYER)) > 0xd1ff) return;
        if (iabs(obj_y(slot) - obj_y(SLOT_PLAYER)) > 0x95ff) return;
        int k = aim_deg(slot);
        const Object *ob = &g_objects[slot];
        vy = ((g_trig_b[k] / 0x55 + ob->vy + ob->ay) * 0x3d9) / 1000;
        vx = ((g_trig_a[k] / 0x55 + ob->vx + ob->ax) * 0x3d9) / 1000;
        break;
    }
    }
    obj_set_velocity(slot, vx, vy);
}

/* ------------------------------------------------------- the core 0x3c */

#define CORE 0x3c
#define CORE_PHASE (g_state.obj[CORE].o_c & 0xff)   /* a byte in the original */

/* 0x153c4: two shots spinning out from opposite sides (below 2500 hp). */
static void core_spray(void)
{
    int32_t a = ((g_state.mission_frames >> 2) * 5 - 4) % 0x168;
    if (g_state.obj[CORE].o_8 & 1) a = (a + 0x5a) % 0x168;
    int32_t ox = g_trig_a[a], oy = (g_trig_b[a] * 0x14) / 0x18;
    bullet_spawn(0x550, 0x11, obj_x(CORE) + ox * 8, obj_y(CORE) + oy * 8, 0, 0, 6, a, CB_194e4, 500);
    bullet_spawn(0x550, 0x11, obj_x(CORE) - ox * 8, obj_y(CORE) - oy * 8, 0, 0, 6, a + 0xb4, CB_194e4, 500);
}

/* 0x154b8: two flames from opposite sides, turning with time. */
static void core_flames(void)
{
    int32_t a = ((g_state.mission_frames >> 2) * 5 - 6) % 0x168;
    if (CORE_PHASE == 0) a = 0x167 - a;
    int32_t ox = g_trig_a[a], oy = (g_trig_b[a] * 0x14) / 0x18;
    for (int side = 1; side >= -1; side -= 2) {
        int f = obj_find_free(0xd8, 0x113, false);
        if (f == -1) return;
        obj_spawn(f, 0, obj_x(CORE) + side * ox * 8, obj_y(CORE) + side * oy * 8, 0x24);
        obj_set_anim(f, ANIM_4ae88, 0);
        obj_set_velocity(f, side * sar_n(ox, 2), side * sar_n(oy, 2));
        g_state.obj[f].o_0 = g_anims[ANIM_4ae88].count;
        g_state.obj[f].o_4 = 0;
        obj_set_callback(f, 0, CB_18f2c, 1);
    }
}

/* 63 steps of 8 frames attacking (flames, or in phase 1 below 2500 hp the
 * spinning shots), then a pause animation, alternating phases. */
void cb_core_cycle(int slot)
{
    (void)slot;
    ObjState *st = &g_state.obj[CORE];
    if (st->o_8 == 0x3f) {
        obj_set_anim(CORE, CORE_PHASE == 0 ? ANIM_493c8 : ANIM_49060, 0);
        obj_set_velocity(CORE, 0, 0);
        st->o_8 = 0;
        st->o_c = (st->o_c & ~0xff) | (CORE_PHASE == 0);
        obj_set_callback(CORE, 0, CB_15a18, g_anims[ANIM_49060].count);
        obj_set_callback(CORE, 1, CB_1567c, g_anims[ANIM_49060].count);
        return;
    }
    if (st->o_8++ == 0) obj_set_anim(CORE, CORE_PHASE == 0 ? ANIM_48cf8 : ANIM_48bd0, 0);
    obj_set_callback(CORE, 0, CB_15a18, 8);
    if (CORE_PHASE != 0 && st->o_0 < 0x9c4) {
        obj_set_callback(CORE, 1, 0, -1);
        core_spray();
        return;
    }
    core_flames();
}

/* Drifts towards a close player (away from a far one), plus the push of
 * any direction marker under it. */
void cb_core_move(int slot)
{
    (void)slot;
    obj_set_callback(CORE, 1, CB_1567c, 1);
    int k = aim_deg(CORE);
    if (!player_near(CORE, 0x14000, 0xc800)) k = (k + 0xb4) % 0x168;
    obj_move(CORE, sar_n(g_trig_a[k], 3), sar_n(g_trig_b[k], 3));
    switch (cell_at(obj_x(CORE), obj_y(CORE)) & 0xffff) {
    case 0x54: obj_move(CORE, 0, -0x92); break;
    case 0x55: obj_move(CORE, 0, 0x92); break;
    case 0x56: obj_move(CORE, -0x92, 0); break;
    case 0x57: obj_move(CORE, 0x92, 0); break;
    case 0x58: obj_move(CORE, -0x92, -0x92); break;
    case 0x59: obj_move(CORE, 0x92, -0x92); break;
    case 0x5a: obj_move(CORE, -0x92, 0x92); break;
    case 0x5b: obj_move(CORE, 0x92, 0x92); break;
    default: break;
    }
}

/* A flame lives its o_0 frames; touching the player pushes and burns it. */
void cb_flame(int slot)
{
    if (--g_state.obj[slot].o_0 != 0) {
        obj_set_callback(slot, 0, CB_18f2c, 1);
        if (g.invulnerable) return;
        if (collide_sprites(slot, SLOT_PLAYER, SLOT_PLAYER) == -1) return;
        effect_spawn(0x158, false, obj_x(slot), obj_y(slot));
        obj_set_velocity(SLOT_PLAYER, obj_motion_x(slot), obj_motion_y(slot));
        player_damage(slot, 0x1e);
    }
    obj_kill(slot);
}

void core_hit(int damage)
{
    ObjState *st = &g_state.obj[CORE];
    int32_t mf = g_state.mission_frames;
    if (!(damage == 10000 || st->o_4 < mf - 1 || g_state.obj[SLOT_PLAYER].o_4 < mf - 1)) return;
    if (st->o_0 == 6000) gate_force_shut();
    if (st->o_0 < 1) {
        if (g_objects[CORE].cb_timer[2] == -1) {
            st->o_0 = -0x1e;
            obj_set_callback(CORE, 2, CB_150d8, 1);
        }
    } else {
        st->o_0 -= damage;
        if (st->o_0 < 1) st->o_0 = 0;
    }
}

/* 30 steps of explosions, then the big one: the guns 0x4f-0x54 restart and
 * switches 0xc1-0xc4 all flip. */
void cb_core_dying(int slot)
{
    (void)slot;
    ObjState *st = &g_state.obj[CORE];
    st->o_0++;
    if (st->o_0 < 0) {
        int32_t ry = next_random_b();
        int32_t y = obj_y(CORE);
        int32_t rx = next_random_b();
        int32_t x = obj_x(CORE);
        bool even = next_random_b() % 2 == 0;
        int kind = st->o_0 < -0xf ? (even ? 0x168 : 0x158) : (even ? 0x148 : 0x158);
        effect_spawn(kind, false, x + (rx % 0x28) * 0x100 - 0x1400, y + (ry % 0x20) * 0x100 - 0x1000);
        obj_set_callback(CORE, 2, CB_150d8, next_random() % 6 + 6);
        return;
    }
    if (!g_state.f_014) g_state.score += (g_state.difficulty + 1) * 1000;
    effect_spawn(0x10a, true, obj_x(CORE), obj_y(CORE));
    effect_spawn(0x158, true, obj_x(CORE), obj_y(CORE));
    obj_kill(CORE);
    switch_c4_hit();
    for (int s = 0x4f; s < 0x55; s++)
        if (obj_alive(s)) obj_set_callback(s, 0, CB_17cd0, g_objects[s].orient * 0xc);
    switch_c1_hit();
    switch_c3_hit();
    switch_c2_hit();
}
