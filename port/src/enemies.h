/* Normal-mission enemies, bosses and level machinery (switches, gate,
 * crusher, rock fountain, water pump, guns): behaviour callbacks and hits. */
#ifndef TF_ENEMIES_H
#define TF_ENEMIES_H

void cb_turret_vertical(int slot);   /* 0x17550: ceiling/floor turret (spawner 0x1762c) */
void cb_turret_wall(int slot);       /* 0x17384: wall turret (spawner 0x17464) */
void cb_gun_mounted(int slot);       /* 0x17cd0: four-way mounted gun (spawner 0x17dec) */
void turret_hit(int slot, int damage);   /* 0x17bf0: shared by turret types 0x3d/0x40/0x48/0x4a/0x4f */
void cb_turret_floor(int slot);      /* 0x179c8: aiming floor/ceiling turret (spawner 0x17b04) */
void gate_switch_hit(void);          /* 0x12890: switch 0xbf/0xc0 opens the 0x1425c gate */
void cb_gate_stop(int slot);         /* 0x14488 */
void cb_gate_close(int slot);        /* 0x143c0 */
void cb_switch_reset(int slot);      /* 0x12ce0 */
void group37_switch_hit(void);       /* 0x127f4: hitting object 0xbe switches the group */
void cb_group37_cycle(int slot);     /* 0x14d50: objects 0x37-0x3b (spawner 0x14dcc) */
void cb_group37_sound(int slot);     /* 0x14eac */
void switch_c1_hit(void);           /* 0x129fc: "ROCK FOUNTAIN DOWN": the 0x35/0x36 fountains pause 250 of every 450 frames */
void switch_c2_hit(void);           /* 0x12a80: "CRUSHER ACTIVATED": the crusher stays still until this is hit */
void switch_c3_hit(void);           /* 0x12ad0: "WATER PUMPS ON": the pump (object 0xb) follows the direction markers */
void switch_c4_hit(void);           /* 0x12b38: "GUNS OFF": guns 0x4f-0x54 fire in bursts instead of nonstop */
void cb_emitter_start(int slot);    /* 0x14c9c: rock fountain pause cycle */
void cb_emitter_puff(int slot);     /* 0x149a0: rock fountain throws a rock */
void cb_bubble(int slot);           /* 0x14c18: a thrown rock */
void cb_follow_markers(int slot);   /* 0x25d38 */
void homer_hit(int slot);           /* 0x1de80: type 0x1c (spawner 0x1ddc4) homes in once hit */
void cb_homer(int slot);            /* 0x1e050 */
void cb_floor_gun(int slot);         /* 0x171a0: spawner 0x172b4 */
void cb_corner_gun(int slot);        /* 0x17734: spawner 0x1783c */
void cb_mine_idle(int slot);         /* 0x1da5c: spawner 0x1d9ac */
void cb_mine_chase(int slot);        /* 0x1db04: also what a bullet hit calls */
void cb_vent_puff(int slot);         /* 0x145fc: object 13 (spawner 0x147c0) */
void cb_vent_sound(int slot);        /* 0x14868 */
void cb_crawler(int slot);           /* 0x13c74: wall crawler 0x27-0x2a (spawner 0x13bcc) */
void strider_hit(int slot, int damage);   /* 0x1c75c: type 0x55 (spawner 0x1c820) */
void cb_strider_down(int slot);      /* 0x1c908 */
void cb_strider_left(int slot);      /* 0x1cb08 */
void cb_strider_right(int slot);     /* 0x1cd08 */
void cb_strider_up(int slot);        /* 0x1cf04 */
void cb_barrier_reset(int slot);     /* 0x1435c: barrier middle 0xcb back to idle */
void cb_barrier_flash(int slot);     /* 0x14370: barrier middle hit */
void cb_post_flash(int slot);        /* 0x143a0: barrier post: resume at its frame */
void crawler_hit(int slot, int damage);   /* 0x13ac4: type 0x27 */
void hanger_hit(int slot, int damage);    /* 0x1249c: type 0x23 */
void cb_hanger_shoot(int slot);      /* 0x1254c: spawner 0x12664 */
void cb_hanger_ride(int slot);       /* 0x12784 */
void cb_machine_sound(int slot);     /* 0x25bcc: object 0xb (spawner 0x25b0c) */
void worm_hit(int slot, int damage);   /* 0x22b30: types 0x67-0x6d (spawner 0x231d8) */
void cb_worm_down(int slot);         /* 0x22f38 */
void cb_worm_left(int slot);         /* 0x22c98 */
void cb_worm_up(int slot);           /* 0x22de8 */
void cb_worm_right(int slot);        /* 0x23088 */
void cb_worm_body(int slot);         /* 0x232c8 */
void cb_worm_explode(int slot);      /* 0x22c4c */
void drone_hit(int slot, int damage);  /* 0x23404: type 0x6e */
void cb_drone(int slot);             /* 0x234c8: spawner 0x237d0 */
void core_hit(int damage);           /* 0x14fe8: object 0x3c (spawner 0x15988) */
void cb_core_cycle(int slot);        /* 0x15a18 */
void cb_core_move(int slot);         /* 0x1567c */
void cb_core_dying(int slot);        /* 0x150d8 */
void cb_flame(int slot);             /* 0x18f2c */
void cb_crusher_move(int slot);      /* 0x1e3bc: object 12 (spawner 0x1e2b8) */
void cb_crusher_sound(int slot);     /* 0x1e510 */

#endif
