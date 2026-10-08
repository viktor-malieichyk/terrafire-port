#include "screens.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "anims.h"
#include "callbacks.h"
#include "frame.h"
#include "game.h"
#include "hud.h"
#include "kbd.h"
#include "levels.h"
#include "menu.h"
#include "missions.h"
#include "objects.h"
#include "palette.h"
#include "platform.h"
#include "render.h"
#include "res.h"
#include "sound.h"
#include "tables.h"
#include "timing.h"

static int32_t sar1(int32_t v) { return v >= 0 ? v >> 1 : ~(~v >> 1); }
static int32_t view_cx(void) { return (g_view.cam_x + 0xa0) * 0x100; }   /* 0x29e04 */
static int32_t view_cy(void) { return (g_view.cam_y + 100) * 0x100; }    /* 0x29e14 */
static bool quit(void) { return tf_quit_requested(); }

/* Present an off-screen 320x200 picture (0x2a898 with a buffer). */
static void present_buffer(const uint8_t *buf)
{
    memcpy(g_screen, buf, SCREEN_W * SCREEN_H);
    tf_present();
}

/* ------------------------------------------------------------ briefing */

/* 0x1a608: mission number, then location, objective and gravity sliding up. */
static void briefing_frame(void)
{
    render_build_occlusion();
    render_objects(-1, 0x31);
    int m = g_state.mission;
    text_draw(true, ALIGN_CENTRE, 0xa000, 0x1400, 0x3a9, 0x100, 0x800, "%s %d", g_text[9], m + 1);
    int32_t y = 200 - ((g_objects[10].anim_frame + 1) * 35) / g_anims[ANIM_44510].count;
    char where[40];
    if ((m & 3) == 3) snprintf(where, sizeof where, "%s", g_text[13]);                  /* HYPERSPACE */
    else snprintf(where, sizeof where, "%s %d", g_text[10], (m >> 2) + 1);           /* SECTOR n */
    text_draw(true, ALIGN_CENTRE, 0xa000, y << 8, 0x3ea, 0x100, 0x200, "%s : %s", g_text[12], where);
    const char *goal = (m & 3) == 3 ? g_text[54] : g_text[55];
    text_draw(true, ALIGN_CENTRE, 0xa000, (y << 8) + 0x800, 0x3ea, 0x100, 0x200, "%s : %s", g_text[11], goal);
    const char *gravity = goal;                       /* sic: an unknown value shows the objective */
    switch (g_missions[m].u04) {
    case 0: gravity = g_text[2]; break;               /* NONE */
    case 8: gravity = g_text[0]; break;               /* LOW */
    case 10: gravity = g_text[1]; break;              /* HIGH */
    default: break;
    }
    text_draw(true, ALIGN_CENTRE, 0xa000, (y << 8) + 0x1000, 0x3ea, 0x100, 0x200, "%s : %s", g_text[16], gravity);
    render_present();
    objects_update();
}

void briefing_show(void)
{
    resources_reset();
    palette_read(0x36, &g_pal_active);
    res_load_frames(0x1a, 0, 1, (const FrameGroup[]){ { 0, 0x3e, 0x3e } });
    res_load_banks(0x53, 0x484, 0x49c, false);
    res_load_banks(0x56, 0x3a9, 0x3e9, false);
    res_load_banks(0x57, 0x3ea, 0x428, false);
    obj_spawn(5, -1, 0, 0, 1);
    obj_set_slot(5, 0x3e);
    obj_spawn(10, 0x484, 0xa000, 0x6600, 0x1c);
    obj_set_anim(10, ANIM_44510, 0);
    palette_fade_start(&g_pal_active, 0x20);
    for (int i = 0; i < g_anims[ANIM_44510].count; i++) briefing_frame();
    resources_reset();
}

/* ------------------------------------------------------- mission complete */

void cb_title_stop(int slot) { obj_set_velocity(slot, 0, 0); }      /* 0x24790 */

/* 0x24b6c: a star lives while on screen. */
void cb_star(int slot)
{
    if (render_obj_on_screen(slot)) obj_set_callback(slot, 0, CB_24b6c, 5);
    else obj_kill(slot);
}

/* 0x24a48: a star (0x1f6-0x259) flying out from the middle, speeding up. */
static void starburst(void)
{
    int p = obj_find_free(0x1f6, 0x259, false);
    if (p == -1) return;
    int a = g_random_table[(g_state.f_019 + 1) & 0xff] % 0x168;
    g_state.f_019 += 2;
    int32_t r = g_random_table[g_state.f_019 & 0xff] % 8, k = r + 4;
    obj_spawn(p, 0x942 - r, k * g_trig_a[a] + 0xa000, g_trig_b[a] * k + 0x6400, 7);
    obj_set_velocity(p, g_trig_a[a] / 0x14, g_trig_b[a] / 0x14);
    int32_t d = r * 0x46 + 0x3c;
    g_objects[p].ax = g_trig_a[a] / d;                  /* 0x2c1e0 */
    g_objects[p].ay = g_trig_b[a] / d;
    obj_set_callback(p, 0, CB_24b6c, 0x14);
}

/* Screen mode 3 (0x2224c case 3 / 0x12d20). */
void complete_frame(bool present)
{
    (void)present;
    starburst();
    render_build_occlusion();
    render_objects(-1, 0x31);
    render_present();
    objects_update();
}

/* 0x12d48 / 0x12d20 / 0x12cf4: the two title halves slide together over a
 * starburst; up to 0x118 frames or until a key is let go. */
void mission_complete_screen(void)
{
    g.mission_type = 3;
    palette_read(0x35, &g_pal_active);
    res_load_banks(0x66, 0x93b, 0x942, false);
    res_load_frames(0x1b, 0, 1, (const FrameGroup[]){ { 0, 0, 1 } });
    res_load_frames(0x22, 0, 1, (const FrameGroup[]){ { 0, 0x10, 0x10 } });
    for (int i = 0; i < 100; i++) {
        starburst();
        objects_update();
    }
    obj_spawn(5, -1, 0, 0, 1);
    obj_set_slot(5, 0x10);
    for (int k = 0; k < 2; k++) {
        int slot = 3 + k;
        const FrameSlot *fs = &g_slots[k];
        int32_t x = view_cx() - sar1(fs->w_fx12), y = view_cy() - sar1(fs->h_fx12) + (k ? 0x8930 : -0x8930);
        obj_spawn(slot, -1, x, y, 0x30);
        obj_set_slot(slot, k);
        obj_set_velocity(slot, 0, k ? -600 : 600);
        obj_set_callback(slot, 0, CB_24790, 0x32);
    }
    palette_fade_start(&g_pal_active, 0x20);
    if (kbd_first_released()) kbd_clear_all();
    for (int i = 0; i < 0x118 && !quit(); i++) {
        complete_frame(true);
        if (kbd_first_released()) {
            kbd_clear_all();
            break;
        }
    }
    palette_fade_start(&g_pal_black, 0x20);
    for (int i = 0; i < 0x20; i++) frame_render(1);
    resources_reset();
}

/* ------------------------------------------------------------ game over */

/* 0x22304: fade to white while the picture is squeezed into a line. */
static void collapse(void)
{
    static uint8_t src[SCREEN_W * SCREEN_H], dst[SCREEN_W * SCREEN_H];
    palette_fade_start(&g_pal_white, 0x40);
    memcpy(src, g_screen, sizeof src);
    memset(dst, 0, sizeof dst);
    for (int step = 0; step < 0x20; step++) {
        int d = 0x20 - step, acc = 0, prev = -1, v = 0;
        int lo = 0x7bc0, hi = 32000, clear = 0;
        for (;;) {
            acc += d;
            v = acc / 32;
            clear = (100 - v) * SCREEN_W;
            lo += SCREEN_W;
            hi -= SCREEN_W;
            if (lo == 64000) break;
            if (v != prev) {
                if (100 - v >= 0 && 100 - v < SCREEN_H) memcpy(&dst[(100 - v) * SCREEN_W], &src[hi], SCREEN_W);
                if (v + 99 >= 0 && v + 99 < SCREEN_H) memcpy(&dst[(v + 99) * SCREEN_W], &src[lo], SCREEN_W);
                prev = v;
            }
        }
        if (v < 0x65 && clear > 0) {
            memset(dst, 0, (size_t)clear);
            if ((v + 100) * SCREEN_W + clear <= SCREEN_W * SCREEN_H)
                memset(&dst[(v + 100) * SCREEN_W], 0, (size_t)clear);
        }
        present_buffer(dst);
    }
    memset(dst, 0, sizeof dst);
    present_buffer(dst);
}

/* 0x16b48: GAME OVER beams in over the last view, waits for a key. */
void game_over(void)
{
    if (!res_bank_loaded(0x5c1)) res_load_banks(0x59, 0x5c1, 0x5d9, false);
    obj_spawn(3, 0x5c1, view_cx(), view_cy(), 0x31);
    obj_freeze(3);
    for (int32_t r = 0x8c00; r != 0x200; r -= 0x200) {
        frame_render(0);
        render_beam(3, r, r);
        render_present();
    }
    obj_unfreeze(3);
    obj_set_anim(3, ANIM_4b2d8, 0);
    kbd_clear_all();
    while (!kbd_first_released() && !quit()) frame_render(1);
    kbd_clear_all();
    sound_pause();
    collapse();
    resources_reset();
}

/* ------------------------------------------------------------ high scores */

/* 0x24ba0: edit `s` (at most `max` characters) with the keyboard; keys with
 * a one-character name type it, Space a space, Backspace deletes; Enter or
 * Esc ends. A '?' cursor blinks. */
static void text_input(char *s, size_t max, bool digits, void (*redraw)(bool))
{
    for (;;) {
        if (quit()) return;
        if (kbd_was_pressed(0x0e)) {
            if (s[0]) s[strlen(s) - 1] = 0;
            kbd_clear_all();
        }
        for (int k = 0; k < 0x80; k++) {
            if (strlen(s) >= max || !kbd_was_pressed((uint8_t)k)) continue;
            const char *name = g_key_names[k];
            if (strlen(name) == 1) {
                if (!digits || isdigit((unsigned char)name[0])) strcat(s, name);
            } else if (k == KEY_SPACE) {
                strcat(s, " ");
            } else {
                continue;
            }
            kbd_clear_all();
        }
        if ((tf_clock() / 0x48ccc & 1) == 0) {
            redraw(true);
        } else {
            strcat(s, "?");
            redraw(true);
            s[strlen(s) - 1] = 0;
        }
        if (kbd_was_pressed(KEY_ENTER) || kbd_was_pressed(KEY_ESC)) {
            kbd_clear_all();
            return;
        }
    }
}

/* 0x22058: a score that makes the table is entered with a name. */
void high_score_entry(void)
{
    int i = 10;
    while (i > 0 && g_state.score >= g.hi_scores[i - 1]) {
        if (i < 10) {
            memcpy(g.hi_names[i], g.hi_names[i - 1], sizeof g.hi_names[i]);
            g.hi_scores[i] = g.hi_scores[i - 1];
        }
        i--;
    }
    if (i == 10) return;
    g.hi_names[i][0] = 0;
    g.hi_scores[i] = g_state.score;
    high_scores_setup();
    palette_fade_start(&g_pal_black, 1);
    high_scores_frame(true);
    palette_fade_start(&g_pal_active, 0x20);
    for (int k = 0; k < 0x20; k++) frame_render(1);
    text_input(g.hi_names[i], 12, false, high_scores_frame);
    if (!g.hi_names[i][0]) snprintf(g.hi_names[i], sizeof g.hi_names[i], "anon");
    palette_fade_start(&g_pal_black, 0x20);
    for (int k = 0; k < 0x20; k++) frame_render(1);
    kbd_clear_all();
    game_save_settings();
}

/* ------------------------------------------------------------ game complete */

/* 0x25ea8: the ending text (bank 0x958, one text line per column) rolls up
 * into the distance over the picture. */
static void text_crawl(bool skippable)
{
    static uint8_t buf[SCREEN_W * SCREEN_H];
    static int32_t width[200], step[200], bright[200], srow[200], dst[200];
    tf_set_frame_rate(TF_RATE_HALF);
    const Bank *bk = &g_banks[0x958];
    if (!bk->pixels) return;
    const BankView *v = &bk->view[0];
    int32_t H = v->h >> 8, W = v->w >> 8;
    for (int i = 0, a = 0x29, b = 0xb9c9; i < 200; i++, a++, b -= 0xef) {
        int32_t q = b / a;
        width[i] = (0x129d1 / (q + 0xef)) / 2 * 2;
        dst[i] = i * SCREEN_W + ((SCREEN_W - width[i]) >> 1);
        int32_t l = 0x488 - q * 3;
        if (l < 0) l = 0;
        bright[i] = (l << 6) / 0x488;
        srow[i] = 0x488 - q;
        step[i] = ((v->h >> 8) << 16) / (width[i] * 2);
    }
    const uint8_t *pix = bk->pixels + v->start;
    int32_t limit = srow[199] + W;
    for (int32_t scroll = srow[199]; srow[56] - W <= scroll && !quit(); scroll--, limit--) {
        render_copy_view(buf);
        for (int r = 56; r < 200; r++) {
            int32_t row = srow[r];
            if (scroll > row) continue;
            if (limit <= row) break;
            const uint8_t *p = pix + H * (row - scroll), *p2 = p;
            if (scroll <= srow[r - 1]) p2 = p - ((row - srow[r - 1]) >> 1) * H;
            uint32_t acc = 0;
            uint8_t *out = &buf[dst[r]];
            for (int n = width[r]; n > 0; n--, out++) {
                uint32_t u1 = acc >> 16, u2 = (acc + (uint32_t)step[r]) >> 16;
                acc += 2 * (uint32_t)step[r];
                int s = p[u1] + p2[u2] + p[u2] + p2[acc >> 16];
                if (s) *out = (uint8_t)((uint32_t)(s * bright[r]) >> 8);
            }
        }
        present_buffer(buf);
        present_buffer(buf);
        if (skippable && kbd_any_pressed()) break;
    }
    render_load_view(buf);                              /* 0x2a388 */
    tf_set_frame_rate(TF_RATE_FULL);
}

/* 0x25dac: the closing picture and text, again and again until a key. */
void game_complete(void)
{
    resources_reset();
    palette_read(0x3b, &g_pal_active);
    res_load_frames(0x33, 0, 1, (const FrameGroup[]){ { 0, 0xf, 0xf } });
    res_load_banks(0x6a, 0x958, 0x958, false);
    bool again = false;
    do {
        render_fill(0, 0, 0, 0x13f00, 0xc700);
        palette_fade_start(&g_pal_black, 1);
        render_present();
        render_frame_slot(0xf, 0, 0);
        palette_fade_start(&g_pal_active, 0x40);
        for (int i = 0; i < 0x40; i++) render_present();
        text_crawl(again);
        again = true;
        palette_fade_start(&g_pal_black, 0x40);
        for (int i = 0; i < 0x40; i++) render_present();
    } while (!kbd_any_pressed() && !quit());
    resources_reset();
}

/* ------------------------------------------------------------ startup */

/* 0x1c414: the picture strip in five bands drifting at different speeds. */
static void splash_frame(void)
{
    int32_t t = (int32_t)tf_frame_counter();
    render_frame_slot(0xf, ((t * 2) % 0x140) * -0x100, 0);
    render_frame_slot(0xf, (t % 0x140) * -0x100, 0x2900);
    render_frame_slot(0xf, 0, 0x5200);
    render_frame_slot(0xf, (t % 0x140) * 0x100 - 0x14000, 0x7b00);
    render_frame_slot(0xf, ((t * 2) % 0x140) * 0x100 - 0x14000, 0xa400);
    render_present();
}

void splash_screen(void)
{
    resources_reset();
    palette_read(0x38, &g_pal_active);
    res_load_frames(0x2b, 0, 1, (const FrameGroup[]){ { 0, 0xf, 0xf } });
    render_fill(0, 0, 0, 0x13f00, 0xc700);
    palette_fade_start(&g_pal_black, 1);
    render_present();
    render_present();
    tf_wait_clock(tf_clock() + 0x123333);              /* 0x2d4a4: about a second */
    palette_fade_start(&g_pal_active, 0x40);
    for (int i = 0; i < 0x40; i++) splash_frame();
    for (int i = 0; i < 0x118 && !kbd_any_pressed() && !quit(); i++) splash_frame();
    palette_fade_start(&g_pal_black, 0x40);
    for (int i = 0; i < 0x40; i++) splash_frame();
    resources_reset();
}

/* 0x225c0: scale a sw x sh picture to dw x dh, centred, into a screen buffer. */
static void scale_into(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh)
{
    if (dh <= 0 || dw <= 0) return;
    uint32_t sy = 0;
    int row = (SCREEN_H - dh) >> 1;
    for (int y = 0; y < dh; y++, row++, sy += (uint32_t)(sh << 16) / (uint32_t)dh) {
        uint8_t *out = &dst[row * SCREEN_W + ((SCREEN_W - dw) >> 1)];
        uint32_t sx = 0;
        for (int x = 0; x < dw; x++, sx += (uint32_t)(sw << 16) / (uint32_t)dw)
            out[x] = src[(sy >> 16) * (uint32_t)sw + (sx >> 16)];
    }
}

/* 0x2289c: the centre w x h of the picture grows out of a line. */
static void zoom_in(uint8_t colour, int w, int h)
{
    static uint8_t src[SCREEN_W * SCREEN_H], full[SCREEN_W * SCREEN_H];
    int w4 = (w >> 2) * 4;
    render_capture(src, w4, h);
    render_fill(colour, ((SCREEN_W - w4) >> 1) << 8, ((SCREEN_H - h) >> 1) << 8,
                ((w4 + SCREEN_W) >> 1) * 0x100 - 0x100, ((h + SCREEN_H) >> 1) * 0x100 - 0x100);
    palette_fade_start(&g_pal_black, 1);
    render_present();
    render_present();
    render_copy_view(full);
    palette_fade_start(&g_pal_active, 0x40);
    int step = (w >> 2) << 3;
    for (int i = 2, acc = step; i < h; i += 2, acc += step) {
        scale_into(src, w4, h, full, acc / h, i);
        present_buffer(full);
    }
    scale_into(src, w4, h, full, w4, h);
    present_buffer(full);
    render_load_view(full);
}

/* 0x24f8c: the logo (bank 0x943) fades in over the picture in 128 steps,
 * by interpolating palette indices. */
static void logo_fade_in(void)
{
    static uint8_t base[SCREEN_W * SCREEN_H], out[SCREEN_W * SCREEN_H];
    render_copy_view(base);
    render_copy_view(out);
    const Bank *bk = &g_banks[0x943];
    if (!bk->pixels) return;
    const uint8_t *logo = bk->pixels + bk->view[0].start;
    for (int k = 1; k < 0x81; k++) {
        for (int y = 0x45; y < 0x7a; y++)
            for (int x = 5; x < 0x13a; x++) {
                uint8_t l = logo[(x - 5) * 0x35 + (y - 0x45)];
                if (!l) continue;
                int at = y * SCREEN_W + x;
                out[at] = (uint8_t)(base[at] + ((((int)l - (int)base[at]) * k) >> 7));
            }
        present_buffer(out);
    }
    render_load_view(out);
}

void title_screen(void)
{
    resources_reset();
    palette_read(0x39, &g_pal_active);
    res_load_frames(0x2f, 0, 1, (const FrameGroup[]){ { 0, 0xf, 0xf } });
    res_load_banks(0x67, 0x943, 0x943, false);
    render_fill(0, 0, 0, 0x13f00, 0xc700);
    render_frame_slot(0xf, 0, 0);
    zoom_in(0, 0xd4, 0xba);
    logo_fade_in();
    for (int i = 0; i < 0xa0 && !quit(); i++) {
        render_present();
        if (kbd_any_pressed()) break;
    }
    palette_fade_start(&g_pal_black, 0x40);
    for (int i = 0; i < 0x40; i++) render_present();
    resources_reset();
}
