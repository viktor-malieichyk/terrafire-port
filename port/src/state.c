#include "game.h"

#include <stdio.h>
#include <string.h>

#include "platform.h"

GameState g_state = {0};
Globals g = {0};

/* Defaults until settings.cfg is read: left/right turn, up thrust, space fire.
 * (The original fires with Ctrl.) */
void game_default_keys(void)
{
    g.keys[0] = 0x4b;
    g.keys[1] = 0x4d;
    g.keys[2] = 0x48;
    g.keys[3] = 0x39;
}

void game_init_globals(void)
{
    memset(&g, 0, sizeof g);
    g.load_slot = -1;
    g.sound = true;
    g.full_rate = true;
    g.volume = 100;
    g.scaling = SCALE_SHARP;
    game_default_keys();
    g.detail = 2;
}

static int32_t rd32(const uint8_t *raw, int off)
{
    return (int32_t)((uint32_t)raw[off] | (uint32_t)raw[off + 1] << 8 |
                     (uint32_t)raw[off + 2] << 16 | (uint32_t)raw[off + 3] << 24);
}

static void put32(uint8_t *raw, int off, int32_t v)
{
    raw[off] = (uint8_t)v;
    raw[off + 1] = (uint8_t)(v >> 8);
    raw[off + 2] = (uint8_t)(v >> 16);
    raw[off + 3] = (uint8_t)(v >> 24);
}

void legacy_store_state(const GameState *st, uint8_t raw[LEGACY_STATE_SIZE])
{
    memcpy(raw, st->legacy, LEGACY_STATE_SIZE);
    put32(raw, 0x00, st->difficulty);
    put32(raw, 0x04, st->mission);
    put32(raw, 0x08, st->mission_frames);
    put32(raw, 0x0c, st->f_00c);
    put32(raw, 0x10, st->score);
    raw[0x14] = st->f_014;
    put32(raw, 0x15, st->f_015);
    put32(raw, 0x19, st->f_019);
    raw[0x1d] = st->f_01d_flag;
    raw[0x1e] = st->f_01e_flag;
    memcpy(raw + 0x1f, st->message, sizeof st->message);
    put32(raw, 0x83, st->message_time);
    memcpy(raw + 0x87, &st->tow_angle, sizeof st->tow_angle);
    put32(raw, 0x8f, st->weapon);
    put32(raw, 0x93, st->barrel);
    put32(raw, 0x97, st->side_ammo);
    put32(raw, 0x9b, st->last_shot);
    put32(raw, 0x9f, st->last_side_shot);
    put32(raw, 0xa3, st->shields_max);
    put32(raw, 0xa7, st->angle);
    put32(raw, 0xab, st->stage);
    put32(raw, 0xaf, st->f_0af);
    for (int i = 0; i < OBJ_SLOTS; i++) {
        uint8_t *o = raw + 0xb3 + i * 16;
        put32(o, 0, st->obj[i].o_0);
        put32(o, 4, st->obj[i].o_4);
        put32(o, 8, st->obj[i].o_8);
        put32(o, 12, st->obj[i].o_c);
    }
}

/* Offsets match re/tools/tf_state.py (HEADER_FIELDS); keep the two in step. */
void legacy_load_state(GameState *st, const uint8_t raw[LEGACY_STATE_SIZE])
{
    memcpy(st->legacy, raw, LEGACY_STATE_SIZE);
    st->difficulty = rd32(raw, 0x00);
    st->mission = rd32(raw, 0x04);
    st->mission_frames = rd32(raw, 0x08);
    st->f_00c = rd32(raw, 0x0c);
    st->score = rd32(raw, 0x10);
    st->f_014 = raw[0x14];
    st->f_015 = rd32(raw, 0x15);
    st->f_019 = rd32(raw, 0x19);
    st->f_01d_flag = raw[0x1d];
    st->f_01e_flag = raw[0x1e];
    memcpy(st->message, raw + 0x1f, sizeof st->message);
    st->message[sizeof st->message - 1] = 0;
    st->message_time = rd32(raw, 0x83);
    st->last_shot = rd32(raw, 0x9b);
    st->last_side_shot = rd32(raw, 0x9f);
    memcpy(&st->tow_angle, raw + 0x87, sizeof st->tow_angle);   /* little-endian IEEE double */
    st->weapon = rd32(raw, 0x8f);
    st->barrel = rd32(raw, 0x93);
    st->side_ammo = rd32(raw, 0x97);
    st->shields_max = rd32(raw, 0xa3);
    st->angle = rd32(raw, 0xa7);
    st->stage = rd32(raw, 0xab);
    st->f_0af = rd32(raw, 0xaf);
    for (int i = 0; i < OBJ_SLOTS; i++) {
        const uint8_t *o = raw + 0xb3 + i * 16;
        st->obj[i].o_0 = rd32(o, 0);
        st->obj[i].o_4 = rd32(o, 4);
        st->obj[i].o_8 = rd32(o, 8);
        st->obj[i].o_c = rd32(o, 12);
    }
}

/* The settings block (0x5e27c, 0x1ab bytes) as last read, and its file. */
static uint8_t s_cfg[0x1ab];
static bool s_cfg_loaded;
static char s_cfg_path[1024];
static char s_dir[1024] = ".";

const char *game_dir(void) { return s_dir; }

static void wr32(uint8_t *p, int off, int32_t v)
{
    p[off] = (uint8_t)v;
    p[off + 1] = (uint8_t)(v >> 8);
    p[off + 2] = (uint8_t)(v >> 16);
    p[off + 3] = (uint8_t)(v >> 24);
}

/* Port-only options live in their own file so SETTINGS.CFG keeps the
 * original's format: "name value" lines. */
static void port_cfg_path(char *out, size_t n) { snprintf(out, n, "%s/PORT.CFG", s_dir); }

static void port_cfg_load(void)
{
    char path[1100], name[32];
    int v;
    port_cfg_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fscanf(f, "%31s %d", name, &v) == 2) {
        if (strcmp(name, "fullscreen") == 0) g.fullscreen = v != 0;
        else if (strcmp(name, "scaling") == 0 && v >= 0 && v < SCALE_MODES) g.scaling = v;
    }
    fclose(f);
}

static bool port_cfg_save(void)
{
    char path[1100];
    port_cfg_path(path, sizeof path);
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "fullscreen %d\nscaling %d\n", g.fullscreen, (int)g.scaling);
    return fclose(f) == 0;
}

bool game_save_settings(void)
{
    port_cfg_save();
    if (!s_cfg_loaded) return false;
    uint8_t *b = s_cfg;
    b[0x01] = g.sound;
    wr32(b, 0x0a, g.volume);
    b[0xb6] = g.full_rate;
    b[0xb7] = g.debris;
    wr32(b, 0xb8, g.detail);
    b[0xbc] = g.joystick;
    for (int k = 0; k < 4; k++) wr32(b, 0xd1 + 4 * k, g.keys[k]);
    for (int i = 0; i < 10; i++) {
        memcpy(b + 0xed + i * 15, g.hi_names[i], 15);
        wr32(b, 0x183 + i * 4, g.hi_scores[i]);
    }
    FILE *f = fopen(s_cfg_path, "wb");
    if (!f) return false;
    bool ok = fwrite(b, 1, sizeof s_cfg, f) == sizeof s_cfg;
    return fclose(f) == 0 && ok;
}

bool game_load_settings(const char *dir)
{
    static const char *names[] = { "SETTINGS.CFG", "settings.cfg" };
    uint8_t *b = s_cfg;
    FILE *f = NULL;
    snprintf(s_dir, sizeof s_dir, "%s", dir ? dir : ".");
    port_cfg_load();
    for (int i = 0; i < 2 && !f; i++) {
        snprintf(s_cfg_path, sizeof s_cfg_path, "%s%s%s", dir ? dir : "", dir ? "/" : "", names[i]);
        f = fopen(s_cfg_path, "rb");
    }
    if (!f) return false;
    bool ok = fread(b, 1, sizeof s_cfg, f) == sizeof s_cfg;
    fclose(f);
    if (!ok) return false;
    s_cfg_loaded = true;
    g.volume = rd32(b, 0x0a);
    for (int i = 0; i < 10; i++) {
        memcpy(g.hi_names[i], b + 0xed + i * 15, 15);
        g.hi_names[i][14] = 0;
        g.hi_scores[i] = rd32(b, 0x183 + i * 4);
    }
    /* Offsets relative to 0x5e27c. */
    g.sound = b[0x01] != 0;
    g.full_rate = b[0xb6] != 0;
    g.debris = b[0xb7] != 0;
    g.detail = rd32(b, 0xb8);
    g.joystick = b[0xbc] != 0;
    for (int k = 0; k < 4; k++) g.keys[k] = (uint8_t)rd32(b, 0xd1 + 4 * k);
    return true;
}
