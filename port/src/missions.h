/* Mission table (0x5656c, 27 x 0x50 bytes). Field order matches the original
 * record so the generated initialiser stays a straight copy. */
#ifndef TF_MISSIONS_H
#define TF_MISSIONS_H

#include <stdint.h>

#define MISSION_COUNT 27

enum { MISSION_NORMAL = 1, MISSION_BONUS = 2 };   /* also the screen mode, see 0x2224c */

typedef struct {
    int32_t type;               /* +0x00 */
    int32_t u04, u08, u0c, u10, u14, u18, u1c;    /* not yet identified */
    int32_t background;         /* +0x20 tile set: background map (29-33) */
    int32_t sprite_bank;        /* +0x24 sprite bank entry (78-82), loaded by 0x29640 */
    int32_t gfx_a;              /* +0x28 tile set (38-42) */
    int32_t gfx_b;              /* +0x2c tile set (45/46) */
    int32_t gfx_c;              /* +0x30 tile set (36/37) */
    int32_t skip_a;             /* +0x34 frame skip used with entry 25 */
    int32_t skip_b;             /* +0x38 frame skip used with gfx_a / gfx_c */
    int32_t u3c, u40, u44, u48, u4c;              /* not yet identified */
} MissionDef;

_Static_assert(sizeof(MissionDef) == 0x50, "MissionDef must match the original record");

extern const MissionDef g_missions[MISSION_COUNT];

#endif
