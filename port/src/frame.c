#include "frame.h"
#include "menu.h"
#include "screens.h"
#include "weapons.h"
#include "effects.h"
#include "callbacks.h"

#include "game.h"
#include "hud.h"
#include "levels.h"
#include "missions.h"
#include "objects.h"
#include "render.h"
#include "res.h"
#include "tables.h"
#include "unported.h"

/* Parallax layer objects positioned from the camera (see frame_normal). */
#define OBJ_LAYER_FAR 7
#define OBJ_LAYER_MID_A 5
#define OBJ_LAYER_MID_B 6
#define OBJ_TOWED 15

#define STAR_FIRST 0x25a
#define STAR_END 0x28c          /* 50 stars */
#define SKY_LIMIT 0x4b000      /* stars exist while the camera is above y = 1200 */

static int32_t sar4(int32_t v) { return v >= 0 ? v >> 4 : ~(~v >> 4); }

/* 0x247cc: parallax starfield. Each star's state record holds a flag byte
 * (o_0, low byte only), its depth 9-13 (o_4) and a base position (o_8, o_c);
 * it is drawn at base + (camera >> 4) * depth and wraps within 320x200. */
static void starfield(void)
{
    int32_t cam_y = render_cam_y();
    if (!obj_alive(STAR_FIRST)) {
        if (cam_y >= SKY_LIMIT) return;
        g_state.f_019 = 5;
        for (int s = STAR_FIRST; s < STAR_END; s++) {
            ObjState *st = &g_state.obj[s];
            st->o_0 &= ~0xff;
            int d = s % 5;
            st->o_4 = d + 9;
            int32_t cx = render_cam_x();
            g_state.f_019++;
            st->o_8 = (cx - st->o_4 * sar4(cx)) + (g_random_table[g_state.f_019 & 0xff] % 0x140) * 0x100;
            int32_t cy = render_cam_y();
            g_state.f_019++;
            st->o_c = (cy - st->o_4 * sar4(cy)) + (g_random_table[g_state.f_019 & 0xff] % 200) * -0x100;
            obj_spawn(s, 0x2c6 + d, st->o_8 + sar4(render_cam_x()) * st->o_4,
                      st->o_4 * sar4(render_cam_y()) + st->o_c, 9 - d);
        }
        return;
    }
    if (cam_y >= SKY_LIMIT) {
        for (int s = STAR_FIRST; s < STAR_END; s++) obj_kill(s);
        return;
    }
    for (int s = STAR_FIRST; s < STAR_END; s++) {
        ObjState *st = &g_state.obj[s];
        int32_t cx = render_cam_x();
        if (obj_x(s) < cx) st->o_8 += 0x14000;
        if (cx + 0x14000 <= obj_x(s)) st->o_8 -= 0x14000;
        int32_t cy = render_cam_y();
        if (obj_y(s) < cy) {
            if ((st->o_0 & 0xff) && st->o_4 * 0x4b00 + st->o_c + 0xc800 < SKY_LIMIT) st->o_c += 0xc800;
        } else {
            st->o_0 = (st->o_0 & ~0xff) | 1;
        }
        if (cy + 0xc800 <= obj_y(s)) st->o_c -= 0xc800;
        obj_set_pos(s, st->o_8 + sar4(render_cam_x()) * st->o_4, st->o_c + sar4(render_cam_y()) * st->o_4);
    }
}

/* 0x226c4 without the debug timing overlay (off unless 0x56df1/0x56df2 are set). */
static void present_frame(uint8_t present)
{
    if (present) g.f_5b838 = render_present();
}

/* Active callback on timer 0, or -1 (0x2aad8). */
static int timer0_cb(int slot) { return g_objects[slot].cb_timer[0] != -1 ? g_objects[slot].cb[0] : -1; }

/* 0x20ab0: shift the player and what travels with it by (dx, dy) when the
 * view wraps: stars (by depth), exhaust and debris, the player's own nearby
 * bullets, on-screen enemies, the towed object. Mines and homers chasing
 * from off screen blow up instead. */
static void world_scroll(int32_t dx, int32_t dy)
{
    if (obj_alive(0x25a))
        for (int s = 0x25a; s < 0x28c; s++) {
            ObjState *st = &g_state.obj[s];
            st->o_8 += dx - sar4(st->o_4 * dx);
            st->o_c += dy - sar4(st->o_4 * dy);
        }
    for (int s = 0x168; s < 0x1ba; s++)
        if (obj_alive(s)) obj_move(s, dx, dy);
    for (int s = 0xd8; s < 0x114; s++) {
        if (!obj_alive(s)) continue;
        int32_t ax = obj_x(s) - obj_x(SLOT_PLAYER), ay = obj_y(s) - obj_y(SLOT_PLAYER);
        if ((ax < 0 ? -ax : ax) < 0xaa00 && (ay < 0 ? -ay : ay) < 0xd200 && timer0_cb(s) == CB_194e4)
            obj_move(s, dx, dy);
    }
    for (int s = 0x17; s < 0x1e; s++) {
        if (!obj_alive(s)) continue;
        if (render_obj_on_screen(s)) {
            obj_move(s, dx, dy);
        } else if (timer0_cb(s) == CB_1db04 || timer0_cb(s) == CB_1e050) {
            effect_spawn(0x10a, false, obj_x(s), obj_y(s));
            obj_kill(s);
            score_add(true, 0x19);
        }
    }
    for (int s = 0x118; s < 0x168; s++)
        if (obj_alive(s) && render_obj_on_screen(s)) obj_move(s, dx, dy);
    if (g_state.stage == 2) obj_move(15, dx, dy);
    obj_move(SLOT_PLAYER, dx, dy);
}

/* 0x1b734 */
static void frame_normal(uint8_t present)
{
    /* Levels wrap horizontally: near either edge, shift the world by the
     * background's width (slot 14). Also keep the player off the top. */
    int32_t level_w = g_slots[SLOT_PLAYER].w_fx12;   /* 0x28bcc(14): slot 14 is the background */
    if (obj_y(SLOT_PLAYER) < 0x60000) {
        if (obj_x(SLOT_PLAYER) < 0xc000)
            world_scroll(level_w - 0x18000, 0);
        else if (obj_x(SLOT_PLAYER) > level_w - 0xc000)
            world_scroll(0x18000 - level_w, 0);
    }
    if (obj_y(SLOT_PLAYER) < 0x6400) world_scroll(0, 0x6400);

    int32_t shake = 0;
    if (g_state.f_0af != 0) {
        uint32_t phase = (uint32_t)g_state.f_0af >> 2;
        g_state.f_0af--;
        shake = (int32_t)(phase & 1) * 0x400 - 0x200;
    }
    render_camera_center(obj_x(SLOT_PLAYER), obj_y(SLOT_PLAYER) + shake);

    if (g.detail < 2) {
        obj_freeze(OBJ_LAYER_FAR);
    } else {
        obj_unfreeze(OBJ_LAYER_FAR);
        obj_set_pos(OBJ_LAYER_FAR, render_cam_x() >> 2, (render_cam_y() >> 2) + 0x3e000);
    }
    if (g.detail < 1) {
        obj_freeze(OBJ_LAYER_MID_A);
        obj_freeze(OBJ_LAYER_MID_B);
        render_fill(0x70, render_cam_x(), render_cam_y(), render_cam_x() + 0x13fff, render_cam_y() + 0xc7ff);
    } else {
        obj_unfreeze(OBJ_LAYER_MID_A);
        obj_unfreeze(OBJ_LAYER_MID_B);
        obj_set_pos(OBJ_LAYER_MID_A, render_cam_x() >> 1, (render_cam_y() >> 1) + 0x30000);
        obj_set_pos(OBJ_LAYER_MID_B, render_cam_x() >> 1, render_cam_y() >> 1);
    }

    starfield();
    tile_animate();
    render_build_occlusion();
    int rest = render_objects(-1, 0x1a);
    int32_t mf = g_state.mission_frames;
    if (SHIELDS != 0 && (g_state.stage == 2 || (g_state.stage == 1 && (mf >> 1 & 1)))) {
        int colour = mf % 30 < 15 ? mf % 15 + 0x20 : 0x2f - mf % 15;
        render_line((uint8_t)colour, obj_x(SLOT_PLAYER), obj_y(SLOT_PLAYER),
                    obj_x(OBJ_TOWED), obj_y(OBJ_TOWED));
    }
    render_objects(rest, 0x31);
    hud_draw();
    present_frame(present);
    g_state.mission_frames++;
    objects_update();
}

/* 0x2224c case 2 */
static void frame_bonus(uint8_t present)
{
    render_camera(50000, 50000);
    render_build_occlusion();
    render_objects(-1, 0x31);
    hud_draw();
    present_frame(present);
    g_state.mission_frames++;
    objects_update();
}

void frame_render(uint8_t present)
{
    switch (g.mission_type) {
    case MISSION_NORMAL: frame_normal(present); break;
    case MISSION_BONUS: frame_bonus(present); break;
    case 0: menu_frame(present, false); break;
    case 3: complete_frame(present); break;
    case 4: high_scores_frame(present); break;
    default: UNPORTED("0x2224c", "unknown screen mode"); break;
    }
}
