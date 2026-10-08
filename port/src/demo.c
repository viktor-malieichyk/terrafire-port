#include "demo.h"

#include "kbd.h"
#include "pak.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

static const Demo *s_demo;      /* active recording, or NULL */
static uint32_t s_queries;      /* 0x5b81c */
static size_t s_next_event;     /* 0x5b874 */

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool demo_load(int index, Demo *out)
{
    memset(out, 0, sizeof *out);
    if (index < 0 || index >= DEMO_COUNT) return false;
    size_t size;
    uint8_t *e = pak_read(index, &size);
    if (!e) return false;
    bool ok = size >= DEMO_STATE_SIZE + 4 && (size - DEMO_STATE_SIZE) % 4 == 0;
    if (ok) {
        memcpy(out->state, e, DEMO_STATE_SIZE);
        out->difficulty = (int32_t)le32(e);
        out->mission = (int32_t)le32(e + 4);
        size_t n = (size - DEMO_STATE_SIZE) / 4;
        out->events = malloc(n * sizeof *out->events);
        ok = out->events != NULL;
        size_t count = n;   /* index of the sentinel */
        for (size_t i = 0; ok && i < n; i++) {
            out->events[i] = le32(e + DEMO_STATE_SIZE + i * 4);
            if (out->events[i] == DEMO_END) { count = i; break; }
            /* Recorded values must be strictly increasing up to the sentinel. */
            if (i > 0 && out->events[i] <= out->events[i - 1]) ok = false;
        }
        /* Two demos carry unused buffer slack after the sentinel; ignore it. */
        ok = ok && count < n;
        out->event_count = count;
    }
    free(e);
    if (!ok) {
        SDL_Log("pak entry %d is not a valid demo", index);
        demo_free(out);
    }
    return ok;
}

void demo_free(Demo *d)
{
    if (s_demo == d) demo_end();
    free(d->events);
    memset(d, 0, sizeof *d);
}

void demo_begin(const Demo *d)
{
    s_demo = d;
    s_queries = 0;
    s_next_event = 0;
}

void demo_end(void) { s_demo = NULL; }
bool demo_playing(void) { return s_demo != NULL; }
uint32_t input_query_count(void) { return s_queries; }
size_t demo_events_used(void) { return s_next_event; }

/* Shared body of 0x18a60 / 0x18ab0. */
static uint8_t query(uint8_t real)
{
    s_queries++;
    if (!s_demo) return real;
    /* The sentinel never compares below the counter, so the original never
     * read past it; the bounds check only guards a malformed demo. */
    if (s_next_event < s_demo->event_count && s_demo->events[s_next_event] < s_queries) {
        s_next_event++;
        return 1;
    }
    return 0;
}

uint8_t input_was_pressed(uint8_t code) { return query(kbd_was_pressed(code)); }
uint8_t input_was_released(uint8_t code) { return query(kbd_was_released(code)); }
