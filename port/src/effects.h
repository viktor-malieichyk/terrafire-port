/* Explosion / impact effects: 0x110d0. */
#ifndef TF_EFFECTS_H
#define TF_EFFECTS_H

#include <stdbool.h>
#include <stdint.h>

/* kind: 0x10a large explosion, 0x129, 0x148, 0x158 player impact, 0x168.
 * ring: also spawn a ring of fragments. Position in 24.8 world coordinates. */
void effect_spawn(int kind, bool ring, int32_t x, int32_t y);

void cb_debris(int slot);   /* 0x229ec (callback CB_229ec) */
void cb_exhaust(int slot);  /* 0x144b8 (callback CB_144b8) */

#endif
