/* Player hits: 0x1e888 (tow stages and collisions), 0x1e648 (bounce and
 * damage), 0x1f8ec (shield damage), 0x257bc (bounce direction markers). */
#ifndef TF_HITS_H
#define TF_HITS_H

#include <stdint.h>

void player_hits(void);                                       /* 0x1e888 */
/* 0x1e648: bounce the player with (bx, by), maybe spin it, then damage it. */
void player_hit(int by_obj, int damage, int32_t cx, int32_t cy, int32_t bx, int32_t by);
void player_damage(int by_obj, int damage);                   /* 0x1f8ec */
/* 0x257bc: bounce for a contact at (x, y) from the placement map's
 * direction markers 0x54-0x5b; anywhere else is fatal for the player. */
void bounce_direction(int32_t x, int32_t y, int32_t *bx, int32_t *by);
int obj_type(int slot);          /* start of the slot range (table 0x563b0) */

#endif
