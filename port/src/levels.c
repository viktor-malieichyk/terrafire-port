#include "levels.h"
#include "render.h"

#include "fatal.h"
#include "game.h"
#include "kbd.h"
#include "missions.h"
#include "objects.h"
#include "palette.h"
#include "res.h"
#include "save.h"
#include "unported.h"

#define SLOT_HUD_FIRST 0x3f      /* HUD loaded once this slot is resident */
#define SLOT_BONUS_FIRST 0x7e    /* bonus-level graphics resident */
#define SLOT_BACKGROUND 14       /* level graphics resident */

void resources_reset(void)
{
    res_reset();
    render_reset_view();
    objects_clear_active();
    kbd_clear_all();
}

/* Last collision-shape bank per background (0x10058), indexed by entry - 29. */
static const int s_last_shape_bank[5] = { 58, 26, 5, 16, 17 };

void level_load_normal(void)
{
    if (!res_slot_loaded(SLOT_BACKGROUND)) {
        resources_reset();
        const MissionDef *m = &g_missions[g_state.mission];
        int bg = m->background;
        if (bg < 29 || bg > 33) tf_fatal("mission %d: unexpected background %d", g_state.mission, bg);

        res_load_frames(m->gfx_a, 0, 2, (const FrameGroup[]){ { 0, 2, 2 }, { m->skip_b, 3, 3 } });
        if (bg == 29)
            res_load_frames(bg, 0, 2, (const FrameGroup[]){ { 0, 0x11, 0x29 }, { m->skip_b, 14, 14 } });
        else if (bg == 32)
            res_load_frames(bg, 0, 2, (const FrameGroup[]){ { 0, 0x2a, 0x3d }, { m->skip_b, 14, 14 } });
        else
            res_load_frames(bg, 0, 1, (const FrameGroup[]){ { m->skip_b, 14, 14 } });
        res_load_frames(25, 0, 1, (const FrameGroup[]){ { m->skip_a, 15, 15 } });
        res_load_frames(m->gfx_b, 0, 1, (const FrameGroup[]){ { 0, 0x66, 0x66 } });
        res_load_frames(m->gfx_c, 0, 1, (const FrameGroup[]){ { 0, 0x60, 0x60 } });
        res_load_banks(m->sprite_bank, 0, s_last_shape_bank[bg - 29], true);
        res_gc();
        tile_animate();
    }
    /* End of 0x1c220: the pod rules start afresh on every level. */
    g_state.f_01d_flag = 0;
    g_state.f_01e_flag = 0;
}

void level_load_bonus(void)
{
    if (!res_slot_loaded(SLOT_BONUS_FIRST))
        res_load_frames(50, 0, 1, (const FrameGroup[]){ { 0, 0x7e, 0x84 } });
}

void hud_load(void)
{
    if (res_slot_loaded(SLOT_HUD_FIRST)) return;
    palette_read(55, &g_pal_active);
    res_load_frames(35, 0, 1, (const FrameGroup[]){ { 0, 0x3f, 0x5f } });
    static const int banks[7][3] = {
        { 74, 846, 936 }, { 75, 937, 1001 }, { 76, 1002, 1064 }, { 77, 1065, 1155 },
        { 68, 660, 674 }, { 101, 2358, 2362 }, { 95, 2008, 2114 },
    };
    for (int i = 0; i < 7; i++) res_load_banks(banks[i][0], banks[i][1], banks[i][2], false);
    for (int slot = 0; slot < 5; slot++) save_slot_read(slot, false);
}

void tile_animate(void)
{
    int bg = g_missions[g_state.mission].background;
    int32_t f = g_state.mission_frames;
    if (bg == 29) {
        res_alias_slot(0x11, (f / 12) % 4 + 0x12);
        res_alias_slot(0x16, (f / 6) % 9 + 0x17);
        res_alias_slot(0x20, (f / 6) % 9 + 0x21);
    } else if (bg == 32) {
        int32_t n = (f >= 0 ? f >> 2 : ~(~f >> 2)) % 0x12;
        res_alias_slot(0x2a, 0x3d - n);
        res_alias_slot(0x2b, n + 0x2c);
    }
}
