/* Per-frame routines: 0x2224c dispatches on the screen mode: 0 menus,
 * 1 normal mission (0x1b734), 2 bonus level, 3 mission complete, 4 high
 * scores. Each draws, presents and runs the object update; the mission
 * modes also advance mission_frames. */
#ifndef TF_FRAME_H
#define TF_FRAME_H

#include <stdint.h>

void frame_render(uint8_t present);   /* 0x2224c */

#endif
