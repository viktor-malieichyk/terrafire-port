/* Pixel collision: ports of 0x2b178 (point test) and the object edge helpers
 * 0x2c844 / 0x2c8a0 / 0x2b948 / 0x2b9c4. A sprite object is solid where its
 * current orientation view has a non-zero pixel; a tile object is solid where
 * the collision shape referenced by the tile's chunk (`bank`) is. */
#ifndef TF_COLLISION_H
#define TF_COLLISION_H

#include <stdbool.h>
#include <stdint.h>

int32_t obj_left(int slot);     /* 0x2c844, 24.8 */
int32_t obj_top(int slot);      /* 0x2c8a0 */
int32_t obj_right(int slot);    /* 0x2b948: last pixel column */
int32_t obj_bottom(int slot);   /* 0x2b9c4: last pixel row */

/* 0x2b178: first live object in first..last solid at world point (x, y), or -1. */
int collide_point(int first, int last, int32_t x, int32_t y);

/* 0x2ab30: does sprite object `a` touch tile object `b` (e.g. the level)?
 * On a hit, cx and cy (if not NULL) get the centre of the overlap with the
 * first touching tile shape, in 24.8 world coordinates. */
bool collide_sprite_tiles(int a, int b, int32_t *cx, int32_t *cy);

/* 0x2b4b8: first live, unfrozen sprite object in lo..hi (other than a) whose
 * pixels touch sprite object a, or -1. */
int collide_sprites(int a, int lo, int hi);

#endif
