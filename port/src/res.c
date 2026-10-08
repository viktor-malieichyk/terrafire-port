#include "res.h"

#include "assets.h"
#include "fatal.h"

#include <stdlib.h>
#include <string.h>

FrameSlot g_slots[RES_SLOTS] = {0};
Chunk g_chunks[RES_CHUNKS] = {0};
Bank g_banks[RES_BANKS] = {0};
int g_chunk_count;              /* 0x84240 */
static int s_chunk_high_water;  /* 0x5961c */

/* Arithmetic shift right, as the original's `sar`, independent of how the C
 * compiler treats negative values. */
static int32_t sar(int32_t v, int n)
{
    return v >= 0 ? v >> n : ~(~v >> n);
}

int32_t orient_x(int k, int32_t x, int32_t y)
{
    switch (k) {
    case 0: case 6: return x;
    case 1: case 5: return -y;
    case 2: case 4: return -x;
    case 3: case 7: return y;
    default: return 0;
    }
}

int32_t orient_y(int k, int32_t x, int32_t y)
{
    switch (k) {
    case 0: case 4: return y;
    case 1: case 7: return x;
    case 2: case 6: return -y;
    case 3: case 5: return -x;
    default: return 0;
    }
}

/* ------------------------------------------------------------------ chunks */

static void chunk_free(int i)                  /* 0x26f48 */
{
    free(g_chunks[i].own);
    g_chunks[i].own = NULL;
    g_chunks[i].image = NULL;
}

static void chunks_free_all(void)              /* 0x26fc8 */
{
    for (int i = 0; i < g_chunk_count; i++) chunk_free(i);
    g_chunk_count = 0;
}

static void slot_free(int i)
{
    free(g_slots[i].cells);
    memset(&g_slots[i], 0, sizeof g_slots[i]);
}

static void bank_free(int i)                   /* 0x263f4 / 0x29da0 */
{
    free(g_banks[i].pixels);
    g_banks[i].pixels = NULL;
}

void res_reset(void)
{
    /* 0x276fc also clears the active-object list and keyboard latches and
     * resets a few unrelated globals; session code does those. */
    for (int i = 0; i < RES_BANKS; i++) bank_free(i);
    for (int i = 0; i < RES_SLOTS; i++) slot_free(i);
    chunks_free_all();
}

bool res_slot_loaded(int slot)
{
    return slot >= 0 && slot < RES_SLOTS && g_slots[slot].cells != NULL;
}

bool res_bank_loaded(int bank)
{
    return bank >= 0 && bank < RES_BANKS && g_banks[bank].pixels != NULL;
}

/* -------------------------------------------------------------- frame sets */

void res_load_frames(int entry, int bank_base, int ngroups, const FrameGroup *groups)
{
    TileSet ts;
    if (!tileset_load(entry, &ts)) tf_fatal("unable to load tile set %d", entry);

    /* Frames are consumed in order: each group skips `skip` frames and then
     * fills slots first..last; anything left over is skipped. */
    int remaining = ts.frame_count, next = 0;
    for (int gi = 0; gi < ngroups; gi++) {
        const FrameGroup *grp = &groups[gi];
        remaining -= (grp->last - grp->first + 1) + grp->skip;
        if (remaining < 0) tf_fatal("tile set %d has too few frames", entry);
        next += grp->skip;
        for (int slot = grp->first; slot <= grp->last; slot++, next++) {
            if (slot < 0 || slot >= RES_SLOTS) tf_fatal("frame slot %d out of range", slot);
            const TileFrame *src = &ts.frames[next];
            FrameSlot *fs = &g_slots[slot];
            slot_free(slot);
            fs->w = src->w;
            fs->h = src->h;
            fs->w_fx4 = src->w << 4;
            fs->h_fx4 = src->h << 4;
            fs->w_fx12 = src->w << 12;
            fs->h_fx12 = src->h << 12;
            size_t n = (size_t)src->w * (size_t)src->h;
            fs->cells = malloc(n * sizeof *fs->cells);
            if (!fs->cells) tf_fatal("out of memory");
            for (size_t k = 0; k < n; k++) {
                /* Cell indices are relative to this entry's chunks (16-bit add). */
                fs->cells[k].chunk = (uint16_t)(src->tiles[k] + g_chunk_count);
                fs->cells[k].flag = src->flags[k];
            }
        }
    }

    /* 0x26d64: append all of the entry's chunks. */
    if (g_chunk_count + ts.tile_count > RES_CHUNKS) tf_fatal("too many tile chunks");
    for (int i = 0; i < ts.tile_count; i++) {
        const Tile *t = &ts.tiles[i];
        Chunk *c = &g_chunks[g_chunk_count + i];
        c->kind = c->own_kind = t->kind;
        c->bank = (int16_t)(t->bank + bank_base);
        c->len = t->len;
        c->own = malloc(sizeof *c->own);
        if (!c->own) tf_fatal("out of memory");
        memcpy(c->own->pixels, t->pixels, sizeof c->own->pixels);
        memcpy(c->own->mask, t->mask, sizeof c->own->mask);
        c->image = c->own;
    }
    g_chunk_count += ts.tile_count;
    if (g_chunk_count > s_chunk_high_water) s_chunk_high_water = g_chunk_count;
    tileset_free(&ts);
}

/* ------------------------------------------------------------ sprite banks */

static void make_views(Bank *b, bool tile_shapes)
{
    const BankView *v0 = &b->view[0];
    for (int k = 1; k < 8; k++) {
        BankView *v = &b->view[k];
        int32_t t;
        v->w = abs(orient_x(k, v0->w, v0->h));
        v->h = abs(orient_y(k, v0->w, v0->h));
        v->px_step = orient_y(k, sar(v0->h, 8), 1);
        v->col_step = orient_x(k, sar(v0->h, 8), 1) - v->px_step * sar(v->h, 8);

        t = orient_x(k, 1, 0);
        v->hot_x = t == -1 ? (0x100 - v0->hot_x) - v0->w : t == 1 ? v0->hot_x : 0;
        t = orient_x(k, 0, 1);
        if (t == -1) v->hot_x += (0x100 - v0->hot_y) - v0->h;
        else if (t == 1) v->hot_x += v0->hot_y;

        t = orient_y(k, 1, 0);
        v->hot_y = t == -1 ? (0x100 - v0->hot_x) - v0->w : t == 1 ? v0->hot_x : 0;
        t = orient_y(k, 0, 1);
        if (t == -1) v->hot_y += (0x100 - v0->hot_y) - v0->h;
        else if (t == 1) v->hot_y += v0->hot_y;

        /* Offset of the first pixel walked, relative to the stored pixels. */
        if (!b->pixels) {
            v->start = 0;
        } else {
            v->start = sar(v->hot_y - orient_y(k, v0->hot_x, v0->hot_y), 8) * v->px_step +
                       (v->col_step + sar(v->h, 8) * v->px_step) *
                       sar(v->hot_x - orient_x(k, v0->hot_x, v0->hot_y), 8);
        }

        if (tile_shapes) {
            t = orient_x(k, 0xf00, 0xf00);
            v->hot_x -= t < 0 ? t : 0;
            t = orient_y(k, 0xf00, 0xf00);
            v->hot_y -= t < 0 ? t : 0;
        }

        for (int i = 0; i < 4; i++) {
            v->quad_x[i] = orient_x(k, v0->quad_x[i], v0->quad_y[i]);
            v->quad_y[i] = orient_y(k, v0->quad_x[i], v0->quad_y[i]);
        }
    }
}

int res_load_banks(int entry, int first, int last, bool tile_shapes)
{
    SpriteBank sb;
    if (!spritebank_load(entry, &sb)) tf_fatal("unable to load sprite bank %d", entry);
    if (first + sb.count - 1 != last) tf_fatal("sprite bank %d has %d sprites, expected %d", entry, sb.count, last - first + 1);
    if (first + sb.count > RES_BANKS) tf_fatal("too many sprite banks");

    int total = 0;
    for (int i = 0; i < sb.count; i++) {
        Sprite *s = &sb.sprites[i];
        Bank *b = &g_banks[first + i];
        bank_free(first + i);
        memset(b, 0, sizeof *b);
        BankView *v0 = &b->view[0];
        for (int q = 0; q < 4; q++) {
            v0->quad_x[q] = (int32_t)s->quad[q][0] * 256;
            v0->quad_y[q] = (int32_t)s->quad[q][1] * 256;
        }
        v0->hot_x = (int32_t)s->hot_x * 256;
        v0->hot_y = (int32_t)s->hot_y * 256;
        v0->w = (int32_t)s->w * 256;
        v0->h = (int32_t)s->h * 256;
        v0->col_step = 0;
        v0->px_step = 1;
        int size = s->w * s->h;
        total += size;
        if (size) {
            b->pixels = s->pixels_cm;      /* take ownership */
            s->pixels_cm = NULL;
        }
        make_views(b, tile_shapes);
    }
    spritebank_free(&sb);
    return total;
}

/* --------------------------------------------------------------------- gc */

void res_gc(void)
{
    /* Phase 1: which chunks does any loaded frame use? */
    int *map = malloc(sizeof *map * (size_t)(g_chunk_count ? g_chunk_count : 1));
    if (!map) tf_fatal("out of memory");
    for (int i = 0; i < g_chunk_count; i++) map[i] = -1;
    for (int s = 0; s < RES_SLOTS; s++) {
        const FrameSlot *fs = &g_slots[s];
        if (!fs->cells) continue;
        for (int k = 0; k < fs->w * fs->h; k++)
            if (fs->cells[k].chunk < g_chunk_count) map[fs->cells[k].chunk] = 1;
    }
    /* Phase 2: free unused chunks and pack the rest down. */
    int kept = 0;
    for (int i = 0; i < g_chunk_count; i++) {
        if (map[i] == -1) {
            chunk_free(i);
            continue;
        }
        map[i] = kept;
        if (i != kept) g_chunks[kept] = g_chunks[i];
        kept++;
    }
    for (int i = kept; i < g_chunk_count; i++) memset(&g_chunks[i], 0, sizeof g_chunks[i]);
    int old_count = g_chunk_count;
    g_chunk_count = kept;
    /* Phase 3: renumber cell references. */
    for (int s = 0; s < RES_SLOTS; s++) {
        FrameSlot *fs = &g_slots[s];
        if (!fs->cells) continue;
        for (int k = 0; k < fs->w * fs->h; k++)
            if (fs->cells[k].chunk < old_count) fs->cells[k].chunk = (uint16_t)map[fs->cells[k].chunk];
    }
    free(map);

    /* Phase 4: free banks that no remaining chunk refers to. */
    static bool used[RES_BANKS];
    memset(used, 0, sizeof used);
    for (int i = 0; i < g_chunk_count; i++) {
        uint16_t b = (uint16_t)g_chunks[i].bank;
        if (b < RES_BANKS) used[b] = true;
    }
    for (int b = 0; b < RES_BANKS; b++)
        if (!used[b] && g_banks[b].pixels) bank_free(b);
}

/* ----------------------------------------------------------------- aliasing */

void res_alias_slot(int dst, int src)
{
    const FrameSlot *d = &g_slots[dst], *s = &g_slots[src];
    if (!d->cells || !s->cells) return;
    int n = d->w * d->h;
    if (n > s->w * s->h) n = s->w * s->h;   /* the original assumed equal sizes */
    for (int k = 0; k < n; k++) {           /* 0x26eb0 per cell */
        Chunk *dc = &g_chunks[d->cells[k].chunk];
        const Chunk *sc = &g_chunks[s->cells[k].chunk];
        dc->image = sc->image;
        dc->kind = sc->kind;
        dc->bank = sc->bank;
    }
}
