/* The player: per-frame callback 0x20318 and what it runs - input handler
 * 0x1f6a0, rotation 0x20358, firing 0x1fbbc, movement 0x20488 and 0x1e888 -
 * plus the request handler 0x15bbc that the session loop calls as its tick. */
#ifndef TF_PLAYER_H
#define TF_PLAYER_H

void cb_player_tick(int slot);   /* 0x20318 (callback CB_20318) */
void player_input(void);         /* 0x1f6a0 */
void player_rotate(void);        /* 0x20358 */
void player_move(void);          /* 0x20488 */
/* 0x14774: the air current (sideways push) at a world position, 0 if none. */
#include <stdint.h>
int32_t air_current(int32_t x, int32_t y);
void game_tick(void);            /* 0x15bbc: act on pause/menu/message requests */

#endif
