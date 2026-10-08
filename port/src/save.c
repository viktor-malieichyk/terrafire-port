#include "save.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "anims.h"
#include "callbacks.h"
#include "game.h"
#include "hud.h"
#include "objects.h"
#include "platform.h"
#include "res.h"
#include "tables.h"
#include "timing.h"

#define MARKER 0xcd
#define THUMB_W 160
#define THUMB_H 100

static char s_stamp[SAVE_SLOTS][25];      /* 0x5b884 */
static bool s_valid[SAVE_SLOTS];

bool save_slot_valid(int slot) { return slot >= 0 && slot < SAVE_SLOTS && s_valid[slot]; }
const char *save_slot_stamp(int slot) { return slot >= 0 && slot < SAVE_SLOTS ? s_stamp[slot] : ""; }

static void slot_path(int slot, char *out, size_t n)
{
    snprintf(out, n, "%s/GAME000%c.SAV", game_dir(), '1' + slot);
}

/* 0x24ddc over a file: backwards over its bytes (but the last 4 if
 * `has_sum`), every non-space byte b adds (b ^ remaining) + rand_table[b]. */
static int32_t checksum(FILE *f, bool has_sum)
{
    if (fseek(f, 0, SEEK_END) != 0) return -1;
    long len = ftell(f) - (has_sum ? 4 : 0);
    rewind(f);
    int32_t sum = 0;
    for (long u = len; u > 0; u--) {
        int b = fgetc(f);
        if (b == EOF) return -1;
        if (b != 0x20) sum += (int32_t)((uint32_t)b ^ (uint32_t)u) + g_random_table[b];
    }
    int32_t v = (int32_t)((uint32_t)sum + 0x105f7u);
    if (v < 0) v = -v;
    return v % 900000 + 100000;
}

static bool w32(FILE *f, int32_t v)
{
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return fwrite(b, 1, 4, f) == 4;
}

static bool r32(FILE *f, int32_t *v)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    *v = (int32_t)((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24);
    return true;
}

static void put16(uint8_t *p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, int32_t v) { put16(p, v & 0xffff); put16(p + 2, (v >> 16) & 0xffff); }
static int16_t get16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }
static int32_t get32(const uint8_t *p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }

/* The original's 0x60-byte object record. Pointers (list links, animation,
 * callbacks) are written as the original's addresses; on loading, the list
 * is rebuilt and animation/callbacks come from the indices that follow. */
static void record_store(const Object *o, uint8_t r[0x60])
{
    memset(r, 0, 0x60);
    put16(r + 0x00, o->id);
    r[0x02] = o->alive;
    put16(r + 0x0b, o->bank);
    put16(r + 0x0d, o->orient);
    r[0x0f] = o->f_0f;
    put16(r + 0x10, o->slot);
    put32(r + 0x12, o->x);
    put32(r + 0x16, o->y);
    put32(r + 0x1a, o->z);
    put32(r + 0x1e, o->vx);
    put32(r + 0x22, o->vy);
    put32(r + 0x26, o->ax);
    put32(r + 0x2a, o->ay);
    for (int k = 0; k < 4; k++) put32(r + 0x2e + 4 * k, o->box[k]);
    put32(r + 0x3e, o->anim >= 0 ? (int32_t)g_anims[o->anim].orig_addr : 0);
    put16(r + 0x42, o->anim_frame);
    r[0x44] = o->frozen;
    put16(r + 0x45, o->parent);
    put16(r + 0x47, o->child);
    r[0x49] = o->f_49;
    put16(r + 0x4a, o->f_4a);
    put16(r + 0x4c, o->no_drag);
    for (int k = 0; k < 3; k++) {
        put16(r + 0x4e + 2 * k, o->cb_timer[k]);
        put32(r + 0x54 + 4 * k, o->cb_timer[k] != -1 ? (int32_t)g_callback_addr[o->cb[k]] : 0);
    }
}

static void record_load(Object *o, const uint8_t r[0x60])
{
    o->id = get16(r + 0x00);
    o->bank = get16(r + 0x0b);
    o->orient = get16(r + 0x0d);
    o->f_0f = r[0x0f];
    o->slot = get16(r + 0x10);
    o->x = get32(r + 0x12);
    o->y = get32(r + 0x16);
    o->z = get32(r + 0x1a);
    o->vx = get32(r + 0x1e);
    o->vy = get32(r + 0x22);
    o->ax = get32(r + 0x26);
    o->ay = get32(r + 0x2a);
    for (int k = 0; k < 4; k++) o->box[k] = get32(r + 0x2e + 4 * k);
    o->anim = get32(r + 0x3e) != 0 ? 0 : -1;          /* index follows */
    o->anim_frame = get16(r + 0x42);
    o->frozen = r[0x44];
    o->parent = get16(r + 0x45);
    o->child = get16(r + 0x47);
    o->f_49 = r[0x49];
    o->f_4a = get16(r + 0x4a);
    o->no_drag = get16(r + 0x4c);
    for (int k = 0; k < 3; k++) o->cb_timer[k] = get16(r + 0x4e + 2 * k);
}

/* 0x277c4 */
static bool objects_write(FILE *f)
{
    if (!w32(f, (int32_t)tf_frame_counter())) return false;
    for (int i = 0; i < OBJ_COUNT; i++) {
        const Object *o = &g_objects[i];
        if (fputc(o->alive ? 1 : 0, f) == EOF) return false;
        if (!o->alive) continue;
        uint8_t r[0x60];
        record_store(o, r);
        if (fwrite(r, 1, sizeof r, f) != sizeof r) return false;
        if (o->anim >= 0 && !w32(f, o->anim)) return false;
        for (int k = 0; k < 3; k++)
            if (o->cb_timer[k] != -1 && !w32(f, o->cb[k])) return false;
    }
    return true;
}

/* 0x2756c */
static bool objects_read(FILE *f)
{
    int32_t counter;
    if (!r32(f, &counter)) return false;
    tf_set_frame_counter((uint32_t)counter);
    objects_clear_active();                       /* as 0x2c190 */
    for (int i = 0; i < OBJ_COUNT; i++) {
        int alive = fgetc(f);
        if (alive == EOF) return false;
        if (!alive) continue;
        uint8_t r[0x60];
        if (fread(r, 1, sizeof r, f) != sizeof r) return false;
        Object *o = &g_objects[i];
        record_load(o, r);
        int32_t idx;
        if (o->anim == 0) {
            if (!r32(f, &idx) || idx < 0 || idx >= ANIM_COUNT) return false;
            o->anim = (int16_t)idx;
        }
        for (int k = 0; k < 3; k++)
            if (o->cb_timer[k] != -1) {
                if (!r32(f, &idx) || idx < 0 || idx >= CALLBACK_COUNT) return false;
                o->cb[k] = (int16_t)idx;
            }
        o->alive = 0;
        obj_link(i);
    }
    return true;
}

/* The checksum in the last 4 bytes (0x24f10), or -2. */
static int32_t stored_checksum(FILE *f)
{
    int32_t v;
    if (fseek(f, -4, SEEK_END) != 0 || !r32(f, &v)) return -2;
    return v;
}

/* 0x29c60 into the slot's thumbnail bank (0x936 + slot, framed 2 px). */
static void thumb_pixel(int bank, int x, int y, uint8_t c)
{
    if (bank < 0 || bank >= RES_BANKS || !g_banks[bank].pixels) return;
    const BankView *v = &g_banks[bank].view[0];
    int h = v->h >> 8;
    if (x < 0 || y < 0 || x >= (v->w >> 8) || y >= h) return;
    g_banks[bank].pixels[v->start + x * h + y] = c;
}

void save_slot_read(int slot, bool load)
{
    s_valid[slot] = false;
    char path[1100];
    slot_path(slot, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) {
        s_stamp[slot][0] = 0;
        g.save_mission[slot] = -1;
        return;
    }
    if (checksum(f, true) != stored_checksum(f)) {
        snprintf(s_stamp[slot], sizeof s_stamp[slot], "%s", g_text[0x38]);   /* INVALID SAVED GAME */
        g.save_mission[slot] = -1;
        fclose(f);
        return;
    }
    rewind(f);
    int32_t marker = 0;
    bool ok = fread(s_stamp[slot], 1, 25, f) == 25 && r32(f, &g.save_mission[slot]) &&
              r32(f, &g.save_difficulty[slot]) && r32(f, &marker);
    s_stamp[slot][24] = 0;
    if (!ok || marker != MARKER) {
        snprintf(s_stamp[slot], sizeof s_stamp[slot], "%s", g_text[0x39]);   /* INCOMPATIBLE VERSION */
        g.save_mission[slot] = -1;
        fclose(f);
        return;
    }
    s_valid[slot] = true;
    for (int y = 0; y < 200; y += 2)
        for (int x = 0; x < 0x13f; x += 2) {
            int c = fgetc(f);
            thumb_pixel(0x936 + slot, (x >> 1) + 2, (y >> 1) + 2, (uint8_t)(c == EOF ? 0 : c));
        }
    if (load) {
        static uint8_t raw[LEGACY_STATE_SIZE];
        if (fread(raw, 1, sizeof raw, f) == sizeof raw) {
            legacy_load_state(&g_state, raw);
            g_state.f_014 = g_state.f_014 || g.cheats;
            tf_set_frame_counter((uint32_t)g_state.mission_frames);
            if (!objects_read(f)) fprintf(stderr, "save slot %d: object data damaged\n", slot + 1);
            hud_message(0xd2, "%s", g_text[0x3c]);              /* GAME LOADED */
        }
    }
    fclose(f);
}

bool save_write(int slot)
{
    char path[1100];
    slot_path(slot, path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f) {
        hud_message(0xd2, "%s", g_text[0x3a]);                  /* UNABLE TO SAVE */
        return false;
    }
    char stamp[26];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    static const char *days[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    static const char *months[12] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                      "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };
    snprintf(stamp, sizeof stamp, "%s %s %2d %02d:%02d:%02d %d",   /* as ctime(), upper case */
             days[tm->tm_wday % 7], months[tm->tm_mon % 12], tm->tm_mday, tm->tm_hour, tm->tm_min,
             tm->tm_sec, tm->tm_year + 1900);
    for (char *p = stamp; *p; p++) *p = (char)toupper((unsigned char)*p);
    char rec[25] = { 0 };
    memcpy(rec, stamp, strlen(stamp) < 24 ? strlen(stamp) : 24);
    bool ok = fwrite(rec, 1, 25, f) == 25 && w32(f, g_state.mission) && w32(f, g_state.difficulty) &&
              w32(f, MARKER);
    for (int y = 0; ok && y < 200; y += 2)
        for (int x = 0; ok && x < 0x13f; x += 2)
            ok = fputc(g_screen[y * SCREEN_W + x], f) != EOF;
    static uint8_t raw[LEGACY_STATE_SIZE];
    legacy_store_state(&g_state, raw);
    ok = ok && fwrite(raw, 1, sizeof raw, f) == sizeof raw;
    if (ok) obj_set_callback(SLOT_PLAYER, 0, CB_20318, 1);   /* the loaded game resumes control */
    ok = ok && objects_write(f);
    ok = fclose(f) == 0 && ok;
    if (ok) {                                            /* 0x24d74: append the checksum */
        f = fopen(path, "rb");
        int32_t sum = f ? checksum(f, false) : -1;
        if (f) fclose(f);
        f = fopen(path, "ab");
        ok = f && sum != -1 && w32(f, sum);
        if (f) ok = fclose(f) == 0 && ok;
    }
    if (!ok) {
        hud_message(0xd2, "%s", g_text[0x3a]);
        return false;
    }
    save_slot_read(slot, false);
    hud_message(0xd2, "%s", g_text[0x3b]);                      /* GAME SAVED */
    return true;
}
