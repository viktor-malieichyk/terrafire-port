/* Bonus levels (missions 3, 7, 11, 15, 19, 23): a wrapping 50000..0x20250 x
 * 50000..0x18a50 arena drawn from frame slot 0x7e. Object setup 0x101fc. */
#ifndef TF_BONUS_H
#define TF_BONUS_H

void level_init_bonus(void);        /* 0x101fc */
void cb_wrap(int slot);             /* 0x1038c: wrap around the arena edges */
void cb_chain_down(int slot);       /* 0x2540c: chain creature (0xb3-0xb9) turns */
void cb_chain_left(int slot);       /* 0x25234 */
void cb_chain_right(int slot);      /* 0x254f8 */
void cb_chain_up(int slot);         /* 0x25320 */
void cb_chain_wrap(int slot);       /* 0x256f0 */
void chain_hit(int slot, int damage);        /* 0x2516c */
void rock_bounce(int slot);                  /* 0x217b0: player contact bounces a rock away */
void rock_large_hit(int slot, int damage);   /* 0x214f4: 0x94-0x96 split into three */
void rock_medium_hit(int slot, int damage);  /* 0x21b28: 0x97-0x9f split into two */
void rock_small_hit(int slot, int damage);   /* 0x21dc0: 0xa0-0xb1 */
void giant_rock_hit(int slot, int damage);   /* 0x21814: 0x93 (mission 3) */
void walker_hit(int slot, int damage);       /* 0x13220: 0x82-0x86 */
void cb_walker_up(int slot);                 /* 0x1384c: edge walkers 0x82-0x86 */
void cb_walker_right(int slot);              /* 0x135cc */
void cb_walker_down(int slot);               /* 0x1370c */
void cb_walker_left(int slot);               /* 0x13988 */
void cb_walker_shoot(int slot);              /* 0x132e8 */
void boss15_hit(int slot, int damage);       /* 0x11910: mission 15's 0x71 */
void seeker_hit(int slot, int damage);       /* 0x11dcc: 0x72-0x75 */
void seeker_small_hit(int slot, int damage); /* 0x12358: 0x76-0x81 */
void cb_boss15(int slot);                    /* 0x11ab4 */
void cb_seeker(int slot);                    /* 0x11fb4 */
void orbiter_hit(int slot, int damage);      /* 0x1b444: 0x87-0x92 (mission 23) */
void cb_orbit_close(int slot);               /* 0x1b360 */
void cb_orbit_pulse(int slot);               /* 0x1b5a8 */
void boss19_hit(int damage);                 /* 0x23b80: mission 19's 0xb2 */
void cb_boss19_hover(int slot);              /* 0x24224 */
void cb_boss19_rise(int slot);               /* 0x24288 */
void cb_boss19_hunt(int slot);               /* 0x23ab0 */
void cb_boss19_settle(int slot);             /* 0x23b04 */
void cb_boss19_chase(int slot);              /* 0x238c0 */
void cb_boss19_fire(int slot);               /* 0x23f58 */
void cb_boss19_dying(int slot);              /* 0x23bf4 */

#endif
