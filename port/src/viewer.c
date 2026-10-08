/* Asset viewer: first runnable target of the port. Exercises the platform
 * layer (video, palette, input, timing, sound) and every pak decoder.
 *
 *   Left/Right     previous/next pak entry
 *   Up/Down        previous/next frame (tile sets) or sprite page (sprite banks)
 *   Shift+arrows   scroll large tile frames
 *   Enter          switch between tile sets and sprite banks
 *   P              cycle palette
 *   Space          play the next sound effect
 *   Esc            quit
 */
#include "assets.h"
#include "platform.h"
#include "timing.h"

#include <SDL3/SDL.h>
#include <string.h>

#define FIRST_TILESET 25
#define LAST_TILESET 51
#define FIRST_SPRITES 61
#define LAST_SPRITES 106
#define FIRST_SOUND 4
#define LAST_SOUND 24

typedef struct {
    bool sprites_mode;
    int entry, frame, page;
    int palette;
    int scroll_x, scroll_y;
    int sound;
    TileSet ts;
    SpriteBank sb;
    bool loaded;
    Sound sfx;
} Viewer;

static void unload(Viewer *v)
{
    if (v->loaded) {
        if (v->sprites_mode) spritebank_free(&v->sb);
        else tileset_free(&v->ts);
    }
    v->loaded = false;
}

static void set_palette(Viewer *v, int pal)
{
    uint8_t rgb[768];
    v->palette = pal;
    if (palette_load(pal, rgb)) platform_set_palette(rgb);
}

static void load_entry(Viewer *v, int entry)
{
    unload(v);
    v->entry = entry;
    v->frame = v->page = v->scroll_x = v->scroll_y = 0;
    v->loaded = v->sprites_mode ? spritebank_load(entry, &v->sb) : tileset_load(entry, &v->ts);
    set_palette(v, asset_palette(entry));
}

static void update_title(const Viewer *v)
{
    char title[160];
    if (!v->loaded)
        SDL_snprintf(title, sizeof title, "TerraFire viewer - entry %d failed to load", v->entry);
    else if (v->sprites_mode)
        SDL_snprintf(title, sizeof title, "TerraFire viewer - sprites %d  (%d sprites, from #%d)  palette %d",
                     v->entry, v->sb.count, v->page, v->palette);
    else
        SDL_snprintf(title, sizeof title, "TerraFire viewer - tiles %d  frame %d/%d (%dx%d tiles)  palette %d",
                     v->entry, v->frame + 1, v->ts.frame_count, v->ts.frames[v->frame].w,
                     v->ts.frames[v->frame].h, v->palette);
    platform_set_title(title);
}

/* Lay sprites out left-to-right from `first` until the screen is full;
 * returns how many fit. */
static int draw_sprite_page(const SpriteBank *sb, int first)
{
    int x = 2, y = 2, row_h = 0, shown = 0;
    for (int i = first; i < sb->count; i++) {
        const Sprite *s = &sb->sprites[i];
        if (x + s->w > SCREEN_W - 2 && x > 2) {
            x = 2;
            y += row_h + 3;
            row_h = 0;
        }
        if (y + s->h > SCREEN_H - 2 && shown > 0) break;
        draw_sprite(s, x, y);
        x += s->w + 3;
        if (s->h > row_h) row_h = s->h;
        shown++;
    }
    return shown;
}

static void render(Viewer *v, int *shown)
{
    memset(g_screen, 0, sizeof g_screen);
    *shown = 0;
    if (!v->loaded) return;
    if (v->sprites_mode) {
        *shown = draw_sprite_page(&v->sb, v->page);
    } else {
        draw_tileframe(&v->ts, v->frame, -v->scroll_x, -v->scroll_y);
    }
}

static void clamp_scroll(Viewer *v)
{
    if (v->sprites_mode || !v->loaded) return;
    const TileFrame *fr = &v->ts.frames[v->frame];
    int max_x = fr->w * TILE - SCREEN_W, max_y = fr->h * TILE - SCREEN_H;
    if (v->scroll_x > max_x) v->scroll_x = max_x;
    if (v->scroll_y > max_y) v->scroll_y = max_y;
    if (v->scroll_x < 0) v->scroll_x = 0;
    if (v->scroll_y < 0) v->scroll_y = 0;
}

int viewer_run(void)
{
    Viewer v;
    memset(&v, 0, sizeof v);
    v.sound = FIRST_SOUND - 1;
    load_entry(&v, FIRST_TILESET);
    update_title(&v);

    tf_set_frame_rate(TF_RATE_FULL);
    int shown = 0;
    while (platform_poll()) {
        bool shift = g_key_down[KEY_LSHIFT] || g_key_down[0x36];
        for (int key; (key = platform_next_key()) != 0;) {
            bool changed = true;
            int first = v.sprites_mode ? FIRST_SPRITES : FIRST_TILESET;
            int last = v.sprites_mode ? LAST_SPRITES : LAST_TILESET;
            if (key == KEY_ESC) {
                unload(&v);
                sound_free(&v.sfx);
                return 0;
            } else if (key == KEY_RIGHT && !shift) {
                load_entry(&v, v.entry >= last ? first : v.entry + 1);
            } else if (key == KEY_LEFT && !shift) {
                load_entry(&v, v.entry <= first ? last : v.entry - 1);
            } else if ((key == KEY_DOWN || key == KEY_UP) && !shift && v.loaded) {
                int dir = key == KEY_DOWN ? 1 : -1;
                if (v.sprites_mode) {
                    int step = dir > 0 ? (shown ? shown : 1) : -(shown ? shown : 1);
                    v.page += step;
                    if (v.page >= v.sb.count) v.page = 0;
                    if (v.page < 0) v.page = 0;
                } else {
                    v.frame = (v.frame + dir + v.ts.frame_count) % v.ts.frame_count;
                    clamp_scroll(&v);
                }
            } else if (key == KEY_ENTER) {
                unload(&v);
                v.sprites_mode = !v.sprites_mode;
                load_entry(&v, v.sprites_mode ? FIRST_SPRITES : FIRST_TILESET);
            } else if (key == KEY_P) {
                set_palette(&v, v.palette >= 59 ? 52 : v.palette + 1);
            } else if (key == KEY_SPACE) {
                v.sound = v.sound >= LAST_SOUND ? FIRST_SOUND : v.sound + 1;
                sound_free(&v.sfx);
                if (sound_load(v.sound, &v.sfx))
                    platform_play_sound(0, v.sfx.samples, v.sfx.count, v.sfx.rate_hz, 1.0f, false);
                SDL_Log("sound %d: %d Hz, %lu samples", v.sound, v.sfx.rate_hz, (unsigned long)v.sfx.count);
                changed = false;
            } else {
                changed = false;
            }
            if (changed) update_title(&v);
        }

        if (shift && !v.sprites_mode) {
            const int speed = 4;
            if (g_key_down[KEY_LEFT]) v.scroll_x -= speed;
            if (g_key_down[KEY_RIGHT]) v.scroll_x += speed;
            if (g_key_down[KEY_UP]) v.scroll_y -= speed;
            if (g_key_down[KEY_DOWN]) v.scroll_y += speed;
            clamp_scroll(&v);
        }

        render(&v, &shown);
        tf_present();
    }
    unload(&v);
    sound_free(&v.sfx);
    return 0;
}
