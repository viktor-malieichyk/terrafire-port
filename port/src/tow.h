/* Towing (stage 2): the rope physics in 0x20488, the towed object's
 * collisions 0x1067c with its hit 0x104a4 and damage 0x10ebc, the player-side
 * pull of 0x1e648, and the self-destruct countdown 0x10db8. Object 15 is the
 * towed object. */
#ifndef TF_TOW_H
#define TF_TOW_H

#include <stdint.h>

/* 0x20488, stage 2: move ship and towed object given the ship's force. */
void tow_move(int32_t fx, int32_t fy);
/* 0x267f0 / 0x2d528: the original's approximate integer square root. */
uint32_t isqrt_approx(uint32_t n);
/* Part of 0x1e648: the ship bounced with (bx, by) pulls the towed object. */
void tow_pull(int32_t bx, int32_t by);
void tow_update(void);                    /* 0x1067c: towed object collisions */
/* 0x104a4: bounce the towed object with (bx, by), pull the ship along,
 * then damage (shared shields). (cx, cy): contact point for the spark. */
void tow_hit(int by_obj, int damage, int32_t cx, int32_t cy, int32_t bx, int32_t by);
void tow_damage(int by_obj, int damage);  /* 0x10ebc: the ship's shields take it */
void cb_tow_countdown(int slot);          /* 0x10db8 */

#endif
