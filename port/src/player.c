#include "player.h"
#include "render.h"
#include "palette.h"

#include <stdlib.h>

#include "anims.h"
#include "callbacks.h"
#include "demo.h"
#include "frame.h"
#include "game.h"
#include "hits.h"
#include "hud.h"
#include "joystick.h"
#include "menu.h"
#include "tow.h"
#include "kbd.h"
#include "missions.h"
#include "objects.h"
#include "platform.h"
#include "res.h"
#include "tables.h"
#include "timing.h"
#include "sound.h"
#include "unported.h"
#include "weapons.h"

enum {
    CTRL_LEFT = 1, CTRL_RIGHT = 2, CTRL_THRUST = 4, CTRL_FIRE = 8,
    CTRL_PAUSE = 0x10, CTRL_F1 = 0x20, CTRL_ESC = 0x40,
};

/* 0x18a14 without the joystick part: during a demo, any released key aborts. */
static bool demo_abort_key(void)
{
    if (kbd_first_released() != 0) {
        kbd_clear_all();
        return true;
    }
    return false;
}

/* 0x1f6a0. The order and number of input_was_* calls (15 per frame) must
 * match the original exactly: demos record which of these calls returned
 * true, by count. */
void player_input(void)
{
    joystick_poll();                   /* 0x18524 */
    if (demo_playing() && demo_abort_key()) g.abort_session = true;

    g.controls = 0;
    for (int k = 0; k < 4; k++) {
        uint8_t key = g.keys[k];
        if (input_was_released(key)) {
            kbd_clear_released(key);   /* key let go: "held" ends */
            kbd_clear_pressed(key);
        }
        if (input_was_pressed(key)) g.controls += 1u << k;
    }
    if (input_was_pressed(KEY_P)) {
        g.controls += CTRL_PAUSE;
        kbd_clear_pressed(KEY_P);
    }
    if (input_was_pressed(0x3b)) {             /* F1 */
        g.controls += CTRL_F1;
        kbd_clear_pressed(0x3b);
    }
    if (input_was_pressed(KEY_ESC)) {
        g.controls += CTRL_ESC;
        kbd_clear_pressed(KEY_ESC);
    }
    /* F5-F8: cheat keys, active only with the cheat flag. */
    if (input_was_released(0x3f)) {
        if (g.cheats) { weapon_set(g_state.weapon + 1); hud_message(0xd2, "CHEAT : WEAPON %d", g_state.weapon + 1); }
        kbd_clear_released(0x3f);
    }
    if (input_was_released(0x40)) {
        if (g.cheats) { hud_message(0xd2, "CHEAT : HEALTH"); SHIELDS = g_state.shields_max; }
        kbd_clear_released(0x40);
    }
    if (input_was_released(0x41)) {
        if (g.cheats) { hud_message(0xd2, "CHEAT : LEVEL SKIPPED"); g.mission_ended = true; }
        kbd_clear_released(0x41);
    }
    if (input_was_released(0x42)) kbd_clear_released(0x42);
}

/* 0x20358: turn in 10-degree steps every 2nd frame (every 3rd at stage 2).
 * The quadrant is the object orientation; within it, one of nine
 * consecutive animations (ANIM_4fcb8 + n) gives the 10-degree step. */
void player_rotate(void)
{
    int32_t mf = g_state.mission_frames;
    if ((g_state.stage != 2 && (mf & 1)) || (g_state.stage == 2 && mf % 3 == 0)) {
        if (g.controls & CTRL_LEFT) g_state.angle = (g_state.angle + 350) % 360;
        if (g.controls & CTRL_RIGHT) g_state.angle = (g_state.angle + 10) % 360;
    }
    int32_t a = g_state.angle;
    obj_set_orient(SLOT_PLAYER, (int16_t)(a / 90));
    obj_set_anim(SLOT_PLAYER, ANIM_4fcb8 + (a / 10 - (a / 90) * 9), g_objects[SLOT_PLAYER].anim_frame);
}

static int32_t sar(int32_t v, int n) { return v >= 0 ? v >> n : ~(~v >> n); }

#define SOUND_ENGINE 0x18
#define OBJ_EXIT 15

/* 0x14774: air current. Map code 0x52 adds -u48 (the mission's) to the
 * horizontal force. */
int32_t air_current(int32_t x, int32_t y)
{
    if (g.mission_type == MISSION_BONUS || y <= 0x4ffff) return 0;
    const FrameSlot *map = &g_slots[3];
    int col = sar(x, 12), row = sar(y, 12) - 0x50;
    if (!map->cells || col < 0 || row < 0 || col >= map->w || row >= map->h) return 0;
    return map->cells[row * map->w + col].chunk == 0x52 ? -g_missions[g_state.mission].u48 : 0;
}

/* Drag: scale by factor/1024 rounding toward zero, clamp to +-0x700. */
static int32_t drag(int32_t v, int32_t factor)
{
    int32_t d = v < 0 ? -sar(factor * -v, 10) : sar(v * factor, 10);
    if (d > 0x700) d = 0x700;
    if (d < -0x700) d = -0x700;
    return d;
}

/* 0x20488 */
void player_move(void)
{
    const MissionDef *m = &g_missions[g_state.mission];
    int32_t a = g_state.angle, tx = 0, ty = 0;
    if (!(g.controls & CTRL_THRUST)) {
        sound_stop(SOUND_ENGINE);
    } else {
        if (!sound_playing(SOUND_ENGINE)) sound_play(SOUND_ENGINE);
        int32_t thrust = (&m->u0c)[g_state.stage];   /* u0c / u10 / u14 by stage */
        tx = sar(g_trig_a[a] * thrust, 10);
        ty = sar(g_trig_b[a] * thrust, 10);
        /* Exhaust particle behind the ship with a randomised velocity. */
        int p = obj_find_free(0x168, 0x199, false);
        if (p != -1) {
            obj_spawn(p, 0x2c6, obj_x(SLOT_PLAYER) - g_trig_a[a], obj_y(SLOT_PLAYER) - g_trig_b[a], 6);
            obj_set_anim(p, ANIM_48228, 0);
            int32_t my = obj_motion_y(SLOT_PLAYER);
            g_state.f_019++;
            int32_t ry = g_random_table[g_state.f_019 & 0xff];
            int32_t mx = obj_motion_x(SLOT_PLAYER);
            g_state.f_019++;
            int32_t rx = g_random_table[g_state.f_019 & 0xff];
            obj_set_velocity(p, rx % 0xa0 + (mx - sar(g_trig_a[a], 1)) - 0x50,
                             ry % 0xa0 + (my - sar(g_trig_b[a], 1)) - 0x50);
            obj_set_callback(p, 0, CB_144b8, 1);
        }
    }
    int32_t dvx = drag(obj_motion_x(SLOT_PLAYER), m->u18);
    int32_t dvy = drag(obj_motion_y(SLOT_PLAYER), m->u1c);
    int32_t current = air_current(obj_x(SLOT_PLAYER), obj_y(SLOT_PLAYER));
    int32_t fx = dvx * 4 + tx + current, fy = m->u04 + dvy * 4 + ty;
    if (g_state.stage == 2) {
        tow_move(fx, fy);
        return;
    }
    int32_t vx = sar(fx, 2);
    int32_t vy = sar(fy, 2) + (m->u04 != 0);
    obj_set_velocity(SLOT_PLAYER, vx, vy);
}

void cb_player_tick(int slot)
{
    (void)slot;
    player_input();
    player_rotate();
    player_fire();                     /* 0x1fbbc */
    player_move();                     /* 0x20488 */
    player_hits();                     /* 0x1e888 */
    if (SHIELDS > 0) obj_set_callback(SLOT_PLAYER, 0, CB_20318, 1);
}

/* 0x15bbc */
void game_tick(void)
{
    int rate = g.full_rate ? TF_RATE_FULL : TF_RATE_HALF;
    {   /* TF_SAVE_TICK=n: behave as if a save point were collected at game tick n (testing). */
        static long ticks;
        const char *e = getenv("TF_SAVE_TICK");
        if (e && ++ticks == atol(e)) g.save_requested = true;
        /* TF_KILL_TICK=n: lose the last life at game tick n (testing). */
        static long kticks;
        const char *k = getenv("TF_KILL_TICK");
        if (k && ++kticks == atol(k)) {
            g_state.f_00c = 1;
            SHIELDS = 0;
        }
    }
    if (g.save_requested) {
        tf_set_frame_rate(TF_RATE_FULL);
        save_point_menu();             /* 0x16f60 */
        g.save_requested = false;
        tf_set_frame_rate(rate);
    }
    if (g.controls & CTRL_ESC) ingame_menu();
    if (g.controls & CTRL_F1) {        /* the instructions */
        tf_set_frame_rate(TF_RATE_FULL);
        sound_pause();
        palette_fade_start(&g_pal_black, 0x10);
        for (int i = 0; i < 0x20; i++) render_present();
        pages_show(0x5b, 4);
        frame_render(1);
        frame_render(1);
        palette_fade_start(&g_pal_active, 0x10);
        for (int i = 0; i < 0x20; i++) render_present();
        tf_set_frame_rate(rate);
    }
    if (g.controls & CTRL_PAUSE) {
        tf_set_frame_rate(TF_RATE_FULL);
        hud_message(2, MSG_PAUSED);
        frame_render(1);
        frame_render(1);
        sound_pause();                 /* 0x2e744 */
        /* These waits use the demo-aware queries, as the original did. */
        while (!input_was_released(KEY_P) && !g.abort_session) {
            if (!platform_poll()) g.abort_session = true;
        }
        kbd_clear_all();
        while (!input_was_released(KEY_P) && !input_was_released(KEY_ESC) &&
               !input_was_released(KEY_SPACE) && !g.abort_session) {
            if (!platform_poll()) g.abort_session = true;
        }
        kbd_clear_all();
        tf_set_frame_rate(rate);
    }
    g.controls = 0;
    if (tf_quit_requested()) g.abort_session = true;
}
