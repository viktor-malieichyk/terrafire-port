/* Recorded demos (DATA.PAK entries 0-3) and the input queries they drive.
 *
 * A demo is a raw dump of the 0x2973-byte game-state block followed by a
 * sorted list of u32 query-counter values ending in DEMO_END. Demos store no
 * keys: every input query increments a global counter, and during playback a
 * query answers "yes" only when the counter has passed the next recorded
 * value (0x18a60 / 0x18ab0). Playback therefore stays in sync only if the
 * port issues exactly the original's queries in exactly the original order. */
#ifndef TF_DEMO_H
#define TF_DEMO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEMO_COUNT 4
#define DEMO_STATE_SIZE 0x2973
#define DEMO_END 99999999u

typedef struct {
    uint8_t state[DEMO_STATE_SIZE];   /* legacy state block, see re/tools/tf_state.py */
    int32_t difficulty;               /* state +0x00 */
    int32_t mission;                  /* state +0x04 */
    uint32_t *events;                 /* includes the DEMO_END sentinel */
    size_t event_count;               /* events before the sentinel */
} Demo;

bool demo_load(int index, Demo *out);
void demo_free(Demo *d);

/* Start/stop driving input from a demo (the counter/index/flag resets of 0x13f44). */
void demo_begin(const Demo *d);
void demo_end(void);
bool demo_playing(void);              /* 0x5e429 */

/* The game's input queries. Outside a demo they return the keyboard latch;
 * during a demo the latch is still read but the recording decides. */
uint8_t input_was_pressed(uint8_t code);    /* 0x18a60 */
uint8_t input_was_released(uint8_t code);   /* 0x18ab0 */

uint32_t input_query_count(void);           /* 0x5b81c */
size_t demo_events_used(void);              /* 0x5b874 */

#endif
