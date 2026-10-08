#include "spawn.h"

#include <SDL3/SDL.h>
#include <stdbool.h>

#include "anims.h"
#include "callbacks.h"
#include "collision.h"
#include "game.h"
#include "missions.h"
#include "objects.h"
#include "res.h"
#include "tables.h"
#include "unported.h"

#define SLOT_MAP 3           /* object placement map */
#define MAP_ROW_OFFSET 0x50  /* map row 0 is world tile row 0x50 */

#define BANK_PLAYER 0x730      /* player sprites; mission start shows the briefing if absent */
#define SLOT_LIVES_FIRST 0x114  /* life indicators */

/* Map tile centre in 24.8 world coordinates. */
static int32_t tile_centre(int t) { return t * 0x1000 + 0x800; }

/* 0x1d2e0: a life indicator. The first sits up-left of the player with a
 * follow callback; each further one hangs off the previous one. */
void life_indicator_spawn(int s)
{
    int32_t x, y;
    if (s == SLOT_LIVES_FIRST) {
        y = obj_y(SLOT_PLAYER) - 0x5f00;
        x = obj_x(SLOT_PLAYER) - 0x6a00;
    } else {
        const Object *prev = &g_objects[s - 1];
        const BankView *v = &g_banks[prev->bank].view[prev->orient];
        y = prev->y + v->quad_y[0];
        x = prev->x + v->quad_x[0];
    }
    obj_spawn(s, 0, x, y, 0x30);
    if (g.mission_type != MISSION_NORMAL) {
        obj_set_anim(s, ANIM_4bf70, 0);
    } else if (s == SLOT_LIVES_FIRST) {
        obj_set_anim(s, ANIM_4bf70, 0);
        obj_set_callback(s, 0, CB_1d3c8, 1);
    } else {
        obj_set_anim(s, ANIM_4bf70, g_objects[s - 1].anim_frame);
        obj_attach(false, s, -1, s - 1, -1);
    }
}

void cb_lives_follow(int s)
{
    obj_set_pos(s, ((s - SLOT_LIVES_FIRST) * 0xc - 0x6a) * 0x100 + obj_x(SLOT_PLAYER),
                obj_y(SLOT_PLAYER) - 0x5f00);
    obj_set_callback(s, 0, CB_1d3c8, 1);
}

/* 0x20138: player start (spawn code 0). */
void spawn_20138(int col, int row)
{
    if (!res_bank_loaded(BANK_PLAYER)) {
        static const int banks[7][3] = {
            { 0x5e, 0x730, 0x7d7 }, { 0x41, 0x10a, 0x172 }, { 0x46, 0x2c6, 0x2cf },
            { 0x55, 0x514, 0x5ae }, { 0x59, 0x5c1, 0x5d9 }, { 0x5b, 0x67e, 0x691 },
            { 0x61, 0x8e1, 0x8f2 },
        };
        for (int i = 0; i < 7; i++) res_load_banks(banks[i][0], banks[i][1], banks[i][2], false);
    }
    obj_spawn(SLOT_PLAYER, BANK_PLAYER, tile_centre(col), tile_centre(row), 0x1c);
    obj_set_anim(SLOT_PLAYER, ANIM_4fcb8, 0);
    obj_set_callback(SLOT_PLAYER, 0, CB_20318, 5);
    g_state.last_shot = 0;
    g_state.last_side_shot = 0;
    /* One indicator per remaining life (f_00c), in slots 0x114 onwards. */
    for (uint32_t s = SLOT_LIVES_FIRST; s < (uint32_t)g_state.f_00c + SLOT_LIVES_FIRST - 1; s++)
        life_indicator_spawn((int)s);
}

#define OBJ_LEVEL 10
#define SETTLE_LIMIT 4096   /* the original loops until settled; cap it */

/* Settle routines: slide an object one pixel at a time until some point on
 * one of its edges is solid in the object but not in the level. */
static void settle_up(int slot)                  /* 0x245b0: scan the bottom edge, move up */
{
    int32_t x = obj_left(slot);
    for (int moves = 0; moves < SETTLE_LIMIT; ) {
        if (x > obj_right(slot)) {
            obj_move(slot, 0, -0x100);
            moves++;
            x = obj_left(slot);
            continue;
        }
        int32_t y = obj_bottom(slot);
        if (collide_point(slot, slot, x, y) != -1 && collide_point(OBJ_LEVEL, OBJ_LEVEL, x, y) == -1) return;
        x += 0x100;
    }
    SDL_Log("settle_up: object %d never settled", slot);
}

static void settle_sideways(int slot, bool rightwards)   /* 0x24628 / 0x246a0 */
{
    int32_t y = obj_top(slot);
    for (int moves = 0; moves < SETTLE_LIMIT; ) {
        if (y > obj_bottom(slot)) {
            obj_move(slot, rightwards ? 0x100 : -0x100, 0);
            moves++;
            y = obj_top(slot);
            continue;
        }
        int32_t x = rightwards ? obj_left(slot) : obj_right(slot);
        if (collide_point(slot, slot, x, y) != -1 && collide_point(OBJ_LEVEL, OBJ_LEVEL, x, y) == -1) return;
        y += 0x100;
    }
    SDL_Log("settle_sideways: object %d never settled", slot);
}

static void settle_down(int slot)                /* 0x24718: scan the top edge, move down */
{
    int32_t x = obj_left(slot);
    for (int moves = 0; moves < SETTLE_LIMIT; ) {
        if (x > obj_right(slot)) {
            obj_move(slot, 0, 0x100);
            moves++;
            x = obj_left(slot);
            continue;
        }
        int32_t y = obj_top(slot);
        if (collide_point(slot, slot, x, y) != -1 && collide_point(OBJ_LEVEL, OBJ_LEVEL, x, y) == -1) return;
        x += 0x100;
    }
    SDL_Log("settle_down: object %d never settled", slot);
}

#define HP(slot) (g_state.obj[(slot)].o_0)   /* first per-object state value */

static int32_t sar12(int32_t v) { return v >= 0 ? v >> 12 : ~(~v >> 12); }

/* 0x10fd4: object 15, the nuke to tow out. Its o_0 is the self-destruct
 * countdown (in 70-frame steps, see cb_tow_countdown): the mission table's
 * value plus 20/15/10/5 steps per difficulty level below insane. */
void spawn_10fd4(int col, int row)
{
    if (!res_bank_loaded(0xf8)) res_load_banks(0x3d, 0xf8, 0x109, false);
    obj_spawn(15, 0xf8, tile_centre(col), row * 0x1000 + 0x2000, 0x1b);
    settle_up(15);
    obj_set_anim(15, ANIM_44850, 0);
    int32_t bonus = 3 - g_state.difficulty, m = g_state.mission;
    int32_t base = g_missions[m].u3c;
    if (m < 4) HP(15) = base + bonus * 20;
    else if (m < 8) HP(15) = base + bonus * 15;
    else if (m < 24) HP(15) = base + bonus * 10;
    else HP(15) = base + bonus * 5;
}

/* 0x250a0: objects 8 and 9, a structure drawn in two layers (z 5 and 0x26). */
void spawn_250a0(int col, int row)
{
    if (!res_slot_loaded(0x67)) res_load_frames(0x30, 0, 1, (const FrameGroup[]){ { 0, 0x67, 0x6d } });
    int32_t x = col * 0x1000 - g_missions[g_state.mission].u40, y = row * 0x1000 - 0x2000;
    obj_spawn(8, 0, x, y, 5);
    obj_set_slot(8, 0x67);
    obj_set_anim(8, ANIM_54c80, 0);
    obj_spawn(9, 0, x, y, 0x26);
    obj_set_slot(9, 0x6d);
}

/* 0x13bcc */
void spawn_13bcc(int col, int row)
{
    if (!res_bank_loaded(0x265)) res_load_banks(0x43, 0x265, 0x293, false);
    int slot = obj_find_free(0x27, 0x2a, true);
    obj_spawn(slot, 0x265, tile_centre(col), tile_centre(row), 0x11);
    HP(slot) = 0x15e;
    obj_set_anim(slot, ANIM_47248, 0);
    obj_set_callback(slot, 0, CB_13c74, g_anims[ANIM_47248].count - 1);
}

/* 0x14dcc: staggered by slot - each one's callback fires 200 frames later. */
void spawn_14dcc(int col, int row)
{
    if (!res_bank_loaded(0x2e3)) res_load_banks(0x48, 0x2e3, 0x310, false);
    int slot = obj_find_free(0x37, 0x3b, true);
    obj_spawn(slot, 0x2e3, col << 12, row * 0x1000 + 0xc00, 0x22);
    obj_set_anim(slot, ANIM_48aa8, (int16_t)((slot * 5) % g_anims[ANIM_48aa8].count));
    settle_up(slot);
    obj_set_callback(slot, 0, CB_14d50, (slot - 0x37) * 200 + 1);
    if (slot == 0x37) {
        g_state.obj[0x37].o_8 = 0;
        obj_set_callback(0x37, 1, CB_14eac, 1);
    }
}

/* 0x1762c: orient 0 hangs from the ceiling, orient 2 stands on the floor. */
void spawn_1762c(int col, int row, int orient)
{
    if (!res_bank_loaded(0x4c1)) res_load_banks(0x54, 0x49d, 0x513, false);
    int slot = obj_find_free(0x40, 0x47, true);
    if (orient == 0) {
        obj_spawn(slot, 0x4c1, col << 12, row * 0x1000 - 0x1000, 0x12);
        settle_down(slot);
    } else if (orient == 2) {
        obj_spawn(slot, 0x4c1, col << 12, row * 0x1000 + 0x1000, 0x12);
        obj_set_orient(slot, 2);
        settle_up(slot);
    }
    obj_set_anim(slot, ANIM_49a18, 0);
    HP(slot) = 0x46;
    int16_t c = g_anims[ANIM_49a18].count;
    obj_set_callback(slot, 0, CB_17550, c >= 0 ? c >> 1 : ~(~c >> 1));
}

/* 0x17b04: orient 0 on the floor, orient 6 upside down on the ceiling. */
void spawn_17b04(int col, int row, int orient)
{
    if (!res_bank_loaded(0x4e4)) res_load_banks(0x54, 0x49d, 0x513, false);
    int slot = obj_find_free(0x4a, 0x4e, true);
    int32_t x = tile_centre(col);
    if (orient == 0) {
        obj_spawn(slot, 0x4e4, x, row * 0x1000 + 0x1000, 0x12);
        settle_up(slot);
    } else if (orient == 6) {
        obj_spawn(slot, 0x4e4, x, row * 0x1000 - 0x1000, 0x12);
        obj_set_orient(slot, 6);
        settle_down(slot);
    }
    obj_set_anim(slot, ANIM_4a930, 0);
    HP(slot) = 200;
    obj_set_callback(slot, 0, CB_179c8, 0);
}


/* 0x27a48 on the placement map at a world position; -1 outside the map. */
int map_cell(int col, int row)
{
    const FrameSlot *map = &g_slots[SLOT_MAP];
    if (!map->cells || col < 0 || row < 0 || col >= map->w || row >= map->h) return -1;
    return map->cells[row * map->w + col].chunk;
}

int map_code_at(int32_t x, int32_t y)
{
    const FrameSlot *map = &g_slots[SLOT_MAP];
    int col = sar12(x), row = sar12(y) - MAP_ROW_OFFSET;
    if (!map->cells || col < 0 || row < 0 || col >= map->w || row >= map->h) return -1;
    return map->cells[row * map->w + col].chunk;
}

/* 0x1d8fc / 0x1e158: share a bank; start frames staggered by slot. */
void spawn_1d8fc(int col, int row)
{
    if (!res_bank_loaded(0x692)) res_load_banks(0x5c, 0x692, 0x72f, false);
    int slot = obj_find_free(0x10, 0x16, true);
    obj_spawn(slot, 0x692, tile_centre(col), tile_centre(row), 0xd);
    obj_set_anim(slot, ANIM_4c3f8, (int16_t)((slot * 0x3a) % g_anims[ANIM_4c3f8].count));
    HP(slot) = 100;
}

void spawn_1e158(int col, int row)
{
    if (!res_bank_loaded(0x692)) res_load_banks(0x5c, 0x692, 0x72f, false);
    int slot = obj_find_free(0x1e, 0x22, true);
    obj_spawn(slot, 0x70e, tile_centre(col), tile_centre(row), 0xd);
    obj_set_anim(slot, ANIM_4d3d8, (int16_t)((slot * 0x3a) % g_anims[ANIM_4d3d8].count));
    HP(slot) = 500;
}

/* 0x1c820: picks its behaviour from a direction marker (map codes 0x54-0x57)
 * in the cell to its left. */
void spawn_1c820(int col, int row)
{
    if (!res_bank_loaded(0x5da)) res_load_banks(0x5a, 0x5da, 0x67d, false);
    int slot = obj_find_free(0x55, 0x58, true);
    obj_spawn(slot, 0x5da, col * 0x1000 - 0x800, tile_centre(row), 0x13);
    HP(slot) = 0x8c;
    static const int behaviour[4] = { CB_1cb08, CB_1cd08, CB_1cf04, CB_1c908 };
    int code = map_code_at(obj_x(slot), obj_y(slot));
    if ((unsigned)(code - 0x54) < 4) obj_set_callback(slot, 0, behaviour[code - 0x54], (slot - 0x55) * 0xf);
}

/* 0x1425c: a four-part structure between two wall posts five tiles apart. */
void spawn_1425c(int col, int row)
{
    if (!res_bank_loaded(0x2a3)) res_load_banks(0x45, 0x2a3, 0x2c5, false);
    obj_spawn(0xbc, 0x2b6, col * 0x1000 - 0x5000, row * 0x1000 + 0xa00, 0x20);
    settle_sideways(0xbc, true);
    obj_spawn(0xbd, 0x2b7, col * 0x1000 + 0x5000, obj_y(0xbc), 0x20);
    settle_sideways(0xbd, false);
    int32_t mid = ((obj_x(0xbd) - obj_x(0xbc)) >> 1) + obj_x(0xbc);
    obj_spawn(0xba, 0x2b4, mid, obj_y(0xbc), 0x1f);
    obj_spawn(0xbb, 0x2b5, obj_x(0xba), obj_y(0xba), 0x1f);
}

/* 0x17dec: mounted on the ceiling (0), right wall (1), floor (2) or left wall (3). */
void spawn_17dec(int col, int row, int orient)
{
    if (!res_bank_loaded(0x508)) res_load_banks(0x54, 0x49d, 0x513, false);
    int slot = obj_find_free(0x4f, 0x54, true);
    int32_t x = col * 0x1000, y = row * 0x1000;
    switch (orient) {
    case 0: obj_spawn(slot, 0x508, x + 0x800, y - 0x1000, 0x12); settle_down(slot); break;
    case 1: obj_spawn(slot, 0x508, x + 0x1000, y + 0x800, 0x12); obj_set_orient(slot, 1); settle_sideways(slot, false); break;
    case 2: obj_spawn(slot, 0x508, x + 0x800, y + 0x1000, 0x12); obj_set_orient(slot, 2); settle_up(slot); break;
    case 3: obj_spawn(slot, 0x508, x - 0x1000, y + 0x800, 0x12); obj_set_orient(slot, 3); settle_sideways(slot, true); break;
    default: break;
    }
    g_state.obj[slot].o_8 = -1;
    obj_set_anim(slot, ANIM_4aab8, 0);
    obj_set_callback(slot, 0, CB_17cd0, g_objects[slot].orient * 0xc);
}

/* 0x17464: on a wall, facing right (0) or mirrored (4). */
void spawn_17464(int col, int row, int orient)
{
    if (!res_bank_loaded(0x4c9)) res_load_banks(0x54, 0x49d, 0x513, false);
    int slot = obj_find_free(0x40, 0x47, true);
    if (orient == 0) {
        obj_spawn(slot, 0x4c9, col * 0x1000 + 0x1000, row << 12, 0x12);
        settle_sideways(slot, false);
    } else if (orient == 4) {
        obj_spawn(slot, 0x4c9, col * 0x1000 - 0x1000, row << 12, 0x12);
        obj_set_orient(slot, 4);
        settle_sideways(slot, true);
    }
    obj_set_anim(slot, ANIM_49be0, 0);
    HP(slot) = 0x46;
    int16_t c = g_anims[ANIM_49be0].count;
    obj_set_callback(slot, 0, CB_17384, c >= 0 ? c >> 1 : ~(~c >> 1));
}

/* 0x172b4: a floor gun popping up, the three staggered 0x21 frames apart. */
void spawn_172b4(int col, int row)
{
    if (!res_bank_loaded(0x49d)) res_load_banks(0x54, 0x49d, 0x513, false);
    int slot = obj_find_free(0x3d, 0x3f, true);
    obj_spawn(slot, 0x49d, col << 12, row * 0x1000 + 0x1000, 0x12);
    settle_up(slot);
    obj_set_anim(slot, ANIM_49850, 0);
    int16_t c = g_anims[ANIM_49850].count;
    int32_t half = c >= 0 ? c >> 1 : ~(~c >> 1);
    HP(slot) = 100;
    int32_t delay = (slot - 0x3d) * 0x21;
    obj_set_callback(slot, 0, CB_171a0, delay + half);
    obj_freeze(slot);
    obj_set_callback(slot, 1, CB_2aaa0, delay);
}

/* 0x1783c: a gun in a corner; orient 0/2/4/6 picks which. */
void spawn_1783c(int col, int row, int orient)
{
    if (!res_bank_loaded(0x4d1)) res_load_banks(0x54, 0x49d, 0x513, false);
    int slot = obj_find_free(0x48, 0x49, true);
    int32_t x = col * 0x1000, y = row * 0x1000;
    switch (orient) {
    case 0:
        obj_spawn(slot, 0x4d1, x, y - 0x1000, 0x12);
        settle_down(slot);
        obj_move(slot, -0x1000, 0);
        settle_sideways(slot, true);
        break;
    case 2:
        obj_spawn(slot, 0x4d1, x + 0xf00, y + 0x1000, 0x12);
        obj_set_orient(slot, 2);
        settle_up(slot);
        obj_move(slot, 0x1000, 0);
        settle_sideways(slot, false);
        break;
    case 4:
        obj_spawn(slot, 0x4d1, x + 0xf00, y - 0x1000, 0x12);
        obj_set_orient(slot, 4);
        settle_down(slot);
        obj_move(slot, 0x1000, 0);
        settle_sideways(slot, false);
        break;
    case 6:
        obj_spawn(slot, 0x4d1, x, y + 0x1e00, 0x12);
        obj_set_orient(slot, 6);
        settle_up(slot);
        obj_move(slot, -0x1000, 0);
        settle_sideways(slot, true);
        break;
    default:
        break;
    }
    obj_set_anim(slot, ANIM_4a4a8, 0);
    HP(slot) = 0x96;
    obj_set_callback(slot, 0, CB_17734, 0);
}

/* 0x14128: a barrier: two posts settled against the walls either side and a
 * translucent middle piece halfway between them. */
void spawn_14128(int col, int row)
{
    if (!res_bank_loaded(0x2a3)) res_load_banks(0x45, 0x2a3, 0x2c5, false);
    int32_t y = row << 12;
    int a = obj_find_free(0xc5, 0xca, true);
    obj_spawn(a, 0x2af, col * 0x1000 - 0x2800, y, 0x20);
    settle_sideways(a, true);
    obj_set_anim(a, ANIM_47af8, 0);
    int b = obj_find_free(0xc5, 0xca, true);
    obj_spawn(b, 0x2af, col * 0x1000 + 0x2800, y, 0x20);
    obj_set_orient(b, 4);
    settle_sideways(b, false);
    obj_set_anim(b, ANIM_47af8, 0);
    int c = obj_find_free(0xcb, 0xcd, true);
    int32_t xa = obj_x(a), xb = obj_x(b);
    obj_spawn(c, 0x2a3, (xb - xa >= 0 ? (xb - xa) >> 1 : ~(~(xb - xa) >> 1)) + xa, y, 0x1f);
    g_objects[c].f_0f = 1;                              /* 0x2c410: blended */
    obj_set_anim(c, ANIM_478c0, 0);
}

/* 0x1d9ac: a mine (0x17-0x1b) that wakes when the player comes close. */
void spawn_1d9ac(int col, int row)
{
    if (!res_bank_loaded(0x692)) res_load_banks(0x5c, 0x692, 0x72f, false);
    int slot = obj_find_free(0x17, 0x1b, true);
    obj_spawn(slot, 0x6b6, col * 0x1000 + 0x800, row * 0x1000 + 0x800, 0xd);
    obj_set_anim(slot, ANIM_4c880, 0);
    HP(slot) = 400;
    g_state.obj[slot].o_8 = 800;
    int16_t c = g_anims[ANIM_4c880].count;
    obj_set_callback(slot, 0, CB_1da5c, c >= 0 ? c >> 2 : ~(~c >> 2));
}

/* 0x147c0: object 13, a vent drawn from frame slot 4. */
void spawn_147c0(int col, int row)
{
    if (!res_slot_loaded(4)) res_load_frames(0x1c, 0, 1, (const FrameGroup[]){ { 0, 4, 0xd } });
    obj_spawn(13, 0x2c6, col * 0x1000 - 0x1000, row << 12, 0x29);
    obj_set_slot(13, 4);
    obj_set_anim(13, ANIM_482d0, 0);
    obj_set_callback(13, 0, CB_145fc, 0x14);
    g_state.obj[13].o_8 = 0;
    obj_set_callback(13, 1, CB_14868, 1);
}

/* 0x14cd0: a rock fountain (0x35-0x36), throwing rocks from the start. */
void spawn_14cd0(int col, int row)
{
    if (!res_bank_loaded(0x2d0)) res_load_banks(0x47, 0x2d0, 0x2e2, false);
    int slot = obj_find_free(0x35, 0x36, true);
    obj_spawn(slot, 0x2e2, col << 12, row * 0x1000 + 0x1600, 0x28);
    obj_set_callback(slot, 0, CB_149a0, 1);
}


/* 0x25b0c: object 0xb, the water pump, drawn from frame slot 0x6e (once
 * switch 0xc3 is hit it moves along the map's direction markers). */
void spawn_25b0c(int col, int row)
{
    if (!res_bank_loaded(0xcd)) {
        res_load_frames(0x31, 0xcd, 1, (const FrameGroup[]){ { 0, 0x6e, 0x7d } });
        res_load_banks(0x69, 0xcd, 0xf7, true);
    }
    obj_spawn(0xb, -1, col * 0x1000 - 0x800, row * 0x1000 + 0x800, 0x25);
    obj_set_slot(0xb, 0x6e);
    obj_set_anim(0xb, ANIM_562c0, 0);
    g_state.obj[0xb].o_8 = 0;
    obj_set_callback(0xb, 1, CB_25bcc, 1);
}

/* 0x12664: a gun (0x23-0x26) that rides up (map code 0x54 to its left) or
 * down (0x55) a shaft. */
void spawn_12664(int col, int row)
{
    if (!res_bank_loaded(0x1d2)) res_load_banks(0x3f, 0x1d2, 0x1f1, false);
    int slot = obj_find_free(0x23, 0x26, true);
    obj_spawn(slot, 0x1d2, col * 0x1000 + 0x800, row * 0x1000 + 0x700, 0xe);
    obj_set_anim(slot, ANIM_45650, 0);
    g_state.f_015++;
    obj_set_callback(slot, 1, CB_1254c, g_random_table[g_state.f_015 & 0xff] % 100);
    HP(slot) = 200;
    int code = map_cell(sar12(col * 0x1000 - 0x800), row - 0x50) & 0xffff;
    if (code == 0x54) {
        obj_set_velocity(slot, 0, -0x100);
        obj_set_callback(slot, 0, CB_12784, 8);
    } else if (code == 0x55) {
        obj_set_velocity(slot, 0, 0x100);
        obj_set_callback(slot, 0, CB_12784, 9);
    }
}

/* 0x231d8: the worm: head 0x67 (3000 hit points, shared by the body) and
 * body segments 0x68-0x6d following 16 frames apart. */
void spawn_231d8(int col, int row)
{
    if (!res_bank_loaded(0x8f3)) res_load_banks(0x62, 0x8f3, 0x8f9, false);
    int32_t x = col * 0x1000 - 0x800, y = row * 0x1000 + 0x800;
    obj_spawn(0x67, 0x8f3, x, y, 0x15);
    g_state.obj[0x67].o_0 = 3000;
    g_state.obj[0x67].o_8 = 0;          /* turns right minus turns left */
    g_state.obj[0x67].o_c = 9999999;    /* random turns left; 0 once hit */
    for (int i = 0, slot = 0x68; slot < 0x6e; i++, slot++) {
        obj_spawn(slot, 0x8f4 + i, x, y, 0x14 - i);
        obj_set_callback(slot, 0, CB_232c8, 0x10 * (i + 1));
    }
    obj_set_callback(0x67, 0, CB_22f38, 0);
}

/* 0x237d0: a drone (0x6e-0x70). */
void spawn_237d0(int col, int row)
{
    if (!res_bank_loaded(0x8fa)) res_load_banks(99, 0x8fa, 0x8ff, false);
    int slot = obj_find_free(0x6e, 0x70, true);
    obj_spawn(slot, 0x8fa, col * 0x1000 + 0x800, row * 0x1000 + 0x800, 0x16);
    g_state.f_015++;
    obj_set_anim(slot, ANIM_543d8, (int16_t)(g_random_table[g_state.f_015 & 0xff] % g_anims[ANIM_543d8].count));
    HP(slot) = 200;
    g_state.f_015++;
    obj_set_callback(slot, 0, CB_234c8, g_random_table[g_state.f_015 & 0xff] % 0x10);
}

/* 0x15988: object 0x3c, the core (6000 hit points). */
void spawn_15988(int col, int row)
{
    if (!res_bank_loaded(0x311)) res_load_banks(0x49, 0x311, 0x34d, false);
    obj_spawn(0x3c, 0x311, col * 0x1000 + 0x800, row * 0x1000 + 0x800, 0x23);
    g_state.obj[0x3c].o_0 = 6000;
    g_state.obj[0x3c].o_8 = 0;
    g_state.obj[0x3c].o_c = (g_state.obj[0x3c].o_c & ~0xff) | 1;
    obj_set_callback(0x3c, 0, CB_15a18, 1);
}

/* 0x1ddc4: shares the 0x1d8fc bank; 250 hit points, o_8 = 500. */
void spawn_1ddc4(int col, int row)
{
    if (!res_bank_loaded(0x692)) res_load_banks(0x5c, 0x692, 0x72f, false);
    int slot = obj_find_free(0x1c, 0x1d, true);
    obj_spawn(slot, 0x6ea, tile_centre(col), tile_centre(row), 0xd);
    obj_set_anim(slot, ANIM_4cf50, (int16_t)((slot * 0x3a) % g_anims[ANIM_4cf50].count));
    HP(slot) = 0xfa;
    g_state.obj[slot].o_8 = 500;
}

/* 0x1e2b8: object 12, the crusher - a tile object with its own collision
 * shapes (banks 200-204), moving up and down in a tall clip box. */
void spawn_1e2b8(int col, int row)
{
    if (!res_bank_loaded(200)) {
        res_load_frames(0x2c, 0xc8, 1, (const FrameGroup[]){ { 0, 0x61, 0x65 } });
        res_load_banks(0x5d, 200, 0xcc, true);
    }
    int32_t y = row * 0x1000;
    obj_spawn(12, -1, col * 0x1000 + 0x600, y - 0xa000, 0x24);
    obj_set_slot(12, 0x61);
    settle_up(12);
    obj_set_anim(12, ANIM_4d430, 0);
    obj_set_box(12, 0, y - 0xe000, g_slots[0xe].w_fx12, y + 0x4000);
    obj_set_callback(12, 0, CB_1e3bc, 1);
    g_state.obj[12].o_8 = 0;
    obj_set_callback(12, 1, CB_1e510, 1);
}

/* 0x20d1c: a pod. `contents` is what it holds (a bank id), recorded in the
 * object's state record; some combinations are swapped for 0x816. */
void spawn_20d1c(int col, int row, int contents)
{
    int slot = obj_find_free(0x59, 0x66, true);
    obj_spawn(slot, 0x7d8, col << 12, row * 0x1000 + 0x1000, 0x14);
    settle_up(slot);
    g_state.f_015++;
    obj_set_anim(slot, ANIM_50080,
                 (int16_t)(g_random_table[g_state.f_015 & 0xff] % g_anims[ANIM_50080].count));
    int32_t d = g_state.difficulty, m = g_state.mission;
    bool swap = (contents == 0x7da && g_state.f_01d_flag) ||
                (contents == 0x7f8 && (g_state.f_01e_flag ||
                                       4u - (uint32_t)((d + 1) >> 1) < (uint32_t)g_state.f_00c)) ||
                (contents == 0x7e9 && d == 2 && m % 4 == 1) ||
                (contents == 0x7e9 && d == 3 && m % 4 > 0);
    g_state.obj[slot].o_8 = swap ? 0x816 : contents;
}

/* 0x12bbc: one of six fixed-slot objects resting against a wall; orient 4
 * is the mirrored version. */
void spawn_12bbc(int type, int col, int row, int orient)
{
    if (!res_bank_loaded(0x2b8)) res_load_banks(0x45, 0x2a3, 0x2c5, false);
    static const int slots[6] = { 0xbe, -1, 0xc1, 0xc2, 0xc3, 0xc4 };
    if (type < 0 || type > 5) return;
    int slot = type == 1 ? obj_find_free(0xbf, 0xc0, true) : slots[type];
    if (orient == 0) {
        obj_spawn(slot, 0x2b8, col * 0x1000 - 0x800, row << 12, 0x1e);
        settle_sideways(slot, true);
    } else if (orient == 4) {
        obj_spawn(slot, 0x2b8, col * 0x1000 + 0x1800, row << 12, 0x1e);
        obj_set_orient(slot, 4);
        settle_sideways(slot, false);
    }
    obj_set_anim(slot, ANIM_47ca8, 0);
}

static void spawn_code(int code, int col, int row)
{
    switch (code) {
    case 0x00: spawn_20138(col, row); break;
    case 0x01: spawn_10fd4(col, row); break;
    case 0x02: spawn_250a0(col, row); break;
    case 0x03: spawn_20d1c(col, row, 0x7da); break;
    case 0x04: spawn_20d1c(col, row, 0x7e9); break;
    case 0x05: spawn_20d1c(col, row, 0x7f8); break;
    case 0x06: spawn_20d1c(col, row, 0x807); break;
    case 0x07: spawn_20d1c(col, row, 0x816); break;
    case 0x08: spawn_20d1c(col, row, 0x825); break;
    case 0x09: spawn_20d1c(col, row, 0x834); break;
    case 0x11: spawn_172b4(col, row); break;
    case 0x12: spawn_17dec(col, row, 3); break;
    case 0x13: spawn_17dec(col, row, 1); break;
    case 0x14: spawn_1762c(col, row, 0); break;
    case 0x15: spawn_1762c(col, row, 2); break;
    case 0x16: spawn_17464(col, row, 4); break;
    case 0x17: spawn_17464(col, row, 0); break;
    case 0x18: spawn_1783c(col, row, 0); break;
    case 0x19: spawn_1783c(col, row, 4); break;
    case 0x1a: spawn_1783c(col, row, 6); break;
    case 0x1b: spawn_1783c(col, row, 2); break;
    case 0x1c: spawn_17b04(col, row, 6); break;
    case 0x1d: spawn_17b04(col, row, 0); break;
    case 0x1e: spawn_17dec(col, row, 0); break;
    case 0x1f: spawn_17dec(col, row, 2); break;
    case 0x20: spawn_1d8fc(col, row); break;
    case 0x21: spawn_1d9ac(col, row); break;
    case 0x22: spawn_1ddc4(col, row); break;
    case 0x23: spawn_231d8(col, row); break;
    case 0x24: spawn_237d0(col, row); break;
    case 0x25: spawn_12664(col, row); break;
    case 0x26: spawn_1c820(col, row); break;
    case 0x27: spawn_13bcc(col, row); break;
    case 0x28: spawn_15988(col, row); break;
    case 0x29: spawn_1e158(col, row); break;
    case 0x30: spawn_14128(col, row); break;
    case 0x32: spawn_1425c(col, row); break;
    case 0x38: spawn_1e2b8(col, row); break;
    case 0x39: spawn_14dcc(col, row); break;
    case 0x3a: spawn_25b0c(col, row); break;
    case 0x3b: spawn_14cd0(col, row); break;
    case 0x50: spawn_147c0(col, row); break;
    case 0x5c: spawn_12bbc(0, col, row, 0); break;
    case 0x5d: spawn_12bbc(0, col, row, 4); break;
    case 0x5e: spawn_12bbc(1, col, row, 0); break;
    case 0x5f: spawn_12bbc(1, col, row, 4); break;
    case 0x60: spawn_12bbc(2, col, row, 0); break;
    case 0x61: spawn_12bbc(2, col, row, 4); break;
    case 0x62: spawn_12bbc(3, col, row, 0); break;
    case 0x63: spawn_12bbc(3, col, row, 4); break;
    case 0x64: spawn_12bbc(4, col, row, 0); break;
    case 0x65: spawn_12bbc(4, col, row, 4); break;
    case 0x66: spawn_12bbc(5, col, row, 0); break;
    case 0x67: spawn_12bbc(5, col, row, 4); break;
    default: break;          /* empty or decorative cell */
    }
}

void level_init_normal(void)
{
    objects_clear_active();                         /* 0x2c190 */

    obj_spawn(5, -1, 0, 0, 1);                      /* far parallax layer */
    obj_set_slot(5, 0xf);
    obj_spawn(10, -1, 0, MAP_ROW_OFFSET << 12, 0x27);   /* the level itself */
    obj_set_slot(10, 0xe);
    obj_set_callback(10, 0, CB_15b30, 100);
    if (res_slot_loaded(0x66)) {                    /* two more parallax layers */
        int32_t level_w = g_slots[0xe].w_fx12;
        obj_spawn(6, -1, 0, 0, 3);
        obj_set_slot(6, 0x66);
        obj_set_box(6, 0, 0, level_w, 0x60000);
        obj_spawn(7, -1, 0, 0, 4);
        obj_set_slot(7, 0x60);
        obj_set_box(7, 0, 0, level_w, 0x60000);
    }

    const FrameSlot *map = &g_slots[SLOT_MAP];
    for (int row = MAP_ROW_OFFSET; row < map->h + MAP_ROW_OFFSET; row++)
        for (int col = 0; col < map->w; col++)
            spawn_code(map->cells[(row - MAP_ROW_OFFSET) * map->w + col].chunk, col, row);

    if (obj_alive(0x3c)) {
        for (int s = 0x35; s < 0x37; s++)
            if (obj_alive(s)) obj_set_callback(s, 0, 0, -1);
        for (int s = 0x4f; s < 0x55; s++)
            if (obj_alive(s)) obj_set_callback(s, 0, 0, -1);
    }
}
