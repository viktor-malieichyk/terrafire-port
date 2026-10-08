/* Level object setup: 0x1bc7c (normal missions). The level's object
 * placement map is frame slot 3: each cell's tile number is a spawn code, and
 * slot 2 is a legend frame that uses every tile so the numbers survive
 * res_gc(). Map row r is world tile row r + 0x50. */
#ifndef TF_SPAWN_H
#define TF_SPAWN_H

void level_init_normal(void);     /* 0x1bc7c */
#include <stdint.h>
/* 0x27a48 on the placement map at a 24.8 world position; -1 outside the map. */
int map_code_at(int32_t x, int32_t y);
/* 0x27a48(3, col, row): placement map cell, -1 outside. */
int map_cell(int col, int row);
void life_indicator_spawn(int slot);   /* 0x1d2e0 */
void cb_lives_follow(int slot);        /* 0x1d3c8: first indicator follows the player */

/* Spawners, one per object type (unported ones are stubs in spawn_stubs.c). */
void spawn_10fd4(int col, int row);   /* 0x10fd4 */
void spawn_12664(int col, int row);   /* 0x12664 */
void spawn_12bbc(int type, int col, int row, int orient);   /* 0x12bbc */
void spawn_13bcc(int col, int row);   /* 0x13bcc */
void spawn_14128(int col, int row);   /* 0x14128 */
void spawn_1425c(int col, int row);   /* 0x1425c */
void spawn_147c0(int col, int row);   /* 0x147c0 */
void spawn_14cd0(int col, int row);   /* 0x14cd0 */
void spawn_14dcc(int col, int row);   /* 0x14dcc */
void spawn_15988(int col, int row);   /* 0x15988 */
void spawn_172b4(int col, int row);   /* 0x172b4 */
void spawn_17464(int col, int row, int arg);   /* 0x17464 */
void spawn_1762c(int col, int row, int arg);   /* 0x1762c */
void spawn_1783c(int col, int row, int arg);   /* 0x1783c */
void spawn_17b04(int col, int row, int arg);   /* 0x17b04 */
void spawn_17dec(int col, int row, int arg);   /* 0x17dec */
void spawn_1c820(int col, int row);   /* 0x1c820 */
void spawn_1d8fc(int col, int row);   /* 0x1d8fc */
void spawn_1d9ac(int col, int row);   /* 0x1d9ac */
void spawn_1ddc4(int col, int row);   /* 0x1ddc4 */
void spawn_1e158(int col, int row);   /* 0x1e158 */
void spawn_1e2b8(int col, int row);   /* 0x1e2b8 */
void spawn_20138(int col, int row);   /* 0x20138 */
void spawn_20d1c(int col, int row, int arg);   /* 0x20d1c */
void spawn_231d8(int col, int row);   /* 0x231d8 */
void spawn_237d0(int col, int row);   /* 0x237d0 */
void spawn_250a0(int col, int row);   /* 0x250a0 */
void spawn_25b0c(int col, int row);   /* 0x25b0c */

#endif
