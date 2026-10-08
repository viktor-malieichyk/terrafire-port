#include "assets.h"

#include "pak.h"
#include "platform.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

/* Bounds-checked little-endian reader over a pak entry. Any overrun sets
 * `bad`, so decoders can read freely and check once at the end. */
typedef struct {
    const uint8_t *p;
    size_t size, pos;
    bool bad;
} Reader;

static bool need(Reader *r, size_t n)
{
    if (r->bad || r->size - r->pos < n) { r->bad = true; return false; }
    return true;
}
static uint8_t rd_u8(Reader *r) { return need(r, 1) ? r->p[r->pos++] : 0; }
static uint16_t rd_u16(Reader *r)
{
    if (!need(r, 2)) return 0;
    uint16_t v = (uint16_t)(r->p[r->pos] | r->p[r->pos + 1] << 8);
    r->pos += 2;
    return v;
}
static int16_t rd_s16(Reader *r) { return (int16_t)rd_u16(r); }
static uint32_t rd_u32(Reader *r)
{
    uint32_t lo = rd_u16(r);
    return lo | (uint32_t)rd_u16(r) << 16;
}
static const uint8_t *rd_bytes(Reader *r, size_t n)
{
    if (!need(r, n)) return NULL;
    const uint8_t *q = r->p + r->pos;
    r->pos += n;
    return q;
}

/* ------------------------------------------------------------------ palette */

bool palette_load(int entry, uint8_t rgb_out[768])
{
    size_t size;
    uint8_t *e = pak_read(entry, &size);
    if (!e) return false;
    bool ok = size == 770 && e[0] == 0x00 && e[1] == 0xff;
    if (ok) {
        for (int i = 0; i < 768; i++) {
            uint8_t c = e[2 + i] & 0x3f;
            rgb_out[i] = (uint8_t)(c << 2 | c >> 4);   /* 6-bit VGA -> 8-bit */
        }
    }
    free(e);
    return ok;
}

/* Taken from the code that loads each asset; see re/tools/pak_extract.py. */
int asset_palette(int entry)
{
    switch (entry) {
    case 26: return 54;
    case 27: case 34: return 53;
    case 43: return 56;
    case 47: return 57;
    default: return PAL_GAMEPLAY;
    }
}

/* -------------------------------------------------------------------- sound */

static const char DWD_MAGIC[] = "DiamondWare Digitized\n\0\x1a";   /* 24 bytes */

bool sound_load(int entry, Sound *out)
{
    memset(out, 0, sizeof *out);
    size_t size;
    uint8_t *e = pak_read(entry, &size);
    if (!e) return false;
    Reader r = { e, size, 0, false };
    const uint8_t *magic = rd_bytes(&r, 24);
    bool ok = magic && memcmp(magic, DWD_MAGIC, 24) == 0;
    if (ok) {
        rd_u8(&r); rd_u8(&r);              /* version major/minor */
        rd_u32(&r);                         /* unique id */
        rd_u8(&r);                          /* reserved */
        uint8_t comp = rd_u8(&r);
        int rate = rd_u16(&r);
        uint8_t channels = rd_u8(&r), bits = rd_u8(&r);
        rd_s16(&r);                         /* peak */
        uint32_t length = rd_u32(&r);
        rd_u32(&r);                         /* sample count */
        uint32_t data_off = rd_u32(&r);
        ok = !r.bad && comp == 0 && channels == 1 && bits == 8 &&
             data_off <= size && length <= size - data_off;
        if (ok) {
            out->samples = malloc(length ? length : 1);
            ok = out->samples != NULL;
            if (ok) {
                memcpy(out->samples, e + data_off, length);
                out->count = length;
                out->rate_hz = rate;
            }
        }
    }
    free(e);
    return ok;
}

void sound_free(Sound *s)
{
    free(s->samples);
    memset(s, 0, sizeof *s);
}

/* ----------------------------------------------------------------- tile set */

/* The original buffer the game draws tiles into is 352 bytes wide. */
#define TILE_STRIDE 352

/* Kind-0 tiles are compiled sprites: x86 code consisting only of
 * `mov [edi+disp], imm8/16/32` and a final `ret`, which the game called with
 * EDI at the tile's top-left. Decode the stores into pixels + mask; never
 * execute them. */
static bool decode_compiled_sprite(const uint8_t *code, size_t len, Tile *t)
{
    memset(t->pixels, 0, sizeof t->pixels);
    memset(t->mask, 0, sizeof t->mask);
    size_t p = 0;
    for (;;) {
        if (p >= len) return false;
        uint8_t op = code[p];
        if (op == 0xc3) return p + 1 == len;
        int width;
        if (op == 0x66) {
            if (++p >= len || code[p] != 0xc7) return false;
            width = 2;
        } else if (op == 0xc6) {
            width = 1;
        } else if (op == 0xc7) {
            width = 4;
        } else {
            return false;
        }
        if (p + 1 >= len) return false;
        uint8_t modrm = code[p + 1];
        p += 2;
        int32_t disp;
        if (modrm == 0x07) {
            disp = 0;
        } else if (modrm == 0x47) {
            if (p >= len) return false;
            disp = (int8_t)code[p++];
        } else if (modrm == 0x87) {
            if (p + 4 > len) return false;
            disp = (int32_t)((uint32_t)code[p] | (uint32_t)code[p + 1] << 8 |
                             (uint32_t)code[p + 2] << 16 | (uint32_t)code[p + 3] << 24);
            p += 4;
        } else {
            return false;
        }
        if (p + (size_t)width > len) return false;
        for (int k = 0; k < width; k++) {
            int32_t at = disp + k;
            int row = at / TILE_STRIDE, col = at % TILE_STRIDE;
            if (at < 0 || row >= TILE || col >= TILE) return false;
            t->pixels[row * TILE + col] = code[p + k];
            t->mask[row * TILE + col] = 1;
        }
        p += (size_t)width;
    }
}

void tileset_free(TileSet *ts)
{
    if (ts->frames)
        for (int i = 0; i < ts->frame_count; i++) {
            free(ts->frames[i].tiles);
            free(ts->frames[i].flags);
        }
    free(ts->frames);
    free(ts->tiles);
    memset(ts, 0, sizeof *ts);
}

bool tileset_load(int entry, TileSet *out)
{
    memset(out, 0, sizeof *out);
    size_t size;
    uint8_t *e = pak_read(entry, &size);
    if (!e) return false;
    Reader r = { e, size, 0, false };
    bool ok = true;

    out->frame_count = rd_u16(&r);
    out->frames = calloc((size_t)out->frame_count + 1, sizeof *out->frames);
    ok = out->frames != NULL;
    for (int f = 0; ok && f < out->frame_count; f++) {
        TileFrame *fr = &out->frames[f];
        fr->w = rd_s16(&r);
        fr->h = rd_s16(&r);
        if (r.bad || fr->w <= 0 || fr->h <= 0) { ok = false; break; }
        size_t n = (size_t)fr->w * (size_t)fr->h;
        fr->tiles = malloc(n * sizeof *fr->tiles);
        fr->flags = malloc(n);
        if (!fr->tiles || !fr->flags) { ok = false; break; }
        for (size_t k = 0; k < n; k++) {
            fr->tiles[k] = rd_u16(&r);
            fr->flags[k] = rd_u8(&r);
        }
    }

    if (ok) {
        out->tile_count = rd_u16(&r);
        out->tiles = calloc((size_t)out->tile_count + 1, sizeof *out->tiles);
        ok = out->tiles != NULL;
    }
    for (int i = 0; ok && i < out->tile_count; i++) {
        Tile *t = &out->tiles[i];
        t->kind = rd_u8(&r);
        t->bank = rd_s16(&r);
        uint16_t len = rd_u16(&r);
        t->len = len;
        const uint8_t *body = rd_bytes(&r, len);
        if (!body) { ok = false; break; }
        if (t->kind == 0) {
            ok = decode_compiled_sprite(body, len, t);
        } else if (len == TILE * TILE) {
            memcpy(t->pixels, body, TILE * TILE);
            memset(t->mask, 1, TILE * TILE);
        } else {
            ok = false;
        }
    }

    /* Every cell must reference a tile, and the entry must be consumed exactly. */
    ok = ok && !r.bad && r.pos == size;
    for (int f = 0; ok && f < out->frame_count; f++)
        for (int k = 0; ok && k < out->frames[f].w * out->frames[f].h; k++)
            ok = out->frames[f].tiles[k] < out->tile_count;

    free(e);
    if (!ok) {
        SDL_Log("pak entry %d is not a valid tile set", entry);
        tileset_free(out);
    }
    return ok;
}

/* ------------------------------------------------------------- sprite bank */

void spritebank_free(SpriteBank *sb)
{
    if (sb->sprites)
        for (int i = 0; i < sb->count; i++) {
            free(sb->sprites[i].pixels);
            free(sb->sprites[i].pixels_cm);
        }
    free(sb->sprites);
    memset(sb, 0, sizeof *sb);
}

bool spritebank_load(int entry, SpriteBank *out)
{
    memset(out, 0, sizeof *out);
    size_t size;
    uint8_t *e = pak_read(entry, &size);
    if (!e) return false;
    Reader r = { e, size, 0, false };
    out->count = rd_u16(&r);
    out->sprites = calloc((size_t)out->count + 1, sizeof *out->sprites);
    bool ok = out->sprites != NULL;

    for (int i = 0; ok && i < out->count; i++) {
        Sprite *s = &out->sprites[i];
        for (int k = 0; k < 4; k++) {
            s->quad[k][0] = rd_s16(&r);
            s->quad[k][1] = rd_s16(&r);
        }
        s->hot_x = rd_s16(&r);
        s->hot_y = rd_s16(&r);
        s->w = rd_s16(&r);
        s->h = rd_s16(&r);
        if (r.bad || s->w < 0 || s->h < 0) { ok = false; break; }
        size_t total = (size_t)s->w * (size_t)s->h, got = 0;
        uint8_t *colmajor = s->pixels_cm = malloc(total ? total : 1);
        s->pixels = malloc(total ? total : 1);
        if (!colmajor || !s->pixels) { ok = false; break; }
        /* Alternating runs: {u8 n; u8 value if n} then {u8 n; u8 literal[n]}. */
        while (got < total && !r.bad) {
            uint8_t run = rd_u8(&r);
            if (run) {
                uint8_t v = rd_u8(&r);
                if (run > total - got) { r.bad = true; break; }
                memset(colmajor + got, v, run);
                got += run;
            }
            if (got == total) break;
            uint8_t lit = rd_u8(&r);
            const uint8_t *src = rd_bytes(&r, lit);
            if (!src || lit > total - got) { r.bad = true; break; }
            memcpy(colmajor + got, src, lit);
            got += lit;
        }
        if (r.bad || got != total) { ok = false; break; }
        /* Stored column-major; keep row-major in memory for straightforward blits. */
        for (int x = 0; x < s->w; x++)
            for (int y = 0; y < s->h; y++)
                s->pixels[y * s->w + x] = colmajor[x * s->h + y];
    }
    ok = ok && !r.bad && r.pos == size;
    free(e);
    if (!ok) {
        SDL_Log("pak entry %d is not a valid sprite bank", entry);
        spritebank_free(out);
    }
    return ok;
}

/* ----------------------------------------------------------------- drawing */

static void put_tile(const Tile *t, int x0, int y0)
{
    for (int y = 0; y < TILE; y++) {
        int sy = y0 + y;
        if (sy < 0 || sy >= SCREEN_H) continue;
        for (int x = 0; x < TILE; x++) {
            int sx = x0 + x;
            if (sx < 0 || sx >= SCREEN_W || !t->mask[y * TILE + x]) continue;
            g_screen[sy * SCREEN_W + sx] = t->pixels[y * TILE + x];
        }
    }
}

void draw_tileframe(const TileSet *ts, int frame, int x, int y)
{
    if (frame < 0 || frame >= ts->frame_count) return;
    const TileFrame *fr = &ts->frames[frame];
    for (int ty = 0; ty < fr->h; ty++) {
        int py = y + ty * TILE;
        if (py <= -TILE || py >= SCREEN_H) continue;
        for (int tx = 0; tx < fr->w; tx++) {
            int px = x + tx * TILE;
            if (px <= -TILE || px >= SCREEN_W) continue;
            put_tile(&ts->tiles[fr->tiles[ty * fr->w + tx]], px, py);
        }
    }
}

void draw_sprite(const Sprite *s, int x0, int y0)
{
    for (int y = 0; y < s->h; y++) {
        int sy = y0 + y;
        if (sy < 0 || sy >= SCREEN_H) continue;
        for (int x = 0; x < s->w; x++) {
            int sx = x0 + x;
            uint8_t c = s->pixels[y * s->w + x];
            if (sx >= 0 && sx < SCREEN_W && c != 0)
                g_screen[sy * SCREEN_W + sx] = c;
        }
    }
}
