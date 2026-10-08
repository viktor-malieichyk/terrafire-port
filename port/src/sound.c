#include "sound.h"

#include "assets.h"
#include "platform.h"
#include "tables.h"
#include "timing.h"

#define NEVER (-9999)            /* 0xffffd8f1 */

static bool s_enabled;
static float s_master = 1.0f;     /* 0x2e7a4: the volume setting */

void sound_set_volume(int percent) { s_master = percent / 100.0f; }
static Sound s_samples[SOUND_COUNT];
static int32_t s_started[SOUND_COUNT];   /* +0x08: present frame of the last start */

void sound_init(bool enabled)
{
    s_enabled = enabled;
    for (int i = 0; i < SOUND_COUNT; i++) {
        s_started[i] = NEVER;
        if (enabled && !s_samples[i].samples && !sound_load(g_sound_table[i].entry, &s_samples[i]))
            s_samples[i].count = 0;
    }
}

/* A sound restarts only outside its retrigger window, and not while a sound
 * on its skip chain plays; it stops the sounds on its stop chain. */
void sound_play(int n)
{
    if (!s_enabled || n < 0 || n >= SOUND_COUNT) return;
    const SoundDef *d = &g_sound_table[n];
    int32_t now = (int32_t)tf_frame_counter();
    if (now >= s_started[n] && now < s_started[n] + d->retrigger) return;
    sound_stop(n);
    for (int m = d->skip_next; m >= 0 && m < SOUND_COUNT; m = g_sound_table[m].skip_next)
        if (sound_playing(m)) return;
    for (int m = d->stop_next; m >= 0 && m < SOUND_COUNT; m = g_sound_table[m].stop_next)
        sound_stop(m);
    const Sound *s = &s_samples[n];
    platform_play_sound(n, s->samples, s->count, s->rate_hz, d->volume / 100.0f * s_master, d->loop);
    s_started[n] = now;
}

void sound_stop(int n)
{
    if (!s_enabled || n < 0 || n >= SOUND_COUNT) return;
    platform_stop_sound(n);
    s_started[n] = NEVER;
}

bool sound_playing(int n)
{
    return s_enabled && n >= 0 && n < SOUND_COUNT && platform_sound_playing(n);
}

void sound_pause(void)
{
    for (int i = 0; i < SOUND_COUNT; i++) sound_stop(i);
}
