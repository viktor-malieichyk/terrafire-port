/* Attract mode, demo start-up, a new game, the session loop, mission start
 * and end: ports of 0x13f00, 0x13f44, 0x17064, 0x166e8, 0x1a26c, 0x1a23c,
 * 0x25890 (beam in), 0x19fe8 (mission end), 0x2594c (beam out) and 0x1fa04
 * (ship explodes). */
#include "session.h"

#include "anims.h"
#include "demo.h"
#include "effects.h"
#include "frame.h"
#include "game.h"
#include "hud.h"
#include "kbd.h"
#include "levels.h"
#include "missions.h"
#include "objects.h"
#include "palette.h"
#include "player.h"
#include "render.h"
#include "res.h"
#include "tables.h"
#include "spawn.h"
#include "bonus.h"
#include "callbacks.h"
#include "timing.h"
#include "sound.h"
#include "save.h"
#include "screens.h"
#include "unported.h"
#include "weapons.h"

#include <SDL3/SDL.h>
#include <stdlib.h>

#define BRIEFING_MIN_PIT 0x246666   /* briefing stays up at least ~2.0 s */
#define BRIEFING_BANK 0x730         /* resident once a level has been loaded */

static int s_tick_limit = -1;       /* testing aid: abort the session after N ticks */
static int s_ticks;

void session_set_tick_limit(int ticks) { s_tick_limit = ticks; }
int session_ticks(void) { return s_ticks; }

/* 0x1a23c: has the current mission been completed? */
static bool mission_complete(void)
{
    if (g.mission_type != MISSION_NORMAL) {
        /* Bonus level: done ~200 ticks after every enemy slot is empty. */
        static const int ranges[3][2] = { { 0x71, 0xba }, { 0xce, 0xd8 }, { 0x118, 0x168 } };
        for (int r = 0; r < 3; r++)
            for (int slot = ranges[r][0]; slot < ranges[r][1]; slot++)
                if (obj_alive(slot)) return false;
        int32_t *timer = &g_state.obj[SLOT_BONUS_TIMER].o_0;
        if (*timer > 0) *timer = 0;
        (*timer)--;
        return *timer < -200;
    }
    /* Normal mission: done once towing (stage 2) with the ship above y = 900. */
    return g_state.stage == 2 && obj_y(SLOT_PLAYER) < 0x38400;
}

/* 0x25890: the player beams in. Frozen with no callbacks, the palette fades
 * in over 32 frames, then 99 frames of a materialising effect; then the
 * player's per-frame callback starts. 131 frames in all. */
static void fade_in(void)
{
    obj_freeze(SLOT_PLAYER);
    obj_clear_callbacks(SLOT_PLAYER);
    palette_fade_start(&g_pal_active, 0x20);
    for (int i = 0; i < 0x20; i++) frame_render(1);
    sound_play(0x19);
    for (int32_t r = 0xc800; r != 0x200; r -= 0x200) {
        frame_render(0);
        render_beam(SLOT_PLAYER, r, r);
        render_present();
    }
    obj_unfreeze(SLOT_PLAYER);
    obj_set_callback(SLOT_PLAYER, 0, CB_20318, 1);
    if (g.mission_type == MISSION_BONUS) obj_set_callback(SLOT_PLAYER, 1, CB_1038c, 1);
}

/* 0x15b30: object 10's ambient sound, rescheduled 100-499 frames ahead.
 * Ported exactly because it advances the shared random counter. */
void cb_level_ambient(int slot)
{
    (void)slot;
    g_state.f_019++;
    obj_set_callback(10, 0, CB_15b30, g_random_table[g_state.f_019 & 0xff] % 400 + 100);
    if (obj_y(SLOT_PLAYER) > 0x5ffff) sound_play(3);
}

/* 0x1c6f0: fade to black over 16 frames, presenting 32. */
static void fade_out_black(void)
{
    palette_fade_start(&g_pal_black, 16);
    for (int i = 0; i < 32; i++) tf_present();
}

/* 0x1a26c: set up and load the current mission. */
static void mission_start(void)
{
    GameState *st = &g_state;
    st->f_015 = st->f_00c;
    st->f_019 = st->f_00c + 0x80;
    if (g.load_slot != -1) {
        st->mission = g.save_mission[g.load_slot];
        st->difficulty = g.save_difficulty[g.load_slot];
    }
    st->stage = 0;
    st->angle = 0;
    if (st->mission < 0 || st->mission >= MISSION_COUNT) {
        SDL_Log("mission %d out of range", st->mission);
        g.abort_session = true;
        return;
    }
    g.mission_type = g_missions[st->mission].type;
    st->f_0af = 0;
    for (int i = 0; i < OBJ_SLOTS; i++) st->obj[i].o_4 = 0;
    kbd_clear_all();
    g.controls = 0;
    g.f_5b838 = 0;
    g.mission_ended = false;
    g.mission_start_clock = tf_clock();
    g.f_5b86c = 0;
    g.f_5b834 = 0;
    g.f_5b854 = 0;
    st->mission_frames = 0;
    tf_set_frame_counter(0);

    bool resident = res_bank_loaded(BRIEFING_BANK);
    uint64_t briefing_clock = 0;
    if (!resident) {
        briefing_show();
        briefing_clock = tf_clock();
    }
    if (g.mission_type == MISSION_NORMAL) level_load_normal(); else level_load_bonus();
    hud_load();
    if (g.mission_type == MISSION_NORMAL) level_init_normal(); else level_init_bonus();
    hud_message(0, "");
    if (g.load_slot != -1) save_slot_read(g.load_slot, true);

    if (!resident) {
        tf_wait_clock(briefing_clock + BRIEFING_MIN_PIT);
        kbd_clear_all();
        palette_read(54, &g_pal_active);
        fade_out_black();
        palette_read(55, &g_pal_active);
    }
    palette_fade_start(&g_pal_black, 1);
    frame_render(1);
    tf_set_frame_rate(g.full_rate ? TF_RATE_FULL : TF_RATE_HALF);
    if (g.load_slot == -1) {
        fade_in();
    } else {
        palette_fade_start(&g_pal_active, 16);
        for (int i = 0; i < 32; i++) tf_present();
        g.load_slot = -1;
    }
}

/* 0x1fa04: the ship explodes. The towed object goes with it (white flash)
 * while towing or once its countdown has run out. Then 180 frames. */
static void player_explode(void)
{
    if (obj_alive(0xb)) {
        obj_set_velocity(0xb, 0, 0);
        obj_clear_callbacks(0xb);
    }
    int32_t x = obj_x(SLOT_PLAYER), y = obj_y(SLOT_PLAYER);
    effect_spawn(0x10a, true, x, y);
    int kind = g.mission_type == MISSION_BONUS ? 0x148 : 0x158;
    effect_spawn(kind, true, x + 0x1200, y - 0xe00);
    effect_spawn(kind, true, x + 0x600, y + 0x1300);
    effect_spawn(kind, true, x - 0x1800, y - 0x500);
    obj_freeze(SLOT_PLAYER);
    if (g_state.stage == 2 || (obj_alive(15) && g_state.obj[15].o_0 == 0)) {
        sound_stop(0);
        effect_spawn(0x10a, false, obj_x(15), obj_y(15));
        obj_kill(15);
        g_state.stage = 0;
        palette_fade_start(&g_pal_white, 1);
        render_present();
        palette_fade_start(&g_pal_active, 0x20);
    }
    for (int i = 0; i < 0xb4; i++) frame_render(1);
}

/* Show the first image of the object's animation, frozen. */
static void freeze_on_first_frame(int slot)
{
    if (g_objects[slot].anim >= 0) {
        g_objects[slot].bank = g_anims[g_objects[slot].anim].frames[0].image;   /* 0x2c3e4 */
        g_objects[slot].slot = -1;
    }
    obj_freeze(slot);
    obj_clear_callbacks(slot);
}

/* 0x2594c: the ship (and on normal missions the towed object) beams out,
 * then the screen fades (to black in a demo, else white). */
static void beam_out(void)
{
    bool normal = g.mission_type == MISSION_NORMAL;
    freeze_on_first_frame(SLOT_PLAYER);
    sound_stop(0x18);
    sound_play(0x19);
    if (normal) {
        g_state.stage = 0;
        freeze_on_first_frame(15);
    }
    int32_t r = 0;
    for (int k = 10; k < 0xb6; k += 2) {
        frame_render(0);
        r = g_trig_a[k] * 7;
        render_beam(SLOT_PLAYER, r, r);
        if (normal) render_beam(15, r, r);
        render_present();
    }
    sound_stop(0);
    obj_unfreeze(SLOT_PLAYER);
    obj_set_orient(SLOT_PLAYER, 0);
    obj_set_anim(SLOT_PLAYER, ANIM_4fe38, 0);
    obj_set_velocity(SLOT_PLAYER, 0, 0);
    if (normal) {
        obj_unfreeze(15);
        obj_set_anim(15, ANIM_4fe38, 0);
        obj_set_velocity(15, 0, 0);
    }
    for (int i = 0; i < g_anims[ANIM_4fe38].count; i++) frame_render(1);
    palette_fade_start(demo_playing() ? &g_pal_black : &g_pal_white, 0x20);
    for (int i = 0; i < 0x20; i++) frame_render(1);
    sound_pause();
    resources_reset();
}

/* 0x19fe8: after the mission loop - beam out on success, explode on death,
 * and either move on, lose a life, or end the game or demo. */
static void mission_end(void)
{
    if (g.session_over) {
        beam_out();
        tf_set_frame_rate(TF_RATE_FULL);
        return;
    }
    if (demo_playing()) {
        if (SHIELDS == 0) player_explode();
        if (!g.abort_session && SHIELDS != 0) beam_out();
        else resources_reset();
        tf_set_frame_rate(TF_RATE_FULL);
        palette_fade_start(&g_pal_black, 0x10);
        for (int i = 0; i < 0x20; i++) render_present();
        sound_pause();
        if (!g.abort_session) title_screen();
        return;
    }
    if (mission_complete() || g.mission_ended) {
        beam_out();
        tf_set_frame_rate(TF_RATE_FULL);
        mission_complete_screen();
        if (++g_state.mission > 0x1a) {
            g_state.f_00c = 0;
            game_complete();
            high_score_entry();
        }
        return;
    }
    if (g.abort_session) {
        sound_pause();
        resources_reset();
        tf_set_frame_rate(TF_RATE_FULL);
        return;
    }
    if (g.load_slot != -1) {
        sound_pause();
        if (g_state.mission != g.save_mission[g.load_slot]) resources_reset();
        tf_set_frame_rate(TF_RATE_FULL);
        return;
    }
    if (SHIELDS == 0) player_explode();
    if (--g_state.f_00c == 0) {
        game_over();
        tf_set_frame_rate(TF_RATE_FULL);
        high_score_entry();
        return;
    }
    palette_fade_start(&g_pal_black, 0x20);
    for (int i = 0; i < 0x20; i++) frame_render(1);
    sound_pause();
    static const int32_t shields_by_difficulty[4] = { 600, 425, 275, 125 };
    if (g_state.difficulty >= 0 && g_state.difficulty < 4)
        g_state.shields_max = shields_by_difficulty[g_state.difficulty];
    SHIELDS = g_state.shields_max;
    g_state.obj[SLOT_PLAYER].o_4 = 0;
    g_state.stage = 0;
    g_state.angle = 0;
    g_state.last_shot = 0;
    g_state.last_side_shot = 0;
}

/* 0x166e8: play missions until the session ends. */
void session_run(void)
{
    g.abort_session = false;
    g.in_session = true;
    g.f_56df0 = 0;
    g.f_580e4 = 0;
    s_ticks = 0;
    do {
        mission_start();
        /* Order matters: mission_complete() has side effects (bonus countdown). */
        while (SHIELDS != 0 && !g.abort_session && !mission_complete() &&
               !g.mission_ended && g.load_slot == -1) {
            frame_render(1);
            game_tick();
            if (s_tick_limit >= 0 && ++s_ticks >= s_tick_limit) g.abort_session = true;
        }
        mission_end();
        if (g_state.f_00c == 0 || g.abort_session || demo_playing()) {
            g.in_session = false;
            return;
        }
    } while (!g.session_over);
    g.in_session = false;
}

void game_new(int difficulty, int start_mission, int weapon, int side_ammo)
{
    g_state.difficulty = difficulty;
    g_state.mission = start_mission;
    static const int32_t shields_by_difficulty[4] = { 600, 425, 275, 125 };
    if (difficulty >= 0 && difficulty < 4) g_state.shields_max = shields_by_difficulty[difficulty];
    SHIELDS = g_state.shields_max;
    g_state.obj[SLOT_PLAYER].o_4 = 0;
    g_state.stage = 0;
    g_state.angle = 0;
    g_state.last_shot = 0;
    g_state.last_side_shot = 0;
    g_state.score = 0;
    g_state.f_00c = 3;                               /* lives */
    g_state.f_014 = g.cheats || start_mission || weapon || side_ammo;
    g_state.weapon = 0;
    g_state.side_ammo = 0;
    weapon_set(weapon);                              /* 0x2044c */
    if (side_ammo) g_state.side_ammo = side_ammo;
    g.invulnerable = false;
    session_run();
}

/* 0x13f44: load the current demo and play it. */
void demo_play(void)
{
    Demo d;
    if (!demo_load(g.demo_index, &d)) {
        g.abort_session = true;
        return;
    }
    legacy_load_state(&g_state, d.state);
    /* TF_DEMO_MISSION=n: replay this demo's input on mission n (testing). */
    const char *override = getenv("TF_DEMO_MISSION");
    if (override) g_state.mission = atoi(override);
    static const int32_t shields_by_difficulty[4] = { 600, 425, 275, 125 };
    if (g_state.difficulty >= 0 && g_state.difficulty < 4)
        g_state.shields_max = shields_by_difficulty[g_state.difficulty];
    SHIELDS = g_state.shields_max;
    g.invulnerable = 0;
    g_state.obj[SLOT_PLAYER].o_4 = 0;
    g_state.stage = 0;
    g_state.angle = 0;
    g_state.last_shot = 0;
    g_state.last_side_shot = 0;
    g_state.f_014 = 0;
    if (getenv("TF_INVULNERABLE")) g.invulnerable = true;   /* testing, like 0x5e427 */
    demo_begin(&d);   /* resets the query counter and event index, sets demo mode */
    session_run();
    /* The original leaves demo mode set and lets the caller clear it; the
     * recording is freed here, so stop reading from it. */
    demo_free(&d);
}

/* 0x13f00: cycle through the demos until something aborts. */
void attract_run(void)
{
    kbd_clear_all();
    do {
        demo_play();
        g.demo_index = (g.demo_index + 1) % DEMO_COUNT;
    } while (!g.abort_session);
}
