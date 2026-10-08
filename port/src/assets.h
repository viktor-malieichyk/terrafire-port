/* Decoders for DATA.PAK entry formats. Reference implementations and format
 * notes live in re/tools/pak_extract.py and re/tools/tf_gfx.py. */
#ifndef TF_ASSETS_H
#define TF_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- palettes (entries 52-59): 2-byte marker + 256 x 6-bit VGA RGB */
enum { PAL_FADE_BLACK = 52, PAL_GAMEPLAY = 55, PAL_FADE_WHITE = 58 };
bool palette_load(int entry, uint8_t rgb_out[768]);
/* Palette the game draws a given tile set / sprite bank with. */
int asset_palette(int entry);

/* ---- sounds (entries 4-24): DiamondWare DWD, signed 8-bit mono */
typedef struct {
    int rate_hz;
    size_t count;
    int8_t *samples;
} Sound;
bool sound_load(int entry, Sound *out);
void sound_free(Sound *s);

/* ---- tile sets (entries 25-51): frames are grids of 16x16 tiles */
#define TILE 16
typedef struct {
    uint8_t kind;            /* 1 = opaque, 0 = was a compiled sprite (masked) */
    int16_t bank;
    uint16_t len;            /* stored chunk length */
    uint8_t pixels[TILE * TILE];
    uint8_t mask[TILE * TILE];
} Tile;
typedef struct {
    int w, h;                /* in tiles */
    uint16_t *tiles;         /* w*h tile indices */
    uint8_t *flags;          /* w*h per-cell flags: non-zero = opaque, hides objects behind (0x2be20) */
} TileFrame;
typedef struct {
    int frame_count, tile_count;
    TileFrame *frames;
    Tile *tiles;
} TileSet;
bool tileset_load(int entry, TileSet *out);
void tileset_free(TileSet *ts);

/* ---- sprite banks (entries 61-106): RLE, column-major, index 0 transparent */
typedef struct {
    int16_t quad[4][2];      /* collision quad */
    int16_t hot_x, hot_y;
    int w, h;
    uint8_t *pixels;         /* converted to row-major on load */
    uint8_t *pixels_cm;      /* original column-major order (what bank views walk) */
} Sprite;
typedef struct {
    int count;
    Sprite *sprites;
} SpriteBank;
bool spritebank_load(int entry, SpriteBank *out);
void spritebank_free(SpriteBank *sb);

/* ---- drawing into g_screen (clipped) */
void draw_tileframe(const TileSet *ts, int frame, int x, int y);
void draw_sprite(const Sprite *s, int x, int y);

#endif
