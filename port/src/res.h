/* The engine's resident resource tables, ported from the original's
 * allocator (0x27164) and loaders:
 *
 *   frame slots  133  tile grids (0x28470 loads them from tile-set entries)
 *   chunks      2600  16x16 tile images referenced by frame cells (0x26d64)
 *   banks       2393  sprites with 8 orientation views (0x29640)
 *
 * Loading appends chunks; res_gc() (0x28850) drops chunks no frame uses and
 * banks no chunk uses. A chunk's `bank` is the index of the bank it goes
 * with; for level tiles those banks come from the per-mission entries 78-82,
 * the tiles' collision shapes (see collision.c). */
#ifndef TF_RES_H
#define TF_RES_H

#include <stdbool.h>
#include <stdint.h>

#define RES_SLOTS 0x85
#define RES_CHUNKS 0xa28
#define RES_BANKS 0x959

typedef struct {
    uint16_t chunk;
    uint8_t flag;
} Cell;

typedef struct {               /* 0x1c-byte record */
    Cell *cells;               /* +0x00, NULL = slot empty */
    int32_t w_fx12, h_fx12;    /* +0x04, +0x08  w<<12, h<<12 */
    int32_t w, h;              /* +0x0c, +0x10  in tiles */
    int32_t w_fx4, h_fx4;      /* +0x14, +0x18  w<<4, h<<4 (pixels) */
} FrameSlot;

typedef struct {
    uint8_t pixels[256];
    uint8_t mask[256];
} TileImage;

typedef struct {               /* 0x0e-byte record */
    uint8_t kind;              /* +0x00  current kind (0 masked, 1 opaque) */
    uint8_t own_kind;          /* +0x01  kind of the image this chunk owns */
    uint16_t len;              /* +0x02 */
    int16_t bank;              /* +0x04 */
    const TileImage *image;    /* +0x06  what is drawn (may alias another chunk's image) */
    TileImage *own;            /* +0x0a  owned allocation, freed with the chunk */
} Chunk;

typedef struct {               /* one 0x3c-byte orientation view */
    int32_t start;             /* +0x00  offset into the bank's pixels of the first pixel */
    int32_t quad_x[4];         /* +0x04  collision quad, 24.8 */
    int32_t quad_y[4];         /* +0x14 */
    int32_t hot_x, hot_y;      /* +0x24, +0x28  24.8 */
    int32_t w, h;              /* +0x2c, +0x30  24.8 */
    int32_t col_step;          /* +0x34  added after each column */
    int32_t px_step;           /* +0x38  added after each pixel */
} BankView;

typedef struct {               /* 0x1e0-byte record */
    uint8_t *pixels;           /* column-major; NULL = not loaded (also true of empty sprites) */
    BankView view[8];          /* 0: as stored; 1-7: rotations and mirrors */
} Bank;

extern FrameSlot g_slots[RES_SLOTS];
extern Chunk g_chunks[RES_CHUNKS];
extern Bank g_banks[RES_BANKS];
extern int g_chunk_count;      /* 0x84240 */

typedef struct {
    int skip;                  /* frames to skip first */
    int first, last;           /* frame slots to fill */
} FrameGroup;

void res_reset(void);                         /* 0x276fc (resource part) */
bool res_slot_loaded(int slot);               /* 0x2874c */
bool res_bank_loaded(int bank);               /* 0x29b84 */
/* 0x28470: load frames of a tile-set entry into slots and append its chunks;
 * every chunk's bank index is offset by `bank_base`. */
void res_load_frames(int entry, int bank_base, int ngroups, const FrameGroup *groups);
/* 0x29640: load a sprite-bank entry into banks first..last; `tile_shapes`
 * applies the 15-pixel correction used for tile collision shapes. */
int res_load_banks(int entry, int first, int last, bool tile_shapes);
void res_gc(void);                            /* 0x28850 */
void res_alias_slot(int dst, int src);        /* 0x28ad4 */

/* Fixed-point orientation helpers (0x2681c / 0x2685c). */
int32_t orient_x(int k, int32_t x, int32_t y);
int32_t orient_y(int k, int32_t x, int32_t y);

#endif
