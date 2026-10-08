#include "tow.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>

#include "callbacks.h"
#include "collision.h"
#include "effects.h"
#include "enemies.h"
#include "game.h"
#include "hits.h"
#include "hud.h"
#include "missions.h"
#include "objects.h"
#include "pickups.h"
#include "player.h"
#include "tables.h"
#include "sound.h"
#include "unported.h"
#include "weapons.h"

#define OBJ_LEVEL 10
#define OBJ_TOW 15
#define FATAL 10000
#define LAST_HIT(slot) (g_state.obj[(slot)].o_4)   /* mission_frames of the last hit */
#define COUNTDOWN (g_state.obj[OBJ_TOW].o_0)       /* self-destruct, in 70-frame steps; -1 = none */

static int32_t sar(int32_t v, int n) { return v >= 0 ? v >> n : ~(~v >> n); }

static int32_t next_random(void)
{
    g_state.f_015++;
    return g_random_table[g_state.f_015 & 0xff];
}

/* 0x267f0 / 0x2d528: two Newton steps from 2^(floor(log2 n) / 2). */
uint32_t isqrt_approx(uint32_t n)
{
    if (n == 0) return 0;
    int k = 31;
    while (!(n >> k)) k--;
    uint32_t b = 1u << (k >> 1);
    uint32_t a = (n / b + b) >> 1;
    a = (n / a + a) >> 1;
    return a & 0xffff;
}

/* 0x30722 + fistp: truncate an x87 value to an integer. The original's x87
 * runs with precision control at 53 bits (control word 0x127f, set by the C
 * startup 0x33158), so plain double arithmetic reproduces it; only fsin /
 * fcos / fpatan results are wider there before the next operation. */
static int32_t chop(double v) { return (int32_t)trunc(v); }

/* 0x20488, stage 2: the ship (14) and the towed object (15) on a rope of
 * fixed length. Each side's force (fx, fy and the object's own) is split
 * along the rope (tow_angle, radians: 0 = towed object straight above, pi = straight below), the shared
 * part along the rope is averaged over both, and object 15 is then put back
 * on the rope 0x2800 (40 px) from the ship. A rope hanging straight down
 * swings by 0.003 rad towards the object. */
void tow_move(int32_t fx, int32_t fy)
{
    const MissionDef *m = &g_missions[g_state.mission];
    int32_t tx = obj_motion_x(15) * 8 + air_current(obj_x(15), obj_y(15));
    int32_t ty = m->u08 + obj_motion_y(15) * 8;
    double t = g_state.tow_angle;

    /* Force components across the rope: K = -|F| cos(t - atan2(Fx, -Fy) - pi). */
    double d1 = t - atan2(fx, -fy) + -3.14159265;
    int32_t k1 = chop((double)(int32_t)isqrt_approx((uint32_t)fx * (uint32_t)fx + (uint32_t)fy * (uint32_t)fy) * -cos(d1));
    double sin_t = sin(t), cos_t = cos(t);
    int32_t npx = chop(fx - sin_t * k1);
    int32_t npy = chop(fy + cos_t * k1);

    double d2 = t - atan2(tx, -ty) + -3.14159265;
    int32_t k2 = chop((double)(int32_t)isqrt_approx((uint32_t)ty * (uint32_t)ty + (uint32_t)tx * (uint32_t)tx) * -cos(d2));
    int32_t ntx = chop(tx - (double)k2 * sin_t);
    int32_t nty = chop(ty + (double)k2 * cos_t);

    int32_t mean = (k1 + k2) / 0xc;
    int32_t q1 = chop((double)mean * sin_t);
    int32_t q2 = chop((double)mean * -cos_t);
    int32_t pvx = sar(npx, 2) + q1, pvy = sar(npy, 2) + q2;
    int32_t px = obj_x(SLOT_PLAYER) + pvx, py = obj_y(SLOT_PLAYER) + pvy;
    int32_t ovx = sar(ntx, 3) + q1, ovy = sar(nty, 3) + q2;
    ovx -= sar(ovx - pvx, 6);
    ovy -= sar(ovy - pvy, 6);
    int32_t ox = obj_x(15) + ovx, oy = obj_y(15) + ovy;

    t = atan2(ox - px, -(oy - py));
    if (g_state.angle == 0 && ox != px) t += ox > px ? 0.003 : -0.003;
    g_state.tow_angle = t;
    int32_t nx = chop(sin(t) * 10240.0 + px);
    int32_t ny = chop(-cos(t) * 10240.0 + py);
    obj_set_pos(15, nx - ovx, ny - ovy);
    obj_set_velocity(SLOT_PLAYER, pvx, pvy);
    obj_set_velocity(15, ovx, ovy + 2);
}

/* Component of a bounce (bx, by) across the rope, as 0x104a4 / 0x1e648
 * compute it: |b| * -cos(t - atan2(bx, -by) - pi), stored as a double. */
static double rope_part(int32_t bx, int32_t by)
{
    double t = g_state.tow_angle;
    return -cos(t - atan2(bx, -by) + -3.14159265);
}

void tow_pull(int32_t bx, int32_t by)
{
    double lc = (double)(int32_t)isqrt_approx((uint32_t)by * (uint32_t)by + (uint32_t)bx * (uint32_t)bx)
                     * rope_part(bx, by) * 0.8;
    double t = g_state.tow_angle;
    int32_t dvy = chop(-cos(t) * lc), dvx = chop(sin(t) * lc);
    obj_add_velocity(OBJ_TOW, dvx, dvy);
}

static int32_t clamp_bounce(int32_t v)
{
    int32_t a = v < 0 ? -v : v;
    if (a <= 0x15e) return v;
    int32_t r = next_random() % 100 + 300;
    return v < 0 ? -r : r;
}

void tow_hit(int by_obj, int damage, int32_t cx, int32_t cy, int32_t bx, int32_t by)
{
    bx = clamp_bounce(bx);
    by = clamp_bounce(by);
    obj_set_velocity(OBJ_TOW, bx, by);
    int32_t mf = g_state.mission_frames;
    if (LAST_HIT(OBJ_TOW) < mf - 1 || LAST_HIT(by_obj) < mf - 1) {
        double lc = (double)(int32_t)isqrt_approx((uint32_t)bx * (uint32_t)bx + (uint32_t)by * (uint32_t)by)
                         * rope_part(bx, by);
        double t = g_state.tow_angle;
        int32_t dvy = chop(-cos(t) * lc), dvx = chop(sin(t) * lc);
        obj_add_velocity(SLOT_PLAYER, dvx, dvy);
        effect_spawn(0x158, false, cx, cy);
    }
    tow_damage(by_obj, damage);
}

void tow_damage(int by_obj, int damage)
{
    int32_t mf = g_state.mission_frames;
    if (damage != FATAL && mf - 30 <= LAST_HIT(OBJ_TOW) && mf - 30 <= LAST_HIT(by_obj)) return;
    int32_t sixth = g_state.shields_max / 6;
    int32_t lo = sixth > 1 ? 1 : sixth, hi = sixth < 1 ? 1 : sixth;
    bool was_outside = !(lo <= SHIELDS && SHIELDS <= hi);
    int32_t after = SHIELDS - damage;
    if (was_outside && lo <= after && after <= hi) hud_message(0xd2, MSG_SHIELDS_CRITICAL);
    SHIELDS = after < 0 ? 0 : after;
    LAST_HIT(OBJ_TOW) = mf;
    LAST_HIT(by_obj) = mf;
}

/* Every 70 frames: repeat the warning while no other message is up (always
 * in the last 10 steps); at 0 the towed object blows up the ship. */
void cb_tow_countdown(int slot)
{
    (void)slot;
    if (COUNTDOWN > 1) {
        obj_set_callback(OBJ_TOW, 0, CB_10db8, 0x46);
        if (!sound_playing(0)) sound_play(0);
    }
    uint32_t n = (uint32_t)--COUNTDOWN;
    if (g_state.message_time > 0 && (int32_t)n >= 0xb) return;
    if (n == 180 || n == 140 || n == 100 || n == 60 || n == 20) {
        hud_message(0x8c, MSG_EVACUATE);
    } else if (n == 10) {
        hud_message(0x8c, MSG_DETONATION);
    } else if (n == 0) {
        hud_message(0xd2, MSG_AUTO_DESTRUCT);
        tow_damage(OBJ_TOW, FATAL);
    }
}

void tow_update(void)
{
    if (g.invulnerable) return;
    int32_t x = obj_x(OBJ_TOW), y = obj_y(OBJ_TOW);
    int32_t vx = obj_motion_x(OBJ_TOW), vy = obj_motion_y(OBJ_TOW);
    int32_t cx, cy;
    if (collide_sprite_tiles(OBJ_TOW, OBJ_LEVEL, &cx, &cy)) {
        int32_t bx, by;
        bounce_direction(cx, cy, &bx, &by);
        tow_hit(OBJ_LEVEL, (int32_t)((uint32_t)vx * (uint32_t)vx + (uint32_t)vy * (uint32_t)vy) < 0x240001 ? 0x14 : FATAL, cx, cy, bx, by);
        return;
    }
    if (obj_alive(12) && collide_sprite_tiles(OBJ_TOW, 12, &cx, &cy)) {   /* the crusher */
        if (obj_motion_y(12) < 1) tow_hit(12, 0x1e, cx, cy, obj_x(12) < x ? 400 : -400, -vy);
        else tow_damage(12, FATAL);
        return;
    }
    if (obj_alive(0xb) && collide_point(0xb, 0xb, x, y - 0x400) != -1) {
        tow_damage(0xb, FATAL);
        return;
    }
    int32_t mf = g_state.mission_frames;
    for (int o = OBJ_TOW;;) {
        o = collide_sprites(OBJ_TOW, o + 1, 0xd7);
        if (o == -1) return;
        int32_t ox = obj_x(o), oy = obj_y(o);
        (void)obj_motion_x(o);
        int32_t omy = obj_motion_y(o);
        int32_t dx = x - ox, dy = y - oy;
        int32_t mx2 = x - sar(dx, 1), my2 = y - sar(dy, 1);   /* halfway between */
        bool recent = mf - 30 <= LAST_HIT(o);
        switch (obj_type(o)) {
        case 0x10: case 0x17: case 0x1c: case 0x1e:          /* destroyed on contact */
            effect_spawn(0x10a, false, ox, oy);
            obj_kill(o);
            score_add(true, 0x19);
            tow_hit(o, 0x3c, mx2, my2, sar(dx, 4), sar(dy, 4));
            return;
        case 0x23: hanger_hit(o, 0x28); tow_hit(o, 0x32, mx2, my2, sar(dx, 4), sar(dy, 4)); return;
        case 0x27: crawler_hit(o, 0x28); tow_hit(o, 0x32, mx2, my2, sar(dx, 4), sar(dy, 4)); return;
        case 0x2b:
            obj_kill(o);
            tow_hit(o, 0x1e, ox, oy, sar(dx, 4), sar(omy, 1));
            return;
        case 0x37:
            if (recent) return;
            tow_hit(o, 0x28, mx2, y, ox < x ? 0xfa : -0xfa, vy - 0xfa);
            return;
        case 0x3d: case 0x40: case 0x48: case 0x4a: case 0x4f:
            turret_hit(o, 0x28);
            tow_hit(o, 0x28, mx2, my2, sar(dx, 4), sar(dy, 4));
            return;
        case 0x55: strider_hit(o, 10000); tow_hit(o, 0x32, mx2, my2, sar(dx, 4), sar(dy, 4)); return;
        case 0x59: pod_hit(o); tow_hit(o, 0x28, mx2, my2, sar(dx, 4), sar(dy, 4)); return;
        case 0x6e:
            obj_set_velocity(o, sar(ox - x, 6), sar(oy - y, 6));
            drone_hit(o, 0x28);
            tow_hit(o, 0x32, mx2, my2, sar(dx, 4), sar(dy, 4));
            return;
        case 0xba: case 0xc5: case 0xcb:                     /* bounce off */
            if (recent) return;
            if (obj_left(o) <= x && x <= obj_right(o)) {
                int32_t r = next_random();
                tow_hit(o, 0x1e, x, my2, r % 200 - 100, dy < 0 ? -0x200 : 0x200);
            } else {
                tow_hit(o, 0x1e, mx2, my2, dx < 0 ? -0x80 : 0x80, dy < 0 ? -0x100 : 0x100);
            }
            return;
        case 0xbe:                                            /* the fixed objects 0xbe-0xc4 */
            tow_hit(o, 0x1e, mx2, my2, sar(dx, 4), sar(dy, 4));
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
        default: break;                                       /* passes through: check the next one */
        }
    }
}
