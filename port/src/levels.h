/* Level and HUD resource loading: ports of 0x1c220, 0x1035c, 0x1aebc and the
 * background tile animation 0x1b9e0. */
#ifndef TF_LEVELS_H
#define TF_LEVELS_H

void resources_reset(void);      /* 0x276fc */
void level_load_normal(void);    /* 0x1c220 */
void level_load_bonus(void);     /* 0x1035c */
void hud_load(void);             /* 0x1aebc */
void tile_animate(void);         /* 0x1b9e0 */

#endif
