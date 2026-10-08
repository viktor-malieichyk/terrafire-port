#ifndef TF_TABLES_H
#define TF_TABLES_H

#include <stdint.h>

/* 256 pseudo-random values from PART2.EXE (0x43660); indexed by a counter. */
extern const int32_t g_random_table[256];
/* Trig tables indexed by angle in degrees (0..360): g_trig_a for x, g_trig_b
 * for y. They sit in one block with the 19 dwords before them in the
 * original (0x42acc), because some code indexes them with angles down to
 * -19 and so reads that data; g_trig_b[-n] is the end of g_trig_a. */
#define TRIG_BEFORE 19
extern const int32_t g_trig_mem[TRIG_BEFORE + 722];
#define g_trig_a (g_trig_mem + TRIG_BEFORE)
#define g_trig_b (g_trig_mem + TRIG_BEFORE + 361)
/* Bullet banks per weapon level: [level * 3 + barrel], barrel 0/1 alternate. */
#define WEAPON_LEVELS 18
extern const int32_t g_weapon_banks[WEAPON_LEVELS * 3];

/* Sound table entry (0x43c10, 0x38 bytes each in the original). */
#include <stdbool.h>
typedef struct {
    int32_t entry;       /* DATA.PAK entry of the DWD sample */
    int32_t volume;      /* 0-100 */
    int32_t retrigger;   /* frames before the same sound may restart */
    int32_t stop_next;   /* chain of sounds stopped when this one starts, -1 = end */
    int32_t skip_next;   /* chain of sounds that block this one while playing */
    int32_t priority;
    bool loop;
} SoundDef;
#define SOUND_COUNT 34

/* Message texts by id (0x4276c, English): menu labels, HUD messages, option
 * strings (formats: "%%" prints "%"). */
#define TEXT_COUNT 235
extern const char *const g_text[TEXT_COUNT];
/* Key names by XT scancode (0x59620); "" = not assignable. */
extern const char *const g_key_names[128];
extern const SoundDef g_sound_table[SOUND_COUNT];

#endif
