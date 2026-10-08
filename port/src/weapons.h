/* Firing and bullets: 0x1fbbc (player firing), 0x18fc8 (bullet), 0x1923c
 * (side projectile), 0x194e4 (bullet callback), 0x193dc (side projectile
 * drag and trail), 0x18d20 (damage), 0x1e25c (enemy destroyed). */
#ifndef TF_WEAPONS_H
#define TF_WEAPONS_H

#include <stdbool.h>
#include <stdint.h>

void player_fire(void);                       /* 0x1fbbc */
/* 0x18fc8: bullet of bank_base (+ rotation frame) at (x, y), moving at
 * (vx0, vy0) + speed along `angle`; callback `cb` from 3 frames, life in o_0. */
void bullet_spawn(int bank_base, int sound, int32_t x, int32_t y, int32_t vx0, int32_t vy0,
                  int32_t speed, int32_t angle, int cb, int32_t life);
/* 0x2c728 / 0x2c77c with point 0: the object's first collision-quad point. */
int32_t quad_x(int slot, int k);
int32_t quad_y(int slot, int k);
int32_t quad_x0(int slot);
int32_t quad_y0(int slot);
int bullet_damage(int bullet);                /* 0x18d20 */
void weapon_set(int level);                   /* 0x2044c: also adds side ammo */
void aimed_shot(int shooter);                 /* 0x18b00: enemy shot aimed at the player */
/* 0x21f20: add n to the score, times (difficulty + 1) if scaled; nothing
 * while f_014 is set. */
void score_add(bool scaled, int32_t n);
void enemy_destroyed(int slot);               /* 0x1e25c */
void cb_bullet(int slot);                     /* 0x194e4 */
void cb_enemy_bullet(int slot);               /* 0x18dc0 */
void cb_side_trail(int slot);                 /* 0x193dc */

#endif
