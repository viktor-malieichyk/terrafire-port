#include "assets.h"
#include "demo.h"
#include "game.h"
#include "levels.h"
#include "anims.h"
#include "missions.h"
#include "objects.h"
#include "res.h"
#include "render.h"
#include "timing.h"
#include "palette.h"
#include "menu.h"
#include "screens.h"
#include "session.h"
#include "sound.h"
#include "spawn.h"
#include "unported.h"
#include "pak.h"
#include "platform.h"

#include <SDL3/SDL_main.h>
#include <SDL3/SDL_messagebox.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int viewer_run(void);

static const char *s_data_dir;   /* directory holding DATA.PAK and SETTINGS.CFG */

/* Decode every pak entry and print totals; must match re/tools/pak_extract.py. */
static int selftest(void)
{
    int palettes = 0, sounds = 0, tilesets = 0, frames = 0, tiles = 0, compiled = 0,
        banks = 0, sprites = 0, failures = 0;
    for (int i = 0; i < PAK_ENTRIES; i++) {
        if (i >= 52 && i <= 59) {
            uint8_t rgb[768];
            if (palette_load(i, rgb)) palettes++; else failures++;
        } else if (i >= 4 && i <= 24) {
            Sound s;
            if (sound_load(i, &s)) { sounds++; sound_free(&s); } else failures++;
        } else if (i >= 25 && i <= 51) {
            TileSet ts;
            if (tileset_load(i, &ts)) {
                tilesets++;
                frames += ts.frame_count;
                tiles += ts.tile_count;
                for (int t = 0; t < ts.tile_count; t++) compiled += ts.tiles[t].kind == 0;
                tileset_free(&ts);
            } else failures++;
        } else if (i >= 61 && i <= 106) {
            SpriteBank sb;
            if (spritebank_load(i, &sb)) { banks++; sprites += sb.count; spritebank_free(&sb); }
            else failures++;
        }
    }
    printf("palettes %d, sounds %d, tile sets %d (%d frames, %d tiles, %d from compiled sprites), "
           "sprite banks %d (%d sprites), failures %d\n",
           palettes, sounds, tilesets, frames, tiles, compiled, banks, sprites, failures);
    return failures ? 1 : 0;
}

/* Replay each demo's event list through the real query function and check
 * every recorded event fires exactly once, at the query after its value. */
static int demo_check(void)
{
    int failures = 0;
    for (int i = 0; i < DEMO_COUNT; i++) {
        Demo d;
        if (!demo_load(i, &d)) { failures++; continue; }
        demo_begin(&d);
        uint32_t last = d.event_count ? d.events[d.event_count - 1] : 0;
        size_t fired = 0;
        uint32_t first_fire = 0;
        for (uint32_t q = 0; q < last + 10; q++)
            if (input_was_pressed(0x39)) {
                if (!fired) first_fire = input_query_count();
                fired++;
            }
        bool ok = fired == d.event_count && demo_events_used() == d.event_count &&
                  (d.event_count == 0 || first_fire == d.events[0] + 1);
        printf("demo %d: mission %d, difficulty %d, %lu events, last at query %u, "
               "first fired at query %u: %s\n",
               i, d.mission, d.difficulty, (unsigned long)d.event_count, (unsigned)last, (unsigned)first_fire,
               ok ? "OK" : "FAIL");
        failures += !ok;
        demo_free(&d);
    }
    return failures ? 1 : 0;
}

/* Load a mission's resources as mission start does and check the tables:
 * every frame cell must reference a live chunk, and every bank orientation
 * view must visit each stored pixel exactly once. Optionally render the top
 * of the background (slot 14) to a BMP. */
static int load_mission_check(int mission, const char *bmp)
{
    if (mission < 0 || mission >= MISSION_COUNT) return 1;
    game_init_globals();
    game_load_settings(pak_dir());
    objects_init();
    g_state.mission = mission;
    g.mission_type = g_missions[mission].type;
    if (g.mission_type == MISSION_NORMAL) level_load_normal(); else level_load_bonus();
    hud_load();
    int live = 0;
    if (g.mission_type == MISSION_NORMAL) {
        level_init_normal();
        for (int i = objects_first(); i != -1; i = g_objects[i].next) live++;
    }

    int slots = 0, bad_cells = 0;
    for (int s = 0; s < RES_SLOTS; s++) {
        if (!g_slots[s].cells) continue;
        slots++;
        for (int k = 0; k < g_slots[s].w * g_slots[s].h; k++) {
            uint16_t c = g_slots[s].cells[k].chunk;
            if (c >= g_chunk_count || !g_chunks[c].image) bad_cells++;
        }
    }
    int banks = 0, bad_views = 0;
    for (int b = 0; b < RES_BANKS; b++) {
        const Bank *bk = &g_banks[b];
        if (!bk->pixels) continue;
        banks++;
        int size = (bk->view[0].w >> 8) * (bk->view[0].h >> 8);
        unsigned char *seen = calloc((size_t)size, 1);
        for (int k = 0; k < 8 && seen; k++) {
            const BankView *v = &bk->view[k];
            memset(seen, 0, (size_t)size);
            int p = v->start, ok = (v->w >> 8) * (v->h >> 8) == size;
            for (int x = 0; ok && x < (v->w >> 8); x++) {
                for (int y = 0; ok && y < (v->h >> 8); y++) {
                    if (p < 0 || p >= size || seen[p]) ok = 0; else seen[p] = 1;
                    p += v->px_step;
                }
                p += v->col_step;
            }
            bad_views += !ok;
        }
        free(seen);
    }
    printf("mission %d (type %d): %d frame slots, %d chunks, %d banks loaded, %d objects; "
           "bad cells %d, bad bank views %d\n",
           mission, g.mission_type, slots, g_chunk_count, banks, live, bad_cells, bad_views);

    if (bmp && g_slots[14].cells) {
        uint8_t rgb[768];
        palette_load(PAL_GAMEPLAY, rgb);
        platform_set_palette(rgb);
        memset(g_screen, 0, sizeof g_screen);
        const FrameSlot *fs = &g_slots[14];
        for (int ty = 0; ty < fs->h && ty * 16 < SCREEN_H; ty++)
            for (int tx = 0; tx < fs->w && tx * 16 < SCREEN_W; tx++) {
                const TileImage *im = g_chunks[fs->cells[ty * fs->w + tx].chunk].image;
                for (int i = 0; i < 256; i++)
                    if (im->mask[i] && ty * 16 + i / 16 < SCREEN_H)
                        g_screen[(ty * 16 + i / 16) * SCREEN_W + tx * 16 + i % 16] = im->pixels[i];
            }
        platform_save_screenshot(bmp);
    }
    return bad_cells || bad_views;
}

static int s_cb_log[16], s_cb_log_n;
static void test_cb_record(int slot) { if (s_cb_log_n < 16) s_cb_log[s_cb_log_n++] = slot; }

static void test_cb_kill_next(int slot) { obj_kill(31); test_cb_record(slot); }

/* Exercise the object list and update loop against the original's rules. */
static int object_check(void)
{
    int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); fails++; } } while (0)
    objects_init();
    /* z order, ties after existing objects. */
    obj_spawn(10, 0, 0, 0, 5);
    obj_spawn(11, 0, 0, 0, 1);
    obj_spawn(12, 0, 0, 0, 5);
    obj_spawn(13, 0, 0, 0, 3);
    obj_spawn(14, 0, 0, 0, 1);
    int order[8], n = 0;
    for (int i = 11; n < 8 && i != -1; ) {
        order[n++] = i;
        i = g_objects[i].next;
    }
    CHECK(n == 5 && order[0] == 11 && order[1] == 14 && order[2] == 13 && order[3] == 10 && order[4] == 12,
          "active list is z-sorted with ties in insertion order");
    CHECK(obj_find_free(10, 20, false) == 15, "find_free skips live slots");

    /* Killing a parent kills its child and detaches it. */
    g_objects[13].child = 10;
    g_objects[10].parent = 13;
    obj_kill(13);
    CHECK(!obj_alive(13) && !obj_alive(10), "kill takes the attached child with it");
    CHECK(g_objects[11].next == 14 && g_objects[14].next == 12 && g_objects[12].prev == 14, "list relinked after kills");

    /* Movement: velocity += acceleration, then position += velocity. */
    objects_clear_active();
    obj_spawn(20, 0, 0, 0, 0);
    g_objects[20].vx = 0x100;
    g_objects[20].ax = 0x10;
    objects_update();
    objects_update();
    CHECK(g_objects[20].x == 0x110 + 0x120 && g_objects[20].vx == 0x120, "velocity/acceleration integration");

    /* Callbacks: a timer set for n frames fires on the n-th update; killing
     * the next object inside a callback must not break the loop. */
    objects_clear_active();
    obj_spawn(30, 0, 0, 0, 0);
    obj_spawn(31, 0, 0, 0, 1);
    obj_spawn(32, 0, 0, 0, 2);
    s_cb_log_n = 0;
    objects_set_callback_override(test_cb_kill_next);
    obj_set_callback(30, 0, 0, 2);
    objects_update();
    CHECK(s_cb_log_n == 0 && obj_alive(31), "callback not fired early");
    objects_update();
    CHECK(s_cb_log_n == 1 && s_cb_log[0] == 30 && !obj_alive(31), "callback fired on time and killed the next object");
    CHECK(g_objects[30].cb_timer[0] == -1, "callback is one-shot");
    CHECK(g_objects[30].next == 32, "list intact after a kill inside the update");
    objects_set_callback_override(NULL);

    /* A real animation from the game data runs to its end and clears. */
    objects_clear_active();
    obj_spawn(40, 0, 0, 0, 0);
    int anim = -1;
    for (int a = 0; a < ANIM_COUNT && anim < 0; a++) if (!g_anims[a].loop) anim = a;
    obj_set_anim(40, anim, 0);
    for (int i = 1; i < g_anims[anim].count; i++) obj_anim_step(40);
    CHECK(g_objects[40].anim == -1, "non-looping animation clears after its last frame");
    printf("object check: %s (%d failures)\n", fails ? "FAIL" : "OK", fails);
#undef CHECK
    return fails ? 1 : 0;
}

/* Draw a hand-built scene with the renderer: mission N's background (frame
 * slot 14) plus HUD sprites in several orientations and one blended copy. */
static int render_test(int mission, const char *bmp)
{
    if (mission < 0 || mission >= MISSION_COUNT || !bmp) return 1;
    game_init_globals();
    game_load_settings(pak_dir());
    objects_init();
    if (!render_init()) return 1;
    g_state.mission = mission;
    g.mission_type = g_missions[mission].type;
    if (g.mission_type != MISSION_NORMAL) return 1;
    level_load_normal();
    hud_load();

    const int bank = 2008;  /* first bank of entry 95: larger, asymmetric sprites */
    obj_spawn(20, 0, 0, 0, 0x10);
    g_objects[20].slot = 14;
    static const int orient[3] = { 0, 1, 4 };
    for (int i = 0; i < 3; i++) {
        obj_spawn(21 + i, bank, (30 + i * 75) << 8, 20 << 8, 0x20);
        g_objects[21 + i].orient = (int16_t)orient[i];
    }
    obj_spawn(24, bank, 150 << 8, 120 << 8, 0x20);   /* over the stone */
    g_objects[24].f_0f = 1;   /* blend mode */

    tf_set_frame_rate(TF_RATE_FULL);
    render_camera(0, 0);
    memset(g_buf, 0, sizeof g_buf);
    render_build_occlusion();
    render_objects(-1, 0x7fffffff);
    for (int y = 0; y < SCREEN_H; y++)
        memcpy(&g_screen[y * SCREEN_W], &g_buf[(BUF_MARGIN + y) * BUF_W + BUF_MARGIN], SCREEN_W);
    uint8_t rgb[768];
    palette_load(PAL_GAMEPLAY, rgb);
    platform_set_palette(rgb);
    return platform_save_screenshot(bmp) ? 0 : 1;
}

/* Run a demo's real start-up and first game frame, then save the screen.
 * The session has faded out by then, so show it with the gameplay palette. */
static int first_frame(int demo, const char *bmp)
{
    if (demo < 0 || demo >= DEMO_COUNT || !bmp) return 1;
    game_init_globals();
    game_load_settings(pak_dir());
    objects_init();
    palette_read(PAL_FADE_BLACK, &g_pal_black);
    palette_read(PAL_FADE_WHITE, &g_pal_white);
    if (!render_init()) return 1;
    g.demo_index = demo;
    session_set_tick_limit(1);
    demo_play();
    int live = 0;
    for (int i = objects_first(); i != -1; i = g_objects[i].next) live++;
    printf("demo %d: mission %d, %d live objects after the first frame\n", demo, g_state.mission, live);
    uint8_t rgb[768];
    palette_load(PAL_GAMEPLAY, rgb);
    platform_set_palette(rgb);
    return platform_save_screenshot(bmp) ? 0 : 1;
}

/* After a demo's start-up, render one extra frame centred on object SLOT
 * (camera only; no game state changes) so placed objects can be inspected. */
static int view_object(int demo, int slot, const char *bmp)
{
    if (demo < 0 || demo >= DEMO_COUNT || slot < 0 || slot >= OBJ_COUNT || !bmp) return 1;
    game_init_globals();
    game_load_settings(pak_dir());
    objects_init();
    palette_read(PAL_FADE_BLACK, &g_pal_black);
    palette_read(PAL_FADE_WHITE, &g_pal_white);
    if (!render_init()) return 1;
    g.demo_index = demo;
    session_set_tick_limit(1);
    demo_play();
    if (!obj_alive(slot)) {
        printf("object %d is not alive\n", slot);
        return 1;
    }
    printf("object %d: bank %d, slot %d, orient %d, at (%d, %d) px\n", slot, g_objects[slot].bank,
           g_objects[slot].slot, g_objects[slot].orient, g_objects[slot].x >> 8, g_objects[slot].y >> 8);
    render_camera_center(obj_x(slot), obj_y(slot));
    render_build_occlusion();
    render_objects(-1, 0x7fffffff);
    for (int y = 0; y < SCREEN_H; y++)
        memcpy(&g_screen[y * SCREEN_W], &g_buf[(BUF_MARGIN + y) * BUF_W + BUF_MARGIN], SCREEN_W);
    uint8_t rgb[768];
    palette_load(PAL_GAMEPLAY, rgb);
    platform_set_palette(rgb);
    return platform_save_screenshot(bmp) ? 0 : 1;
}

/* Play a demo for TICKS session ticks, then report input-timeline progress
 * and the player's heading, and save the screen. */
static int demo_run(int demo, int ticks, const char *bmp)
{
    if (demo < 0 || demo >= DEMO_COUNT) return 1;
    game_init_globals();
    game_load_settings(pak_dir());
    objects_init();
    palette_read(PAL_FADE_BLACK, &g_pal_black);
    palette_read(PAL_FADE_WHITE, &g_pal_white);
    if (!render_init()) return 1;
    g.demo_index = demo;
    session_set_tick_limit(ticks);
    demo_play();
    printf("demo %d after %d ticks: mission_frames %d, queries %u, events used %lu, angle %d, "
           "player at (%d, %d) px, velocity (%d, %d), shields %d, stage %d, alive %d\n",
           demo, session_ticks(), g_state.mission_frames, (unsigned)input_query_count(),
           (unsigned long)demo_events_used(), g_state.angle, obj_x(SLOT_PLAYER) >> 8, obj_y(SLOT_PLAYER) >> 8,
           g_objects[SLOT_PLAYER].vx, g_objects[SLOT_PLAYER].vy, SHIELDS, g_state.stage,
           obj_alive(SLOT_PLAYER));
    if (!bmp) return 0;
    uint8_t rgb[768];
    palette_load(PAL_GAMEPLAY, rgb);
    platform_set_palette(rgb);
    return platform_save_screenshot(bmp) ? 0 : 1;
}

/* --trace: one CSV row per objects_update() call, same fields as
 * re/tools/trace_original.py records from the original code. */
static FILE *s_trace;
static int s_trace_steps, s_trace_wanted;

static void trace_step(void)
{
    if (!s_trace) return;
    const Object *p = &g_objects[SLOT_PLAYER];
    int live = 0;
    uint32_t objhash = 0;   /* order-independent; same formula as trace_original.py */
    for (int i = objects_first(); i != -1 && live < 1000; i = g_objects[i].next) {
        live++;
        const Object *o = &g_objects[i];
        int32_t x = o->x >= 0 ? o->x >> 8 : ~(~o->x >> 8), y = o->y >= 0 ? o->y >> 8 : ~(~o->y >> 8);
        objhash += (uint32_t)((i * 31 + o->bank * 7 + x * 13 + y * 17) * (i + 1));
    }
    fprintf(s_trace, "%d,%d,%d,%u,%lu,%d,%d,%d,%d,%d,%d,%d,%.9f,%d,%d,%d,%d,%d,%u\n",
            s_trace_steps, g.mission_type, g_state.mission_frames, (unsigned)input_query_count(),
            (unsigned long)demo_events_used(), p->x, p->y, p->vx, p->vy, g_state.angle, SHIELDS, g_state.stage,
            g_state.tow_angle, g_state.f_015, g_state.f_019, live, p->frozen, p->cb_timer[0], objhash);
    /* TF_DUMP_MF=n: also list every live object at that mission frame. */
    const char *dump = getenv("TF_DUMP_MF");
    if (dump && atoi(dump) == g_state.mission_frames)
        for (int i = objects_first(); i != -1; i = g_objects[i].next)
            printf("obj %d bank %d slot %d x %d y %d\n", i, g_objects[i].bank, g_objects[i].slot,
                   g_objects[i].x >> 8, g_objects[i].y >> 8);
    /* TF_DUMP_OBJ=n: print object n's exact position and motion every frame. */
    const char *dobj = getenv("TF_DUMP_OBJ");
    if (dobj) {
        const Object *o = &g_objects[atoi(dobj)];
        printf("o%s mf %d x %d y %d vx %d vy %d ax %d ay %d\n", dobj, g_state.mission_frames,
               o->x, o->y, o->vx, o->vy, o->ax, o->ay);
    }
    if (++s_trace_steps >= s_trace_wanted) g.abort_session = true;
}

static int trace_demo(int demo, int steps, const char *path)
{
    if (demo < 0 || demo >= DEMO_COUNT || !path) return 1;
    s_trace = fopen(path, "w");
    if (!s_trace) return 1;
    fprintf(s_trace, "step,mission_type,mission_frames,queries,events,x,y,vx,vy,angle,shields,"
                     "stage,tow_angle,rand_a,rand_b,objects,p_frozen,p_timer0,objhash\n");
    game_init_globals();
    game_load_settings(pak_dir());
    objects_init();
    palette_read(PAL_FADE_BLACK, &g_pal_black);
    palette_read(PAL_FADE_WHITE, &g_pal_white);
    if (!render_init()) return 1;
    g.demo_index = demo;
    s_trace_wanted = steps;
    tf_set_virtual_clock(true);   /* as fast as possible; same frames */
    objects_set_step_hook(trace_step);
    demo_play();
    objects_set_step_hook(NULL);
    fclose(s_trace);
    s_trace = NULL;
    printf("%d steps traced to %s\n", s_trace_steps, path);
    return 0;
}

/* Render one tile-set frame or sprite to a BMP: --screenshot <entry> <frame/sprite> <out.bmp> */
static int screenshot(int entry, int index, const char *path)
{
    uint8_t rgb[768];
    if (!palette_load(asset_palette(entry), rgb)) return 1;
    platform_set_palette(rgb);
    memset(g_screen, 0, sizeof g_screen);
    if (entry >= 61) {
        SpriteBank sb;
        if (!spritebank_load(entry, &sb) || index >= sb.count) return 1;
        draw_sprite(&sb.sprites[index], 0, 0);
        spritebank_free(&sb);
    } else {
        TileSet ts;
        if (!tileset_load(entry, &ts)) return 1;
        draw_tileframe(&ts, index, 0, 0);
        tileset_free(&ts);
    }
    return platform_save_screenshot(path) ? 0 : 1;
}

int main(int argc, char **argv)
{
    /* Usage: terrafire [mode] [folder with DATA.PAK]. With no mode: the game
 * (splash, title, main menu). Modes: --play, --attract, --viewer, the checks
 * (--selftest, --demo-check, --object-check, --load-mission, --render-test,
 * --first-frame, --view-object, --demo-run, --trace) and --screenshot. */
    int argi = 1;
    enum { RUN, SELFTEST, DEMO_CHECK, SCREENSHOT, ATTRACT, PLAY, LOAD_MISSION, OBJECT_CHECK, RENDER_TEST, FIRST_FRAME,
           VIEW_OBJECT, DEMO_RUN, TRACE, VIEWER } mode = RUN;
    int view_slot = 0;
    int load_mission = 0;
    const char *load_bmp = NULL;
    int attract_ticks = -1;
    int play_difficulty = 1, play_mission = 0;
    int shot_entry = 0, shot_index = 0;
    const char *shot_path = NULL;
    if (argi < argc && strcmp(argv[argi], "--viewer") == 0) {
        mode = VIEWER;
        argi++;
    } else if (argi < argc && strcmp(argv[argi], "--selftest") == 0) {
        mode = SELFTEST;
        argi++;
    } else if (argi + 1 < argc && strcmp(argv[argi], "--load-mission") == 0) {
        /* --load-mission N [OUT.bmp] */
        mode = LOAD_MISSION;
        load_mission = atoi(argv[argi + 1]);
        argi += 2;
        if (argi < argc && strstr(argv[argi], ".bmp")) load_bmp = argv[argi++];
    } else if (argi + 2 < argc && strcmp(argv[argi], "--render-test") == 0) {
        /* --render-test N OUT.bmp */
        mode = RENDER_TEST;
        load_mission = atoi(argv[argi + 1]);
        load_bmp = argv[argi + 2];
        argi += 3;
    } else if (argi + 2 < argc && strcmp(argv[argi], "--first-frame") == 0) {
        /* --first-frame DEMO OUT.bmp */
        mode = FIRST_FRAME;
        load_mission = atoi(argv[argi + 1]);
        load_bmp = argv[argi + 2];
        argi += 3;
    } else if (argi + 3 < argc && strcmp(argv[argi], "--view-object") == 0) {
        /* --view-object DEMO SLOT OUT.bmp */
        mode = VIEW_OBJECT;
        load_mission = atoi(argv[argi + 1]);
        view_slot = atoi(argv[argi + 2]);
        load_bmp = argv[argi + 3];
        argi += 4;
    } else if (argi + 2 < argc && strcmp(argv[argi], "--demo-run") == 0) {
        /* --demo-run DEMO TICKS [OUT.bmp] */
        mode = DEMO_RUN;
        load_mission = atoi(argv[argi + 1]);
        view_slot = atoi(argv[argi + 2]);
        argi += 3;
        if (argi < argc && strstr(argv[argi], ".bmp")) load_bmp = argv[argi++];
    } else if (argi + 3 < argc && strcmp(argv[argi], "--trace") == 0) {
        /* --trace DEMO STEPS OUT.csv */
        mode = TRACE;
        load_mission = atoi(argv[argi + 1]);
        view_slot = atoi(argv[argi + 2]);
        load_bmp = argv[argi + 3];
        argi += 4;
    } else if (argi < argc && strcmp(argv[argi], "--attract") == 0) {
        /* --attract [TICKS]: run attract mode, optionally stopping after TICKS game ticks. */
        mode = ATTRACT;
        argi++;
        if (argi < argc && argv[argi][0] >= '0' && argv[argi][0] <= '9') attract_ticks = atoi(argv[argi++]);
    } else if (argi < argc && strcmp(argv[argi], "--play") == 0) {
        /* --play [DIFFICULTY 0-3 [MISSION 1-27]]: start a new game directly. */
        mode = PLAY;
        argi++;
        if (argi < argc && argv[argi][0] >= '0' && argv[argi][0] <= '9') play_difficulty = atoi(argv[argi++]);
        if (argi < argc && argv[argi][0] >= '0' && argv[argi][0] <= '9') play_mission = atoi(argv[argi++]) - 1;
        if (play_difficulty < 0 || play_difficulty > 3) play_difficulty = 1;
        if (play_mission < 0 || play_mission > 0x1a) play_mission = 0;
    } else if (argi < argc && strcmp(argv[argi], "--object-check") == 0) {
        mode = OBJECT_CHECK;
        argi++;
    } else if (argi < argc && strcmp(argv[argi], "--demo-check") == 0) {
        mode = DEMO_CHECK;
        argi++;
    } else if (argi + 3 < argc + 0 && strcmp(argv[argi], "--screenshot") == 0) {
        mode = SCREENSHOT;
        shot_entry = atoi(argv[argi + 1]);
        shot_index = atoi(argv[argi + 2]);
        shot_path = argv[argi + 3];
        argi += 4;
    }
    const char *data_dir = argi < argc ? argv[argi] : NULL;
    s_data_dir = data_dir;

    if (!pak_open(data_dir)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TerraFire",
                                 "DATA.PAK not found. Put terrafire next to the game's DATA.PAK, "
                                 "or pass the folder containing it on the command line.", NULL);
        fprintf(stderr, "usage: %s [--selftest | --demo-check | --attract [TICKS] | --play [DIFFICULTY [MISSION]] | --screenshot ENTRY INDEX OUT.bmp] [directory containing DATA.PAK]\n",
                argv[0]);
        return 1;
    }
    int rc;
    if (mode == SELFTEST) {
        rc = selftest();
    } else if (mode == DEMO_CHECK) {
        rc = demo_check();
    } else if (mode == OBJECT_CHECK) {
        rc = object_check();
    } else {
        /* Screenshots still need the platform layer for the palette and BMP writer. */
        if (!platform_init("TerraFire", 3)) {
            pak_close();
            return 1;
        }
        if (mode == TRACE) {
            rc = trace_demo(load_mission, view_slot, load_bmp);
        } else if (mode == DEMO_RUN) {
            rc = demo_run(load_mission, view_slot, load_bmp);
        } else if (mode == VIEW_OBJECT) {
            rc = view_object(load_mission, view_slot, load_bmp);
        } else if (mode == FIRST_FRAME) {
            rc = first_frame(load_mission, load_bmp);
        } else if (mode == RENDER_TEST) {
            rc = render_test(load_mission, load_bmp);
        } else if (mode == LOAD_MISSION) {
            rc = load_mission_check(load_mission, load_bmp);
        } else if (mode == ATTRACT) {
            /* Startup loads the fade targets (0x1006c: entries 52 and 58). */
            game_init_globals();
            game_load_settings(pak_dir());
            objects_init();
            palette_read(PAL_FADE_BLACK, &g_pal_black);
            palette_read(PAL_FADE_WHITE, &g_pal_white);
            render_init();
            sound_init(g.sound);
            session_set_tick_limit(attract_ticks);
            attract_run();
            printf("attract: demo %d, mission %d (type %d), difficulty %d, shields %d, "
                   "%d ticks, %d unported routines reached\n",
                   (g.demo_index + DEMO_COUNT - 1) % DEMO_COUNT, g_state.mission, g.mission_type,
                   g_state.difficulty, SHIELDS, session_ticks(), unported_reached());
            rc = 0;
        } else if (mode == PLAY || mode == RUN) {
            game_init_globals();
            game_load_settings(pak_dir());
            objects_init();
            palette_read(PAL_FADE_BLACK, &g_pal_black);
            palette_read(PAL_FADE_WHITE, &g_pal_white);
            render_init();
            sound_init(g.sound);
            sound_set_volume(g.volume);
            if (getenv("TF_CHEATS")) g.cheats = true;   /* testing: F5-F8 as after F4 */
            if (mode == RUN) {
                splash_screen();                 /* 0x1006c */
                title_screen();
                main_menu();
            } else {
                game_new(play_difficulty, play_mission, 0, 0);
                printf("game over: mission %d, score %d, lives %d, %d unported routines reached\n",
                       g_state.mission + 1, g_state.score, g_state.f_00c, unported_reached());
            }
            rc = 0;
        } else {
            rc = mode == SCREENSHOT ? screenshot(shot_entry, shot_index, shot_path) : viewer_run();
        }
        platform_shutdown();
    }
    pak_close();
    return rc;
}
