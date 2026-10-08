#ifndef TF_SESSION_H
#define TF_SESSION_H

void attract_run(void);     /* 0x13f00 */
void demo_play(void);       /* 0x13f44 */
/* 0x17064 (after the skill menu): start a new game and play it.
 * difficulty 0-3; the rest are the original's command-line options
 * (start mission 0-26, weapon level 0-17, side ammo 0-100); any of them
 * non-zero marks the game as a cheat (no score). */
void game_new(int difficulty, int start_mission, int weapon, int side_ammo);
void session_run(void);     /* 0x166e8 */

/* Testing aid: abort the session after `ticks` game ticks (-1 = no limit). */
void session_set_tick_limit(int ticks);
int session_ticks(void);

#endif
