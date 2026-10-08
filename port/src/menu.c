#include "menu.h"

#include <stdio.h>
#include <string.h>

#include "anims.h"
#include "callbacks.h"
#include "frame.h"
#include "game.h"
#include "hud.h"
#include "joystick.h"
#include "kbd.h"
#include "levels.h"
#include "missions.h"
#include "objects.h"
#include "palette.h"
#include "platform.h"
#include "render.h"
#include "save.h"
#include "res.h"
#include "session.h"
#include "sound.h"
#include "tables.h"
#include "timing.h"
#include "unported.h"

/* The original's menus are data (0x57108...): a header and 0x65-byte items
 * with a label, a message id that refills it, and three actions run in turn
 * when it is chosen; `ret` items end the menu, returning their index. */
typedef void (*MenuAction)(int item);

typedef struct {
    char text[0x50];
    int msg;                     /* g_text id copied into text, -1 = set by code */
    int advance;                 /* space width / 256 */
    MenuAction act[3];
    bool ret;
} MenuItem;

typedef struct {
    int title;                   /* g_text id, -1 = the logo (slot 0x3f) */
    void (*enter)(void), (*exit)(void);
    int style;                   /* panel frame slots: style + 0x42 / 0x4c / 0x56 */
    int32_t x, y;                /* 24.8: centre of the panel column, top */
    int cursor, count;
    MenuItem *items;
    bool slots;                  /* the load / save menus: show the slot's game */
} Menu;

#define FONT_MENU 0x3a9
#define ROW 0x1400

static Menu *s_menu;             /* 0x5b588 */

static int32_t sar1(int32_t v) { return v >= 0 ? v >> 1 : ~(~v >> 1); }
static int32_t slot_w(int slot) { return g_slots[slot].w_fx12; }   /* 0x28bcc */

/* ------------------------------------------------------------ transitions */

static void fade_out(int item) { (void)item; palette_fade_start(&g_pal_black, 0x20); for (int i = 0; i < 0x20; i++) frame_render(1); }  /* 0x1c684 */
static void fade_in(int item) { (void)item; palette_fade_start(&g_pal_active, 0x20); for (int i = 0; i < 0x20; i++) frame_render(1); }    /* 0x1c6a8 */
static void fade_out_v(void) { fade_out(0); }

/* 0x22498: the menu slides in, even rows from the left, odd rows from the
 * right. */
static void curtain(int item)
{
    (void)item;
    palette_fade_start(&g_pal_black, 1);
    render_fill(0, 0, 0, 0x13f00, 0xc700);
    render_present();
    frame_render(0);
    static uint8_t src[SCREEN_W * SCREEN_H];
    render_copy_view(src);                        /* 0x2a310 */
    palette_fade_start(&g_pal_active, 1);
    for (int n = 4; n < SCREEN_W + 1; n += 4) {
        memset(g_screen, 0, sizeof g_screen);
        for (int y = 0; y < SCREEN_H; y += 2)
            memcpy(&g_screen[y * SCREEN_W], &src[y * SCREEN_W + SCREEN_W - n], (size_t)n);
        for (int y = 1; y < SCREEN_H; y += 2)
            memcpy(&g_screen[y * SCREEN_W + SCREEN_W - n], &src[y * SCREEN_W], (size_t)n);
        tf_present();
    }
}
static void curtain_v(void) { curtain(0); }

/* ------------------------------------------------------------ the engine */

/* 0x1a900: the menu background tiles scroll diagonally, jumping back. */
void cb_menu_backdrop(int slot)
{
    obj_set_pos(slot, -0x14000, -0x1b800);
    obj_set_callback(slot, 0, CB_1a900, 0x280);
}

/* 0x1ad74 */
static void menu_init(Menu *m)
{
    g.mission_type = 0;
    s_menu = m;
    kbd_clear_all();
    for (int i = 0; i < m->count; i++)
        if (m->items[i].msg != -1) snprintf(m->items[i].text, sizeof m->items[i].text, "%s", g_text[m->items[i].msg]);
    hud_load();                                    /* 0x1aebc: frames and fonts */
    if (!obj_alive(0)) {
        obj_spawn(0, -1, 0, 0, 1);
        obj_set_slot(0, 0x40);
        obj_set_velocity(0, 0x80, 0x80);
        obj_set_callback(0, 0, CB_1a900, 0);
    }
    if (!obj_alive(1)) {
        obj_spawn(1, -1, -1, -1, 2);
        obj_set_anim(1, ANIM_44608, 0);
        obj_spawn(2, -1, -1, -1, 2);
        obj_set_anim(2, ANIM_44608, 0);
        obj_set_orient(2, 4);
    } else {
        obj_set_pos(1, -1, -1);
        obj_set_pos(2, -1, -1);
    }
    render_camera(0, 0);
}

/* 0x1a930: one menu frame. The arrows slide to the cursor row; `flash`
 * shows the cursor row's button lit. */
void menu_frame(bool present, bool flash)
{
    Menu *m = s_menu;
    if (!m) return;
    int32_t w = slot_w(m->style + 0x42), bx = m->x - sar1(w);
    int32_t ty = m->cursor * ROW + m->y + 0xb00;
    if (obj_y(1) == -1) {
        obj_set_pos(1, bx - 0x400, ty);
        obj_set_pos(2, w + bx + 0x3ff, ty);
    } else if (obj_y(1) != ty) {
        int32_t d = obj_y(1) - ty, ad = d < 0 ? -d : d;
        int32_t step = ad < 0x401 ? 0x100 : ad < 0xa01 ? 0x200 : 0x500;
        if (ty - obj_y(1) < 0) step = -step;
        obj_move(1, 0, step);
        obj_move(2, 0, step);
    }
    render_build_occlusion();
    render_mark_covered(bx, m->y, w + bx - 1, (m->count - 1) * ROW + m->y + ROW);
    render_objects(-1, 0x31);
    if (m->title == -1) {
        render_frame_slot(0x3f, 0xa000 - sar1(slot_w(0x3f)), 0xa00);
    } else {
        render_frame_slot(m->style + 0x56, bx, m->y - 0x2400);
        text_draw(true, ALIGN_CENTRE, m->x, m->y - 0x1300, FONT_MENU, 0x100, 0x500, g_text[m->title]);
    }
    for (int i = 0; i < m->count; i++) {
        render_frame_slot(flash && i == m->cursor ? m->style + 0x4c : m->style + 0x42, bx, m->y + i * ROW);
        text_draw(true, ALIGN_CENTRE, m->x, m->y + i * ROW + 0xb00, FONT_MENU, 0x100,
                  m->items[i].advance << 8, m->items[i].text);
    }
    if (m->slots && m->cursor < SAVE_SLOTS) {
        int s = m->cursor;
        render_mark_covered(0x9900, 0x3000, 0x13c00, 0x9700);
        text_draw(true, ALIGN_LEFT, 0x9900, 0x2600, 0x34e, -0x100, 0x400, "%s", save_slot_stamp(s));
        if (g.save_mission[s] != -1) {
            text_draw(true, ALIGN_LEFT, 0x9900, 0x9900, 0x34e, -0x100, 0x400, "%s %d", g_text[9], g.save_mission[s] + 1);
            int d = g.save_difficulty[s];
            text_draw(true, ALIGN_RIGHT, 0x13d00, 0x9900, 0x34e, -0x100, 0x400, "%s",
                      d >= 0 && d < 4 ? g_text[68 + d] : "");
        }
        render_bank(0x936 + s, 0, 0, 0x9900, 0x3000, RENDER_NO_CLIP);
    }
    if (present) render_present();
    objects_update();
}

static bool quit(void) { return tf_quit_requested(); }

/* Until a key is let go (0x18a14 without the joystick). */
static void wait_release(void)
{
    while (!kbd_first_released() && !quit()) menu_frame(true, false);
    kbd_clear_all();
}

static void move_cursor(Menu *m, int by)
{
    sound_play(0x10);
    m->cursor += by;
    if (m->count < 2) m->cursor = 0;
    else if (m->cursor < 0) m->cursor = m->count - (-m->cursor) % m->count;
    else m->cursor %= m->count;
    if (m->cursor == m->count) m->cursor = 0;
    wait_release();
}

/* 0x1afd4: run a menu; the chosen item's index, or -1 for Esc. */
static int menu_run(Menu *m)
{
    menu_init(m);
    if (m->enter) m->enter();
    for (;;) {
        if (quit()) return -1;
        menu_frame(true, false);
        joystick_poll();                               /* 0x18524 */
        if (kbd_was_pressed(KEY_UP)) move_cursor(m, -1);
        if (kbd_was_pressed(KEY_DOWN)) move_cursor(m, 1);
        if (kbd_was_pressed(KEY_ENTER) || kbd_was_pressed(KEY_SPACE) || kbd_was_pressed(KEY_LCTRL)) {
            sound_play(1);
            menu_frame(true, false);
            /* The button stays lit at least 9 frames and until let go. */
            int n = 0;
            do {
                do {
                    menu_frame(true, true);
                    joystick_poll();
                } while (n++ < 8 && !quit());
            } while (!kbd_was_released(KEY_ENTER) && !kbd_was_released(KEY_SPACE) &&
                     !kbd_was_released(KEY_LCTRL) && !quit());
            menu_frame(true, false);
            kbd_clear_all();
            int c = m->cursor;
            MenuItem *it = &m->items[c];
            if (it->act[0]) it->act[0](c);
            if (it->act[1]) it->act[1](c);
            menu_init(m);
            if (it->act[2]) it->act[2](c);
            if (m->items[m->cursor].ret) return m->cursor;
            continue;
        }
        if (kbd_was_pressed(KEY_ESC)) {
            kbd_clear_all();
            if (m->exit) m->exit();
            return -1;
        }
        if (kbd_was_pressed(0x3e)) {                   /* F4: cheat keys on */
            sound_play(1);
            g.cheats = true;
            kbd_clear_all();
        }
    }
}

/* A submenu leaves its cursor where it was, except on DONE. */
static void run_sub(Menu *m)
{
    menu_run(m);
    if (m->cursor == m->count - 1) m->cursor = 0;
}

#define ITEM(msg, a0, a1, a2, ret) { "", (msg), 5, { (a0), (a1), (a2) }, (ret) }
#define ITEM_DYN(a1) { "", -1, 5, { NULL, (a1), NULL }, false }

/* ------------------------------------------------------------ options */

enum { TXT_LEFT = 3, TXT_FRAME_RATE = 72, TXT_DETAIL = 74, TXT_LAYERS = 76, TXT_VOLUME = 79, TXT_JOYSTICK = 85 };

static void set_text(MenuItem *it, const char *s) { snprintf(it->text, sizeof it->text, "%s", s); }

static void graphics_toggle(int item);
static MenuItem s_graphics_items[] = {
    ITEM_DYN(graphics_toggle), ITEM_DYN(graphics_toggle), ITEM_DYN(graphics_toggle),
    ITEM(27, NULL, NULL, NULL, true),
};
static Menu s_graphics = { 24, NULL, NULL, 8, 0xa000, 0x4800, 0, 4, s_graphics_items, false };

static void graphics_texts(void)
{
    set_text(&s_graphics_items[0], g_text[TXT_FRAME_RATE + g.full_rate]);
    set_text(&s_graphics_items[1], g_text[TXT_DETAIL + g.debris]);
    set_text(&s_graphics_items[2], g_text[TXT_LAYERS + g.detail]);
}

/* 0x1693c */
static void graphics_toggle(int item)
{
    if (item == 0) {
        g.full_rate = !g.full_rate;
        tf_set_frame_rate(TF_RATE_FULL);    /* menus always run at full rate */
    } else if (item == 1) {
        g.debris = !g.debris;
    } else if (item == 2) {
        g.detail = (g.detail + 1) % 3;
    }
    graphics_texts();
}

static void graphics_menu(int item) { (void)item; graphics_texts(); run_sub(&s_graphics); }   /* 0x16878 */

static void volume_step(int item);
static MenuItem s_sound_items[] = { ITEM_DYN(volume_step), ITEM(27, NULL, NULL, NULL, true) };
static Menu s_sound = { 25, NULL, NULL, 6, 0xa000, 0x6000, 0, 2, s_sound_items, false };

static void volume_text(void)
{
    /* Copied as is ("... 20%%"): the label is a format when drawn. */
    set_text(&s_sound_items[0], g_text[TXT_VOLUME + g.volume / 0x14]);
}

/* 0x16ac0: 0, 20, ... 100 %, then 0 again; the click is heard at the new level. */
static void volume_step(int item)
{
    (void)item;
    g.volume += 0x14;
    if (g.volume > 100) g.volume = 0;
    volume_text();
    while (sound_playing(1) && !quit()) menu_frame(true, false);
    sound_set_volume(g.volume);
}

static void sound_menu(int item) { (void)item; volume_text(); run_sub(&s_sound); }   /* 0x16a54 */

static void key_capture(int item);
static void keys_reset(int item);
/* Port addition: a DEFAULT KEYS button; the original has only the four
 * keys and DONE, at y 0x4000. */
static MenuItem s_keys_items[] = {
    ITEM_DYN(key_capture), ITEM_DYN(key_capture), ITEM_DYN(key_capture), ITEM_DYN(key_capture),
    { "DEFAULT KEYS", -1, 5, { NULL, keys_reset, NULL }, false },
    ITEM(27, NULL, NULL, NULL, true),
};
static Menu s_keys = { 28, NULL, NULL, 7, 0xa000, 0x3600, 0, 6, s_keys_items, false };

static void key_text(int k)
{
    char s[64];
    snprintf(s, sizeof s, "%s : %s", g_text[TXT_LEFT + k], g_key_names[g.keys[k] & 0x7f]);
    set_text(&s_keys_items[k], s);
}

/* 0x15e04: the label blinks until a key is pressed; Esc keeps the old one.
 * Keys already in use, P, F1, F5 and F6 are refused, and so are keys
 * without a name. */
static void key_capture(int k)
{
    int taken[9] = { g.keys[0], g.keys[1], g.keys[2], g.keys[3], -1, 0x19, 0x3b, 0x3f, 0x40 };
    taken[k] = -1;
    obj_freeze(1);
    obj_freeze(2);
    for (;;) {
        do {
            char s[64];
            if (((tf_frame_counter() / 0x14) & 1) == 0) snprintf(s, sizeof s, "%s : ?", g_text[TXT_LEFT + k]);
            else s[0] = 0;
            set_text(&s_keys_items[k], s);
            frame_render(1);
        } while (!kbd_any_pressed() && !quit());
        if (quit() || kbd_was_pressed(KEY_ESC)) break;
        int key = kbd_any_pressed(), i = 0;
        while (i < 9 && taken[i] != key) i++;
        if (i == 9 && g_key_names[key & 0x7f][0]) {
            g.keys[k] = (uint8_t)key;
            kbd_clear_all();
            break;
        }
        kbd_clear_all();
    }
    obj_unfreeze(1);
    obj_unfreeze(2);
    key_text(k);
}

static void keys_reset(int item)
{
    (void)item;
    game_default_keys();
    for (int k = 0; k < 4; k++) key_text(k);
}

static void keys_menu(int item)   /* 0x1619c */
{
    (void)item;
    for (int k = 0; k < 4; k++) key_text(k);
    run_sub(&s_keys);
}

static void joystick_toggle(int item);
static MenuItem s_control_items[] = {
    ITEM(28, NULL, keys_menu, NULL, false), ITEM_DYN(joystick_toggle), ITEM(27, NULL, NULL, NULL, true),
};
static Menu s_control = { 26, NULL, NULL, 5, 0xa000, 0x5000, 0, 3, s_control_items, false };

/* 0x16a00 (gamepads need no centring, so the set-up 0x183dc is not needed;
 * without a gamepad the option switches itself off again) */
static void joystick_toggle(int item)
{
    (void)item;
    g.joystick = !g.joystick;
    set_text(&s_control_items[1], g_text[TXT_JOYSTICK + g.joystick]);
}

static void control_menu(int item)   /* 0x16818 */
{
    (void)item;
    set_text(&s_control_items[1], g_text[TXT_JOYSTICK + g.joystick]);
    run_sub(&s_control);
}

static MenuItem s_options_items[] = {
    ITEM(24, NULL, graphics_menu, NULL, false), ITEM(25, NULL, sound_menu, NULL, false),
    ITEM(26, NULL, control_menu, NULL, false), ITEM(27, NULL, NULL, NULL, true),
};
static Menu s_options = { 18, NULL, NULL, 3, 0xa000, 0x4800, 0, 4, s_options_items, false };

static void options_menu(int item) { (void)item; run_sub(&s_options); game_save_settings(); }   /* 0x1678c */

/* ------------------------------------------------------------ info */

/* Port addition: the instructions list the default keys as fixed text
 * (lines 104/105: "key desc key desc" in tab columns). Draw them with the
 * current bindings instead, widening a column when a key name needs it so
 * both rows stay aligned. */
#define TXT_KEYS_HEAD 102
#define TXT_KEYS_ROW  104

/* Split a tab-column line into up to 4 words with their tab-stop x. */
static void keys_columns(const char *line, char words[4][32], int32_t x[4])
{
    int32_t pos = 0;
    int n = 0;
    for (const char *p = line; *p && n < 4;) {
        if (*p == '\t') { pos = ((pos >> 12) + 1) * 0x1000; p++; continue; }
        size_t len = strcspn(p, "\t");
        snprintf(words[n], sizeof words[n], "%.*s", (int)len, p);
        x[n] = pos;
        pos += text_width(0x429, 0, 0x300, words[n]);
        n++;
        p += len;
    }
    for (; n < 4; n++) { words[n][0] = 0; x[n] = pos; }
}

static void keys_row_draw(int row, int32_t y)
{
    char cols[2][4][32];
    int32_t x[2][4], w[2][4];
    for (int r = 0; r < 2; r++) {
        keys_columns(g_text[TXT_KEYS_ROW + r], cols[r], x[r]);
        snprintf(cols[r][0], sizeof cols[r][0], "%s", g_key_names[g.keys[r] & 0x7f]);      /* left / right */
        snprintf(cols[r][2], sizeof cols[r][2], "%s", g_key_names[g.keys[2 + r] & 0x7f]);  /* thrust / fire */
        for (int c = 0; c < 4; c++) w[r][c] = text_width(0x429, 0, 0x300, cols[r][c]);
    }
    int32_t at = 0;
    for (int c = 0; c < 4; c++) {
        int32_t cx = x[0][c] > x[1][c] ? x[0][c] : x[1][c];
        if (cx < at) cx = at;
        text_draw(true, ALIGN_LEFT, 0x1500 + cx, y, 0x429, 0, 0x300, "%s", cols[row][c]);
        int32_t wide = w[0][c] > w[1][c] ? w[0][c] : w[1][c];
        at = cx + wide + 0x800;
    }
}

/* 0x17fc0: one page of 18 message lines from `first`. A leading '-' or
 * '_' is a heading with a rule under it (left / centred), '^' a centred
 * heading, '@n' puts animated icon n beside the line. */
static void page_draw(int first, int page, int pages)
{
    static const int icon_anims[5] = { ANIM_50bd8, ANIM_50fa0, ANIM_50448, ANIM_50810, ANIM_51550 };
    int id = first + page * 18;
    render_frame_slot(0x41, 0, 0);
    if (pages > 1) {
        text_draw(false, ALIGN_LEFT, 0x1200, 0xb100, 0x3ea, 0x100, 0x600, "%d/%d", page + 1, pages);
        text_draw(true, ALIGN_RIGHT, 0x12c00, 0xb100, 0x3ea, 0x100, 0x200, "%s", g_text[0x26]);
    }
    int32_t y = 0x1500;
    for (int n = 0; n < 18; n++, id++) {
        const char *line = id < TEXT_COUNT ? g_text[id] : "";
        int32_t next = y + 0xb00;
        if (id == TXT_KEYS_HEAD) line = "-KEYS";
        if (id == TXT_KEYS_ROW || id == TXT_KEYS_ROW + 1) {
            keys_row_draw(id - TXT_KEYS_ROW, y);
            next = y + 0x900;
        } else if (line[0] == '-') {
            text_draw(true, ALIGN_LEFT, 0x1500, y, 0x429, 0, 0x300, "%s", line + 1);
            int32_t w = text_width(0x429, 0, 0x300, line + 1);
            render_line(0x88, 0x1500, y + 0x800, w + 0x1300, y + 0x800);
        } else if (line[0] == '_') {
            text_draw(true, ALIGN_CENTRE, 0xa000, y + 0x400, 0x429, 0, 0x300, "%s", line + 1);
            int32_t w = text_width(0x429, 0, 0x300, line + 1);
            render_line(0x88, sar1(0x14000 - w), y + 0x800, sar1(w + 0x14000) - 0x200, y + 0x800);
        } else if (line[0] == '^') {
            text_draw(true, ALIGN_CENTRE, 0xa000, y, 0x429, 0, 0x300, "%s", line + 1);
            next = y + 0x900;
        } else {
            if (line[0] == '@') {
                int count = g_anims[ANIM_50fa0].count;
                int f = (int)(tf_frame_counter() % (uint32_t)count);
                if (line[1] & 1) f = (count - f) % count;
                y += 0x800;
                unsigned k = (unsigned char)line[1] - '0';
                if (k < 5) render_bank(g_anims[icon_anims[k]].frames[f].image, 0, 0, 0x7800, y + 0x300, RENDER_NO_CLIP);
                line += 2;
            }
            text_draw(true, ALIGN_LEFT, 0x1500, y, 0x429, 0, 0x300, "%s", line);
            next = y + (line[0] == 0 ? 0x600 : 0x900);
        }
        y = next;
    }
    render_present();
}

/* 0x182e8: show a document; Up / Page Up and Down / Page Down turn pages,
 * Esc leaves (a one-page document leaves on any key). */
void pages_show(int first, int pages)
{
    render_camera(0, 0);
    page_draw(first, 0, pages);
    page_draw(first, 0, pages);
    palette_fade_start(&g_pal_active, 0x10);
    for (int i = 0; i < 0x20; i++) render_present();
    int page = 0;
    if (pages == 1) {
        while (!kbd_first_released() && !quit()) render_present();
    } else {
        while (!kbd_was_pressed(KEY_ESC) && !quit()) {
            if ((kbd_was_pressed(KEY_UP) || kbd_was_pressed(KEY_PGUP)) && page > 0) { kbd_clear_all(); page--; }
            if ((kbd_was_pressed(KEY_DOWN) || kbd_was_pressed(KEY_PGDN)) && page < pages - 1) { kbd_clear_all(); page++; }
            page_draw(first, page, pages);
        }
    }
    palette_fade_start(&g_pal_black, 0x10);
    for (int i = 0; i < 0x20; i++) render_present();
    kbd_clear_all();
}

static void instructions(int item) { (void)item; pages_show(0x5b, 4); }   /* 0x182c0 */
static void credits(int item) { (void)item; pages_show(199, 1); }          /* 0x17f98 */

/* 0x21f44 (screen mode 4): the high score table. */
void high_scores_frame(bool present)
{
    render_build_occlusion();
    render_objects(-1, 0x31);
    render_frame_slot(0x5b, 0x9f00 - sar1(slot_w(0x5b)), 0x1800);
    text_draw(true, ALIGN_CENTRE, 0x9f00, 0x2900, FONT_MENU, 0x100, 0x500, "%s", g_text[31]);
    for (int i = 0; i < 10; i++) {
        int32_t y = 0x4000 + i * 0xb00;
        text_draw(true, ALIGN_RIGHT, 0x5000, y, 0x34e, 0, 0x400, "%d", i + 1);
        text_draw(true, ALIGN_LEFT, 0x6400, y, 0x34e, 0, 0x400, "%s", g.hi_names[i]);
        text_draw(true, ALIGN_RIGHT, 0xf000, y, 0x34e, 0, 0x400, "%d", g.hi_scores[i]);
    }
    if (present) render_present();
    objects_update();
}

/* 0x2221c / 0x2219c: until a key is let go. */
void high_scores_setup(void)
{
    g.mission_type = 4;
    hud_load();
    if (!obj_alive(0)) {
        obj_spawn(0, -1, 0, 0, 1);
        obj_set_slot(0, 0x40);
        obj_set_velocity(0, 0x80, 0x80);
        obj_set_callback(0, 0, CB_1a900, 0);
    }
    if (obj_alive(1)) {
        obj_kill(1);
        obj_kill(2);
    }
}

static void high_scores(int item)
{
    (void)item;
    high_scores_setup();
    while (!kbd_first_released() && !quit()) high_scores_frame(true);
    kbd_clear_all();
}

static MenuItem s_info_items[] = {
    ITEM(29, fade_out, instructions, fade_in, false), ITEM(31, NULL, high_scores, NULL, false),
    ITEM(32, fade_out, credits, fade_in, false), ITEM(27, NULL, NULL, NULL, true),
};
static Menu s_info = { 21, NULL, NULL, 6, 0xa000, 0x5000, 0, 4, s_info_items, false };

static void info_menu(int item) { (void)item; run_sub(&s_info); }   /* 0x17f70 */

/* ------------------------------------------------------------ main menu */

static MenuItem s_skill_items[] = {
    ITEM(68, fade_out, NULL, NULL, true), ITEM(69, fade_out, NULL, NULL, true),
    ITEM(70, fade_out, NULL, NULL, true), ITEM(71, fade_out, NULL, NULL, true),
};
static Menu s_skill = { 33, NULL, fade_out_v, 2, 0xa000, 0x4800, 1, 4, s_skill_items, false };

/* 0x17064: pick a skill, then play. */
static void start_game(int item)
{
    (void)item;
    int skill = menu_run(&s_skill);
    if (skill == -1 || quit()) return;
    game_new(skill, 0, 0, 0);
    tf_set_frame_rate(TF_RATE_FULL);
}

/* ------------------------------------------------------------ load / save */

#define SLOT_ITEM(n) { n, -1, 5, { NULL, NULL, NULL }, false }
static MenuItem s_load_items[] = {
    SLOT_ITEM("1"), SLOT_ITEM("2"), SLOT_ITEM("3"), SLOT_ITEM("4"), SLOT_ITEM("5"),
    ITEM(27, NULL, NULL, NULL, true),
};
static Menu s_load = { 20, NULL, NULL, 0, 0x4b00, 0x3000, 0, 6, s_load_items, true };

static MenuItem s_save_items[] = {
    { "1", -1, 5, { fade_out, NULL, NULL }, true }, { "2", -1, 5, { fade_out, NULL, NULL }, true },
    { "3", -1, 5, { fade_out, NULL, NULL }, true }, { "4", -1, 5, { fade_out, NULL, NULL }, true },
    { "5", -1, 5, { fade_out, NULL, NULL }, true }, ITEM(27, fade_out, NULL, NULL, true),
};
static void fade_in_v(void) { fade_in(0); }
static Menu s_save = { 19, fade_in_v, fade_out_v, 0, 0x4b00, 0x3000, 0, 6, s_save_items, true };

/* Only slots holding a valid game can be chosen (set by 0x163f4). */
static void load_items_update(void)
{
    for (int s = 0; s < SAVE_SLOTS; s++) {
        s_load_items[s].ret = save_slot_valid(s);
        s_load_items[s].act[0] = save_slot_valid(s) ? fade_out : NULL;
    }
}

/* 0x166a0: from the main menu this starts the session; in a game, the
 * mission loop sees load_slot and restarts with it. */
static void load_game(int item)
{
    (void)item;
    for (int s = 0; s < SAVE_SLOTS; s++) save_slot_read(s, false);
    load_items_update();
    int r = menu_run(&s_load);
    g.load_slot = r == 5 ? -1 : r;
    if (g.load_slot != -1 && !g.in_session) {
        session_run();
        tf_set_frame_rate(TF_RATE_FULL);
    }
}

/* 0x16f60: a save point: pick a slot (or DONE) and save. */
void save_point_menu(void)
{
    static bool parked[OBJ_COUNT];
    obj_set_velocity(SLOT_PLAYER, 0, 0);
    frame_render(1);
    frame_render(1);
    palette_fade_start(&g_pal_black, 0x10);
    for (int i = 0; i < 0x20; i++) render_present();
    sound_pause();
    objects_park(parked);
    for (int s = 0; s < SAVE_SLOTS; s++) save_slot_read(s, false);
    int r = menu_run(&s_save);
    for (int s = 0; s < 3; s++) obj_kill(s);
    objects_unpark(parked);
    g.mission_type = g_missions[g_state.mission].type;
    frame_render(1);
    frame_render(1);
    if (r != -1 && r != 5) save_write(r);
    palette_fade_start(&g_pal_active, 0x10);
    for (int i = 0; i < 0x20; i++) render_present();
}

static void demos(int item)   /* 0x13f00 */
{
    (void)item;
    attract_run();
    g.abort_session = false;
    tf_set_frame_rate(TF_RATE_FULL);
}

static MenuItem s_main_items[] = {
    ITEM(17, NULL, start_game, curtain, false), ITEM(18, NULL, options_menu, NULL, false),
    ITEM(20, NULL, load_game, curtain, false), ITEM(21, NULL, info_menu, NULL, false),
    ITEM(22, fade_out, demos, fade_in, false), ITEM(23, fade_out, NULL, NULL, true),
};
static Menu s_main = { -1, curtain_v, fade_out_v, 3, 0xa000, 0x4000, 0, 6, s_main_items, false };

void main_menu(void)
{
    tf_set_frame_rate(TF_RATE_FULL);
    menu_run(&s_main);
    game_save_settings();
}

/* ------------------------------------------------------------ in game */

static void quit_game(int item) { (void)item; g.abort_session = true; }   /* 0x16bf4 */

static MenuItem s_ingame_items[] = {
    ITEM(34, NULL, NULL, NULL, true), ITEM(18, NULL, options_menu, NULL, false),
    ITEM(20, NULL, load_game, NULL, true), ITEM(21, NULL, info_menu, NULL, false),
    ITEM(35, fade_out, quit_game, NULL, true),
};
static Menu s_ingame = { -1, NULL, NULL, 3, 0xa000, 0x4000, 0, 5, s_ingame_items, false };

/* Part of 0x15bbc: Esc during a mission. The game's objects are taken out
 * of the list while the menu uses its own (0-2). */
void ingame_menu(void)
{
    static bool parked[OBJ_COUNT];
    tf_set_frame_rate(TF_RATE_FULL);
    sound_pause();
    objects_park(parked);
    menu_run(&s_ingame);
    for (int s = 0; s < 3; s++) obj_kill(s);
    objects_unpark(parked);
    g.mission_type = g_missions[g_state.mission].type;
    tf_set_frame_rate(g.full_rate ? TF_RATE_FULL : TF_RATE_HALF);
}
